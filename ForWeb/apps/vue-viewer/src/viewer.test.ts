import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { Camera } from "@native3dgs/web";
import type { Result, WebEngine } from "@native3dgs/web";
import { ViewerSession } from "./viewer";
beforeEach(() => vi.useFakeTimers());
afterEach(() => {
  vi.clearAllTimers();
  vi.useRealTimers();
});
const success: Result<void> = { ok: true, value: undefined };
function fixture() {
  const completions: ((result: Result<void>) => void)[] = [];
  const cancels: ReturnType<typeof vi.fn>[] = [];
  const engine = {
    camera: new Camera(),
    requestFrame: vi.fn(),
    fitScene: () => success,
    open: vi.fn(() => {
      const cancel = vi.fn();
      cancels.push(cancel);
      return {
        requestId: completions.length + 1,
        cancel,
        result: new Promise<Result<void>>((resolve) =>
          completions.push(resolve),
        ),
      };
    }),
    closeScene: vi.fn(async () => {}),
    recover: vi.fn(async () => success),
    capture: vi.fn(async () => ({
      ok: false,
      error: {
        code: "DeviceLost",
        stage: "Capture",
        diagnostic: "device unavailable",
      },
    })),
  };
  const viewer = new ViewerSession(engine as unknown as WebEngine);
  return { viewer, engine, completions, cancels };
}
describe("Standalone viewer host transactions", () => {
  it("ignores stale results and keeps the active label until a candidate succeeds", async () => {
    const { viewer, completions } = fixture();
    viewer.openFile(new File([], "first.ply"));
    completions[0]!(success);
    await Promise.resolve();
    viewer.openFile(new File([], "old.spz"));
    viewer.openFile(new File([], "latest.ply"));
    expect(viewer.getSnapshot().name).toBe("first.ply");
    completions[2]!(success);
    await Promise.resolve();
    completions[1]!(success);
    await Promise.resolve();
    expect(viewer.getSnapshot().name).toBe("latest.ply");
    expect(viewer.getSnapshot().candidate).toBe("");
  });
  it("cancels on close and ignores a late successful load", async () => {
    const { viewer, engine, completions, cancels } = fixture();
    viewer.openFile(new File([], "candidate.ply"));
    await viewer.close();
    completions[0]!(success);
    await Promise.resolve();
    expect(cancels[0]).toHaveBeenCalledOnce();
    expect(engine.closeScene).toHaveBeenCalledOnce();
    expect(viewer.getSnapshot().name).toBe("");
    expect(viewer.getSnapshot().closing).toBe(false);
  });
  it("rejects invalid local format and non-HTTP URLs without starting SDK loads", () => {
    const { viewer, engine } = fixture();
    viewer.openFile(new File([], "not-a-model.txt"));
    viewer.openUrl("file:///private/model.ply");
    expect(engine.open).not.toHaveBeenCalled();
    expect(viewer.getSnapshot().message).toContain("HTTP");
  });
  it("reports structured capture failure and permits a subsequent capture", async () => {
    const { viewer, engine } = fixture();
    await viewer.capture();
    expect(viewer.getSnapshot().message).toContain("DeviceLost");
    expect(viewer.getSnapshot().capturing).toBe(false);
    await viewer.capture();
    expect(engine.capture).toHaveBeenCalledTimes(2);
  });
  it("unsubscribes/cancels and suppresses late notifications on dispose", async () => {
    const { viewer, completions, cancels } = fixture();
    const listener = vi.fn();
    viewer.subscribe(listener);
    viewer.openFile(new File([], "pending.spz"));
    viewer.dispose();
    expect(vi.getTimerCount()).toBe(0);
    const called = listener.mock.calls.length;
    completions[0]!(success);
    await Promise.resolve();
    expect(cancels[0]).toHaveBeenCalledOnce();
    expect(listener).toHaveBeenCalledTimes(called);
  });
  it("uses persistent SDK reflection and mode without changing the canonical pose", () => {
    const { viewer, engine } = fixture(),
      before = engine.camera.getPose();
    viewer.flip();
    viewer.setMode("fly");
    viewer.reset();
    expect(engine.camera.getPose()).toEqual(before);
    expect(engine.camera.flipY).toBe(true);
    expect(engine.camera.mode).toBe("fly");
    expect(viewer.getSnapshot().flipY).toBe(true);
  });
  it("clears obsolete host errors after successful recovery and close", async () => {
    const { viewer } = fixture();
    viewer.openUrl("file:///bad.ply");
    expect(viewer.getSnapshot().message).not.toBe("");
    await viewer.recover();
    expect(viewer.getSnapshot().message).toBe("");
    viewer.openFile(new File([], "bad.txt"));
    await viewer.close();
    expect(viewer.getSnapshot().message).toBe("");
    viewer.dispose();
  });
});
