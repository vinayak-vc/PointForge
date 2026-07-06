import type { Cfg } from './cfg';

// Viewport colour legend — mirrors the C++ viewer's elevation/intensity ramp.
// Floats at the viewport's right edge (next to the inspector), above the
// gesture stage but click-through. Turbo stops sampled from the same
// polynomial ramp the viewer's shader/legend use.
const TURBO_GRADIENT =
  'linear-gradient(to top,' +
  ' rgb(0,0,224) 0%, rgb(0,0,246) 12.5%, rgb(0,132,232) 25%,' +
  ' rgb(0,224,183) 37.5%, rgb(0,255,97) 50%, rgb(0,255,0) 62.5%,' +
  ' rgb(71,236,0) 75%, rgb(175,153,0) 87.5%, rgb(255,26,0) 100%)';

export default function Legend({ cfg }: { cfg: Cfg | null }) {
  if (!cfg || (cfg.colorMode !== 1 && cfg.colorMode !== 3)) return null;
  const elevation = cfg.colorMode === 1;
  const zmin = cfg.zmin ?? 0;
  const zmax = zmin + (cfg.cubeSize ?? 0);
  return (
    <div className="viewport-legend" aria-hidden="true">
      <div className="legend-title">{elevation ? 'Z (m)' : 'Intensity'}</div>
      <div className="legend-body">
        <div className="legend-bar" style={{ background: TURBO_GRADIENT }} />
        <div className="legend-labels">
          <span>{elevation ? zmax.toFixed(1) : 'max'}</span>
          <span>{elevation ? zmin.toFixed(1) : '0'}</span>
        </div>
      </div>
    </div>
  );
}
