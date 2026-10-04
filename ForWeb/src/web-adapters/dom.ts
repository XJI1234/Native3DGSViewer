import type { WebEngine } from '../engine/engine';
export function bindCanvas(
    engine: WebEngine,
    host: HTMLElement,
    canvas: HTMLCanvasElement,
    pixelRatio = window.devicePixelRatio,
): () => void {
    if (!Number.isFinite(pixelRatio) || pixelRatio <= 0) throw Error('Invalid pixelRatio');
    const resize = () => {
        if (['Stopping', 'Stopped'].includes(engine.getSnapshot().phase)) return;
        const rect = host.getBoundingClientRect();
        const logical = Math.max(rect.width, rect.height);
        if (!Number.isFinite(logical) || logical < 0) return;
        // Divide before multiplying to avoid overflow for a finite but very large ratio.
        const ratio = Math.min(pixelRatio, engine.maxViewportDimension / Math.max(1, logical));
        engine.resize(Math.round(rect.width * ratio), Math.round(rect.height * ratio));
    };
    const observer = new ResizeObserver(resize);
    try {
        resize();
        observer.observe(host);
    } catch (reason) {
        observer.disconnect();
        throw reason;
    }
    let pointer: number | null = null;
    let button = 0;
    let x = 0;
    let y = 0;
    const down = (event: PointerEvent) => {
        if (pointer !== null) return;
        pointer = event.pointerId;
        button = event.button;
        x = event.clientX;
        y = event.clientY;
        canvas.setPointerCapture(pointer);
        canvas.focus();
    };
    const move = (event: PointerEvent) => {
        if (pointer !== event.pointerId) return;
        const dx = (event.clientX - x) * pixelRatio,
            dy = (event.clientY - y) * pixelRatio;
        x = event.clientX;
        y = event.clientY;
        if (button === 2) engine.camera.pan(dx, dy);
        else engine.camera.orbit(dx, dy);
        engine.requestFrame();
    };
    const up = () => {
        if (pointer !== null && canvas.hasPointerCapture(pointer)) canvas.releasePointerCapture(pointer);
        pointer = null;
    };
    const pointerUp = (event: PointerEvent) => {
        if (event.pointerId === pointer) up();
    };
    const wheel = (event: WheelEvent) => {
        event.preventDefault();
        engine.camera.dolly(Math.sign(event.deltaY));
        engine.requestFrame();
    };
    const context = (event: Event) => event.preventDefault();
    const visibility = () => {
        if (document.hidden) up();
        else {
            resize();
            engine.requestFrame();
        }
    };
    canvas.tabIndex = 0;
    canvas.addEventListener('pointerdown', down);
    canvas.addEventListener('pointermove', move);
    canvas.addEventListener('pointerup', pointerUp);
    canvas.addEventListener('pointercancel', pointerUp);
    canvas.addEventListener('lostpointercapture', pointerUp);
    canvas.addEventListener('wheel', wheel, { passive: false });
    canvas.addEventListener('contextmenu', context);
    window.addEventListener('blur', up);
    document.addEventListener('visibilitychange', visibility);
    return () => {
        up();
        observer.disconnect();
        canvas.removeEventListener('pointerdown', down);
        canvas.removeEventListener('pointermove', move);
        canvas.removeEventListener('pointerup', pointerUp);
        canvas.removeEventListener('pointercancel', pointerUp);
        canvas.removeEventListener('lostpointercapture', pointerUp);
        canvas.removeEventListener('wheel', wheel);
        canvas.removeEventListener('contextmenu', context);
        window.removeEventListener('blur', up);
        document.removeEventListener('visibilitychange', visibility);
    };
}
