package org.native3dgs.consumer

import android.content.Context
import android.net.Uri
import android.view.Surface
import kotlinx.coroutines.flow.StateFlow
import org.native3dgs.sdk.EngineState
import org.native3dgs.sdk.Native3dgsEngine

class Consumer(context: Context) : AutoCloseable {
    private val engine = Native3dgsEngine(context)
    val state: StateFlow<EngineState> get() = engine.state

    fun attach(surface: Surface) = engine.attach(surface)
    fun resize(generation: Long, width: Int, height: Int) =
        engine.resize(generation, width, height)
    fun detach(generation: Long) = engine.detach(generation)
    suspend fun open(uri: Uri) = engine.open(uri)
    fun orbit(dx: Double, dy: Double) = engine.orbit(dx, dy)
    override fun close() = engine.close()
    suspend fun closeAndWait() = engine.closeAndWait()
}
