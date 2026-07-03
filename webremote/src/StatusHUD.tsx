import type { StateMsg, WsStatus } from './useWebSocket';

// Small overlay showing viewer telemetry from the ~5 Hz state messages,
// plus a connection dot (green = connected, orange = handshaking, red = down).

export interface StatusHUDProps {
  state: StateMsg | null;
  status: WsStatus;
}

export function formatPoints(n: number): string {
  if (n >= 1e9) return `${(n / 1e9).toFixed(1)}B`;
  if (n >= 1e6) return `${(n / 1e6).toFixed(1)}M`;
  if (n >= 1e3) return `${(n / 1e3).toFixed(1)}K`;
  return String(n);
}

function dotClass(status: WsStatus): string {
  if (status === 'connected') return 'dot-green';
  if (status === 'disconnected') return 'dot-red';
  return 'dot-orange'; // connecting / pin / pin_bad
}

export default function StatusHUD({ state, status }: StatusHUDProps) {
  return (
    <div className="status-hud">
      <span className={`conn-dot ${dotClass(status)}`} />
      {state ? (
        <>
          <span className="hud-file">{state.file || '—'}</span>
          <span>{Math.round(state.fps)} fps</span>
          <span>{formatPoints(state.pts)} pts</span>
          <span className="hud-pos">
            {state.pos[0].toFixed(1)}, {state.pos[1].toFixed(1)},{' '}
            {state.pos[2].toFixed(1)}
          </span>
        </>
      ) : (
        <span className="hud-file">waiting for state…</span>
      )}
    </div>
  );
}
