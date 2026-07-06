import type { ButtonHTMLAttributes } from 'react';

export interface PrimaryButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  loading?: boolean;
}

export default function PrimaryButton({ loading, disabled, children, className, ...rest }: PrimaryButtonProps) {
  return (
    <button
      type="submit"
      className={`primary-btn${className ? ` ${className}` : ''}`}
      disabled={disabled || loading}
      aria-busy={loading}
      {...rest}
    >
      {loading ? <span className="primary-btn-spinner" aria-hidden="true" /> : children}
    </button>
  );
}
