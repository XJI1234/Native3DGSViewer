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
    const keys = new Set<string>();
    let alive = true;
    let lockEpoch = 0;
    let pendingLock = false;
    let flight = 0;
    let lastFlight = 0;
    const clearFlight = () => {
        lockEpoch++;
        keys.clear();
        cancelAnimationFrame(flight);
        flight = 0;
        if (document.pointerLockElement === canvas) document.exitPointerLock();
    };
    const flyTick = (time: number) => {
        flight = 0;
        if (
            engine.camera.mode !== 'fly' ||
            document.hidden ||
            (document.pointerLockElement !== canvas && document.activeElement !== canvas)
        ) {
            clearFlight();
            return;
        }
        if (!keys.size) {
            lastFlight = time;
            if (document.pointerLockElement === canvas) flight = requestAnimationFrame(flyTick);
            return;
        }
        const dt = Math.max(0, Math.min(0.1, (time - lastFlight) / 1000));
        lastFlight = time;
        const speed = keys.has('ShiftLeft') || keys.has('ShiftRight') ? 4 : 1;
        engine.camera.fly(
            (Number(keys.has('KeyD')) - Number(keys.has('KeyA'))) * speed,
            (Number(keys.has('KeyE')) - Number(keys.has('KeyQ'))) * speed,
            (Number(keys.has('KeyW')) - Number(keys.has('KeyS'))) * speed,
            dt,
        );
        engine.requestFrame();
        flight = requestAnimationFrame(flyTick);
    };
    const down = (event: PointerEvent) => {
        if (pointer !== null || (event.button !== 0 && event.button !== 2)) return;
        pointer = event.pointerId;
        button = event.button;
        x = event.clientX;
        y = event.clientY;
        canvas.setPointerCapture(pointer);
        canvas.focus();
        if (engine.camera.mode === 'fly' && event.button === 0 && !pendingLock) {
            const epoch = ++lockEpoch;
            pendingLock = true;
            // Retain only this request's completion guard after unbind. Void-returning
            // browsers complete via lock/error events; Promise browsers use both.
            let settled = false;
            const finish = () => {
                if (settled) return;
                settled = true;
                pendingLock = false;
                document.removeEventListener('pointerlockchange', acquired);
                document.removeEventListener('pointerlockerror', failed);
            };
            const acquired = () => {
                if (settled || document.pointerLockElement !== canvas) return;
                if (
                    !alive ||
                    epoch !== lockEpoch ||
                    document.hidden ||
                    engine.camera.mode !== 'fly' ||
                    document.activeElement !== canvas
                )
                    document.exitPointerLock();
                finish();
            };
            const failed = () => {
                finish();
            };
            document.addEventListener('pointerlockchange', acquired);
            document.addEventListener('pointerlockerror', failed);
            try {
                const lock = canvas.requestPointerLock();
                void lock?.then(() => {
                    acquired();
                    finish();
                }, failed);
            } catch {
                failed(); /* Primary drag remains available. */
            }
        }
    };
    const move = (event: PointerEvent) => {
        if (document.pointerLockElement === canvas) {
            if (engine.camera.mode !== 'fly') clearFlight();
            else {
                engine.camera.look(event.movementX * pixelRatio, event.movementY * pixelRatio);
                engine.requestFrame();
            }
            return;
        }
        if (pointer !== event.pointerId) return;
        const dx = (event.clientX - x) * pixelRatio,
            dy = (event.clientY - y) * pixelRatio;
        x = event.clientX;
        y = event.clientY;
        if (button === 2) engine.camera.pan(dx, dy);
        else if (engine.camera.mode === 'fly') engine.camera.look(dx, dy);
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
    const blur = () => {
        up();
        clearFlight();
    };
    const lockChange = () => {
        if (document.pointerLockElement !== canvas) {
            up();
            clearFlight();
        } else if (!flight) {
            lastFlight = performance.now();
            flight = requestAnimationFrame(flyTick);
        }
    };
    const keydown = (event: KeyboardEvent) => {
        if (document.activeElement !== canvas && document.pointerLockElement !== canvas) return;
        if (event.code === 'Escape') {
            blur();
            return;
        }
        if (
            engine.camera.mode === 'fly' &&
            ['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE', 'ShiftLeft', 'ShiftRight'].includes(event.code)
        ) {
            event.preventDefault();
            keys.add(event.code);
            if (!flight) {
                lastFlight = performance.now();
                flight = requestAnimationFrame(flyTick);
            }
            return;
        }
        const arrows: Record<string, [number, number]> = {
            ArrowLeft: [-10, 0],
            ArrowRight: [10, 0],
            ArrowUp: [0, -10],
            ArrowDown: [0, 10],
        };
        const delta = arrows[event.code];
        if (delta) {
            event.preventDefault();
            if (engine.camera.mode === 'fly') engine.camera.look(...delta);
            else engine.camera.orbit(...delta);
        } else if (event.code === 'Equal' || event.code === 'Minus') {
            event.preventDefault();
            engine.camera.dolly(event.code === 'Equal' ? -1 : 1);
        } else return;
        engine.requestFrame();
    };
    const keyup = (event: KeyboardEvent) => {
        keys.delete(event.code);
    };
    const visibility = () => {
        if (document.hidden) blur();
        else {
            resize();
            engine.requestFrame();
        }
    };
    canvas.tabIndex = 0;
    canvas.setAttribute('aria-label', '3D 模型视口，方向键旋转，加减键缩放');
    canvas.addEventListener('blur', blur);
    canvas.addEventListener('pointerdown', down);
    canvas.addEventListener('pointermove', move);
    canvas.addEventListener('pointerup', pointerUp);
    canvas.addEventListener('pointercancel', pointerUp);
    canvas.addEventListener('lostpointercapture', pointerUp);
    canvas.addEventListener('wheel', wheel, { passive: false });
    canvas.addEventListener('contextmenu', context);
    window.addEventListener('blur', blur);
    window.addEventListener('keydown', keydown);
    window.addEventListener('keyup', keyup);
    document.addEventListener('pointerlockchange', lockChange);
    document.addEventListener('visibilitychange', visibility);
    return () => {
        alive = false;
        blur();
        observer.disconnect();
        canvas.removeEventListener('blur', blur);
        canvas.removeEventListener('pointerdown', down);
        canvas.removeEventListener('pointermove', move);
        canvas.removeEventListener('pointerup', pointerUp);
        canvas.removeEventListener('pointercancel', pointerUp);
        canvas.removeEventListener('lostpointercapture', pointerUp);
        canvas.removeEventListener('wheel', wheel);
        canvas.removeEventListener('contextmenu', context);
        window.removeEventListener('blur', blur);
        window.removeEventListener('keydown', keydown);
        window.removeEventListener('keyup', keyup);
        document.removeEventListener('pointerlockchange', lockChange);
        document.removeEventListener('visibilitychange', visibility);
    };
}
