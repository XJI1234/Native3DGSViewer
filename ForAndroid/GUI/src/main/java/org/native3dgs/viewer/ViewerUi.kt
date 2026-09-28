package org.native3dgs.viewer

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView

internal class ViewerUi(private val context: Context, private val actions: Actions) {
    interface Actions {
        fun open()
        fun closeScene()
        fun fit()
        fun reset()
        fun flipY(enabled: Boolean): Boolean
        fun freeMode(enabled: Boolean): Boolean
        fun qualityMode(mobile: Boolean): Boolean
        fun cancel()
        fun retry()
        fun flyInput(right: Float, forward: Float, up: Float)
    }

    private val ink = Color.rgb(24, 33, 43)
    private val muted = Color.rgb(75, 87, 98)
    private val accent = Color.rgb(18, 109, 115)
    private val accentSoft = Color.rgb(221, 238, 239)
    private val line = Color.rgb(219, 225, 229)
    private val paper = Color.rgb(247, 248, 250)
    private val root = LinearLayout(context).apply {
        orientation = LinearLayout.VERTICAL
        setBackgroundColor(paper)
    }
    val view: View get() = root
    val surface = ViewerSurfaceView(context).apply { contentDescription = context.getString(R.string.viewport) }
    val viewport = FrameLayout(context)
    private val toolbar = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
    private val status = TextView(context).apply {
        setTextColor(muted); textSize = 12f; gravity = Gravity.CENTER_VERTICAL
        maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END
    }
    private val fileName = TextView(context).apply {
        setTextColor(muted); textSize = 12f; gravity = Gravity.CENTER_VERTICAL or Gravity.END
        maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.MIDDLE
    }
    private val empty = TextView(context).apply {
        setText(R.string.empty_scene)
        setTextColor(ink); textSize = 16f; typeface = Typeface.DEFAULT_BOLD
        setBackgroundColor(paper)
        gravity = Gravity.CENTER
    }
    private val busy = LinearLayout(context).apply {
        orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
        setPadding(dp(12), 0, dp(4), 0)
        background = shape(Color.WHITE, line)
    }
    private val busyText = TextView(context).apply {
        textSize = 13f; setTextColor(ink); setText(R.string.opening)
    }
    private val retry = command(context.getString(R.string.retry), actions::retry)
    private val freeControls = FrameLayout(context)
    private val joystick = JoystickView(context).apply {
        onInput = { x, y -> actions.flyInput(x, -y, elevation) }
    }
    private var elevation = 0f
    private var upPressed = false
    private var downPressed = false
    private val open = command(context.getString(R.string.open), true, actions::open)
    private val close = command(context.getString(R.string.close), actions::closeScene)
    private val fit = command(context.getString(R.string.fit), actions::fit)
    private val reset = command(context.getString(R.string.reset), actions::reset)
    private val flip: Button = command(context.getString(R.string.flip_y)) { toggleFlip() }
    private val fixed = command(context.getString(R.string.fixed)) { setMode(false, true) }
    private val free = command(context.getString(R.string.free)) { setMode(true, true) }
    private val fullQuality = command(context.getString(R.string.full_quality)) { setQuality(false, true) }
    private val mobileQuality = command(context.getString(R.string.mobile_quality)) { setQuality(true, true) }
    private val renderScale = TextView(context).apply {
        textSize = 12f; setTextColor(muted); gravity = Gravity.CENTER_VERTICAL
        minWidth = dp(44)
    }
    private var compact = false
    private var hasScene = false
    var isFree = false
        private set
    var isMobile = false
        private set

