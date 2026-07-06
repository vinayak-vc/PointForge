// Small breathing glow ring around the brand mark — a calm, professional
// idle animation (no spin/bounce), respects prefers-reduced-motion via CSS.
export interface AnimatedLogoProps {
  size?: number;
}

export default function AnimatedLogo({ size = 64 }: AnimatedLogoProps) {
  return (
    <div className="brand-logo" style={{ width: size, height: size }}>
      <div className="brand-logo-pulse" aria-hidden="true" />
      <img src="/logo.svg" alt="ViitorXPC" className="brand-logo-img" />
    </div>
  );
}
