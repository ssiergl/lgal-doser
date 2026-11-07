// Status LED (settings page): what the LED shows right now, what the colours
// mean, and the night mode that dims the idle light in a time window.
import { useEffect, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, t } from '../lib/i18n.js';
import { SaveBar, Seg, useMsg } from '../lib/ui.js';

add({
  en: {
    'led.title': 'Status LED', 'led.now': 'Now', 'led.legend': 'What the colours mean',
    'led.s.critical': 'Red, fast blinking – alarm: daily limit reached, driver or clock module fault',
    'led.s.ota': 'Magenta/white – firmware update running, pumps locked',
    'led.s.service': 'Violet pulsing – calibration or filling the tube',
    'led.s.dosing': 'Cyan pulsing – a pump is dosing',
    'led.s.setup_ap': 'Orange pulsing – setup Wi-Fi is on',
    'led.s.warning': 'Yellow, slow blinking – warning: container low (≤ 10 % or < 3 days), tube worn, no network or no valid time',
    'led.s.idle_lan': 'Green – ready, connected by LAN',
    'led.s.idle_wlan': 'Turquoise – ready, connected by Wi-Fi',
    'led.s.boot': 'White fading in – starting', 'led.s.button_ap': 'Orange – button held 3 s', 'led.s.button_reset': 'Red – button held 10 s',
    'led.s.off': 'Off',
    'led.n.critical': 'Red blinking – alarm', 'led.n.ota': 'Magenta/white – update', 'led.n.service': 'Violet – calibration / filling',
    'led.n.dosing': 'Cyan – dosing', 'led.n.setup_ap': 'Orange – setup Wi-Fi', 'led.n.warning': 'Yellow blinking – warning',
    'led.n.idle_lan': 'Green – ready (LAN)', 'led.n.idle_wlan': 'Turquoise – ready (Wi-Fi)', 'led.n.boot': 'White – starting',
    'led.n.button_ap': 'Orange – button held', 'led.n.button_reset': 'Red – button held', 'led.n.off': 'Off',
    'led.why': 'The overview shows which pump it is about.',
    'led.dimmed': 'dimmed by the night mode', 'led.dark': 'switched off by the night mode',
    'led.night': 'Night mode', 'led.night.hint': 'Dims or switches off the green/turquoise ready light at night. Alarms, warnings and dosing stay fully visible.',
    'led.night.on': 'On', 'led.night.off': 'Off', 'led.from': 'From', 'led.to': 'To', 'led.bright': 'Ready light at night',
    'led.b.0': 'Off', 'led.saved': 'Status LED saved.', 'led.same': 'Start and end must differ.',
    'led.field': 'Please check: {f}',
  },
  de: {
    'led.title': 'Status-LED', 'led.now': 'Gerade', 'led.legend': 'Was die Farben bedeuten',
    'led.s.critical': 'Rot, schnell blinkend – Alarm: Tageslimit erreicht, Treiber- oder Uhrenmodul-Fehler',
    'led.s.ota': 'Magenta/Weiß – Firmware-Update läuft, Pumpen gesperrt',
    'led.s.service': 'Violett pulsierend – Kalibrieren oder Schlauch füllen',
    'led.s.dosing': 'Cyan pulsierend – eine Pumpe dosiert',
    'led.s.setup_ap': 'Orange pulsierend – Setup-WLAN ist an',
    'led.s.warning': 'Gelb, langsam blinkend – Warnung: Behälter fast leer (≤ 10 % oder < 3 Tage), Schlauch verschlissen, kein Netzwerk oder keine gültige Uhrzeit',
    'led.s.idle_lan': 'Grün – betriebsbereit, per LAN verbunden',
    'led.s.idle_wlan': 'Türkis – betriebsbereit, per WLAN verbunden',
    'led.s.boot': 'Weiß aufblendend – startet', 'led.s.button_ap': 'Orange – Taster 3 s gehalten', 'led.s.button_reset': 'Rot – Taster 10 s gehalten',
    'led.s.off': 'Aus',
    'led.n.critical': 'Rot blinkend – Alarm', 'led.n.ota': 'Magenta/Weiß – Update', 'led.n.service': 'Violett – Kalibrieren / Füllen',
    'led.n.dosing': 'Cyan – dosiert', 'led.n.setup_ap': 'Orange – Setup-WLAN', 'led.n.warning': 'Gelb blinkend – Warnung',
    'led.n.idle_lan': 'Grün – bereit (LAN)', 'led.n.idle_wlan': 'Türkis – bereit (WLAN)', 'led.n.boot': 'Weiß – startet',
    'led.n.button_ap': 'Orange – Taster gehalten', 'led.n.button_reset': 'Rot – Taster gehalten', 'led.n.off': 'Aus',
    'led.why': 'Die Übersicht zeigt, um welche Pumpe es geht.',
    'led.dimmed': 'vom Nachtmodus gedimmt', 'led.dark': 'vom Nachtmodus ausgeschaltet',
    'led.night': 'Nachtmodus', 'led.night.hint': 'Dimmt das grüne/türkise Bereit-Licht nachts oder schaltet es aus. Alarme, Warnungen und Dosierungen bleiben voll sichtbar.',
    'led.night.on': 'An', 'led.night.off': 'Aus', 'led.from': 'Von', 'led.to': 'Bis', 'led.bright': 'Bereit-Licht nachts',
    'led.b.0': 'Aus', 'led.saved': 'Status-LED gespeichert.', 'led.same': 'Beginn und Ende müssen sich unterscheiden.',
    'led.field': 'Bitte prüfen: {f}',
  },
});

