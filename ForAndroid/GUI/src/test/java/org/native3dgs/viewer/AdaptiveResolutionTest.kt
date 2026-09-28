package org.native3dgs.viewer

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class AdaptiveResolutionTest {
    @Test fun changesOnlyAfterSustainedFrames() {
        val controller = AdaptiveResolution()
        for (frame in 1L..11L)
            assertNull(controller.observe(frame, 47_000, frame * 180_000_000L))
        assertEquals(1000, controller.scalePermille)
        for (frame in 12L..16L)
            controller.observe(frame, 47_000, frame * 180_000_000L)
        assertEquals(900, controller.scalePermille)
        assertNull(controller.observe(16, 47_000, 4_000_000_000L))
        assertEquals(900, controller.scalePermille)
    }

    @Test fun clampsAndUsesSeparateUpwardThreshold() {
        val controller = AdaptiveResolution()
        var time = 0L
        var frame = 0L
        repeat(4) {
            repeat(16) {
                time += 180_000_000L
                controller.observe(++frame, 47_000, time)
            }
        }
        assertEquals(750, controller.scalePermille)
        repeat(16) {
            time += 180_000_000L
            controller.observe(++frame, 33_000, time)
        }
        assertEquals(750, controller.scalePermille)
        repeat(20) {
            time += 180_000_000L
            controller.observe(++frame, 24_000, time)
        }
        assertEquals(800, controller.scalePermille)
        controller.reset()
        assertEquals(1000, controller.scalePermille)
    }

    @Test fun waitsTwoSecondsAndKeepsScaleAfterFrameIdRestart() {
        val controller = AdaptiveResolution()
        for (frame in 1L..20L)
            assertNull(controller.observe(frame, 47_000, frame * 50_000_000L))
        assertEquals(1000, controller.scalePermille)
        for (frame in 21L..50L)
            controller.observe(frame, 47_000, frame * 50_000_000L)
        assertEquals(900, controller.scalePermille)
        assertNull(controller.observe(1, 47_000, 2_550_000_000L))
        assertEquals(900, controller.scalePermille)
    }
}
