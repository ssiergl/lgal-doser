// Full backup in the engineering dialog (M6): export of the whole
// configuration with or without secrets, restore of chosen parts.
import { useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, getLang, t } from '../lib/i18n.js';
import { reloadAll } from '../lib/store.js';
import { useMsg } from '../lib/ui.js';

add({
  en: {
    'bk.title': 'Backup', 'bk.hint': 'All settings, pumps, schedules, lock times, service values and hardware parameters as one file. Container contents, counters and the history are not included.',
    'bk.secrets': 'Include Wi-Fi password, admin password and API keys',
    'bk.secrets.hint': 'Admin password and API keys are only included as hashes, the Wi-Fi password in plain text – keep the file safe.',
    'bk.export': 'Download backup', 'bk.exported': 'Backup downloaded',
    'bk.import': 'Restore from file …', 'bk.from': '{dev} · firmware {fw} · {d}', 'bk.parts': 'Restore these parts:',
    'bk.p.system': 'Language, time zone, time server, LED night mode', 'bk.p.network': 'Network (LAN/Wi-Fi)',
    'bk.p.pumps': 'Pumps, schedules and lock times', 'bk.p.engineering': 'Service values and hardware',
    'bk.p.auth': 'Admin password and API keys',
    'bk.w.network': 'The device may get another address and the page loses the connection.',
    'bk.w.auth': 'Everyone is signed out, including you; afterwards the password from the backup applies.',
    'bk.nopw': 'without Wi-Fi password – the stored one stays for the same network',
    'bk.restore': 'Restore', 'bk.cancel': 'Cancel', 'bk.ask': 'Restore the chosen parts now? Current values are overwritten.',
    'bk.done': 'Restored: {list}.', 'bk.relogin': 'Please sign in again.',
    'bk.bad': 'This file is not a backup of this device type.', 'bk.newer': 'This backup is from a newer firmware and cannot be read.',
    'bk.rejected': 'The backup was rejected: {f} ({e})', 'bk.failed': 'Restoring “{p}” failed ({e}). Already restored: {list}',
    'err.nothing_to_restore': 'None of the chosen parts is in the file',
  },
  de: {
    'bk.title': 'Datensicherung', 'bk.hint': 'Alle Einstellungen, Pumpen, Zeitpläne, Sperrzeiten, Servicewerte und Hardware-Parameter als eine Datei. Behälterinhalt, Zähler und Verlauf gehören nicht dazu.',
    'bk.secrets': 'Mit WLAN-Passwort, Admin-Passwort und API-Keys',
    'bk.secrets.hint': 'Admin-Passwort und API-Keys stehen nur als Hash drin, das WLAN-Passwort im Klartext – Datei sicher aufbewahren.',
    'bk.export': 'Sicherung herunterladen', 'bk.exported': 'Sicherung heruntergeladen',
    'bk.import': 'Aus Datei wiederherstellen …', 'bk.from': '{dev} · Firmware {fw} · {d}', 'bk.parts': 'Diese Teile wiederherstellen:',
    'bk.p.system': 'Sprache, Zeitzone, Zeitserver, LED-Nachtmodus', 'bk.p.network': 'Netzwerk (LAN/WLAN)',
    'bk.p.pumps': 'Pumpen, Zeitpläne und Sperrzeiten', 'bk.p.engineering': 'Servicewerte und Hardware',
    'bk.p.auth': 'Admin-Passwort und API-Keys',
    'bk.w.network': 'Das Gerät bekommt eventuell eine andere Adresse und die Seite verliert die Verbindung.',
    'bk.w.auth': 'Alle werden abgemeldet, auch du; danach gilt das Passwort aus der Sicherung.',
    'bk.nopw': 'ohne WLAN-Passwort – für dasselbe Netz bleibt das gespeicherte',
    'bk.restore': 'Wiederherstellen', 'bk.cancel': 'Abbrechen', 'bk.ask': 'Die gewählten Teile jetzt wiederherstellen? Aktuelle Werte werden überschrieben.',
    'bk.done': 'Wiederhergestellt: {list}.', 'bk.relogin': 'Bitte neu anmelden.',
    'bk.bad': 'Diese Datei ist keine Sicherung dieses Gerätetyps.', 'bk.newer': 'Diese Sicherung stammt von einer neueren Firmware und kann nicht gelesen werden.',
    'bk.rejected': 'Die Sicherung wurde abgelehnt: {f} ({e})', 'bk.failed': 'Wiederherstellen von „{p}“ fehlgeschlagen ({e}). Bereits übernommen: {list}',
    'err.nothing_to_restore': 'Keiner der gewählten Teile ist in der Datei',
  },
});

