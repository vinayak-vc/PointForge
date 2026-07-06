import { useEffect, useRef, useState } from 'react';
import type { MutableRefObject } from 'react';
import type { StateMsg, WsStatus } from './useWebSocket';
import { ChevronUp, ChevronDown, Hand, Zap, ZoomIn } from 'lucide-react';

// Fly page: premium glass controls overlay. Touch gesture logic is preserved
// exactly from before; this adds a parallel desktop input path (mouse +
// keyboard) for browsers with a fine pointer (mouse), since a desktop
// browser previously had NO way to move the camera at all — FlyTab only
// wired touch events. It also adds tap-to-measure: while the server's
// Measure tool is active, a short tap/click (not a drag) on the video sends
// its position — mapped through the object-fit:contain letterboxing back to
// video-content-relative coordinates — as a `measure_pick` cmd, which the
// server resolves with the exact same screen-ray pick used for a local LMB
// click (main.cpp's pendingPick path).
//
// Gesture summary:
//   Touch:   1-finger drag → Look; 2-finger drag → Pan or Zoom (pinch mode
//            toggle); UP/DOWN/BOOST floating buttons.
//   Desktop: Left-drag → Look; Right-drag → Pan; Wheel → Zoom; WASD → fly;
//            Space/Ctrl → up/down; Shift → boost; UP/DOWN/BOOST buttons
//            also work via mouse click-hold.
//   Measuring (either input): a short tap/left-click (no drag) places a
//   measurement point instead of looking around — look/pan is suspended
//   for the primary pointer while the Measure tool is active, matching the
//   PC's "measure mode reclaims LMB" behaviour.

export interface MoveValues {
  f: number; s: number; u: number; yaw: number; pit: number; boost: 0 | 1;
}

export const ZERO_MOVE: MoveValues = { f: 0, s: 0, u: 0, yaw: 0, pit: 0, boost: 0 };

// A press/tap that moved less than this many px is a "tap", not a drag.
const TAP_MOVE_THRESHOLD = 10;

// ─── HoldButton ─────────────────────────────────────────────────────────────
interface HoldButtonProps {
  label: string;
  className?: string;
  icon?: React.ReactNode;
  onHold: (held: boolean) => void;
}

function HoldButton({ label, className, icon, onHold }: HoldButtonProps) {
  const heldRef = useRef(false);

  const press = (e: React.PointerEvent<HTMLButtonElement>) => {
    e.currentTarget.setPointerCapture(e.pointerId);
    heldRef.current = true;
    onHold(true);
  };
  const release = () => {
    if (!heldRef.current) return;
    heldRef.current = false;
    onHold(false);
  };

  return (
    <button
      className={`hold-btn${className ? ` ${className}` : ''}`}
      onPointerDown={press}
      onPointerUp={release}
      onPointerCancel={release}
      onLostPointerCapture={release}
      onContextMenu={(e) => e.preventDefault()}
    >
      {icon}
      {label}
    </button>
  );
}

// ─── FlyTab ─────────────────────────────────────────────────────────────────
export interface FlyTabProps {
  moveRef: MutableRefObject<MoveValues>;
  lastState: StateMsg | null;
  status: WsStatus;
  /** True while the server's Measure tool is active (cfg.tool === 1). */
  measuring?: boolean;
  send?: (obj: unknown) => void;
  /** Intrinsic (decoded) frame size, or null if no frame has arrived yet. */
  getVideoNaturalSize?: () => { w: number; h: number } | null;
}

