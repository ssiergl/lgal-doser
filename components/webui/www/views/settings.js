// Settings: appearance, time, status LED, login, rules between
// pumps (pro), network, MQTT, API keys (pro) and the device (name, pages).
import { html } from '../lib/html.js';
import { add, languages, setLang, t, useLang } from '../lib/i18n.js';
import { setClock, setMode, setTheme, usePrefs } from '../lib/prefs.js';
import { LangPicker } from '../lib/ui.js';
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
        <div class="field"><label>${t('set.lang')}</label><${LangPicker} value=${lang} options=${languages} onChange=${setLang} left/></div>
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
