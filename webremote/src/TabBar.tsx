// Bottom tab bar (portrait) / left rail (landscape, via CSS) selecting one
// of the four remote pages.

export type Tab = 'fly' | 'display' | 'camera' | 'tools';

const TABS: ReadonlyArray<readonly [Tab, string]> = [
  ['fly', 'Fly'],
  ['display', 'Display'],
  ['camera', 'Camera'],
  ['tools', 'Tools'],
];

export interface TabBarProps {
  tab: Tab;
  onSelect: (t: Tab) => void;
}

export default function TabBar({ tab, onSelect }: TabBarProps) {
  return (
    <nav className="tabbar">
      {TABS.map(([id, label]) => (
        <button
          key={id}
          type="button"
          className={`tab-btn${tab === id ? ' active' : ''}`}
          onClick={() => onSelect(id)}
        >
          {label}
        </button>
      ))}
    </nav>
  );
}
