package org.native3dgs.sdk

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.view.PixelCopy
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

@RunWith(AndroidJUnit4::class)
class EngineIntegrationTest {
    @Test fun idleEngineCloseDoesNotStall() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        repeat(32) {
            val engine = Native3dgsEngine(context)
            runBlocking { engine.closeAndWait() }
            assertEquals(EnginePhase.Stopped, engine.state.value.phase)
        }
    }

    private fun fixture(context: android.content.Context): java.io.File {
        val file = java.io.File.createTempFile("engine-fixture-", ".ply", context.cacheDir)
        val header = "ply\nformat binary_little_endian 1.0\nelement vertex 1\n" +
            listOf("x", "y", "z", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1",
                "rot_2", "rot_3", "opacity", "f_dc_0", "f_dc_1", "f_dc_2")
                .joinToString("") { "property float $it\n" } + "end_header\n"
        val row = ByteBuffer.allocate(56).order(ByteOrder.LITTLE_ENDIAN)
        listOf(0f, 0f, 0f, -0.5f, -0.5f, -0.5f, 1f, 0f, 0f, 0f,
            3f, 1f, -1f, -1f).forEach { row.putFloat(it) }
        file.outputStream().use { it.write(header.toByteArray()); it.write(row.array()) }
        return file
    }

    @Test fun serviceToSdkToVulkanFrameAndClose() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val file = fixture(context)
        val activity = instrumentation.startActivitySync(
            Intent(context, EngineTestActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        ) as EngineTestActivity
        val engine = Native3dgsEngine(context)
        try {
            assertTrue(activity.created.await(10, TimeUnit.SECONDS))
            assertTrue(activity.sized.await(10, TimeUnit.SECONDS))
            val generation = engine.attach(activity.viewport.holder.surface)
            engine.resize(generation, activity.viewport.width, activity.viewport.height)
            val request = runBlocking { engine.open(Uri.fromFile(file)) }
            assertTrue(request > 0)
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20)
            while ((engine.state.value.phase != EnginePhase.Ready ||
                    engine.state.value.framesPresented == 0L) && System.nanoTime() < deadline)
                Thread.sleep(50)
            assertEquals(engine.state.value.toString(), EnginePhase.Ready, engine.state.value.phase)
            assertEquals(1L, engine.state.value.sceneCount)
            assertTrue(engine.state.value.framesPresented > 0)
            assertTrue(engine.state.value.lastFrameMicros > 0)
            engine.orbit(-50.0, 0.0)
            engine.flipAxes(false, true, false)
            val copied = CountDownLatch(1)
            val bitmap = Bitmap.createBitmap(activity.viewport.width, activity.viewport.height,
                Bitmap.Config.ARGB_8888)
            var copyResult = Int.MIN_VALUE
            val main = Handler(Looper.getMainLooper())
            main.post {
                PixelCopy.request(activity.viewport, bitmap, { result ->
                    copyResult = result
                    copied.countDown()
                }, main)
            }
            assertTrue(copied.await(5, TimeUnit.SECONDS))
            assertEquals(PixelCopy.SUCCESS, copyResult)
            val center = bitmap.getPixel(bitmap.width / 2, bitmap.height / 2)
            assertTrue(Color.red(center) != 255 || Color.green(center) != 255 ||
                Color.blue(center) != 255)
            bitmap.recycle()
            engine.closeScene()
            engine.detach(generation)
        } finally {
            runBlocking { engine.closeAndWait() }
            engine.close()
            instrumentation.runOnMainSync { activity.finish() }
            file.delete()
        }
    }
}
