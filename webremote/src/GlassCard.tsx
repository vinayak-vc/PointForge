import type { ReactNode } from 'react';

export default function GlassCard({ children, className }: { children: ReactNode; className?: string }) {
  return <div className={`glass-card${className ? ` ${className}` : ''}`}>{children}</div>;
}
