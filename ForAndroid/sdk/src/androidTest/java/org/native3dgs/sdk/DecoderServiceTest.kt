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
import android.os.Process
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

@RunWith(AndroidJUnit4::class)
class DecoderServiceTest {
    private fun fixture(context: Context): java.io.File {
        val file = java.io.File.createTempFile("decoder-fixture-", ".ply", context.cacheDir)
        val header = "ply\nformat binary_little_endian 1.0\nelement vertex 1\n" +
            listOf("x", "y", "z", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1",
                "rot_2", "rot_3", "opacity", "f_dc_0", "f_dc_1", "f_dc_2")
                .joinToString("") { "property float $it\n" } + "end_header\n"
        val row = ByteBuffer.allocate(56).order(ByteOrder.LITTLE_ENDIAN)
        listOf(1f, 2f, 3f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 0f, 0f, 0f)
            .forEach { row.putFloat(it) }
        file.outputStream().use { it.write(header.toByteArray()); it.write(row.array()) }
        return file
    }

    @Test fun decodeInSeparateProcessAndValidateTransferredScene() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val bound = CountDownLatch(1)
        val completed = CountDownLatch(1)
        var service: Messenger? = null
        var result: ParcelFileDescriptor? = null
        var error = Int.MIN_VALUE
        var decoderPid = 0
        val receiver = Messenger(Handler(Looper.getMainLooper()) { message ->
            if (message.what == DecoderProtocol.PROGRESS) {
                decoderPid = message.data.getInt(DecoderProtocol.PROCESS_KEY)
            }
            if (message.what == DecoderProtocol.COMPLETE) {
                result = DecoderProtocol.descriptor(message.data)
                error = message.data.getInt(DecoderProtocol.ERROR_KEY)
                completed.countDown()
            }
            true
        })
        val connection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName, binder: IBinder) {
                service = Messenger(binder)
                bound.countDown()
            }
            override fun onServiceDisconnected(name: ComponentName) { }
        }
        val fixture = fixture(context)
        var isBound = false
        try {
            isBound = context.bindService(Intent(context, DecoderService::class.java),
                connection, Context.BIND_AUTO_CREATE)
            assertTrue(isBound)
            assertTrue(bound.await(10, TimeUnit.SECONDS))
            ParcelFileDescriptor.open(fixture, ParcelFileDescriptor.MODE_READ_ONLY).use { input ->
                service!!.send(Message.obtain(null, DecoderProtocol.OPEN).apply {
                    data = DecoderProtocol.envelope(1).apply {
                        putParcelable(DecoderProtocol.FD_KEY, input)
                    }
                    replyTo = receiver
                })
            }
            assertTrue(completed.await(15, TimeUnit.SECONDS))
            assertEquals(-1, error)
            assertNotNull(result)
            assertTrue(decoderPid > 0 && decoderPid != Process.myPid())
            assertEquals(1L, NativeDecoder.validateScene(result!!.fd, 1L shl 20))
        } finally {
            result?.close()
            if (isBound) context.unbindService(connection)
            fixture.delete()
        }
    }

    @Test fun cancelIdleProviderReturnsTerminalResult() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val client = DecoderClient(context)
        val pipe = ParcelFileDescriptor.createPipe()
        val completed = CountDownLatch(1)
        var error = Int.MIN_VALUE
        try {
            client.open(pipe[0]) { _, descriptor, value ->
                descriptor?.close()
                error = value
                completed.countDown()
            }
            pipe[0].close()
            client.cancel()
            assertTrue(completed.await(5, TimeUnit.SECONDS))
            assertEquals(12, error)
        } finally {
            client.close()
            pipe.forEach { it.close() }
        }
    }

    @Test fun timeoutThenRebindAllowsNextDecode() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val client = DecoderClient(context, timeoutMillis = 2_000)
        val pipe = ParcelFileDescriptor.createPipe()
        val timedOut = CountDownLatch(1)
        val reopened = CountDownLatch(1)
        var timeoutError = Int.MIN_VALUE
        var nextError = Int.MIN_VALUE
        val file = fixture(context)
        try {
            client.open(pipe[0]) { _, descriptor, value ->
                descriptor?.close()
                timeoutError = value
                timedOut.countDown()
            }
            pipe[0].close()
            assertTrue(timedOut.await(10, TimeUnit.SECONDS))
            assertEquals(13, timeoutError)
            ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY).use { input ->
                client.open(input) { _, descriptor, value ->
                    nextError = value
                    descriptor?.close()
                    reopened.countDown()
                }
            }
            assertTrue(reopened.await(10, TimeUnit.SECONDS))
            assertEquals(-1, nextError)
        } finally {
            client.close()
            pipe.forEach { it.close() }
            file.delete()
        }
    }

    @Test fun decoderDeathReturnsCrashedAndNextRequestRebinds() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val client = DecoderClient(context)
        val pipe = ParcelFileDescriptor.createPipe()
        val started = CountDownLatch(1)
        val crashed = CountDownLatch(1)
        val reopened = CountDownLatch(1)
        var decoderPid = 0
        var crashError = Int.MIN_VALUE
        var reopenError = Int.MIN_VALUE
        val file = fixture(context)
        try {
            client.open(pipe[0], progress = { state ->
                decoderPid = state.processId
                started.countDown()
            }) { _, descriptor, error ->
                descriptor?.close()
                crashError = error
                crashed.countDown()
            }
            pipe[0].close()
            assertTrue(started.await(10, TimeUnit.SECONDS))
            assertTrue(decoderPid > 0 && decoderPid != Process.myPid())
            Process.killProcess(decoderPid)
            assertTrue(crashed.await(10, TimeUnit.SECONDS))
            assertEquals(DecoderError.DECODER_CRASHED, crashError)
            ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY).use { input ->
                client.open(input) { _, descriptor, error ->
                    descriptor?.close()
                    reopenError = error
                    reopened.countDown()
                }
            }
            assertTrue(reopened.await(10, TimeUnit.SECONDS))
            assertEquals(-1, reopenError)
        } finally {
            client.close()
            pipe.forEach { it.close() }
            file.delete()
        }
    }
}
