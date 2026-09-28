package org.native3dgs.viewer

import android.app.Activity
import android.content.Intent
import android.content.res.Configuration
import android.database.Cursor
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.provider.OpenableColumns
import android.util.Log
import android.view.Choreographer
import android.view.SurfaceHolder
import android.view.View
import android.view.WindowInsets
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.native3dgs.sdk.EngineException
import org.native3dgs.sdk.EnginePhase
import org.native3dgs.sdk.EngineState
import org.native3dgs.sdk.Native3dgsEngine
import org.native3dgs.sdk.QualityMode

class ViewerActivity : Activity(), ViewerUi.Actions, SurfaceHolder.Callback {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private lateinit var ui: ViewerUi
    private var engine: Native3dgsEngine? = null
    private var state = EngineState()
    private var generation = 0L
    private var resumed = false
    private var opening: Job? = null
    private var stateJob: Job? = null
    private var request = 0L
    private var loadStartedAtNanos = 0L
    private var selection = 0L
    private var pendingName: String? = null
    private var activeName = ""
    private var message: String? = null
    private var unsupported = false
    private var restarting = false
    private var right = 0f
    private var forward = 0f
    private var up = 0f
    private var lastFrameNanos = 0L
    private var framePosted = false
    private val adaptiveResolution = AdaptiveResolution()
    private var lastPresentCallNanos = 0L
    private var fixedBuffer = false
    private var requestedBufferWidth = 0
    private var requestedBufferHeight = 0
    private var qualitySettings: List<Int>? = null
    private val frame = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            framePosted = false
            val dt = if (lastFrameNanos == 0L) 0.0 else
                ((frameTimeNanos - lastFrameNanos) / 1e9).coerceIn(0.0, 0.05)
            lastFrameNanos = frameTimeNanos
            if (resumed && ui.isFree && usableScene() && dt > 0 &&
                (right != 0f || forward != 0f || up != 0f)) {
                command { fly(right.toDouble(), up.toDouble(), forward.toDouble(), dt) }
            }
            if (movementActive()) postFrame() else lastFrameNanos = 0L
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ui = ViewerUi(this, this)
        setContentView(ui.view)
        ui.view.setOnApplyWindowInsetsListener { view, insets ->
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                view.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            } else {
                @Suppress("DEPRECATION")
                val left = insets.systemWindowInsetLeft
                @Suppress("DEPRECATION")
                val top = insets.systemWindowInsetTop
                @Suppress("DEPRECATION")
                val right = insets.systemWindowInsetRight
                @Suppress("DEPRECATION")
                val bottom = insets.systemWindowInsetBottom
                val cutout = insets.displayCutout
                view.setPadding(maxOf(left, cutout?.safeInsetLeft ?: 0),
                    maxOf(top, cutout?.safeInsetTop ?: 0),
                    maxOf(right, cutout?.safeInsetRight ?: 0),
                    maxOf(bottom, cutout?.safeInsetBottom ?: 0))
            }
            insets
        }
        ui.layout(resources.configuration.screenWidthDp)
        ui.surface.addOnLayoutChangeListener { _, left, top, right, bottom,
            oldLeft, oldTop, oldRight, oldBottom ->
            if (right - left != oldRight - oldLeft || bottom - top != oldBottom - oldTop)
                applyQuality()
        }
        ui.surface.holder.addCallback(this)
        ui.surface.setOnTouchListener(ViewportGestures(
            { ui.isFree }, { usableScene() },
            { x, y -> command { orbit(x, y) } },
            { x, y ->
                val displayHeight = ui.surface.height
                val bufferHeight = ui.surface.holder.surfaceFrame.height()
                val ratio = if (displayHeight > 0 && bufferHeight > 0)
                    bufferHeight.toDouble() / displayHeight else 1.0
                command { pan(x * ratio, y * ratio) }
            },
            { steps -> command { dolly(steps) } },
            { x, y -> command { look(x, y) } },
        ))
        initializeEngine()
    }

    private fun initializeEngine() {
        try {
            val created = Native3dgsEngine(applicationContext)
            engine = created
            qualitySettings = null
            unsupported = false
            message = null
            ui.setUnsupported(false)
            if (resumed) bindSurface()
            stateJob = scope.launch { created.state.collect { snapshot ->
                if (engine === created) {
                    state = snapshot
                    updateAdaptiveResolution(created)
                    render()
                }
            } }
        } catch (error: LinkageError) {
            Log.e(TAG, "Native library initialization failed", error)
            unsupported = true
            message = getString(R.string.device_unavailable)
            ui.setUnsupported(true)
            render()
        } catch (error: Exception) {
            Log.e(TAG, "Engine initialization failed: ${error.javaClass.simpleName}")
            unsupported = true
            message = getString(R.string.device_unavailable)
            ui.setUnsupported(true)
            render()
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        ui.layout(newConfig.screenWidthDp)
    }

    override fun onResume() {
        super.onResume()
        resumed = true
        bindSurface()
        lastFrameNanos = 0L
        if (movementActive()) postFrame()
    }

    override fun onPause() {
        resumed = false
        stopFrames()
        ui.clearMovement()
        lastFrameNanos = 0L
        unbindSurface()
        super.onPause()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (!hasFocus) ui.clearMovement()
    }

    override fun onDestroy() {
        selection++
        opening?.cancel()
        ui.clearMovement()
        unbindSurface()
        val closing = engine
        engine = null
        scope.cancel()
        if (closing != null) CoroutineScope(Dispatchers.IO).launch { closing.closeAndWait() }
        super.onDestroy()
    }

    override fun surfaceCreated(holder: SurfaceHolder) = bindSurface()

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        bindSurface()
        if (generation != 0L && width > 0 && height > 0)
            command { resize(generation, width, height) }
        applyQuality()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) = unbindSurface()

    private fun bindSurface() {
        val current = engine ?: return
        val holder = ui.surface.holder
        if (!resumed || generation != 0L || !holder.surface.isValid) return
        try {
            generation = current.attach(holder.surface)
            val frame = holder.surfaceFrame
            if (frame.width() > 0 && frame.height() > 0)
                current.resize(generation, frame.width(), frame.height())
        } catch (error: EngineException) {
            showCommandError(error)
        }
    }

    private fun unbindSurface() {
        val old = generation
        generation = 0L
        if (old != 0L) try { engine?.detach(old) }
        catch (error: EngineException) { Log.w(TAG, "Surface detach failed: ${error.code}") }
    }

    override fun open() {
        val picker = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/octet-stream", "application/x-ply", "*/*"))
        }
        startActivityForResult(picker, PICK_MODEL)
    }

    @Deprecated("Result callback for platform document picker")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != PICK_MODEL || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        openDocument(uri)
    }

    private fun openDocument(uri: Uri) {
        val current = engine ?: return
        loadStartedAtNanos = SystemClock.elapsedRealtimeNanos()
        selection++
        val token = selection
        opening?.cancel()
        if (request != 0L) try { current.cancel(request) }
        catch (error: EngineException) { Log.w(TAG, "Previous request cancel failed: ${error.code}") }
        request = 0L
        pendingName = getString(R.string.selected_model)
        message = null
        render()
        opening = scope.launch {
            try {
                val name = withContext(Dispatchers.IO) { displayName(uri) }
                if (token != selection) return@launch
                pendingName = name
                render()
                val id = current.open(uri)
                if (token != selection) {
                    try { current.cancel(id) } catch (_: EngineException) { }
                    return@launch
                }
                request = id
                render()
            } catch (_: CancellationException) {
                // SDK cancels the isolated decoder through coroutine cancellation.
            } catch (error: Exception) {
                if (token == selection) {
                    pendingName = null
                    message = getString(if (error is EngineException && error.code == 5)
                        R.string.out_of_memory else R.string.open_failed)
                    Log.w(TAG, "Open failed: ${(error as? EngineException)?.code ?: -1}")
                    render()
                }
            }
        }
    }

    override fun cancel() {
        loadStartedAtNanos = 0L
        selection++
        opening?.cancel()
        if (request != 0L) try { engine?.cancel(request) }
        catch (error: EngineException) { Log.w(TAG, "Cancel failed: ${error.code}") }
        request = 0L
        pendingName = null
        message = getString(if (state.sceneCount > 0) R.string.cancelled_kept_scene else R.string.cancelled)
        render()
    }

    override fun closeScene() {
        cancel()
        if (!command { closeScene() }) return
        ui.setMode(false, false)
        ui.setFlip(false)
        activeName = ""
        message = null
        render()
    }

    override fun fit() {
        if (command { fit() }) ui.setMode(false, false)
    }
    override fun reset() { command { reset() } }
    override fun flipY(enabled: Boolean) = command { flipAxes(false, enabled, false) }
    override fun freeMode(enabled: Boolean) = command {
        if (enabled) freeMode() else fixedMode()
    }
    override fun qualityMode(mobile: Boolean): Boolean {
        val previous = ui.isMobile
        ui.setQuality(mobile, false)
        adaptiveResolution.reset()
        lastPresentCallNanos = 0L
        if (applyQuality()) return true
        ui.setQuality(previous, false)
        applyQuality()
        return false
    }

    private fun applyQuality(): Boolean {
        val current = engine ?: return true
        val width = ui.surface.width
        val height = ui.surface.height
        if (width <= 0 || height <= 0) return true
        val scale = if (ui.isMobile) adaptiveResolution.scalePermille else 1000
        val mode = if (ui.isMobile) QualityMode.Mobile else QualityMode.Full
        val settings = listOf(mode.ordinal, scale, width, height)
        if (settings != qualitySettings) {
            try { current.setQuality(mode, scale, width, height) }
            catch (error: EngineException) { showCommandError(error); return false }
            qualitySettings = settings
        }
        if (ui.isMobile) {
            val bufferWidth = maxOf(1, width * scale / 1000)
            val bufferHeight = maxOf(1, height * scale / 1000)
            if (!fixedBuffer || bufferWidth != requestedBufferWidth ||
                bufferHeight != requestedBufferHeight) {
                requestedBufferWidth = bufferWidth
                requestedBufferHeight = bufferHeight
                fixedBuffer = true
                ui.surface.holder.setFixedSize(bufferWidth, bufferHeight)
                lastPresentCallNanos = 0L
            }
        } else if (fixedBuffer) {
            fixedBuffer = false
            requestedBufferWidth = 0
            requestedBufferHeight = 0
            ui.surface.holder.setSizeFromLayout()
            lastPresentCallNanos = 0L
        }
        return true
    }

    private fun updateAdaptiveResolution(current: Native3dgsEngine) {
        val samples = try { current.drainFrameSamples() }
        catch (_: EngineException) { return }
        if (!resumed || !ui.isMobile || state.sceneCount == 0L) {
            lastPresentCallNanos = 0L
            return
        }
        val now = SystemClock.elapsedRealtimeNanos()
        for (sample in samples) {
            val interval = sample.presentCallNanos - lastPresentCallNanos
            lastPresentCallNanos = sample.presentCallNanos
            if (interval !in 1..200_000_000L) continue
            if (adaptiveResolution.observe(sample.frameId, interval / 1000, now) != null) {
                applyQuality()
                break
            }
        }
    }
    override fun retry() {
        if (restarting) return
        restarting = true
        val old = engine
        old?.close()
        selection++
        opening?.cancel()
        ui.clearMovement()
        ui.setMode(false, false)
        ui.setFlip(false)
        ui.setQuality(false, false)
        adaptiveResolution.reset()
        qualitySettings = null
        fixedBuffer = false
        ui.surface.holder.setSizeFromLayout()
        activeName = ""
        loadStartedAtNanos = 0L
        pendingName = null
        request = 0L
        stateJob?.cancel()
        stateJob = null
        unbindSurface()
        engine = null
        state = EngineState()
        unsupported = false
        message = getString(R.string.checking_device)
        ui.setUnsupported(false)
        render()
        scope.launch {
            try {
                withContext(Dispatchers.IO) { old?.closeAndWait() }
                initializeEngine()
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                Log.e(TAG, "Engine restart failed: ${error.javaClass.simpleName}")
                unsupported = true
                message = getString(R.string.device_unavailable)
            } finally {
                if (scope.isActive) {
                    restarting = false
                    render()
                }
            }
        }
    }
    override fun flyInput(right: Float, forward: Float, up: Float) {
        this.right = right; this.forward = forward; this.up = up
        if (movementActive()) postFrame() else stopFrames()
    }

    private fun command(action: Native3dgsEngine.() -> Unit): Boolean {
        val current = engine ?: return false
        return try { current.action(); true }
        catch (error: EngineException) { showCommandError(error); false }
    }

    private fun showCommandError(error: EngineException) {
        Log.w(TAG, "Engine command failed: ${error.code}")
        if (error.code == 4 && state.sceneCount == 0L) unsupported = true
        message = getString(if (error.code == 4) R.string.device_unsupported else R.string.operation_failed)
        render()
    }

    private fun usableScene() = resumed && state.sceneCount > 0 &&
        state.phase != EnginePhase.Recovering && state.phase != EnginePhase.Failed &&
        state.phase != EnginePhase.Stopping && state.phase != EnginePhase.Stopped

    private fun render() {
        if (request != 0L && state.activeRequestId == request && state.phase == EnginePhase.Ready) {
            if (loadStartedAtNanos != 0L && Log.isLoggable("Native3DGSPerf", Log.DEBUG))
                Log.d("Native3DGSPerf", "open_to_ready ms=${(SystemClock.elapsedRealtimeNanos() - loadStartedAtNanos) / 1_000_000}")
            loadStartedAtNanos = 0L
            ui.setMode(false, false)
            activeName = pendingName.orEmpty()
            pendingName = null
            request = 0L
            message = null
        } else if (request != 0L && state.requestId == request && state.error != 0 &&
            state.phase != EnginePhase.Loading && state.phase != EnginePhase.Uploading) {
            request = 0L
            loadStartedAtNanos = 0L
            pendingName = null
            message = getString(R.string.open_failed)
        }
        ui.setScene(state.sceneCount > 0)
        val recoveryFailed = state.error in 4..5 &&
            (state.phase == EnginePhase.Recovering || state.phase == EnginePhase.Failed)
        ui.setUnsupported(unsupported || recoveryFailed)
        ui.setRestarting(restarting)
        ui.setFileName(activeName)
        ui.setRenderScale(state.actualScalePermille)
        val isBusy = pendingName != null
        ui.setBusy(isBusy, getString(if (state.phase == EnginePhase.Uploading)
            R.string.uploading else R.string.opening))
        ui.setStatus(message ?: when (state.phase) {
            EnginePhase.Loading -> getString(R.string.loading)
            EnginePhase.Uploading -> getString(R.string.uploading)
            EnginePhase.Recovering -> getString(R.string.recovering)
            EnginePhase.Failed -> getString(R.string.render_failed)
            else -> getString(if (unsupported) R.string.device_unavailable else R.string.ready)
        })
        if (movementActive()) postFrame() else stopFrames()
    }

    private fun displayName(uri: Uri): String {
        try {
            contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor: Cursor ->
                if (cursor.moveToFirst()) {
                    val name = cursor.getString(0)
                    if (!name.isNullOrBlank()) return name.take(160)
                }
            }
        } catch (_: RuntimeException) { }
        return getString(R.string.selected_model)
    }

    private fun postFrame() {
        if (!framePosted) { framePosted = true; Choreographer.getInstance().postFrameCallback(frame) }
    }

    private fun movementActive() = resumed && ui.isFree && usableScene() &&
        (right != 0f || forward != 0f || up != 0f)

    private fun stopFrames() {
        if (framePosted) Choreographer.getInstance().removeFrameCallback(frame)
        framePosted = false
        lastFrameNanos = 0L
    }

    companion object {
        private const val TAG = "Native3dgsViewer"
        private const val PICK_MODEL = 1
    }
}
