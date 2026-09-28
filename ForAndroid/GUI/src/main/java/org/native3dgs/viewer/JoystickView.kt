package org.native3dgs.viewer

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

internal class JoystickView(context: Context) : View(context) {
    var onInput: (Float, Float) -> Unit = { _, _ -> }
    private val ring = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(200, 247, 248, 250); style = Paint.Style.FILL
    }
    private val border = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(116, 129, 137); style = Paint.Style.STROKE; strokeWidth = 2f * resources.displayMetrics.density
    }
    private val knob = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.rgb(18, 109, 115) }
    private var pointerId = -1
    var horizontal = 0f
        private set
    var vertical = 0f
        private set

    init { contentDescription = context.getString(R.string.joystick) }

    override fun onDraw(canvas: Canvas) {
        val radius = minOf(width, height) * 0.48f
        val cx = width / 2f
        val cy = height / 2f
        canvas.drawCircle(cx, cy, radius, ring)
        canvas.drawCircle(cx, cy, radius, border)
        canvas.drawCircle(cx + horizontal * radius * 0.6f,
            cy + vertical * radius * 0.6f, radius * 0.29f, knob)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> pointerId = event.getPointerId(0)
            MotionEvent.ACTION_MOVE -> if (pointerId < 0) return false
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (event.actionMasked == MotionEvent.ACTION_UP) performClick()
                reset(); pointerId = -1; return true
            }
            MotionEvent.ACTION_POINTER_UP -> if (event.getPointerId(event.actionIndex) == pointerId) {
                val next = (0 until event.pointerCount).firstOrNull { it != event.actionIndex }
                if (next == null) { reset(); pointerId = -1; return true }
                pointerId = event.getPointerId(next)
            }
            else -> return false
        }
        val index = event.findPointerIndex(pointerId)
        if (index < 0) { reset(); return true }
        val radius = minOf(width, height) * 0.48f
        if (radius <= 0f) return true
        var x = (event.getX(index) - width / 2f) / radius
        var y = (event.getY(index) - height / 2f) / radius
        val length = hypot(x, y)
        if (length > 1f) { x /= length; y /= length }
        horizontal = x; vertical = y
        onInput(x, y)
        invalidate()
        return true
    }

    fun reset() {
        horizontal = 0f; vertical = 0f
        onInput(0f, 0f)
        invalidate()
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }
}
