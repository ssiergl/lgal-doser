// Per-browser view preferences: beginner/pro mode, colour theme and clock
// (24 h or 12 h with AM/PM).
import { hub, load, save } from './hub.js';

const changes = hub();
const prefs = {
  mode: load('lgal.mode', 'beginner') === 'pro' ? 'pro' : 'beginner',
  theme: ['dark', 'light'].includes(load('lgal.theme', '')) ? load('lgal.theme', '') : 'auto',
  clock: load('lgal.clock', '24') === '12' ? '12' : '24',
};

function applyTheme() {
  const el = document.documentElement;
  if (prefs.theme === 'auto') el.removeAttribute('data-theme');
  else el.setAttribute('data-theme', prefs.theme);
}
applyTheme();

export const usePrefs = () => { changes.use(); return prefs; };

export function setMode(m) {
  prefs.mode = m;
  save('lgal.mode', m);
  changes.emit();
}

export function setTheme(th) {
  prefs.theme = th;
  save('lgal.theme', th === 'auto' ? '' : th);
  applyTheme();
  changes.emit();
}

export function setClock(c) {
  prefs.clock = c;
  save('lgal.clock', c);
  changes.emit();
}

// Device-local "HH:MM" or "HH:MM:SS" in the chosen clock: "20:30" or "8:30 PM"
export function hm(s) {
  if (prefs.clock !== '12' || !s) return s;
  const [h, ...rest] = s.split(':');
  return `${(+h % 12) || 12}:${rest.join(':')} ${+h < 12 ? 'AM' : 'PM'}`;
}

// Full hour on an axis: "18" or "6 PM"
export const hourLabel = h => (prefs.clock === '12' ? `${(h % 12) || 12} ${h % 24 < 12 ? 'AM' : 'PM'}` : String(h));

// Intl option for formatted times
export const hourCycle = () => ({ hourCycle: prefs.clock === '12' ? 'h12' : 'h23' });
