package org.native3dgs.sdk

import android.app.Activity
import android.graphics.Bitmap
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.PixelCopy
import android.view.SurfaceHolder
import android.view.SurfaceView
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

class SurfaceProbeActivity : Activity(), SurfaceHolder.Callback {
    val completed = CountDownLatch(1)
    @Volatile var probeResult = Int.MIN_VALUE
    @Volatile var copyResult = Int.MIN_VALUE
    @Volatile var captured: Bitmap? = null
    lateinit var viewport: SurfaceView
    private val renderer = Executors.newSingleThreadExecutor()
    private val surfaceGeneration = AtomicInteger()
    private val main = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        viewport = SurfaceView(this)
        viewport.holder.addCallback(this)
        setContentView(viewport)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val generation = surfaceGeneration.incrementAndGet()
        renderer.execute {
            try {
                if (surfaceGeneration.get() != generation) return@execute
                val result = if (intent.getBooleanExtra("splat", false))
                    NativeDecoder.probeSplatSurface(holder.surface) { captureFrame(generation) }
                else NativeDecoder.probeSurface(holder.surface) { captureFrame(generation) }
                if (surfaceGeneration.get() == generation) probeResult = result
            } finally {
                if (surfaceGeneration.get() == generation) completed.countDown()
            }
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) { }
    override fun surfaceDestroyed(holder: SurfaceHolder) { surfaceGeneration.incrementAndGet() }

    private fun captureFrame(generation: Int) {
        val copied = CountDownLatch(1)
        val finished = AtomicBoolean()
        fun finish(status: Int, bitmap: Bitmap? = null) {
            if (finished.compareAndSet(false, true)) {
                val active = surfaceGeneration.get() == generation
                if (active) {
                    copyResult = status
                    if (status == PixelCopy.SUCCESS) captured = bitmap
                }
                if (status != PixelCopy.SUCCESS || !active)
                    bitmap?.recycle()
                copied.countDown()
            } else bitmap?.recycle()
        }
        main.post {
            if (finished.get() || surfaceGeneration.get() != generation ||
                !viewport.holder.surface.isValid || viewport.width <= 0 || viewport.height <= 0) {
                finish(PixelCopy.ERROR_SOURCE_INVALID)
                return@post
            }
            val bitmap = try {
                Bitmap.createBitmap(viewport.width, viewport.height, Bitmap.Config.ARGB_8888)
            } catch (_: RuntimeException) {
                finish(PixelCopy.ERROR_UNKNOWN)
                return@post
            }
            var attempts = 0
            fun copy() {
                if (finished.get() || surfaceGeneration.get() != generation ||
                    !viewport.holder.surface.isValid) {
                    finish(PixelCopy.ERROR_SOURCE_INVALID, bitmap)
                    return
                }
                try {
                    PixelCopy.request(viewport, bitmap, { status ->
                        if (status == PixelCopy.ERROR_SOURCE_NO_DATA && ++attempts < 60 &&
                            !finished.get()) main.postDelayed({ copy() }, 16)
                        else finish(status, bitmap)
                    }, main)
                } catch (_: IllegalArgumentException) {
                    finish(PixelCopy.ERROR_SOURCE_INVALID, bitmap)
                }
            }
            copy()
        }
        if (!copied.await(5, TimeUnit.SECONDS)) {
            finish(PixelCopy.ERROR_TIMEOUT)
            copied.await()
        }
    }

    override fun onDestroy() {
        surfaceGeneration.incrementAndGet()
        viewport.holder.removeCallback(this)
        renderer.shutdownNow()
        super.onDestroy()
    }
}
