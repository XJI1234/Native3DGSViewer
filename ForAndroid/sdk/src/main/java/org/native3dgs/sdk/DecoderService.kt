package org.native3dgs.sdk

import android.app.ActivityManager
import android.app.Service
import android.content.Intent
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import android.os.ParcelFileDescriptor
import android.os.Process
import android.os.RemoteException
import android.os.SystemClock
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/** Host-private decoder process. Binder messages carry metadata and descriptors only. */
internal class DecoderService : Service() {
    private val worker = Executors.newSingleThreadExecutor()
    private val requests = mutableMapOf<Long, AtomicBoolean>()
    private val handler = Handler(Looper.getMainLooper()) { message ->
        when (message.what) {
            DecoderProtocol.OPEN -> open(message)
            DecoderProtocol.CANCEL -> {
                if (message.data.getInt(DecoderProtocol.VERSION_KEY) == DecoderProtocol.VERSION)
                    requests[message.data.getLong(DecoderProtocol.ID_KEY)]?.set(true)
                DecoderProtocol.descriptor(message.data)?.close()
            }
            else -> DecoderProtocol.descriptor(message.data)?.close()
        }
        true
    }
    private val messenger = Messenger(handler)

    override fun onBind(intent: Intent): IBinder = messenger.binder

    private fun send(destination: Messenger, message: Message) {
        try { destination.send(message) } catch (_: RemoteException) { }
    }

    private fun open(message: Message) {
        val data = message.data
        val descriptor = DecoderProtocol.descriptor(data)
        val requestId = data.getLong(DecoderProtocol.ID_KEY)
        val destination = message.replyTo
        if (data.getInt(DecoderProtocol.VERSION_KEY) != DecoderProtocol.VERSION ||
            requestId <= 0 || descriptor == null || destination == null ||
            requests.containsKey(requestId)) {
            descriptor?.close()
            if (destination != null) {
                send(destination, Message.obtain(null, DecoderProtocol.COMPLETE).apply {
                    this.data = DecoderProtocol.envelope(requestId).apply {
                        putInt(DecoderProtocol.ERROR_KEY, DecoderError.UNSUPPORTED_VERSION)
                        putString(DecoderProtocol.DIAGNOSTIC_KEY, "Invalid decoder request")
                    }
                })
            }
            return
        }
        if (requests.size >= 8) {
            descriptor.close()
            send(destination, Message.obtain(null, DecoderProtocol.COMPLETE).apply {
                this.data = DecoderProtocol.envelope(requestId).apply {
                    putInt(DecoderProtocol.ERROR_KEY, DecoderError.RESOURCE_LIMIT)
                    putString(DecoderProtocol.DIAGNOSTIC_KEY, "Decoder request queue capacity")
                }
            })
            return
        }
        val cancellation = AtomicBoolean()
        requests[requestId] = cancellation
        worker.execute {
            var transferred: ParcelFileDescriptor? = null
            try {
                descriptor.use { input ->
                    send(destination, Message.obtain(null, DecoderProtocol.PROGRESS).apply {
                        this.data = DecoderProtocol.envelope(requestId).apply {
                            putInt(DecoderProtocol.STAGE_KEY, 0)
                            putInt(DecoderProtocol.PROCESS_KEY, Process.myPid())
                        }
                    })
                    val memory = ActivityManager.MemoryInfo()
                    getSystemService(ActivityManager::class.java).getMemoryInfo(memory)
                    val observer = object : DecodeObserver {
                        private var lastStage = -1
                        private var lastPublished = 0L
                        override fun cancelled() = cancellation.get()
                        override fun progress(stage: Int, done: Long, total: Long) {
                            val now = SystemClock.elapsedRealtime()
                            if (stage == lastStage && now - lastPublished < 100) return
                            lastStage = stage
                            lastPublished = now
                            val progress = DecoderProtocol.envelope(requestId).apply {
                                putInt(DecoderProtocol.STAGE_KEY, stage)
                                putLong(DecoderProtocol.DONE_KEY, done)
                                putLong(DecoderProtocol.TOTAL_KEY, total)
                                putInt(DecoderProtocol.PROCESS_KEY, Process.myPid())
                            }
                            send(destination, Message.obtain(null, DecoderProtocol.PROGRESS).apply {
                                this.data = progress
                            })
                        }
                    }
                    val result = NativeDecoder.decode(input.fd,
                        (memory.availMem - memory.threshold).coerceAtLeast(1),
                        cacheDir.absolutePath, observer)
                    if (result.fd >= 0) transferred = ParcelFileDescriptor.adoptFd(result.fd)
                    val completion = DecoderProtocol.envelope(requestId).apply {
                        putInt(DecoderProtocol.ERROR_KEY, if (cancellation.get()) DecoderError.CANCELLED else result.error)
                        putInt(DecoderProtocol.STAGE_KEY, result.stage)
                        putString(DecoderProtocol.DIAGNOSTIC_KEY, result.diagnostic.take(512))
                        if (!cancellation.get()) putParcelable(DecoderProtocol.FD_KEY, transferred)
                    }
                    send(destination, Message.obtain(null, DecoderProtocol.COMPLETE).apply {
                        this.data = completion
                    })
                }
            } catch (_: Exception) {
                send(destination, Message.obtain(null, DecoderProtocol.COMPLETE).apply {
                    this.data = DecoderProtocol.envelope(requestId).apply {
                        putInt(DecoderProtocol.ERROR_KEY, DecoderError.DECODER_FAILURE)
                        putString(DecoderProtocol.DIAGNOSTIC_KEY, "Decoder execution failed")
                    }
                })
            } finally {
                transferred?.close()
                handler.post { requests.remove(requestId) }
            }
        }
    }

    override fun onDestroy() {
        requests.values.forEach { it.set(true) }
        worker.shutdown()
        super.onDestroy()
    }
}
