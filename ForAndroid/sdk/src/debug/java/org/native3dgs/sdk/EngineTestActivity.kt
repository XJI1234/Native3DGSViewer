package org.native3dgs.sdk

import android.app.Activity
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import java.util.concurrent.CountDownLatch

class EngineTestActivity : Activity(), SurfaceHolder.Callback {
    val created = CountDownLatch(1)
    val sized = CountDownLatch(1)
    lateinit var viewport: SurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        viewport = SurfaceView(this)
        viewport.holder.addCallback(this)
        setContentView(viewport)
    }
    override fun surfaceCreated(holder: SurfaceHolder) { created.countDown() }
    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        if (width > 0 && height > 0) sized.countDown()
    }
    override fun surfaceDestroyed(holder: SurfaceHolder) { }
}
