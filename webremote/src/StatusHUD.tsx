import type { Cfg } from './cfg';
import type { StateMsg, WsStatus } from './useWebSocket';
import { Activity, Cpu, MapPin, FileText } from 'lucide-react';

// Premium status bar — rendered at the bottom of the app shell.
// Previously a floating overlay; now a fixed-height bar with chip layout.

export interface StatusHUDProps {
  cfg: Cfg | null;
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
  return 'dot-orange';
}

function statusLabel(status: WsStatus): string {
  if (status === 'connected') return 'Connected';
  if (status === 'disconnected') return 'Disconnected';
  if (status === 'pin' || status === 'pin_bad') return 'Awaiting PIN';
  return 'Connecting…';
}

export default function StatusHUD({ cfg, state, status }: StatusHUDProps) {
  return (
    <div className="status-bar">
      {/* Connection */}
      <div className="status-chip">
        <span className={`conn-dot ${dotClass(status)}`} />
        <span className="status-chip-val">{statusLabel(status)}</span>
      </div>

      {/* Build version */}
      {cfg?.version && (
        <div className="status-chip">
          <span className="status-chip-val">v{cfg.version}</span>
        </div>
      )}

      {state ? (
        <>
          {/* FPS */}
          <div className="status-chip">
            <Activity />
            <span>{Math.round(state.fps)}</span>
            <span className="status-chip-val"> fps</span>
          </div>

          {/* Points */}
          <div className="status-chip">
            <Cpu />
            <span className="status-chip-accent">{formatPoints(state.pts)}</span>
            <span> pts</span>
          </div>

          {/* File */}
          {state.file && (
            <div className="status-chip status-chip--file">
              <FileText />
              <span className="status-chip-val">{state.file}</span>
            </div>
          )}

          {/* Position */}
          <div className="status-chip status-chip--pos">
            <MapPin />
            <span className="status-chip-val">
              {state.pos[0].toFixed(1)}, {state.pos[1].toFixed(1)}, {state.pos[2].toFixed(1)}
            </span>
          </div>
        </>
      ) : (
        <div className="status-chip">
          <span>Waiting for telemetry…</span>
        </div>
      )}
    </div>
  );
}
