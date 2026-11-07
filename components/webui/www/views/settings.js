// Settings: appearance, time, network, login and API keys, rules between
// pumps (pro) and device pages.
import { html } from '../lib/html.js';
import { add, languages, setLang, t, useLang } from '../lib/i18n.js';
import { setMode, setTheme, usePrefs } from '../lib/prefs.js';
import { AccountCard } from './account.js';
import { LocksCard } from './locks.js';
import { NetworkCard } from './network.js';
import { TimeCard } from './time.js';

add({
  en: {
    'set.title': 'Settings', 'set.look': 'Appearance', 'set.lang': 'Language', 'set.theme': 'Theme',
    'theme.auto': 'Like the system', 'theme.dark': 'Dark', 'theme.light': 'Light',
    'set.mode': 'View', 'set.mode.hint': 'Simple shows what daily use needs. Pro adds detailed schedules, pump fine-tuning, rules between pumps and network options. Settings made in Pro stay active in Simple. Stored in this browser.',
    'set.device': 'Device', 'set.setup': 'Setup wizard', 'set.maint': 'Maintenance and firmware update',
  },
  de: {
    'set.title': 'Einstellungen', 'set.look': 'Darstellung', 'set.lang': 'Sprache', 'set.theme': 'Farbschema',
    'theme.auto': 'Wie das System', 'theme.dark': 'Dunkel', 'theme.light': 'Hell',
    'set.mode': 'Ansicht', 'set.mode.hint': 'Einfach zeigt, was der Alltag braucht. Profi ergänzt detaillierte Zeitpläne, Pumpen-Feintuning, Regeln zwischen Pumpen und Netzwerk-Optionen. In Profi Eingestelltes bleibt in Einfach aktiv. Gespeichert in diesem Browser.',
    'set.device': 'Gerät', 'set.setup': 'Einrichtungsassistent', 'set.maint': 'Wartung und Firmware-Update',
  },
});

export function SettingsView() {
  const lang = useLang();
  const prefs = usePrefs();
  const pro = prefs.mode === 'pro';
  const seg = (value, options, set) => html`<div class="seg">${options.map(([v, label]) =>
    html`<button class=${value === v ? 'on' : ''} onClick=${() => set(v)}>${label}</button>`)}</div>`;
  return html`<div class="page-head"><h1>${t('set.title')}</h1></div>
    <div class="grid settings">
      <section class="card"><h2>${t('set.look')}</h2>
        <div class="field"><label>${t('set.lang')}</label>${seg(lang, languages, setLang)}</div>
        <div class="field"><label>${t('set.theme')}</label>
          ${seg(prefs.theme, ['auto', 'dark', 'light'].map(v => [v, t('theme.' + v)]), setTheme)}</div>
        <div class="field"><label>${t('set.mode')}</label>
          ${seg(prefs.mode, ['beginner', 'pro'].map(v => [v, t('mode.' + v)]), setMode)}
          <p class="mut small">${t('set.mode.hint')}</p></div>
      </section>
      <${TimeCard} pro=${pro}/>
      <${NetworkCard} pro=${pro}/>
      <${AccountCard} pro=${pro}/>
      ${pro && html`<${LocksCard}/>`}
      <section class="card"><h2>${t('set.device')}</h2>
        <ul class="links">
          <li><a href="/setup">${t('set.setup')}</a></li>
          <li><a href="/recovery">${t('set.maint')}</a></li>
        </ul>
      </section>
    </div>`;
}
