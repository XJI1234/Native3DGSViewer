package org.native3dgs.sdk

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import android.os.ParcelFileDescriptor
import java.io.Closeable
import java.util.concurrent.atomic.AtomicLong

/** Completion transfers an untrusted descriptor; the native engine validates before publication. */
internal class DecoderClient(context: Context, private val timeoutMillis: Long = 300_000) : Closeable {
    private val context = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private var service: Messenger? = null
    private var bound = false
    private var closed = false
    private var decoderPid = 0
    private var pending: Pending? = null
    private var deadline: Runnable? = null
    private var binder: IBinder? = null
    private var death: IBinder.DeathRecipient? = null
    private var bindingGeneration = 0L
    private var connection: ServiceConnection? = null

    private class Pending(val id: Long, val input: ParcelFileDescriptor,
                          val progress: (DecodeProgress) -> Unit,
                          val completion: (Long, ParcelFileDescriptor?, Int) -> Unit) {
        var sent = false
    }

    private val receiver = Messenger(Handler(Looper.getMainLooper()) { message ->
        val data = message.data
        val descriptor = DecoderProtocol.descriptor(data)
        val current = pending
        if (closed || current == null ||
            data.getLong(DecoderProtocol.ID_KEY) != current.id) {
            descriptor?.close()
            return@Handler true
        }
        if (data.getInt(DecoderProtocol.VERSION_KEY) != DecoderProtocol.VERSION) {
            descriptor?.close()
            complete(current, null, DecoderError.UNSUPPORTED_VERSION)
            return@Handler true
        }
        if (message.what == DecoderProtocol.PROGRESS) {
            descriptor?.close()
            decoderPid = data.getInt(DecoderProtocol.PROCESS_KEY, decoderPid)
            val total = data.getLong(DecoderProtocol.TOTAL_KEY, -1)
            try {
                current.progress(DecodeProgress(current.id,
                    data.getInt(DecoderProtocol.STAGE_KEY), data.getLong(DecoderProtocol.DONE_KEY),
                    if (total >= 0) total else null, decoderPid))
            } catch (_: Exception) { cancelPending(DecoderError.OBSERVER_FAILURE) }
        } else if (message.what == DecoderProtocol.COMPLETE) {
            val error = data.getInt(DecoderProtocol.ERROR_KEY, DecoderError.DECODER_FAILURE)
            if (error >= 0 || descriptor == null) {
                descriptor?.close()
                complete(current, null, if (error in 0..16) error else DecoderError.DECODER_FAILURE)
            } else {
                complete(current, descriptor, -1)
            }
        } else descriptor?.close()
        true
    })

    private fun newConnection(generation: Long) = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, value: IBinder) {
            if (closed || !bound || generation != bindingGeneration) return
            binder = value
            try {
                val recipient = IBinder.DeathRecipient {
                    main.post { if (binder === value) disconnected() }
                }
                death = recipient
                value.linkToDeath(recipient, 0)
                service = Messenger(value)
                pending?.let { send(it) }
            } catch (_: Exception) { disconnected() }
        }
        override fun onServiceDisconnected(name: ComponentName) {
            if (generation == bindingGeneration) disconnected()
        }
        override fun onBindingDied(name: ComponentName) {
            if (generation == bindingGeneration) disconnected()
        }
        override fun onNullBinding(name: ComponentName) {
            if (generation == bindingGeneration) disconnected()
        }
    }

    fun open(input: ParcelFileDescriptor,
             progress: (DecodeProgress) -> Unit = {},
             completion: (Long, ParcelFileDescriptor?, Int) -> Unit): Long {
        val id = nextId.incrementAndGet()
        // Dup is synchronous so the caller can close input immediately after this method returns.
        val owned = try { ParcelFileDescriptor.dup(input.fileDescriptor) } catch (_: Exception) { null }
        main.post {
            if (owned == null) { completion(id, null, DecoderError.IO_FAILURE); return@post }
            if (closed) { owned.close(); completion(id, null, DecoderError.CANCELLED); return@post }
            cancelPending()
            val current = Pending(id, owned, progress, completion)
            pending = current
            val timer = Runnable {
                if (pending === current) {
                    if (current.sent) {
                        try {
                            service?.send(Message.obtain(null, DecoderProtocol.CANCEL).apply {
                                data = DecoderProtocol.envelope(current.id)
                            })
                        } catch (_: Exception) { }
                    }
                    disconnectBinding()
                    complete(current, null, DecoderError.TIMEOUT)
                }
            }
            deadline = timer
            main.postDelayed(timer, timeoutMillis.coerceAtLeast(1))
            if (service != null) send(current)
            else if (!bound) {
                val activeConnection = newConnection(++bindingGeneration)
                connection = activeConnection
                bound = try {
                    context.bindService(Intent(context, DecoderService::class.java),
                        activeConnection, Context.BIND_AUTO_CREATE)
                } catch (_: Exception) { false }
                if (!bound) complete(current, null, DecoderError.DECODER_CRASHED)
            }
        }
        return id
    }

    fun cancel() { main.post { cancelPending() } }
    fun cancel(requestId: Long) {
        main.post { if (pending?.id == requestId) cancelPending() }
    }

    private fun send(current: Pending) {
        if (current.sent) return
        try {
            service!!.send(Message.obtain(null, DecoderProtocol.OPEN).apply {
                data = DecoderProtocol.envelope(current.id).apply {
                    putParcelable(DecoderProtocol.FD_KEY, current.input)
                }
                replyTo = receiver
            })
            current.sent = true
            current.input.close()
        } catch (_: Exception) { disconnected() }
    }

    private fun complete(current: Pending, scene: ParcelFileDescriptor?, error: Int) {
        if (pending !== current) { scene?.close(); return }
        deadline?.let { main.removeCallbacks(it) }
        deadline = null
        pending = null
        current.input.close()
        try { current.completion(current.id, scene, error) }
        catch (_: Exception) { scene?.close() }
    }

    private fun cancelPending(error: Int = DecoderError.CANCELLED) {
        val current = pending ?: return
        if (current.sent) {
            try {
                service?.send(Message.obtain(null, DecoderProtocol.CANCEL).apply {
                    data = DecoderProtocol.envelope(current.id)
                })
            } catch (_: Exception) { }
        }
        complete(current, null, error)
    }

    private fun disconnected() {
        pending?.let { complete(it, null, DecoderError.DECODER_CRASHED) }
        disconnectBinding()
    }

    private fun disconnectBinding() {
        val recipient = death
        binder?.let {
            if (recipient != null) try { it.unlinkToDeath(recipient, 0) } catch (_: Exception) { }
        }
        death = null
        binder = null
        service = null
        decoderPid = 0
        if (bound) {
            bound = false
            connection?.let { context.unbindService(it) }
        }
        connection = null
        ++bindingGeneration
    }

    override fun close() {
        main.post {
            if (closed) return@post
            closed = true
            cancelPending()
            disconnectBinding()
        }
    }

    companion object { private val nextId = AtomicLong() }
}
