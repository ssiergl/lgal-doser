// Time settings (settings page): device time and where it comes from, time
// zone for everyone, NTP server in the pro view. A browser can hand over
// its clock when the device has no valid or a wrong time.
import { useEffect, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, getLang, t } from '../lib/i18n.js';
import { reloadAll } from '../lib/store.js';
import { SaveBar, useMsg } from '../lib/ui.js';
import { browserZone, ZONES, zoneIndex } from '../lib/zones.js';

add({
  en: {
    'tm.title': 'Time', 'tm.now': 'Device time', 'tm.src': 'Source', 'tm.last': 'Last time server sync',
    'tm.src.ntp': 'Time server (NTP)', 'tm.src.rtc': 'Clock module', 'tm.src.manual': 'Set by hand', 'tm.src.none': '–',
    'tm.invalid': 'no valid time', 'tm.never': 'never', 'tm.rtc_bad': 'The clock module does not answer – after a power cut the time is lost until the network is back.',
    'tm.off': 'Differs from this browser by {m} min.',
    'tm.zone': 'Time zone', 'tm.custom': 'Other (POSIX string) …', 'tm.posix': 'POSIX time zone',
    'tm.suggest': 'This browser uses {z}.', 'tm.use': 'Use it',
    'tm.ntp': 'Time server (NTP)', 'tm.ntp.hint': 'Empty = no time server; the time then comes from the clock module.',
    'tm.browser': 'Take the time from this browser', 'tm.browser.ok': 'Time taken from this browser.',
    'tm.saved': 'Time settings saved.', 'tm.field': 'Please check: {f}',
    'err.storage_failed': 'Could not save',
  },
  de: {
    'tm.title': 'Uhrzeit', 'tm.now': 'Gerätezeit', 'tm.src': 'Quelle', 'tm.last': 'Letzter Abgleich mit Zeitserver',
    'tm.src.ntp': 'Zeitserver (NTP)', 'tm.src.rtc': 'Uhrenmodul', 'tm.src.manual': 'Von Hand gesetzt', 'tm.src.none': '–',
    'tm.invalid': 'keine gültige Uhrzeit', 'tm.never': 'noch nie', 'tm.rtc_bad': 'Das Uhrenmodul antwortet nicht – nach einem Stromausfall fehlt die Uhrzeit, bis das Netzwerk wieder da ist.',
    'tm.off': 'Weicht um {m} min von diesem Browser ab.',
    'tm.zone': 'Zeitzone', 'tm.custom': 'Andere (POSIX-Angabe) …', 'tm.posix': 'POSIX-Zeitzone',
    'tm.suggest': 'Dieser Browser nutzt {z}.', 'tm.use': 'Übernehmen',
    'tm.ntp': 'Zeitserver (NTP)', 'tm.ntp.hint': 'Leer = kein Zeitserver; die Zeit kommt dann vom Uhrenmodul.',
    'tm.browser': 'Uhrzeit von diesem Browser übernehmen', 'tm.browser.ok': 'Uhrzeit von diesem Browser übernommen.',
    'tm.saved': 'Zeiteinstellungen gespeichert.', 'tm.field': 'Bitte prüfen: {f}',
    'err.storage_failed': 'Speichern fehlgeschlagen',
  },
});

const draftOf = T => {
  const i = zoneIndex(T.timezone);
  return { zone: i >= 0 ? String(i) : 'c', posix: i >= 0 ? '' : T.timezone, ntp: T.ntp_server };
};
const bodyOf = (d, pro) => {
  const b = { timezone: d.zone === 'c' ? d.posix.trim() : ZONES[+d.zone][1] };
  if (pro) b.ntp_server = d.ntp.trim();
  return b;
};
const fieldName = { timezone: 'tm.zone', ntp_server: 'tm.ntp', utc: 'tm.browser' };
const errOf = e => {
  const f = e.code === 'invalid_param' && e.body && e.body.error && e.body.error.field;
  return f && fieldName[f] ? t('tm.field', { f: t(fieldName[f]) }) : errText(e);
};

// Seconds ago -> "5 minutes ago" in the UI language
function ago(s) {
  const rtf = new Intl.RelativeTimeFormat(getLang(), { numeric: 'auto' });
  if (s < 3600) return rtf.format(-Math.round(s / 60), 'minute');
  if (s < 86400) return rtf.format(-Math.round(s / 3600), 'hour');
  return rtf.format(-Math.round(s / 86400), 'day');
}

