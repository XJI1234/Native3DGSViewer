package org.native3dgs.sdk

import android.content.Context
import android.net.Uri
import android.view.Surface
import java.io.Closeable
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.Job
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

enum class EnginePhase { Empty, Loading, Uploading, Ready, Recovering, Failed, Stopping, Stopped }

data class EngineState(
    val phase: EnginePhase = EnginePhase.Empty,
    val requestId: Long = 0,
    val activeRequestId: Long = 0,
    val uploadTicket: Long = 0,
    val surfaceGeneration: Long = 0,
    val sceneCount: Long = 0,
    val error: Int = 0,
    val framesPresented: Long = 0,
    val lastFrameMicros: Long = 0,
)

class EngineException(val code: Int, message: String) : Exception(message)

/** Owns one renderer. Surface calls may originate on the UI thread; decode and GPU work do not. */
class Native3dgsEngine(context: Context) : Closeable {
    private val appContext = context.applicationContext
    private val decoder = DecoderClient(appContext)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val shutdownScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val lock = Any()
    private var handle = EngineBridge.create(appContext.cacheDir.absolutePath)
    private var generation = 0L
    private var revision = 0L
    private var decoding = false
    private var decodeError: Int? = null
    private var shutdown: Job? = null
    private val decodeGeneration = AtomicLong()
    private val mutableState = MutableStateFlow(EngineState())
    val state: StateFlow<EngineState> = mutableState

    init {
        if (handle == 0L) throw EngineException(5, "Engine initialization failed")
        scope.launch {
            while (isActive) {
                refresh()
                delay(100)
            }
        }
    }

    private fun currentHandle(): Long = handle.takeIf { it != 0L }
        ?: throw EngineException(2, "Engine closed")

    private fun check(code: Int) {
        if (code != 0) throw EngineException(code, "Engine command failed: $code")
    }

    fun attach(surface: Surface): Long = synchronized(lock) {
        val next = generation + 1
        check(EngineBridge.attach(currentHandle(), surface, next))
        generation = next
        revision = 0
        next
    }

    fun detach(surfaceGeneration: Long) = synchronized(lock) {
        check(EngineBridge.detach(currentHandle(), surfaceGeneration))
    }

    fun resize(surfaceGeneration: Long, width: Int, height: Int) = synchronized(lock) {
        check(EngineBridge.resize(currentHandle(), surfaceGeneration, ++revision, width, height))
    }

    /** Opens a document selected through ACTION_OPEN_DOCUMENT. Returns the native request ID. */
    suspend fun open(uri: Uri): Long {
        val operation = decodeGeneration.incrementAndGet()
        try {
            return withContext(Dispatchers.IO) {
                val input = appContext.contentResolver.openFileDescriptor(uri, "r")
                    ?: throw EngineException(3, "Document unavailable")
                input.use {
                    synchronized(lock) {
                        currentHandle()
                        if (decodeGeneration.get() != operation)
                            throw kotlinx.coroutines.CancellationException("Superseded model open")
                        decoding = true
                        decodeError = null
                        mutableState.value = mutableState.value.copy(phase = EnginePhase.Loading)
                    }
                    val shared = suspendCancellableCoroutine<android.os.ParcelFileDescriptor> { continuation ->
                        try {
                            val decodeId = synchronized(lock) {
                                if (decodeGeneration.get() != operation) {
                                    throw kotlinx.coroutines.CancellationException("Superseded model open")
                                }
                                decoder.open(input, completion = { _, descriptor, error ->
                                    if (descriptor == null) {
                                        if (continuation.isActive)
                                            continuation.resumeWithException(
                                                EngineException(error, "Model decoding failed: $error"))
                                    } else if (continuation.isActive)
                                        continuation.resume(descriptor) { _, value, _ -> value.close() }
                                    else descriptor.close()
                                })
                            }
                            continuation.invokeOnCancellation { decoder.cancel(decodeId) }
                        } catch (error: Exception) {
                            if (continuation.isActive) continuation.resumeWithException(error)
                        }
                    }
                    shared.use { descriptor ->
                        synchronized(lock) {
                            if (decodeGeneration.get() != operation)
                                throw kotlinx.coroutines.CancellationException("Superseded model open")
                            val request = EngineBridge.openFd(currentHandle(), descriptor.fd, true)
                            if (request <= 0)
                                throw EngineException((-request).toInt(), "Scene transfer failed")
                            request
                        }
                    }
                }
            }
        } catch (error: Exception) {
            synchronized(lock) {
                if (handle != 0L && decodeGeneration.get() == operation &&
                    error !is kotlinx.coroutines.CancellationException &&
                    (error as? EngineException)?.code != DecoderError.CANCELLED) {
                    decodeError = (error as? EngineException)?.code ?: 3
                    mutableState.value = mutableState.value.copy(
                        phase = if (mutableState.value.sceneCount > 0) EnginePhase.Ready
                                else EnginePhase.Failed,
                        error = decodeError ?: 3)
                }
            }
            throw error
        } finally {
            synchronized(lock) {
                if (decodeGeneration.get() == operation) decoding = false
            }
        }
    }

