package org.native3dgs.sdk

import android.os.Bundle
import android.os.ParcelFileDescriptor

internal object DecoderProtocol {
    const val VERSION = 1
    const val OPEN = 1
    const val CANCEL = 2
    const val PROGRESS = 3
    const val COMPLETE = 4
    const val VERSION_KEY = "version"
    const val ID_KEY = "request"
    const val FD_KEY = "fd"
    const val STAGE_KEY = "stage"
    const val DONE_KEY = "done"
    const val TOTAL_KEY = "total"
    const val ERROR_KEY = "error"
    const val DIAGNOSTIC_KEY = "diagnostic"
    const val PROCESS_KEY = "process"

    fun envelope(requestId: Long) = Bundle().apply {
        putInt(VERSION_KEY, VERSION)
        putLong(ID_KEY, requestId)
    }

    @Suppress("DEPRECATION")
    fun descriptor(bundle: Bundle): ParcelFileDescriptor? =
        bundle.getParcelable(FD_KEY)
}

internal object DecoderError {
    const val IO_FAILURE = 2
    const val UNSUPPORTED_VERSION = 4
    const val INVALID_ATTRIBUTE = 8
    const val RESOURCE_LIMIT = 10
    const val CANCELLED = 12
    const val TIMEOUT = 13
    const val DECODER_FAILURE = 14
    const val DECODER_CRASHED = 15
    const val OBSERVER_FAILURE = 16
}

internal data class DecodeProgress(val requestId: Long, val stage: Int,
                                   val bytesDone: Long, val totalBytes: Long?, val processId: Int)

internal interface DecodeObserver {
    fun cancelled(): Boolean
    fun progress(stage: Int, done: Long, total: Long)
}

internal class DecodeResult(
    val fd: Int,
    val error: Int,
    val stage: Int,
    val diagnostic: String,
)

internal object NativeDecoder {
    init { System.loadLibrary("gs_android_decoder") }
    external fun decode(fd: Int, availableBytes: Long, directory: String,
                        observer: DecodeObserver): DecodeResult
    external fun validateScene(fd: Int, limit: Long): Long
    external fun probeSurface(surface: android.view.Surface, onPresented: Runnable): Int
    external fun probeSplatSurface(surface: android.view.Surface, onPresented: Runnable): Int
}