const kLegend = ['critical', 'ota', 'service', 'dosing', 'setup_ap', 'warning', 'idle_lan', 'idle_wlan'];
const kColor = {
  critical: '#ff3b30', ota: '#e040fb', service: '#8c3cff', dosing: '#00ccff', setup_ap: '#ff7a00', warning: '#ffcc00',
  idle_lan: '#22c55e', idle_wlan: '#14e0a0', boot: '#ffffff', button_ap: '#ff7a00', button_reset: '#ff3b30', off: 'transparent',
};
const kLevels = [0, 10, 25, 50];
const fieldKey = { 'night.start': 'led.from', 'night.end': 'led.to', 'night.brightness': 'led.bright' };

const draftOf = l => ({ ...l.night });
const bodyOf = d => ({ night: { enabled: d.enabled, start: d.start, end: d.end, brightness: d.brightness } });

function Dot({ s }) {
  return html`<i class=${'led-dot' + (s === 'off' ? ' off' : '')} style=${{ '--c': kColor[s] || 'transparent' }}></i>`;
}

export function LedCard() {
  const [L, setL] = useState(null);
  const [d, setDraft] = useState(null);
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const take = (j, reset) => { setL(j); if (reset) setDraft(draftOf(j)); };
  useEffect(() => {
    let first = true;
    const get = () => api('GET', '/api/v1/system/led').then(j => { take(j, first); first = false; })
      .catch(e => { if (first) setL({ error: e }); });
    get();
    const id = setInterval(get, 5000);
    return () => clearInterval(id);
  }, []);

  const head = html`<h2>${t('led.title')}</h2>`;
  if (!L) return html`<section class="card">${head}<p class="mut">…</p></section>`;
  if (L.error) return html`<section class="card">${head}<p class="msg err">${errText(L.error)}</p></section>`;

  const set = patch => setDraft(prev => ({ ...prev, ...patch }));
  const dirty = JSON.stringify(bodyOf(d)) !== JSON.stringify(bodyOf(draftOf(L)));
  const idle = L.shown === 'idle_lan' || L.shown === 'idle_wlan';
  const dim = idle && L.idle_brightness < 100 ? (L.idle_brightness === 0 ? t('led.dark') : t('led.dimmed')) : '';
  const levels = kLevels.includes(d.brightness) ? kLevels : [...kLevels, d.brightness].sort((a, b) => a - b);
  const submit = e => {
    e.preventDefault();
    if (d.enabled && d.start === d.end) { msg.err(t('led.same')); return; }
    setBusy(true);
    api('PUT', '/api/v1/system/led', bodyOf(d))
      .then(j => { take(j, true); msg.ok(t('led.saved')); })
      .catch(e => {
        const f = e.code === 'invalid_param' && e.body && e.body.error && e.body.error.field;
        msg.err(f && fieldKey[f] ? t('led.field', { f: t(fieldKey[f]) }) : errText(e));
      })
      .finally(() => setBusy(false));
  };

  return html`<form class="card" novalidate onSubmit=${submit}>
    ${head}
    <p class="led-now"><${Dot} s=${L.shown}/><span><span class="mut">${t('led.now')}:</span> ${t('led.n.' + L.shown)}${dim && html` <span class="mut">(${dim})</span>`}
      ${(L.shown === 'critical' || L.shown === 'warning') && html`<br/><span class="small mut">${t('led.why')}</span>`}</span></p>
    <details class="led-legend"><summary class="small">${t('led.legend')}</summary>
      <ul>${kLegend.map(s => html`<li><${Dot} s=${s}/><span class="small">${t('led.s.' + s)}</span></li>`)}</ul>
    </details>
    <div class="field"><span class="lbl">${t('led.night')}</span>
      <${Seg} value=${d.enabled} onChange=${v => set({ enabled: v })} options=${[[false, t('led.night.off')], [true, t('led.night.on')]]}/>
      <p class="small mut">${t('led.night.hint')}</p></div>
    ${d.enabled && html`<div class="row led-times">
        <label class="field">${t('led.from')}<input type="time" step="60" value=${d.start} onInput=${e => set({ start: e.target.value })}/></label>
        <label class="field">${t('led.to')}<input type="time" step="60" value=${d.end} onInput=${e => set({ end: e.target.value })}/></label>
      </div>
      <div class="field"><span class="lbl">${t('led.bright')}</span>
        <${Seg} value=${d.brightness} onChange=${v => set({ brightness: v })}
          options=${levels.map(b => [b, b === 0 ? t('led.b.0') : b + ' %'])}/></div>`}
    <${SaveBar} busy=${busy} dirty=${dirty} onDiscard=${() => { setDraft(draftOf(L)); msg.clear(); }}/>
    ${msg.node}
  </form>`;
}