export function TimeCard({ pro }) {
  const [T, setT] = useState(null);     // device time as last read
  const [off, setOff] = useState(0);    // device minus browser clock in s
  const [d, setDraft] = useState(null);
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const take = (j, reset) => {
    setT(j);
    setOff(j.valid ? j.utc - Date.now() / 1000 : 0);
    if (reset) setDraft(draftOf(j));
  };
  useEffect(() => {
    let first = true;
    const get = () => api('GET', '/api/v1/system/time')
      .then(j => { take(j, first); first = false; })
      .catch(e => { if (first) setT({ error: e }); });
    get();
    const id = setInterval(get, 15000);
    return () => clearInterval(id);
  }, []);

  const head = html`<h2>${t('tm.title')}</h2>`;
  if (!T) return html`<section class="card">${head}<p class="mut">…</p></section>`;
  if (T.error) return html`<section class="card">${head}<p class="msg err">${errText(T.error)}</p></section>`;

  const set = patch => setDraft(prev => ({ ...prev, ...patch }));
  const dirty = JSON.stringify(bodyOf(d, pro)) !== JSON.stringify(bodyOf(draftOf(T), pro));
  const wrong = T.valid && Math.abs(off) >= 120;
  const showBrowser = !T.valid || T.source !== 'ntp' || wrong;
  const bz = browserZone();
  const submit = e => {
    e.preventDefault();
    setBusy(true);
    api('PUT', '/api/v1/system/time', bodyOf(d, pro))
      .then(j => { take(j, true); msg.ok(t('tm.saved')); reloadAll(); })
      .catch(e => msg.err(errOf(e)))
      .finally(() => setBusy(false));
  };
  const fromBrowser = () => {
    setBusy(true);
    api('PUT', '/api/v1/system/time', { utc: Math.floor(Date.now() / 1000) })
      .then(j => { take(j, false); msg.ok(t('tm.browser.ok')); reloadAll(); })
      .catch(e => msg.err(errOf(e)))
      .finally(() => setBusy(false));
  };

  return html`<form class="card" novalidate onSubmit=${submit}>
    ${head}
    <dl class="stats">
      <dt>${t('tm.now')}</dt><dd>${T.valid ? T.local : html`<span class="warn">${t('tm.invalid')}</span>`}</dd>
      <dt>${t('tm.src')}</dt><dd>${t('tm.src.' + T.source)}</dd>
      ${pro && html`<dt>${t('tm.last')}</dt><dd>${T.last_ntp_sync > 0 && T.valid ? ago(Math.max(0, T.utc - T.last_ntp_sync)) : t('tm.never')}</dd>`}
    </dl>
    ${!T.rtc_ok && html`<p class="small warn">${t('tm.rtc_bad')}</p>`}
    ${wrong && html`<p class="small warn">${t('tm.off', { m: Math.round(Math.abs(off) / 60) })}</p>`}
    ${showBrowser && html`<button type="button" class="sec" disabled=${busy} onClick=${fromBrowser}>${t('tm.browser')}</button>`}
    <div class="field"><label for="tm-zone">${t('tm.zone')}</label>
      <select id="tm-zone" value=${d.zone} onChange=${e => set({ zone: e.target.value })}>
        ${ZONES.map((z, i) => html`<option value=${String(i)}>${z[0]}</option>`)}
        <option value="c">${t('tm.custom')}</option>
      </select>
      ${bz >= 0 && String(bz) !== d.zone && html`<p class="small mut">${t('tm.suggest', { z: ZONES[bz][0] })}
        <button type="button" class="ghost small" onClick=${() => set({ zone: String(bz) })}>${t('tm.use')}</button></p>`}
    </div>
    ${d.zone === 'c' && html`<div class="field"><label for="tm-posix">${t('tm.posix')}</label>
      <input id="tm-posix" maxlength="63" autocomplete="off" spellcheck="false" placeholder="CET-1CEST,M3.5.0,M10.5.0/3"
        value=${d.posix} onInput=${e => set({ posix: e.target.value })}/></div>`}
    ${pro && html`<div class="field"><label for="tm-ntp">${t('tm.ntp')}</label>
      <input id="tm-ntp" maxlength="63" autocomplete="off" spellcheck="false" placeholder="pool.ntp.org"
        value=${d.ntp} onInput=${e => set({ ntp: e.target.value })}/>
      <p class="small mut">${t('tm.ntp.hint')}</p></div>`}
    <${SaveBar} busy=${busy} dirty=${dirty} onDiscard=${() => { setDraft(draftOf(T)); msg.clear(); }}/>
    ${msg.node}
  </form>`;
}
