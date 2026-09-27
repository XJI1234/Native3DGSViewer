package org.native3dgs.sdk

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Color
import android.view.PixelCopy
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.TimeUnit

@RunWith(AndroidJUnit4::class)
class SurfaceProbeTest {
    @Test fun presentsSortedGaussianScene() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val activity = instrumentation.startActivitySync(
            Intent(context, SurfaceProbeActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK).putExtra("splat", true)
        ) as SurfaceProbeActivity
        try {
            assertTrue(activity.completed.await(20, TimeUnit.SECONDS))
            assertEquals(0, activity.probeResult)
            assertEquals(PixelCopy.SUCCESS, activity.copyResult)
            val bitmap = activity.captured!!
            val center = bitmap.getPixel(bitmap.width / 2, bitmap.height / 2)
            assertTrue("front red should dominate back blue", Color.red(center) > Color.blue(center))
            assertTrue(Color.red(center) > Color.green(center))
            assertEquals(Color.WHITE, bitmap.getPixel(10, 10))
            bitmap.recycle()
        } finally {
            instrumentation.runOnMainSync { activity.finish() }
        }
    }

    @Test fun presentsNonblankTriangleToNativeWindow() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val activity = instrumentation.startActivitySync(
            Intent(context, SurfaceProbeActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        ) as SurfaceProbeActivity
        try {
            assertTrue(activity.completed.await(15, TimeUnit.SECONDS))
            assertEquals(0, activity.probeResult)
            assertEquals(PixelCopy.SUCCESS, activity.copyResult)
            val bitmap = activity.captured!!
            val center = bitmap.getPixel(bitmap.width / 2, bitmap.height / 2)
            assertTrue(Color.red(center) in 10..240)
            assertTrue(Color.green(center) in 10..240)
            assertTrue(Color.blue(center) in 10..240)
            assertEquals(Color.WHITE, bitmap.getPixel(10, 10))
            val output = java.io.File(context.getExternalFilesDir(null), "surface-probe.png")
            output.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            bitmap.recycle()
        } finally {
            instrumentation.runOnMainSync { activity.finish() }
        }
    }
}