    init {
        root.addView(toolbar)
        viewport.addView(surface, FrameLayout.LayoutParams(-1, -1))
        viewport.addView(empty, FrameLayout.LayoutParams(-1, -1))
        val busyParams = FrameLayout.LayoutParams(-2, dp(52), Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL)
        busyParams.bottomMargin = dp(16)
        busy.addView(ProgressBar(context).apply { isIndeterminate = true }, LinearLayout.LayoutParams(dp(20), dp(20)))
        busy.addView(busyText, LinearLayout.LayoutParams(-2, -2).apply { marginStart = dp(10) })
        busy.addView(command(context.getString(R.string.cancel), actions::cancel), LinearLayout.LayoutParams(-2, dp(48)).apply { marginStart = dp(8) })
        viewport.addView(busy, busyParams)
        busy.visibility = View.GONE
        viewport.addView(freeControls, FrameLayout.LayoutParams(-1, -1))
        freeControls.addView(joystick, FrameLayout.LayoutParams(dp(116), dp(116), Gravity.START or Gravity.BOTTOM).apply {
            leftMargin = dp(20); bottomMargin = dp(20)
        })
        val lift = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        lift.addView(holdButton(context.getString(R.string.up), 1f))
        lift.addView(holdButton(context.getString(R.string.down), -1f))
        freeControls.addView(lift, FrameLayout.LayoutParams(dp(64), -2, Gravity.END or Gravity.BOTTOM).apply {
            rightMargin = dp(20); bottomMargin = dp(20)
        })
        freeControls.visibility = View.GONE
        root.addView(viewport, LinearLayout.LayoutParams(-1, 0, 1f))
        val footer = LinearLayout(context).apply {
            gravity = Gravity.CENTER_VERTICAL; setPadding(dp(16), 0, dp(16), 0)
            setBackgroundColor(Color.WHITE)
            addView(status, LinearLayout.LayoutParams(0, -1, 1f))
            addView(fileName, LinearLayout.LayoutParams(0, -1, 1f))
        }
        root.addView(footer, LinearLayout.LayoutParams(-1, dp(36)))
        setMode(false, false)
        setQuality(false, false)
        setRenderScale(1000)
        setScene(false)
        setStatus(context.getString(R.string.ready))
    }

    fun layout(widthDp: Int) {
        val nextCompact = widthDp < 760
        if (toolbar.childCount != 0 && compact == nextCompact) return
        compact = nextCompact
        listOf(open, close, fit, reset, flip, fixed, free, fullQuality, mobileQuality,
            renderScale).forEach { button ->
            (button.parent as? ViewGroup)?.removeView(button)
        }
        toolbar.removeAllViews()
        toolbar.setBackgroundColor(Color.WHITE)
        val title = TextView(context).apply {
            setText(R.string.app_name); textSize = 16f; setTextColor(ink)
            typeface = Typeface.DEFAULT_BOLD; gravity = Gravity.CENTER_VERTICAL
        }
        if (compact) {
            val upper = row()
            upper.addView(title, LinearLayout.LayoutParams(0, -1, 1f))
            upper.addView(open); upper.addView(fixed); upper.addView(free)
            toolbar.addView(upper, LinearLayout.LayoutParams(-1, dp(52)))
            val lower = row()
            lower.addView(close); lower.addView(fit); lower.addView(reset); lower.addView(flip)
            toolbar.addView(lower, LinearLayout.LayoutParams(-1, dp(52)))
            val quality = row()
            quality.addView(fullQuality); quality.addView(mobileQuality)
            quality.addView(renderScale, LinearLayout.LayoutParams(-2, -1).apply {
                marginStart = dp(12)
            })
            toolbar.addView(quality, LinearLayout.LayoutParams(-1, dp(48)))
        } else {
            val single = row()
            single.addView(title, LinearLayout.LayoutParams(0, -1, 1f))
            listOf(open, close, fit, reset, flip, fixed, free, fullQuality,
                mobileQuality).forEach(single::addView)
            single.addView(renderScale, LinearLayout.LayoutParams(-2, -1).apply {
                marginStart = dp(12)
            })
            toolbar.addView(single, LinearLayout.LayoutParams(-1, dp(60)))
        }
    }

    fun setScene(available: Boolean) {
        hasScene = available
        close.isEnabled = available; fit.isEnabled = available; reset.isEnabled = available
        flip.isEnabled = available
        fixed.isEnabled = available; free.isEnabled = available
        empty.visibility = if (available) View.GONE else View.VISIBLE
        freeControls.visibility = if (available && isFree) View.VISIBLE else View.GONE
    }

