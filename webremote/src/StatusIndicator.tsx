import type { WsStatus } from './useWebSocket';

// Unified connection status readout — replaces the separate ad-hoc
// error/status/hint paragraphs with one component whose dot colour, title,
// and detail line animate together as `status` changes.
export interface StatusIndicatorProps {
  status: WsStatus;
}

interface StatusCopy {
  dot: 'idle' | 'accent' | 'success' | 'warning' | 'danger';
  title: string;
  detail: string;
  spin?: boolean;
}

function copyFor(status: WsStatus): StatusCopy {
  switch (status) {
    case 'connected':
      return { dot: 'success', title: 'Connected', detail: 'PIN-protected local link' };
    case 'connecting':
      return { dot: 'accent', title: 'Connecting…', detail: 'Reaching the viewer', spin: true };
    case 'pin_bad':
      return { dot: 'warning', title: 'Incorrect PIN', detail: 'Check the viewer and try again' };
    case 'disconnected':
      return { dot: 'danger', title: 'Offline', detail: 'No connection — reconnecting…', spin: true };
    case 'pin':
    default:
      return { dot: 'idle', title: 'Awaiting PIN', detail: 'Enter the code shown in ViitorXPC Viewer' };
  }
}

export default function StatusIndicator({ status }: StatusIndicatorProps) {
  const c = copyFor(status);
  return (
    <div className={`status-indicator status-indicator--${c.dot}`} role="status" aria-live="polite">
      <span className="status-indicator-dot">
        {c.spin && <span className="status-indicator-ring" aria-hidden="true" />}
      </span>
      <span className="status-indicator-text">
        <span className="status-indicator-title">{c.title}</span>
        <span className="status-indicator-detail">{c.detail}</span>
      </span>
    </div>
  );
}
