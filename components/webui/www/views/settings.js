// Settings: appearance, time, status LED, login, rules between
// pumps (pro), network, MQTT, API keys (pro) and the device (name, pages).
import { html } from '../lib/html.js';
import { add, languages, setLang, t, useLang } from '../lib/i18n.js';
import { setClock, setMode, setTheme, usePrefs } from '../lib/prefs.js';
import { AccountCard, ApiCard } from './account.js';
import { DeviceCard } from './device.js';
import { LedCard } from './led.js';
import { LocksCard } from './locks.js';
import { MqttCard } from './mqtt.js';
import { NetworkCard } from './network.js';
import { TimeCard } from './time.js';

add({
  en: {
    'set.title': 'Settings', 'set.look': 'Appearance', 'set.lang': 'Language', 'set.theme': 'Theme', 'set.clock': 'Time format', 'clock.24': '24 hours', 'clock.12': '12 hours (AM/PM)',
    'theme.auto': 'Like the system', 'theme.dark': 'Dark', 'theme.light': 'Light',
    'set.mode': 'View', 'set.mode.hint': 'Simple shows what daily use needs. Pro adds detailed schedules, pump fine-tuning, rules between pumps and network options. Settings made in Pro stay active in Simple. Stored in this browser.',
    'set.device': 'Device', 'set.setup': 'Setup wizard', 'set.maint': 'Maintenance and firmware update',
  },
  de: {
    'set.title': 'Einstellungen', 'set.look': 'Darstellung', 'set.lang': 'Sprache', 'set.theme': 'Farbschema', 'set.clock': 'Zeitformat', 'clock.24': '24 Stunden', 'clock.12': '12 Stunden (AM/PM)',
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
        <div class="field"><label>${t('set.clock')}</label>
          ${seg(prefs.clock, ['24', '12'].map(v => [v, t('clock.' + v)]), setClock)}</div>
        <div class="field"><label>${t('set.mode')}</label>
          ${seg(prefs.mode, ['beginner', 'pro'].map(v => [v, t('mode.' + v)]), setMode)}
          <p class="mut small">${t('set.mode.hint')}</p></div>
      </section>
      <${TimeCard} pro=${pro}/>
      <${LedCard}/>
      <${AccountCard}/>
      ${pro && html`<${LocksCard}/>`}
      <${NetworkCard} pro=${pro}/>
      <${MqttCard} pro=${pro}/>
      ${pro && html`<${ApiCard}/>`}
      <${DeviceCard} pro=${pro}/>
    </div>`;
}
