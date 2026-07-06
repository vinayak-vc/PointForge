import { useEffect, useRef } from 'react';

// Four individual PIN boxes replacing a single text field. Keyboard typing,
// backspace-to-previous, and pasting a full code are all supported; digit
// extraction/validation logic is unchanged from the old single <input> —
// this only changes how the same string value is edited.
export interface PinInputProps {
  value: string;
  length?: number;
  onChange: (next: string) => void;
  disabled?: boolean;
  error?: boolean;
  autoFocus?: boolean;
}

export default function PinInput({ value, length = 4, onChange, disabled, error, autoFocus }: PinInputProps) {
  const boxRefs = useRef<Array<HTMLInputElement | null>>([]);

  useEffect(() => {
    if (autoFocus) boxRefs.current[0]?.focus();
  }, [autoFocus]);

  const setDigit = (index: number, digit: string) => {
    const chars = value.padEnd(length, ' ').split('');
    chars[index] = digit;
    onChange(chars.join('').replace(/ /g, '').slice(0, length));
  };

  const handleChange = (index: number, raw: string) => {
    const digit = raw.replace(/\D/g, '').slice(-1);
    if (!digit) return;
    setDigit(index, digit);
    if (index < length - 1) boxRefs.current[index + 1]?.focus();
  };

  const handleKeyDown = (index: number, e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Backspace') {
      if (value[index]) {
        setDigit(index, '');
      } else if (index > 0) {
        setDigit(index - 1, '');
        boxRefs.current[index - 1]?.focus();
      }
      e.preventDefault();
    } else if (e.key === 'ArrowLeft' && index > 0) {
      boxRefs.current[index - 1]?.focus();
    } else if (e.key === 'ArrowRight' && index < length - 1) {
      boxRefs.current[index + 1]?.focus();
    }
  };

  const handlePaste = (e: React.ClipboardEvent<HTMLInputElement>) => {
    const digits = e.clipboardData.getData('text').replace(/\D/g, '').slice(0, length);
    if (!digits) return;
    e.preventDefault();
    onChange(digits);
    boxRefs.current[Math.min(digits.length, length - 1)]?.focus();
  };

  return (
    <div className={`pin-boxes${error ? ' pin-boxes--error' : ''}`} role="group" aria-label="4-digit PIN">
      {Array.from({ length }, (_, i) => (
        <input
          key={i}
          ref={(el) => { boxRefs.current[i] = el; }}
          className="pin-box"
          type="text"
          inputMode="numeric"
          pattern="\d*"
          maxLength={1}
          autoComplete={i === 0 ? 'one-time-code' : 'off'}
          disabled={disabled}
          value={value[i] ?? ''}
          onChange={(e) => handleChange(i, e.target.value)}
          onKeyDown={(e) => handleKeyDown(i, e)}
          onPaste={handlePaste}
          onFocus={(e) => e.currentTarget.select()}
          aria-label={`PIN digit ${i + 1}`}
        />
      ))}
    </div>
  );
}