    fun setFileName(name: String) { fileName.text = name }
    fun setStatus(message: String) { status.text = message }
    fun setBusy(visible: Boolean, message: String) {
        busy.visibility = if (visible) View.VISIBLE else View.GONE
        busyText.text = message
    }
    fun setUnsupported(visible: Boolean) {
        retry.visibility = if (visible) View.VISIBLE else View.GONE
        if (visible) {
            empty.setText(R.string.unsupported_device)
            if (retry.parent == null) viewport.addView(retry, FrameLayout.LayoutParams(-2, dp(48), Gravity.CENTER).apply {
                topMargin = dp(96)
            })
            open.isEnabled = false
        } else {
            empty.setText(R.string.empty_scene)
            open.isEnabled = true
        }
    }
    fun setRestarting(restarting: Boolean) {
        open.isEnabled = !restarting && retry.visibility != View.VISIBLE
        retry.isEnabled = !restarting
    }
    fun setMode(freeMode: Boolean, notify: Boolean) {
        if (notify && !actions.freeMode(freeMode)) return
        if (isFree != freeMode) clearMovement()
        isFree = freeMode
        showToggle(fixed, !freeMode)
        showToggle(free, freeMode)
        freeControls.visibility = if (freeMode && hasScene) View.VISIBLE else View.GONE
    }
    fun setFlip(enabled: Boolean) { showToggle(flip, enabled) }
    fun setQuality(mobile: Boolean, notify: Boolean) {
        if (notify && !actions.qualityMode(mobile)) return
        isMobile = mobile
        showToggle(fullQuality, !mobile)
        showToggle(mobileQuality, mobile)
    }
    fun setRenderScale(permille: Int) {
        renderScale.text = context.getString(R.string.render_scale, permille / 10)
    }
    fun clearMovement() {
        upPressed = false
        downPressed = false
        elevation = 0f
        joystick.reset()
        actions.flyInput(0f, 0f, 0f)
    }

    private fun toggleFlip() {
        val enabled = !flip.isSelected
        if (actions.flipY(enabled)) setFlip(enabled)
    }

    private fun showToggle(button: Button, selected: Boolean) {
        button.isSelected = selected
        button.setTextColor(if (selected) accent else ink)
        button.background = shape(if (selected) accentSoft else Color.WHITE,
            if (selected) accent else line)
    }

    private fun row() = LinearLayout(context).apply {
        orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
        setPadding(dp(12), 0, dp(12), 0)
    }
    private fun command(label: String, primary: Boolean = false, action: () -> Unit): Button =
        Button(context).apply {
            text = label; textSize = 13f; isAllCaps = false
            minWidth = dp(56); minimumHeight = dp(48)
            setPadding(dp(8), 0, dp(8), 0)
            contentDescription = label
            setTextColor(if (primary) Color.WHITE else ink)
            background = shape(if (primary) accent else Color.WHITE, if (primary) accent else line)
            setOnClickListener { action() }
        }
    private fun command(label: String, action: () -> Unit) = command(label, false, action)
    private fun holdButton(label: String, value: Float): Button = command(label) {}.apply {
        var pointerId = -1
        fun update(pressed: Boolean) {
            if (value > 0) upPressed = pressed else downPressed = pressed
            elevation = (if (upPressed) 1f else 0f) - (if (downPressed) 1f else 0f)
            actions.flyInput(joystick.horizontal, -joystick.vertical, elevation)
        }
        setOnTouchListener { _, event ->
            when (event.actionMasked) {
                android.view.MotionEvent.ACTION_DOWN -> {
                    pointerId = event.getPointerId(0)
                    update(true)
                }
                android.view.MotionEvent.ACTION_POINTER_DOWN -> {
                    if (pointerId == -1) {
                        pointerId = event.getPointerId(event.actionIndex)
                        update(true)
                    }
                }
                android.view.MotionEvent.ACTION_POINTER_UP -> {
                    if (pointerId == event.getPointerId(event.actionIndex)) {
                        pointerId = (0 until event.pointerCount)
                            .firstOrNull { it != event.actionIndex }?.let(event::getPointerId) ?: -1
                        update(pointerId != -1)
                    }
                }
                android.view.MotionEvent.ACTION_UP, android.view.MotionEvent.ACTION_CANCEL -> {
                    pointerId = -1
                    update(false)
                    if (event.actionMasked == android.view.MotionEvent.ACTION_UP) performClick()
                }
                android.view.MotionEvent.ACTION_MOVE -> Unit
                else -> return@setOnTouchListener false
            }
            true
        }
    }
    private fun shape(fill: Int, stroke: Int) = GradientDrawable().apply {
        setColor(fill); setStroke(dp(1), stroke); cornerRadius = dp(6).toFloat()
    }
    private fun dp(value: Int) = (value * context.resources.displayMetrics.density + 0.5f).toInt()
}
