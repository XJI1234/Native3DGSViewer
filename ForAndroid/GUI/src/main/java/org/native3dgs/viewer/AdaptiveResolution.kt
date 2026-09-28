package org.native3dgs.viewer

internal class AdaptiveResolution {
    private val scales = intArrayOf(1000, 900, 800, 750)
    private val frameTimes = ArrayList<Long>(16)
    private var index = 0
    private var lastFrameId = 0L
    private var lastDecisionNanos = 0L

    val scalePermille: Int get() = scales[index]

    fun reset() {
        index = 0
        lastFrameId = 0
        lastDecisionNanos = 0
        frameTimes.clear()
    }

    fun observe(frameId: Long, frameMicros: Long, nowNanos: Long): Int? {
        if (frameId == lastFrameId || frameMicros <= 0) return null
        if (frameId < lastFrameId) {
            frameTimes.clear()
            lastDecisionNanos = 0L
        }
        if (lastDecisionNanos == 0L) lastDecisionNanos = nowNanos
        lastFrameId = frameId
        frameTimes.add(frameMicros)
        if (frameTimes.size < 12 || nowNanos - lastDecisionNanos < 2_000_000_000L)
            return null

        frameTimes.sort()
        val median = frameTimes[frameTimes.size / 2]
        frameTimes.clear()
        lastDecisionNanos = nowNanos
        val next = when {
            median > 38_000 && index < scales.lastIndex -> index + 1
            median < 28_000 && index > 0 -> index - 1
            else -> index
        }
        if (next == index) return null
        index = next
        return scalePermille
    }
}
