import type { WebEngine } from '../engine/engine';
export function bindCanvas(
    engine: WebEngine,
    host: HTMLElement,
    canvas: HTMLCanvasElement,
    pixelRatio = window.devicePixelRatio,
): () => void {
    if (!Number.isFinite(pixelRatio) || pixelRatio <= 0) throw Error('Invalid pixelRatio');
    const resize = () => {
        const rect = host.getBoundingClientRect();
        engine.resize(Math.round(rect.width * pixelRatio), Math.round(rect.height * pixelRatio));
    };
    const observer = new ResizeObserver(resize);
    observer.observe(host);
    resize();
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
    canvas.addEventListener('pointerup', up);
    canvas.addEventListener('pointercancel', up);
    canvas.addEventListener('lostpointercapture', up);
    canvas.addEventListener('wheel', wheel, { passive: false });
    canvas.addEventListener('contextmenu', context);
    window.addEventListener('blur', up);
    document.addEventListener('visibilitychange', visibility);
    return () => {
        up();
        observer.disconnect();
        canvas.removeEventListener('pointerdown', down);
        canvas.removeEventListener('pointermove', move);
        canvas.removeEventListener('pointerup', up);
        canvas.removeEventListener('pointercancel', up);
        canvas.removeEventListener('lostpointercapture', up);
        canvas.removeEventListener('wheel', wheel);
        canvas.removeEventListener('contextmenu', context);
        window.removeEventListener('blur', up);
        document.removeEventListener('visibilitychange', visibility);
    };
}
