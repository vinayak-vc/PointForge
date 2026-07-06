import { ShieldCheck, Radio, Gauge } from 'lucide-react';
import AnimatedLogo from './AnimatedLogo';
import type { WsStatus } from './useWebSocket';

// Left-hand branding pane (desktop landscape) / compact header (stacked
// layouts). Purely presentational — no auth logic lives here.
export interface BrandingPanelProps {
  status: WsStatus;
}

export default function BrandingPanel({ status }: BrandingPanelProps) {
  const ready = status === 'pin' || status === 'pin_bad';
  return (
    <div className="branding-panel">
      <AnimatedLogo />
      <h1 className="app-title">ViitorXPC</h1>
      <p className="app-subtitle">Remote LiDAR Controller</p>

      <div className="branding-facts">
        <div className="branding-fact">
          <ShieldCheck size={15} aria-hidden="true" />
          <span>PIN-secured local connection</span>
        </div>
        <div className="branding-fact">
          <Gauge size={15} aria-hidden="true" />
          <span>Industrial-grade point cloud precision</span>
        </div>
        <div className="branding-fact">
          <Radio size={15} aria-hidden="true" />
          <span>{ready ? 'Viewer ready — enter PIN to pair' : 'Waiting on the local viewer'}</span>
        </div>
      </div>
    </div>
  );
}
