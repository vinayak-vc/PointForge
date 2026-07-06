const KEYS = ['1', '2', '3', '4', '5', '6', '7', '8', '9', 'C', '0', 'DEL'] as const;

export interface NumberPadProps {
  onPress: (key: string) => void;
  disabled?: boolean;
}

export default function NumberPad({ onPress, disabled }: NumberPadProps) {
  return (
    <div className="number-pad" role="group" aria-label="Keypad">
      {KEYS.map((k) => (
        <button
          key={k}
          type="button"
          className={`number-pad-btn${k === 'C' || k === 'DEL' ? ' number-pad-btn--fn' : ''}`}
          disabled={disabled}
          aria-label={k === 'C' ? 'Clear' : k === 'DEL' ? 'Delete digit' : k}
          onClick={() => onPress(k)}
        >
          {k}
        </button>
      ))}
    </div>
  );
}
