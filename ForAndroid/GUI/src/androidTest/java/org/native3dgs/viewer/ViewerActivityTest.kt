package org.native3dgs.viewer

import android.content.Intent
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ViewerActivityTest {
    @Test fun emptyViewerKeepsCommandsAccessibleWithoutAModel() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val activity = instrumentation.startActivitySync(
            Intent(context, ViewerActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        ) as ViewerActivity
        try {
            instrumentation.waitForIdleSync()
            val root = activity.window.decorView
            val surface = find(root, SurfaceView::class.java)
            assertNotNull(surface)
            assertTrue("Viewport must use landscape dimensions", surface!!.width > surface.height)
            val buttons = allButtons(root)
            for (label in listOf("打开", "关闭", "适配", "重置", "翻转 Y", "固定", "自由",
                "全质量", "移动画质")) {
                assertTrue("Missing $label", buttons.any { it.text.toString() == label })
            }
            val minimum = 48 * context.resources.displayMetrics.density
            assertTrue(buttons.filter { it.isShown }.all { it.height >= minimum - 1 })
        } finally {
            instrumentation.runOnMainSync { activity.finish() }
        }
    }

    @Test fun touchGesturesKeepHorizontalAndVerticalAxesSeparate() {
        val view = View(InstrumentationRegistry.getInstrumentation().targetContext)
        val orbit = mutableListOf<Pair<Double, Double>>()
        val gestures = ViewportGestures({ false }, { true },
            { x, y -> orbit += x to y }, { _, _ -> }, { _ -> }, { _, _ -> })
        fun touch(action: Int, x: Float, y: Float) {
            val event = android.view.MotionEvent.obtain(0, 0, action, x, y, 0)
            try { gestures.onTouch(view, event) } finally { event.recycle() }
        }
        touch(android.view.MotionEvent.ACTION_DOWN, 100f, 100f)
        touch(android.view.MotionEvent.ACTION_MOVE, 100f, 180f)
        assertEquals(0.0, orbit.last().first, 0.0)
        assertEquals(80.0, orbit.last().second, 0.0)
        touch(android.view.MotionEvent.ACTION_MOVE, 145f, 180f)
        assertEquals(45.0, orbit.last().first, 0.0)
        assertEquals(0.0, orbit.last().second, 0.0)
    }

    @Test fun elevationButtonsKeepTheRemainingHoldActive() {
        InstrumentationRegistry.getInstrumentation().runOnMainSync {
            verifyElevationButtonsKeepTheRemainingHoldActive()
        }
    }

    @Test fun repeatedRetryKeepsOpenDisabledUntilInitializationCompletes() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val activity = instrumentation.startActivitySync(
            Intent(context, ViewerActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        ) as ViewerActivity
        try {
            instrumentation.waitForIdleSync()
            val root = activity.window.decorView
            val open = allButtons(root).single { it.text == context.getString(R.string.open) }
            instrumentation.runOnMainSync {
                activity.retry()
                activity.retry()
                assertFalse(open.isEnabled)
            }
            var recovered = false
            for (attempt in 0 until 50) {
                instrumentation.runOnMainSync { recovered = open.isEnabled }
                if (recovered) break
                Thread.sleep(100)
            }
            assertTrue("Open must recover after retry", recovered)
            var statusReady = false
            instrumentation.runOnMainSync {
                statusReady = allText(root).any { it.text == context.getString(R.string.ready) }
            }
            assertTrue("Retry status must clear", statusReady)
        } finally {
            instrumentation.runOnMainSync { activity.finish() }
        }
    }

    private fun verifyElevationButtonsKeepTheRemainingHoldActive() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        var elevation = 0f
        val ui = ViewerUi(context, object : ViewerUi.Actions {
            override fun open() {}
            override fun closeScene() {}
            override fun fit() {}
            override fun reset() {}
            override fun flipY(enabled: Boolean) = true
            override fun freeMode(enabled: Boolean) = true
            override fun qualityMode(mobile: Boolean) = true
            override fun cancel() {}
            override fun retry() {}
            override fun flyInput(right: Float, forward: Float, up: Float) { elevation = up }
        })
        ui.setScene(true)
        ui.setMode(true, false)
        val buttons = allButtons(ui.view)
        val up = buttons.single { it.text == context.getString(R.string.up) }
        val down = buttons.single { it.text == context.getString(R.string.down) }
        fun touch(button: Button, action: Int) {
            val event = android.view.MotionEvent.obtain(0, 0, action, 20f, 20f, 0)
            try { button.dispatchTouchEvent(event) } finally { event.recycle() }
        }
        touch(up, android.view.MotionEvent.ACTION_DOWN)
        assertEquals(1f, elevation, 0f)
        touch(down, android.view.MotionEvent.ACTION_DOWN)
        assertEquals(0f, elevation, 0f)
        touch(up, android.view.MotionEvent.ACTION_UP)
        assertEquals(-1f, elevation, 0f)
        touch(down, android.view.MotionEvent.ACTION_UP)
        assertEquals(0f, elevation, 0f)
        touch(down, android.view.MotionEvent.ACTION_DOWN)
        touch(up, android.view.MotionEvent.ACTION_DOWN)
        touch(down, android.view.MotionEvent.ACTION_UP)
        assertEquals(1f, elevation, 0f)
    }

    private fun <T : View> find(view: View, type: Class<T>): T? {
        if (type.isInstance(view)) return type.cast(view)
        if (view is ViewGroup) for (index in 0 until view.childCount)
            find(view.getChildAt(index), type)?.let { return it }
        return null
    }

    private fun allButtons(view: View): List<Button> {
        val found = mutableListOf<Button>()
        if (view is Button) found += view
        if (view is ViewGroup) for (index in 0 until view.childCount)
            found += allButtons(view.getChildAt(index))
        return found
    }

    private fun allText(view: View): List<TextView> {
        val found = mutableListOf<TextView>()
        if (view is TextView) found += view
        if (view is ViewGroup) for (index in 0 until view.childCount)
            found += allText(view.getChildAt(index))
        return found
    }
}
