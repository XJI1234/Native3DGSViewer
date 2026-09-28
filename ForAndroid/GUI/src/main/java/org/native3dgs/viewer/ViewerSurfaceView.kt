package org.native3dgs.viewer

import android.content.Context
import android.view.SurfaceView

internal class ViewerSurfaceView(context: Context) : SurfaceView(context) {
    override fun performClick(): Boolean {
        super.performClick()
        return true
    }
}