const kFormat = 'lgal-doser-backup';
const kParts = ['system', 'pumps', 'engineering', 'network', 'auth'];
const kRisky = ['network', 'auth'];  // not chosen by default
const partName = p => t('bk.p.' + p);
const listOf = a => (a && a.length ? a.map(partName).join(', ') : '–');

function save(doc) {
  const date = doc.exported_utc ? new Date(doc.exported_utc * 1000).toISOString().slice(0, 10) : 'undated';
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([JSON.stringify(doc, null, 2) + '\n'], { type: 'application/json' }));
  a.download = `${doc.device || 'lgal-doser'}-${date}${doc.auth ? '-secrets' : ''}.json`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

export function BackupSection() {
  const [secrets, setSecrets] = useState(false);
  const [busy, setBusy] = useState(false);
  const [doc, setDoc] = useState(null);
  const [pick, setPick] = useState([]);
  const msg = useMsg();

  const exportNow = () => {
    setBusy(true);
    api('GET', '/api/v1/backup?secrets=' + (secrets ? 1 : 0))
      .then(j => { save(j); msg.ok(t('bk.exported')); })
      .catch(e => msg.err(errText(e)))
      .finally(() => setBusy(false));
  };

  const load = e => {
    const file = e.target.files && e.target.files[0];
    e.target.value = '';
    if (!file) return;
    msg.clear();
    file.text().then(txt => {
      let j;
      try { j = JSON.parse(txt); } catch { j = null; }
      if (!j || j.format !== kFormat) { msg.err(t('bk.bad')); return; }
      if (j.version > 1) { msg.err(t('bk.newer')); return; }
      const present = kParts.filter(p => (p === 'pumps' ? j.pumps || j.locks : j[p]));
      setDoc(j);
      setPick(present.filter(p => !kRisky.includes(p)));
    }).catch(() => msg.err(t('bk.bad')));
  };

  const restore = () => {
    if (!pick.length || !confirm(t('bk.ask'))) return;
    setBusy(true);
    api('POST', '/api/v1/backup?parts=' + pick.join(','), doc)
      .then(r => {
        setDoc(null);
        msg.ok(t('bk.done', { list: listOf(r.applied) }) + (r.signed_out ? ' ' + t('bk.relogin') : ''));
        if (r.signed_out) setTimeout(() => { location.href = '/login'; }, 2500);
        else reloadAll();
      })
      .catch(e => {
        const er = (e.body && e.body.error) || {};
        if (er.part) msg.err(t('bk.failed', { p: partName(er.part), e: errText(e), list: listOf(er.applied) }));
        else if (er.field) msg.err(t('bk.rejected', { f: er.field, e: errText(e) }));
        else msg.err(errText(e));
      })
      .finally(() => setBusy(false));
  };

  const toggle = p => setPick(prev => (prev.includes(p) ? prev.filter(x => x !== p) : [...prev, p]));
  const present = doc ? kParts.filter(p => (p === 'pumps' ? doc.pumps || doc.locks : doc[p])) : [];
  const when = doc && doc.exported_utc ? new Date(doc.exported_utc * 1000).toLocaleString(getLang()) : '–';

  return html`<div class="backup">
    <h3>${t('bk.title')}</h3>
    <p class="small mut">${t('bk.hint')}</p>
    <label class="check"><input type="checkbox" checked=${secrets} onChange=${e => setSecrets(e.target.checked)}/>
      <span>${t('bk.secrets')}</span></label>
    ${secrets && html`<p class="small warn">${t('bk.secrets.hint')}</p>`}
    <div class="row">
      <button type="button" class="sec small" disabled=${busy} onClick=${exportNow}>${t('bk.export')}</button>
      <label class=${'btn sec small' + (busy ? ' disabled' : '')}>${t('bk.import')}
        <input type="file" accept="application/json,.json" hidden onChange=${load}/></label>
    </div>
    ${doc && html`<div class="bk-restore">
      <p class="small">${t('bk.from', { dev: doc.device || '?', fw: doc.firmware || '?', d: when })}</p>
      <p class="small mut">${t('bk.parts')}</p>
      ${present.map(p => html`<label class="check"><input type="checkbox" checked=${pick.includes(p)} onChange=${() => toggle(p)}/>
        <span>${partName(p)}${p === 'network' && doc.network && doc.network.wlan_password === undefined ? html` <span class="small mut">(${t('bk.nopw')})</span>` : ''}</span></label>
        ${pick.includes(p) && kRisky.includes(p) && html`<p class="small warn">${t('bk.w.' + p)}</p>`}`)}
      <div class="row savebar">
        <button type="button" disabled=${busy || !pick.length} onClick=${restore}>${t('bk.restore')}</button>
        <button type="button" class="sec" disabled=${busy} onClick=${() => setDoc(null)}>${t('bk.cancel')}</button>
      </div>
    </div>`}
    ${msg.node}
  </div>`;
}