    fun cancel(requestId: Long) = synchronized(lock) {
        check(EngineBridge.cancel(currentHandle(), requestId))
    }

    fun closeScene() = synchronized(lock) {
        decodeGeneration.incrementAndGet()
        decoding = false
        decodeError = null
        decoder.cancel()
        check(EngineBridge.closeScene(currentHandle()))
    }

    fun orbit(dxPixels: Double, dyPixels: Double) = camera(0, dxPixels, dyPixels)
    fun pan(dxPixels: Double, dyPixels: Double) = camera(1, dxPixels, dyPixels)
    fun dolly(steps: Double) = camera(2, steps)
    fun look(dxPixels: Double, dyPixels: Double) = camera(3, dxPixels, dyPixels)
    fun fly(right: Double, up: Double, forward: Double, seconds: Double) =
        camera(4, right, up, forward, seconds)
    fun fit() = camera(5)
    fun reset() = camera(6)
    fun fixedMode() = camera(7)
    fun freeMode() = camera(8)
    fun flipAxes(x: Boolean, y: Boolean, z: Boolean) =
        camera(9, ((if (x) 1 else 0) or (if (y) 2 else 0) or (if (z) 4 else 0)).toDouble())

    private fun camera(action: Int, x: Double = 0.0, y: Double = 0.0, z: Double = 0.0,
                       seconds: Double = 0.0) = synchronized(lock) {
        check(EngineBridge.camera(currentHandle(), action, x, y, z, seconds))
    }

    private fun refresh() = synchronized(lock) {
        if (handle == 0L) return@synchronized
        val values = EngineBridge.snapshot(handle) ?: return@synchronized
        val phase = EnginePhase.entries.getOrElse(values[0].toInt()) { EnginePhase.Failed }
        val localError = decodeError
        mutableState.value = EngineState(
            if (decoding && phase != EnginePhase.Stopping && phase != EnginePhase.Stopped &&
                phase != EnginePhase.Uploading) EnginePhase.Loading
            else if (localError != null && phase == EnginePhase.Empty) EnginePhase.Failed
            else phase,
            values[1], values[2], values[3], values[4], values[5],
            localError ?: values[6].toInt(),
            values[7], values[8])
    }

    override fun close() {
        synchronized(lock) {
            if (handle == 0L) return
            val value = handle
            handle = 0
            mutableState.value = mutableState.value.copy(phase = EnginePhase.Stopped)
            shutdown = shutdownScope.launch { EngineBridge.destroy(value) }
        }
        decoder.close()
        scope.cancel()
    }

    /** Waits until native rendering has released its surface and GPU resources. */
    suspend fun closeAndWait() {
        close()
        synchronized(lock) { shutdown }?.join()
    }
}

internal object EngineBridge {
    init { System.loadLibrary("gs_android_decoder") }
    external fun create(directory: String): Long
    external fun destroy(handle: Long)
    external fun openFd(handle: Long, fd: Int, shared: Boolean): Long
    external fun cancel(handle: Long, request: Long): Int
    external fun closeScene(handle: Long): Int
    external fun attach(handle: Long, surface: Surface, generation: Long): Int
    external fun detach(handle: Long, generation: Long): Int
    external fun resize(handle: Long, generation: Long, revision: Long, width: Int, height: Int): Int
    external fun camera(handle: Long, action: Int, x: Double, y: Double, z: Double,
                        seconds: Double): Int
    external fun snapshot(handle: Long): LongArray?
}
