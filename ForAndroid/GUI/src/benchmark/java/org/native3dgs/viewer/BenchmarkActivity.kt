package org.native3dgs.viewer

import android.app.Activity
import android.net.Uri
import android.os.Bundle
import android.os.PowerManager
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.TextView
import android.widget.FrameLayout
import android.view.Gravity
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.native3dgs.sdk.EnginePhase
import org.native3dgs.sdk.FrameSample
import org.native3dgs.sdk.Native3dgsEngine
import org.native3dgs.sdk.QualityMode

class BenchmarkActivity : Activity(), SurfaceHolder.Callback {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private lateinit var surface: SurfaceView
    private lateinit var status: TextView
    private var engine: Native3dgsEngine? = null
    private var generation = 0L
    private var run: Job? = null
    private val adaptiveResolution = AdaptiveResolution()
    private var lastPresentCallNanos = 0L
    private var configuredQuality: List<Int>? = null
    private var requestedBufferWidth = 0
    private var requestedBufferHeight = 0
    private val qualityMode by lazy {
        if (intent.getStringExtra("quality") == "mobile") QualityMode.Mobile else QualityMode.Full
    }
    private val fixedScale by lazy { intent.getIntExtra("scale_permille", 0) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        surface = SurfaceView(this)
        status = TextView(this).apply {
            textSize = 14f
            setTextColor(android.graphics.Color.WHITE)
            setBackgroundColor(0x99000000.toInt())
            setPadding(20, 12, 20, 12)
            text = "Preparing benchmark"
        }
        val root = FrameLayout(this)
        root.addView(surface, FrameLayout.LayoutParams(-1, -1))
        root.addView(status, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.START))
        setContentView(root)
        surface.holder.addCallback(this)
        surface.addOnLayoutChangeListener { _, left, top, right, bottom,
            oldLeft, oldTop, oldRight, oldBottom ->
            if (right - left != oldRight - oldLeft || bottom - top != oldBottom - oldTop)
                configureSurface()
        }
        engine = Native3dgsEngine(applicationContext)
        run = scope.launch { execute() }
    }

    private suspend fun execute() {
        val native = engine ?: return
        val name = intent.getStringExtra("model") ?: return fail("Missing model")
        val root = getExternalFilesDir(null) ?: return fail("No test directory")
        val model = File(root, name)
        if (model.parentFile != root || !model.isFile) return fail("Model unavailable")
        val output = intent.getStringExtra("output") ?: "benchmark.csv"
        if (!Regex("[A-Za-z0-9_.-]+\\.csv").matches(output)) return fail("Invalid output")
        if (fixedScale != 0 && (qualityMode != QualityMode.Mobile ||
            fixedScale !in 750..1000)) return fail("Invalid scale")
        try {
            native.open(Uri.fromFile(model))
            while (native.state.value.phase != EnginePhase.Ready || generation == 0L) {
                if (native.state.value.phase == EnginePhase.Failed)
                    return fail("Load failed: ${native.state.value.error}")
                delay(100)
            }
            native.fit()
            delay(250)
            native.drainFrameSamples()
            val samples = ArrayList<FrameSample>(8192)
            val thermal = getSystemService(PowerManager::class.java)
            val thermalSamples = ArrayList<Pair<Long, Int>>()
            val started = System.nanoTime()
            val end = started + 90_000_000_000L
            var nextTick = started
            var nextThermal = started
            while (System.nanoTime() < end) {
                val now = System.nanoTime()
                if (now >= nextTick) {
                    native.orbit(0.6, 0.12)
                    nextTick += 16_666_667L
                    if (nextTick < now) nextTick = now + 16_666_667L
                }
                if (now >= nextThermal) {
                    thermalSamples.add(now to thermal.currentThermalStatus)
                    nextThermal += 1_000_000_000L
                    status.text = "Benchmark ${(now - started) / 1_000_000_000L}/90 s"
                }
                val batch = native.drainFrameSamples()
                samples.addAll(batch)
                if (qualityMode == QualityMode.Mobile && fixedScale == 0) {
                    for (sample in batch) {
                        val interval = sample.presentCallNanos - lastPresentCallNanos
                        lastPresentCallNanos = sample.presentCallNanos
                        if (interval in 1..200_000_000L &&
                            adaptiveResolution.observe(sample.frameId, interval / 1000, now) != null) {
                            configureSurface()
                            break
                        }
                    }
                }
                delay(5)
            }
            samples.addAll(native.drainFrameSamples())
            val file = File(root, output)
            withContext(Dispatchers.IO) {
                file.bufferedWriter().use { writer ->
                    writer.appendLine("frame_id,present_call_ns,cpu_frame_us,gpu_frame_id,gpu_project_us,gpu_sort_us,gpu_draw_us,submitted_splats,width,height,quality_mode,source_splats,active_splats,requested_scale_permille,actual_scale_permille,phase")
                    for (sample in samples) {
                        val phase = if (sample.presentCallNanos < started + 30_000_000_000L)
                            "warmup" else "sample"
                        writer.appendLine(listOf(sample.frameId, sample.presentCallNanos,
                            sample.cpuFrameMicros, sample.gpuFrameId, sample.gpuProjectMicros,
                            sample.gpuSortMicros, sample.gpuDrawMicros, sample.submittedSplats,
                            sample.width, sample.height, sample.qualityMode.name.lowercase(),
                            sample.sourceSplats, sample.activeSplats,
                            sample.requestedScalePermille, sample.actualScalePermille,
                            phase).joinToString(","))
                    }
                }
                File(root, output.removeSuffix(".csv") + "-thermal.csv").bufferedWriter().use { writer ->
                    writer.appendLine("elapsed_ns,thermal_status")
                    thermalSamples.forEach { (time, value) ->
                        writer.appendLine("${time - started},$value")
                    }
                }
            }
            status.text = "Saved ${samples.size} frames to $output"
        } catch (error: Exception) {
            fail("Benchmark failed: ${error.javaClass.simpleName}")
        }
    }

    private fun fail(message: String) { status.text = message }

    override fun surfaceCreated(holder: SurfaceHolder) {
        generation = engine?.attach(holder.surface) ?: 0L
        configureSurface()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        if (generation != 0L && width > 0 && height > 0)
            engine?.resize(generation, width, height)
        configureSurface()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        if (generation != 0L) engine?.detach(generation)
        generation = 0L
    }

    override fun onDestroy() {
        run?.cancel()
        scope.cancel()
        engine?.close()
        super.onDestroy()
    }

    private fun configureSurface() {
        val native = engine ?: return
        if (fixedScale != 0 && (qualityMode != QualityMode.Mobile ||
            fixedScale !in 750..1000)) return
        val width = surface.width
        val height = surface.height
        if (width <= 0 || height <= 0) return
        val scale = if (qualityMode == QualityMode.Full) 1000 else
            fixedScale.takeIf { it != 0 } ?: adaptiveResolution.scalePermille
        val settings = listOf(qualityMode.ordinal, scale, width, height)
        if (settings != configuredQuality) {
            native.setQuality(qualityMode, scale, width, height)
            configuredQuality = settings
        }
        if (qualityMode == QualityMode.Mobile) {
            val bufferWidth = maxOf(1, width * scale / 1000)
            val bufferHeight = maxOf(1, height * scale / 1000)
            if (bufferWidth != requestedBufferWidth || bufferHeight != requestedBufferHeight) {
                requestedBufferWidth = bufferWidth
                requestedBufferHeight = bufferHeight
                surface.holder.setFixedSize(bufferWidth, bufferHeight)
                lastPresentCallNanos = 0L
            }
        }
    }
}
