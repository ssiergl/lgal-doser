// Per-browser view preferences: beginner/pro mode and colour theme.
import { hub, load, save } from './hub.js';

const changes = hub();
const prefs = {
  mode: load('lgal.mode', 'beginner') === 'pro' ? 'pro' : 'beginner',
  theme: ['dark', 'light'].includes(load('lgal.theme', '')) ? load('lgal.theme', '') : 'auto',
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
