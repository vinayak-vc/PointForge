import { useEffect, useRef } from 'react';
import type { MutableRefObject } from 'react';
import StatusHUD from './StatusHUD';
import type { StateMsg, WsStatus } from './useWebSocket';

// Fly page: left stick flies (f/s), right stick looks (yaw/pit), centre
// column holds UP/DOWN/BOOST, HUD top-left. Control values are written into
// the shared moveRef; the 30 Hz move loop in App reads them. On unmount
// (tab switch) the ref is zeroed so no stale input keeps the camera moving —
// the loop then emits its single trailing all-zeros move.

export interface MoveValues {
  f: number;
  s: number;
  u: number;
  yaw: number;
  pit: number;
  boost: 0 | 1;
}

export const ZERO_MOVE: MoveValues = { f: 0, s: 0, u: 0, yaw: 0, pit: 0, boost: 0 };

// ---------------------------------------------------------------------------
// Hold button: fires onHold(true) on press and onHold(false) on release.
// Pointer capture makes the release reliable even if the finger slides off.
// ---------------------------------------------------------------------------
interface HoldButtonProps {
  label: string;
  className?: string;
  onHold: (held: boolean) => void;
}

function HoldButton({ label, className, onHold }: HoldButtonProps) {
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
      {label}
    </button>
  );
}

// ---------------------------------------------------------------------------
// FlyTab
// ---------------------------------------------------------------------------
export interface FlyTabProps {
  moveRef: MutableRefObject<MoveValues>;
  lastState: StateMsg | null;
  status: WsStatus;
}

export default function FlyTab({ moveRef, lastState, status }: FlyTabProps) {
  // Zero all controls when this tab unmounts.
  useEffect(() => {
    const ref = moveRef;
    return () => {
      ref.current = { ...ZERO_MOVE };
    };
  }, [moveRef]);

  const touchState = useRef({
    mode: 'none' as 'none' | 'look' | 'pan' | 'zoom',
    lastX: 0,
    lastY: 0,
    startDist: 0,
    lastTaps: [] as number[],
  });

  const getDist = (touches: React.TouchList) => {
    const dx = touches[0].clientX - touches[1].clientX;
    const dy = touches[0].clientY - touches[1].clientY;
    return Math.hypot(dx, dy);
  };

  const handleTouchStart = (e: React.TouchEvent) => {
    const state = touchState.current;
    if (e.touches.length === 1) {
      const now = Date.now();
      state.lastTaps.push(now);
      while (state.lastTaps.length > 0 && now - state.lastTaps[0] > 300) {
        state.lastTaps.shift();
      }
      
      state.lastX = e.touches[0].clientX;
      state.lastY = e.touches[0].clientY;
      
      if (state.lastTaps.length >= 2) {
        state.mode = 'pan';
        state.lastTaps = []; // consume taps
      } else {
        state.mode = 'look';
      }
    } else if (e.touches.length === 2) {
      state.mode = 'zoom';
      state.startDist = getDist(e.touches);
    }
  };

  const handleTouchMove = (e: React.TouchEvent) => {
    const state = touchState.current;
    if (state.mode === 'none') return;

    if ((state.mode === 'look' || state.mode === 'pan') && e.touches.length === 1) {
      const dx = e.touches[0].clientX - state.lastX;
      const dy = e.touches[0].clientY - state.lastY;
      
      // Accumulate deltas since App.tsx clears them every 33ms
      const sensitivity = 0.2;
      
      if (state.mode === 'look') {
        moveRef.current.yaw += dx * sensitivity;
        moveRef.current.pit -= dy * sensitivity; // inverted Y for pitch
      } else {
        moveRef.current.s += dx * sensitivity;
        moveRef.current.u += -dy * sensitivity; // Up is -Y
      }
      
      state.lastX = e.touches[0].clientX;
      state.lastY = e.touches[0].clientY;
    } else if (state.mode === 'zoom' && e.touches.length === 2) {
      const dist = getDist(e.touches);
      const diff = dist - state.startDist;
      
      const sensitivity = 0.1;
      moveRef.current.f += diff * sensitivity;
      
      state.startDist = dist;
    }
  };

  const handleTouchEnd = (e: React.TouchEvent) => {
    const state = touchState.current;
    if (e.touches.length === 0) {
      state.mode = 'none';
    } else if (e.touches.length === 1 && state.mode === 'zoom') {
      state.mode = 'look';
      state.lastX = e.touches[0].clientX;
      state.lastY = e.touches[0].clientY;
    }
  };

  const heldState = useRef({ u: 0, boost: 0 as 0|1 });

  useEffect(() => {
    const id = setInterval(() => {
      if (heldState.current.u !== 0) moveRef.current.u = heldState.current.u;
      if (heldState.current.boost !== 0) moveRef.current.boost = heldState.current.boost;
    }, 16); // Run faster than App's 33ms to ensure it's always populated before send
    return () => clearInterval(id);
  }, [moveRef]);

  return (
    <div 
      className="stage"
      onTouchStart={handleTouchStart}
      onTouchMove={handleTouchMove}
      onTouchEnd={handleTouchEnd}
      onTouchCancel={handleTouchEnd}
    >
      <StatusHUD state={lastState} status={status} />
      
      <div className="instruction-footer" style={{
        position: 'absolute', bottom: '10px', left: '0', right: '0', 
        textAlign: 'center', color: 'var(--text-dim)', fontSize: '12px', pointerEvents: 'none'
      }}>
        Single tap &amp; move: Look | Double tap &amp; move: Pan | Pinch: Zoom
      </div>

      <div className="mid-controls" style={{ zIndex: 10 }}>
        <HoldButton
          label="UP"
          onHold={(h) => {
            heldState.current.u = h ? 1 : 0;
          }}
        />
        <HoldButton
          label="DOWN"
          onHold={(h) => {
            heldState.current.u = h ? -1 : 0;
          }}
        />
        <HoldButton
          label="BOOST"
          className="boost"
          onHold={(h) => {
            heldState.current.boost = h ? 1 : 0;
          }}
        />
      </div>
    </div>
  );
}