export default function FlyTab({ moveRef, measuring = false, send, getVideoNaturalSize }: FlyTabProps) {
  const [lookSpeed, setLookSpeed] = useState(0.2);
  const [panSpeed, setPanSpeed] = useState(0.2);
  const [zoomSpeed, setZoomSpeed] = useState(0.1);
  const [pinchMode, setPinchMode] = useState<'zoom' | 'pan'>('zoom');

  // Fine pointer (mouse) => desktop input path; computed once at mount.
  const [isDesktop] = useState(
    () => typeof window !== 'undefined' && window.matchMedia('(pointer: fine)').matches
  );

  // Zero all controls when this tab unmounts
  useEffect(() => {
    const ref = moveRef;
    return () => { ref.current = { ...ZERO_MOVE }; };
  }, [moveRef]);

  // Keep the latest slider/prop values readable inside non-React event
  // callbacks (window-level mouse/keyboard listeners are attached once, not
  // re-created per render).
  const pinchModeRef = useRef<'zoom' | 'pan'>('zoom');
  useEffect(() => { pinchModeRef.current = pinchMode; }, [pinchMode]);
  const lookSpeedRef = useRef(lookSpeed);
  useEffect(() => { lookSpeedRef.current = lookSpeed; }, [lookSpeed]);
  const panSpeedRef = useRef(panSpeed);
  useEffect(() => { panSpeedRef.current = panSpeed; }, [panSpeed]);
  const zoomSpeedRef = useRef(zoomSpeed);
  useEffect(() => { zoomSpeedRef.current = zoomSpeed; }, [zoomSpeed]);
  const measuringRef = useRef(measuring);
  useEffect(() => { measuringRef.current = measuring; }, [measuring]);

  const stageRef = useRef<HTMLDivElement>(null);

  // Map a viewport-relative point to normalized (0..1) video-content
  // coordinates, accounting for object-fit:contain letterboxing. Returns
  // null if the point falls in a letterbox bar (outside actual video
  // content) or no frame has decoded yet.
  const toVideoNormalized = (clientX: number, clientY: number): [number, number] | null => {
    const stage = stageRef.current;
    const natural = getVideoNaturalSize?.();
    if (!stage || !natural || natural.w <= 0 || natural.h <= 0) return null;
    const rect = stage.getBoundingClientRect();
    const containerAspect = rect.width / rect.height;
    const videoAspect = natural.w / natural.h;
    let contentW = rect.width, contentH = rect.height, offsetX = 0, offsetY = 0;
    if (videoAspect > containerAspect) {
      contentH = rect.width / videoAspect;
      offsetY = (rect.height - contentH) / 2;
    } else {
      contentW = rect.height * videoAspect;
      offsetX = (rect.width - contentW) / 2;
    }
    const localX = clientX - rect.left - offsetX;
    const localY = clientY - rect.top - offsetY;
    if (localX < 0 || localX > contentW || localY < 0 || localY > contentH) return null;
    return [localX / contentW, localY / contentH];
  };

  const sendMeasurePick = (clientX: number, clientY: number) => {
    const n = toVideoNormalized(clientX, clientY);
    if (!n || !send) return;
    send({ t: 'cmd', n: 'measure_pick', v: [n[0], n[1], 0] });
  };

  const touchState = useRef({
    mode: 'none' as 'none' | 'look' | 'pan' | 'zoom',
    lastX: 0, lastY: 0,
    midX: 0, midY: 0,
    startDist: 0,
    pickStartX: 0, pickStartY: 0,
  });

  // iOS Safari non-passive listeners to block system pan/zoom
  useEffect(() => {
    const el = stageRef.current;
    if (!el) return;
    const preventDefault = (e: TouchEvent) => {
      if (e.target instanceof HTMLInputElement) return;
      if (e.cancelable) e.preventDefault();
    };
    el.addEventListener('touchstart', preventDefault, { passive: false });
    el.addEventListener('touchmove',  preventDefault, { passive: false });
    return () => {
      el.removeEventListener('touchstart', preventDefault);
      el.removeEventListener('touchmove',  preventDefault);
    };
  }, []);

  const getDist = (touches: React.TouchList) => {
    const dx = touches[0].clientX - touches[1].clientX;
    const dy = touches[0].clientY - touches[1].clientY;
    return Math.hypot(dx, dy);
  };

  const handleTouchStart = (e: React.TouchEvent) => {
    if (e.cancelable) e.preventDefault();
    const state = touchState.current;
    if (e.touches.length === 1) {
      state.lastX = e.touches[0].clientX;
      state.lastY = e.touches[0].clientY;
      state.pickStartX = e.touches[0].clientX;
      state.pickStartY = e.touches[0].clientY;
      state.mode = 'look';
    } else if (e.touches.length === 2) {
      state.startDist = getDist(e.touches);
      state.midX = (e.touches[0].clientX + e.touches[1].clientX) / 2;
      state.midY = (e.touches[0].clientY + e.touches[1].clientY) / 2;
      state.mode = pinchModeRef.current === 'pan' ? 'pan' : 'zoom';
    }
  };

  const handleTouchMove = (e: React.TouchEvent) => {
    if (e.cancelable) e.preventDefault();
    const state = touchState.current;
    if (state.mode === 'none') return;

    if (state.mode === 'look' && e.touches.length === 1) {
      const dx = e.touches[0].clientX - state.lastX;
      const dy = e.touches[0].clientY - state.lastY;
      // While measuring, a single-finger drag places/aims a point instead of
      // looking around — suspend camera look, same as the PC's LMB reclaim.
      if (!measuringRef.current) {
        moveRef.current.yaw += dx * lookSpeed;
        moveRef.current.pit -= dy * lookSpeed;
      }
      state.lastX = e.touches[0].clientX;
      state.lastY = e.touches[0].clientY;
    } else if (state.mode === 'zoom' && e.touches.length === 2) {
      const dist = getDist(e.touches);
      moveRef.current.f += (dist - state.startDist) * zoomSpeed;
      state.startDist = dist;
    } else if (state.mode === 'pan' && e.touches.length === 2) {
      const midX = (e.touches[0].clientX + e.touches[1].clientX) / 2;
      const midY = (e.touches[0].clientY + e.touches[1].clientY) / 2;
      moveRef.current.s += (midX - state.midX) * panSpeed;
      moveRef.current.u += -(midY - state.midY) * panSpeed;
      state.midX = midX;
      state.midY = midY;
    }
  };

  const handleTouchEnd = (e: React.TouchEvent) => {
    const state = touchState.current;
    if (e.touches.length === 0) {
      if (measuringRef.current && state.mode === 'look') {
        const moved = Math.hypot(state.lastX - state.pickStartX, state.lastY - state.pickStartY);
        if (moved < TAP_MOVE_THRESHOLD) sendMeasurePick(state.lastX, state.lastY);
      }
      state.mode = 'none';
    } else if (e.touches.length === 1 && state.mode === 'zoom') {
      state.mode = 'look';
      state.lastX = e.touches[0].clientX;
      state.lastY = e.touches[0].clientY;
      state.pickStartX = e.touches[0].clientX;
      state.pickStartY = e.touches[0].clientY;
    }
  };

  const heldState = useRef({ u: 0, boost: 0 as 0 | 1, f: 0, s: 0 });

  useEffect(() => {
    const id = setInterval(() => {
      if (heldState.current.u !== 0) moveRef.current.u = heldState.current.u;
      if (heldState.current.boost !== 0) moveRef.current.boost = heldState.current.boost;
      if (heldState.current.f !== 0) moveRef.current.f = heldState.current.f;
      if (heldState.current.s !== 0) moveRef.current.s = heldState.current.s;
    }, 16);
    return () => clearInterval(id);
  }, [moveRef]);

  // ── Desktop mouse: left-drag look (or tap-to-measure), right-drag pan,
  //    wheel zoom ─────────────────────────────────────────────────────────
  const mouseState = useRef({ dragging: false, button: 0, lastX: 0, lastY: 0, startX: 0, startY: 0 });

  useEffect(() => {
    if (!isDesktop) return;
    const onMove = (e: MouseEvent) => {
      const m = mouseState.current;
      if (!m.dragging) return;
      const dx = e.clientX - m.lastX;
      const dy = e.clientY - m.lastY;
      if (m.button === 2) {
        moveRef.current.s += dx * panSpeedRef.current;
        moveRef.current.u += -dy * panSpeedRef.current;
      } else if (!measuringRef.current) {
        moveRef.current.yaw += dx * lookSpeedRef.current;
        moveRef.current.pit -= dy * lookSpeedRef.current;
      }
      m.lastX = e.clientX;
      m.lastY = e.clientY;
    };
    const onUp = (e: MouseEvent) => {
      const m = mouseState.current;
      if (!m.dragging) return;
      m.dragging = false;
      if (measuringRef.current && m.button !== 2) {
        const moved = Math.hypot(e.clientX - m.startX, e.clientY - m.startY);
        if (moved < TAP_MOVE_THRESHOLD) sendMeasurePick(e.clientX, e.clientY);
      }
    };
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
    return () => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup', onUp);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [isDesktop, moveRef]);

  const handleMouseDown = (e: React.MouseEvent) => {
    if (!isDesktop) return;
    e.preventDefault();
    mouseState.current = {
      dragging: true, button: e.button,
      lastX: e.clientX, lastY: e.clientY,
      startX: e.clientX, startY: e.clientY,
    };
  };

  const handleWheel = (e: React.WheelEvent) => {
    if (!isDesktop) return;
    e.preventDefault();
    moveRef.current.f += -e.deltaY * zoomSpeedRef.current * 0.04;
  };

  // ── Desktop keyboard: WASD fly, Space/Ctrl up-down, Shift boost ────────
  useEffect(() => {
    if (!isDesktop) return;
    const keysHeld = new Set<string>();
    const isTypingTarget = (t: EventTarget | null) =>
      t instanceof HTMLInputElement || t instanceof HTMLTextAreaElement || t instanceof HTMLSelectElement;
    const applyKeys = () => {
      let f = 0, s = 0, u = 0;
      if (keysHeld.has('w')) f += 1;
      if (keysHeld.has('s')) f -= 1;
      if (keysHeld.has('d')) s += 1;
      if (keysHeld.has('a')) s -= 1;
      if (keysHeld.has(' ')) u += 1;
      if (keysHeld.has('control') || keysHeld.has('c')) u -= 1;
      heldState.current.f = f;
      heldState.current.s = s;
      heldState.current.u = u;
    };
    const onKeyDown = (e: KeyboardEvent) => {
      if (isTypingTarget(e.target)) return;
      const k = e.key.toLowerCase();
      if (k === 'shift') { heldState.current.boost = 1; return; }
      if (!['w', 'a', 's', 'd', ' ', 'control', 'c'].includes(k)) return;
      e.preventDefault();
      keysHeld.add(k);
      applyKeys();
    };
    const onKeyUp = (e: KeyboardEvent) => {
      const k = e.key.toLowerCase();
      if (k === 'shift') { heldState.current.boost = 0; return; }
      keysHeld.delete(k);
      applyKeys();
    };
    const onBlur = () => {
      keysHeld.clear();
      heldState.current.f = 0;
      heldState.current.s = 0;
      heldState.current.boost = 0;
    };
    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);
    window.addEventListener('blur', onBlur);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
      window.removeEventListener('blur', onBlur);
    };
  }, [isDesktop]);

  return (
    <div
      ref={stageRef}
      className="stage"
      onTouchStart={handleTouchStart}
      onTouchMove={handleTouchMove}
      onTouchEnd={handleTouchEnd}
      onTouchCancel={handleTouchEnd}
      onMouseDown={handleMouseDown}
      onWheel={handleWheel}
      onContextMenu={(e) => { if (isDesktop) e.preventDefault(); }}
    >
      {/* ── Speed & Pinch Panel (bottom-left) ── */}
      <div
        className="fly-speed-panel"
        onTouchStart={e => e.stopPropagation()}
        onTouchMove={e => e.stopPropagation()}
        onTouchEnd={e => e.stopPropagation()}
        onPointerDown={e => e.stopPropagation()}
        onMouseDown={e => e.stopPropagation()}
      >
        <div className="fly-speed-panel-title">{isDesktop ? 'Mouse & Keyboard' : 'Gesture Sensitivity'}</div>

        <div className="speed-row">
          <span className="speed-row-label">Look</span>
          <input
            type="range" min="0.1" max="5.0" step="0.1"
            value={lookSpeed}
            onChange={e => setLookSpeed(parseFloat(e.target.value))}
          />
          <span className="speed-row-val">{lookSpeed.toFixed(1)}</span>
        </div>

        <div className="speed-row">
          <span className="speed-row-label">Pan</span>
          <input
            type="range" min="0.1" max="5.0" step="0.1"
            value={panSpeed}
            onChange={e => setPanSpeed(parseFloat(e.target.value))}
          />
          <span className="speed-row-val">{panSpeed.toFixed(1)}</span>
        </div>

        <div className="speed-row">
          <span className="speed-row-label">Zoom</span>
          <input
            type="range" min="0.1" max="5.0" step="0.1"
            value={zoomSpeed}
            onChange={e => setZoomSpeed(parseFloat(e.target.value))}
          />
          <span className="speed-row-val">{zoomSpeed.toFixed(1)}</span>
        </div>

        {/* Pinch mode toggle — touch only, no 2-finger concept with a mouse */}
        {!isDesktop && (
          <div className="pinch-toggle">
            <button
              className={`pinch-toggle-btn${pinchMode === 'zoom' ? ' active' : ''}`}
              onTouchEnd={e => { e.stopPropagation(); setPinchMode('zoom'); }}
              onClick={() => setPinchMode('zoom')}
            >
              <ZoomIn size={12} /> Zoom
            </button>
            <button
              className={`pinch-toggle-btn${pinchMode === 'pan' ? ' active' : ''}`}
              onTouchEnd={e => { e.stopPropagation(); setPinchMode('pan'); }}
              onClick={() => setPinchMode('pan')}
            >
              <Hand size={12} /> Pan
            </button>
          </div>
        )}
      </div>

      {/* ── Directional Controls (bottom-right) ── */}
      <div className="fly-controls" onMouseDown={e => e.stopPropagation()}>
        <HoldButton
          label="UP"
          icon={<ChevronUp size={14} />}
          onHold={(h) => { heldState.current.u = h ? 1 : 0; }}
        />
        <HoldButton
          label="DOWN"
          icon={<ChevronDown size={14} />}
          onHold={(h) => { heldState.current.u = h ? -1 : 0; }}
        />
        <HoldButton
          label="BOOST"
          className="boost"
          icon={<Zap size={14} />}
          onHold={(h) => { heldState.current.boost = h ? 1 : 0; }}
        />
      </div>

      {/* ── Instruction footer ── */}
      <div className="instruction-footer">
        {measuring
          ? <>Tap the video to place a measurement point</>
          : isDesktop
          ? <>Drag: Look &nbsp;·&nbsp; Right-drag: Pan &nbsp;·&nbsp; Wheel: Zoom &nbsp;·&nbsp; WASD move &nbsp;·&nbsp; Shift boost</>
          : <>1-finger: Look &nbsp;·&nbsp; 2-finger: {pinchMode === 'zoom' ? 'Pinch Zoom' : 'Pan'} &nbsp;·&nbsp; Toggle mode ↙</>}
      </div>
    </div>
  );
}
