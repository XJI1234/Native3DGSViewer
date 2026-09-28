package org.native3dgs.viewer

import android.view.MotionEvent
import android.view.View
import kotlin.math.ln
import kotlin.math.hypot

/** Converts touch coordinates to the SDK's physical-pixel camera commands. */
internal class ViewportGestures(
    private val isFree: () -> Boolean,
    private val enabled: () -> Boolean,
    private val orbit: (Double, Double) -> Unit,
    private val pan: (Double, Double) -> Unit,
    private val dolly: (Double) -> Unit,
    private val look: (Double, Double) -> Unit,
) : View.OnTouchListener {
    private var x = 0f
    private var y = 0f
    private var span = 0f
    private var count = 0

    override fun onTouch(view: View, event: MotionEvent): Boolean {
        if (!enabled()) { count = 0; return true }
        if (isFree() && event.actionMasked == MotionEvent.ACTION_DOWN && event.x < view.width / 2f)
            return false
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN,
            MotionEvent.ACTION_POINTER_UP -> {
                val excluded = if (event.actionMasked == MotionEvent.ACTION_POINTER_UP) event.actionIndex else -1
                capture(event, excluded)
            }
            MotionEvent.ACTION_MOVE -> {
                val points = event.pointerCount
                val nextX = if (points >= 2) (event.getX(0) + event.getX(1)) / 2f else event.x
                val nextY = if (points >= 2) (event.getY(0) + event.getY(1)) / 2f else event.y
                val nextSpan = if (points >= 2) hypot(event.getX(1) - event.getX(0),
                    event.getY(1) - event.getY(0)) else 0f
                if (points == count) {
                    val dx = (nextX - x).toDouble()
                    val dy = (nextY - y).toDouble()
                    if (isFree()) look(dx, dy)
                    else if (points >= 2) {
                        pan(dx, dy)
                        if (span > 1f && nextSpan > 1f) dolly(ln(nextSpan / span).toDouble() * 4.0)
                    } else orbit(dx, dy)
                }
                x = nextX; y = nextY; span = nextSpan; count = points
            }
            MotionEvent.ACTION_CANCEL, MotionEvent.ACTION_UP -> {
                if (event.actionMasked == MotionEvent.ACTION_UP) view.performClick()
                count = 0
            }
        }
        return true
    }

    private fun capture(event: MotionEvent, excluded: Int) {
        val indices = (0 until event.pointerCount).filter { it != excluded }.take(2)
        count = indices.size
        if (indices.isEmpty()) return
        x = indices.map(event::getX).average().toFloat()
        y = indices.map(event::getY).average().toFloat()
        span = if (indices.size == 2) hypot(event.getX(indices[1]) - event.getX(indices[0]),
            event.getY(indices[1]) - event.getY(indices[0])) else 0f
    }
}
