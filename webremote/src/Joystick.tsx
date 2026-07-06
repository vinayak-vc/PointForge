import { useCallback, useRef } from 'react';

// Reusable touch joystick. Pointer events + setPointerCapture so it works
// with multitouch (one pointer per stick). Reports normalized x,y in [-1,1]
// with x = right-positive and y = UP-positive (screen y is inverted for the
// caller's convenience). Springs back to 0,0 on release. No external libs.

export interface JoystickProps {
  /** Normalized deflection: x right-positive, y up-positive, both in [-1,1]. */
  onChange: (x: number, y: number) => void;
  label?: string;
  className?: string;
}

export default function Joystick({ onChange, label, className }: JoystickProps) {
  const baseRef = useRef<HTMLDivElement>(null);
  const knobRef = useRef<HTMLDivElement>(null);
  const pointerIdRef = useRef<number | null>(null);

  // Knob position is written straight to the DOM (no re-render per move).
  const setKnob = (dx: number, dy: number) => {
    const knob = knobRef.current;
    if (knob) {
      knob.style.transform = `translate(-50%, -50%) translate(${dx}px, ${dy}px)`;
    }
  };

  const update = useCallback(
    (clientX: number, clientY: number) => {
      const base = baseRef.current;
      if (!base) return;
      const rect = base.getBoundingClientRect();
      const cx = rect.left + rect.width / 2;
      const cy = rect.top + rect.height / 2;
      const radius = rect.width / 2;
      let dx = clientX - cx;
      let dy = clientY - cy;
      const len = Math.hypot(dx, dy);
      if (len > radius && len > 0) {
        // Clamp the knob to the base radius.
        dx = (dx / len) * radius;
        dy = (dy / len) * radius;
      }
      setKnob(dx, dy);
      onChange(dx / radius, -dy / radius); // invert y: up is positive
    },
    [onChange],
  );

  const handlePointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
    if (pointerIdRef.current !== null) return; // one pointer per stick
    pointerIdRef.current = e.pointerId;
    baseRef.current?.setPointerCapture(e.pointerId);
    update(e.clientX, e.clientY);
  };

  const handlePointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
    if (e.pointerId !== pointerIdRef.current) return;
    update(e.clientX, e.clientY);
  };

  const release = (e: React.PointerEvent<HTMLDivElement>) => {
    if (e.pointerId !== pointerIdRef.current) return;
    pointerIdRef.current = null;
    setKnob(0, 0); // spring back to centre
    onChange(0, 0);
  };

  return (
    <div
      ref={baseRef}
      className={`joystick${className ? ` ${className}` : ''}`}
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={release}
      onPointerCancel={release}
      onLostPointerCapture={release}
    >
      <div ref={knobRef} className="joystick-knob" />
      {label && <span className="joystick-label">{label}</span>}
    </div>
  );
}
