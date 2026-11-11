// Maintenance (#/maintenance): firmware and device info, firmware update by
// file upload, restart, factory reset; update from a URL (with its settings), log and
// coredump in the pro view. The emergency page /recovery is deliberately not
// linked (operating manual only).
import { useEffect, useRef, useState } from 'preact/hooks';
import { api, ApiError } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, num, t } from '../lib/i18n.js';
import { usePrefs } from '../lib/prefs.js';
import { reloadInfo, useStore } from '../lib/store.js';
import { ask, useMsg } from '../lib/ui.js';

add({
  en: {
    'mt.title': 'Maintenance', 'mt.device': 'Device', 'mt.fw': 'Firmware', 'mt.slot': 'Slot', 'mt.built': 'Built',
    'mt.host': 'Address', 'mt.mac': 'MAC', 'mt.uptime': 'Running for', 'mt.reset': 'Last restart', 'mt.mem': 'Memory free / lowest',
    'mt.psram': 'PSRAM free', 'mt.rolledback': 'The last update did not start – the device went back to the previous firmware ({p} is marked invalid).',
    'mt.uptime.v': '{d} d {h} h {m} min', 'mt.uptime.h': '{h} h {m} min',
    'mt.update': 'Firmware update', 'mt.update.hint': 'Choose the firmware file (.bin). Pumps are locked while it is written; the device then restarts with the new firmware. If it does not start properly, the device goes back to the current version by itself.',
    'mt.file': 'Firmware file', 'mt.upload': 'Install update', 'mt.busy': 'A pump is running – the update is possible once it has finished.',
    'mt.nofile': 'Please choose a .bin file first.', 'mt.notbin': 'This is not a .bin file.',
    'mt.sending': 'Uploading … {p} %', 'mt.verifying': 'Checking and writing …', 'mt.conn': 'Connection lost during the upload.',
    'mt.done': 'Update installed. The device restarts …', 'mt.wait': 'Waiting for the device … ({s} s)',
    'mt.back': 'The device is back – the page reloads. Please sign in again.', 'mt.gone': 'The device does not answer. Check the LED and reload the page in a moment.',
    'mt.restart': 'Restart', 'mt.restart.hint': 'Schedules, calibration and settings are kept. Running doses are stopped.',
    'mt.restart.confirm': 'Restart the device now?', 'mt.restarting': 'The device restarts …',
    'mt.fac': 'Factory reset', 'mt.fac.h': 'Deletes everything: settings, schedules, calibration, counters, history, passwords and API keys. Afterwards the device starts the first-time setup again (setup Wi-Fi and the password on the label). Tip: export the schedules on the pump pages first.',
    'mt.fac.go': 'Reset to factory settings', 'mt.fac.no': 'Cancel', 'mt.fac.pw': 'Admin password to confirm', 'mt.fac.yes': 'Delete everything now',
    'mt.fac.ask': 'Really delete everything? This cannot be undone.', 'mt.fac.done': 'The device deletes everything and restarts in setup mode. This page will no longer work – continue with the setup Wi-Fi "LG-Aquatic-Life-Setup-…" or, with the LAN cable plugged in, at the device address with the password on the label.',
    'mt.diag': 'Diagnostics', 'mt.core.t': 'Crash report', 'mt.log.t': 'Device log', 'mt.log.h': 'The latest messages of the firmware.', 'mt.log': 'Show log', 'mt.log.hide': 'Hide log', 'mt.log.reload': 'Reload',
    'mt.core': 'A crash report (coredump) is stored.', 'mt.core.dl': 'Download', 'mt.core.del': 'Delete', 'mt.core.deleted': 'Crash report deleted.',
    'mt.core.none': 'No crash report stored.',
    'err.ota_in_progress': 'An update is already in progress', 'err.pump_running': 'A pump is running – try again when it has finished',
    'err.no_ota_partition': 'No update partition available', 'err.invalid_size': 'Invalid file size', 'err.transfer_aborted': 'Transfer aborted',
    'err.not_esp_image': 'This is not a firmware file', 'err.wrong_chip': 'Firmware is for a different chip',
    'err.no_app_desc': 'Firmware file is incomplete', 'err.wrong_project': 'Firmware is for a different device',
    'err.flash_error': 'Flash write error', 'err.file_too_short': 'File too short', 'err.image_invalid': 'Firmware verification failed',
    'err.upload_failed': 'Update failed', 'err.no_memory': 'Device is out of memory', 'err.no_coredump': 'No crash report stored',
    'mt.url': 'Update from a URL', 'mt.url.hint': 'The device downloads the firmware file (.bin) itself, e.g. from a release page. HTTPS with a checked server certificate.',
    'mt.url.field': 'Address of the firmware file', 'mt.url.start': 'Download and install',
    'mt.url.run': 'Downloading … {r}', 'mt.url.run.pct': 'Downloading … {p} % ({r})',
    'mt.url.http': 'Unencrypted (http): the file can be altered on the way and nobody would notice. Only use it in your own network.',
    'mt.url.http.ask': 'Download the firmware over unencrypted http? It can be altered on the way.',
    'mt.url.http.off': 'http:// addresses are blocked – only https. You can allow http below (not recommended).',
    'mt.url.failed': 'Update failed: {e}',
    'mt.opt': 'Update options', 'mt.opt.http': 'Allow unencrypted http',
    'mt.opt.http.h': 'Off: only https with a checked certificate (recommended).',
    'mt.opt.http.on': 'http is allowed: firmware downloads over http are not protected against tampering. Switch it off again when you do not need it.',
    'mt.opt.http.ask': 'Allow firmware downloads over unencrypted http? Anyone in the network path could swap the firmware. Only do this if you know why.',
    'mt.opt.mqtt': 'Allow firmware updates over MQTT', 'mt.opt.mqtt.h': 'Anyone who may publish to {t} on the broker can then start an update with a URL.',
    'mt.opt.saved': 'Saved.',
    'err.invalid_url': 'Invalid address (http:// or https://)', 'err.http_not_allowed': 'Unencrypted http is not allowed',
    'err.download_failed': 'Download failed', 'err.http_status': 'The server refused the download',
    'err.too_many_redirects': 'Too many redirects', 'err.mqtt_update_disabled': 'Updates over MQTT are switched off',
  },
  de: {
    'mt.title': 'Wartung', 'mt.device': 'Gerät', 'mt.fw': 'Firmware', 'mt.slot': 'Speicherplatz', 'mt.built': 'Erstellt',
    'mt.host': 'Adresse', 'mt.mac': 'MAC', 'mt.uptime': 'Läuft seit', 'mt.reset': 'Letzter Neustart', 'mt.mem': 'Speicher frei / tiefster Wert',
    'mt.psram': 'PSRAM frei', 'mt.rolledback': 'Das letzte Update ist nicht gestartet – das Gerät ist zur vorherigen Firmware zurückgekehrt ({p} ist als ungültig markiert).',
    'mt.uptime.v': '{d} d {h} h {m} min', 'mt.uptime.h': '{h} h {m} min',
    'mt.update': 'Firmware-Update', 'mt.update.hint': 'Firmware-Datei (.bin) wählen. Während sie geschrieben wird, sind die Pumpen gesperrt; danach startet das Gerät mit der neuen Firmware. Startet sie nicht richtig, kehrt das Gerät von selbst zur jetzigen Version zurück.',
    'mt.file': 'Firmware-Datei', 'mt.upload': 'Update installieren', 'mt.busy': 'Eine Pumpe läuft – das Update ist möglich, sobald sie fertig ist.',
    'mt.nofile': 'Bitte zuerst eine .bin-Datei wählen.', 'mt.notbin': 'Das ist keine .bin-Datei.',
    'mt.sending': 'Übertrage … {p} %', 'mt.verifying': 'Prüfe und schreibe …', 'mt.conn': 'Verbindung während der Übertragung abgebrochen.',
    'mt.done': 'Update installiert. Das Gerät startet neu …', 'mt.wait': 'Warte auf das Gerät … ({s} s)',
    'mt.back': 'Das Gerät ist wieder da – die Seite lädt neu. Bitte erneut anmelden.', 'mt.gone': 'Das Gerät antwortet nicht. LED prüfen und die Seite gleich neu laden.',
    'mt.restart': 'Neu starten', 'mt.restart.hint': 'Zeitpläne, Kalibrierung und Einstellungen bleiben erhalten. Laufende Dosierungen werden beendet.',
    'mt.restart.confirm': 'Gerät jetzt neu starten?', 'mt.restarting': 'Das Gerät startet neu …',
    'mt.fac': 'Werkseinstellungen', 'mt.fac.h': 'Löscht alles: Einstellungen, Zeitpläne, Kalibrierung, Zähler, Verlauf, Passwörter und API-Schlüssel. Danach beginnt die Ersteinrichtung von vorn (Setup-WLAN und Passwort vom Etikett). Tipp: vorher die Zeitpläne auf den Pumpenseiten exportieren.',
    'mt.fac.go': 'Auf Werkseinstellungen zurücksetzen', 'mt.fac.no': 'Abbrechen', 'mt.fac.pw': 'Admin-Passwort zur Bestätigung', 'mt.fac.yes': 'Jetzt alles löschen',
    'mt.fac.ask': 'Wirklich alles löschen? Das lässt sich nicht rückgängig machen.', 'mt.fac.done': 'Das Gerät löscht alles und startet im Einrichtungsmodus. Diese Seite funktioniert danach nicht mehr – weiter über das Setup-WLAN „LG-Aquatic-Life-Setup-…“ oder, mit LAN-Kabel, unter der Geräteadresse mit dem Passwort vom Etikett.',
    'mt.diag': 'Diagnose', 'mt.core.t': 'Absturzbericht', 'mt.log.t': 'Geräte-Log', 'mt.log.h': 'Die letzten Meldungen der Firmware.', 'mt.log': 'Log anzeigen', 'mt.log.hide': 'Log ausblenden', 'mt.log.reload': 'Neu laden',
    'mt.core': 'Ein Absturzbericht (Coredump) ist gespeichert.', 'mt.core.dl': 'Herunterladen', 'mt.core.del': 'Löschen', 'mt.core.deleted': 'Absturzbericht gelöscht.',
    'mt.core.none': 'Kein Absturzbericht gespeichert.',
    'err.ota_in_progress': 'Es läuft bereits ein Update', 'err.pump_running': 'Eine Pumpe läuft – nach dem Ende erneut versuchen',
    'err.no_ota_partition': 'Keine Update-Partition verfügbar', 'err.invalid_size': 'Ungültige Dateigröße', 'err.transfer_aborted': 'Übertragung abgebrochen',
    'err.not_esp_image': 'Das ist keine Firmware-Datei', 'err.wrong_chip': 'Firmware ist für einen anderen Chip',
    'err.no_app_desc': 'Firmware-Datei ist unvollständig', 'err.wrong_project': 'Firmware ist für ein anderes Gerät',
    'err.flash_error': 'Fehler beim Schreiben in den Flash', 'err.file_too_short': 'Datei zu kurz', 'err.image_invalid': 'Prüfung der Firmware fehlgeschlagen',
    'err.upload_failed': 'Update fehlgeschlagen', 'err.no_memory': 'Kein freier Speicher im Gerät', 'err.no_coredump': 'Kein Absturzbericht gespeichert',
    'mt.url': 'Update von einer Adresse', 'mt.url.hint': 'Das Gerät lädt die Firmware-Datei (.bin) selbst herunter, z. B. von einer Release-Seite. HTTPS mit geprüftem Server-Zertifikat.',
    'mt.url.field': 'Adresse der Firmware-Datei', 'mt.url.start': 'Herunterladen und installieren',
    'mt.url.run': 'Lade herunter … {r}', 'mt.url.run.pct': 'Lade herunter … {p} % ({r})',
    'mt.url.http': 'Unverschlüsselt (http): Die Datei kann unterwegs verändert werden, ohne dass es jemand merkt. Nur im eigenen Netz verwenden.',
    'mt.url.http.ask': 'Firmware über unverschlüsseltes http laden? Sie kann unterwegs verändert werden.',
    'mt.url.http.off': 'http://-Adressen sind gesperrt – nur https. Unten lässt sich http erlauben (nicht empfohlen).',
    'mt.url.failed': 'Update fehlgeschlagen: {e}',
    'mt.opt': 'Update-Optionen', 'mt.opt.http': 'Unverschlüsseltes http erlauben',
    'mt.opt.http.h': 'Aus: nur https mit geprüftem Zertifikat (empfohlen).',
    'mt.opt.http.on': 'http ist erlaubt: Firmware-Downloads über http sind nicht gegen Manipulation geschützt. Wieder ausschalten, wenn es nicht mehr gebraucht wird.',
    'mt.opt.http.ask': 'Firmware-Downloads über unverschlüsseltes http erlauben? Jeder auf dem Weg durchs Netz könnte die Firmware austauschen. Nur einschalten, wenn klar ist, warum.',
    'mt.opt.mqtt': 'Firmware-Update über MQTT erlauben', 'mt.opt.mqtt.h': 'Wer auf dem Broker nach {t} schreiben darf, kann dann ein Update mit einer Adresse starten.',
    'mt.opt.saved': 'Gespeichert.',
    'err.invalid_url': 'Ungültige Adresse (http:// oder https://)', 'err.http_not_allowed': 'Unverschlüsseltes http ist nicht erlaubt',
    'err.download_failed': 'Download fehlgeschlagen', 'err.http_status': 'Der Server hat den Download abgelehnt',
    'err.too_many_redirects': 'Zu viele Weiterleitungen', 'err.mqtt_update_disabled': 'Updates über MQTT sind ausgeschaltet',
  },
});

const sleep = ms => new Promise(r => setTimeout(r, ms));

// After an update or restart: wait until the device answers again, then reload
// (sessions live in RAM, so the reload usually ends on the login page)
async function waitForDevice(msg) {
  const t0 = Date.now();
  await sleep(4000);
  while (Date.now() - t0 < 120000) {
    msg.ok(t('mt.wait', { s: Math.round((Date.now() - t0) / 1000) }));
    try {
      const r = await fetch('/api/v1/system/language', { cache: 'no-store' });
      if (r.ok) {
        msg.ok(t('mt.back'));
        await sleep(1500);
        location.reload();
        return;
      }
    } catch (e) { /* still restarting */ }
    await sleep(2000);
  }
  msg.err(t('mt.gone'));
}

// Upload with progress (fetch has no upload progress) -> resolves with the JSON reply
function upload(file, onProgress) {
  return new Promise((ok, fail) => {
    const x = new XMLHttpRequest();
    x.open('POST', '/api/v1/ota/firmware');
    x.setRequestHeader('Content-Type', 'application/octet-stream');
    x.upload.onprogress = e => { if (e.lengthComputable) onProgress(e.loaded / e.total); };
    x.onload = () => {
      let j = null;
      try { j = JSON.parse(x.responseText); } catch (e) { /* no JSON */ }
      if (x.status === 401) { location.href = '/login?next=' + encodeURIComponent('/' + location.hash); return; }
      if (x.status >= 200 && x.status < 300) { ok(j); return; }
      const e = (j && j.error) || {};
      fail(new ApiError(e.code || 'http_' + x.status, e.message, x.status, j));
    };
    x.onerror = () => fail(new ApiError('network', t('mt.conn'), 0));
    x.send(file);
  });
}

function uptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return d ? t('mt.uptime.v', { d, h, m }) : t('mt.uptime.h', { h, m });
}

function InfoCard({ info, pro }) {
  const f = info.firmware, s = info.system, d = info.device;
  return html`<section class="card"><h2>${t('mt.device')}</h2>
    ${f.last_invalid_partition && html`<p class="msg err">${t('mt.rolledback', { p: f.last_invalid_partition })}</p>`}
    <dl class="stats">
      <dt>${t('mt.fw')}</dt><dd>${f.version}</dd>
      ${pro && html`<dt>${t('mt.slot')}</dt><dd>${f.partition} · ${f.ota_state}</dd>`}
      <dt>${t('mt.built')}</dt><dd>${f.built}${pro && html`<br/><span class="mut small">IDF ${f.idf}</span>`}</dd>
      <dt>${t('mt.host')}</dt><dd>${d.hostname}.local</dd>
      ${pro && html`<dt>${t('mt.mac')}</dt><dd>${d.mac}</dd>`}
      <dt>${t('mt.uptime')}</dt><dd>${uptime(s.uptime_s)}</dd>
      ${pro && html`<dt>${t('mt.reset')}</dt><dd>${s.reset_reason}</dd>
        <dt>${t('mt.mem')}</dt><dd>${num(s.heap_free / 1024)} / ${num(s.heap_min_free / 1024)} KiB</dd>
        <dt>${t('mt.psram')}</dt><dd>${num(s.psram_free / 1048576, 1)} MiB</dd>`}
    </dl>
  </section>`;
}

function UpdateCard({ busyPumps }) {
  const [file, setFile] = useState(null);
  const [prog, setProg] = useState(null);   // 0..1 while uploading
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const input = useRef(null);
  const start = () => {
    if (!file) { msg.err(t('mt.nofile')); return; }
    if (!/\.bin$/i.test(file.name)) { msg.err(t('mt.notbin')); return; }
    setBusy(true);
    setProg(0);
    msg.ok(t('mt.sending', { p: 0 }));
    upload(file, p => {
      setProg(p);
      msg.ok(p < 1 ? t('mt.sending', { p: Math.floor(p * 100) }) : t('mt.verifying'));
    })
      .then(() => { setProg(1); msg.ok(t('mt.done')); return waitForDevice(msg); })
      .catch(e => { setProg(null); setBusy(false); msg.err(e); });
  };
  return html`<section class="card"><h2>${t('mt.update')}</h2>
    <p class="small mut">${t('mt.update.hint')}</p>
    <label class="field">${t('mt.file')}
      <input ref=${input} type="file" accept=".bin,application/octet-stream" disabled=${busy}
        onChange=${e => { setFile(e.target.files[0] || null); msg.clear(); }}/></label>
    ${busyPumps && !busy && html`<p class="small warn">${t('mt.busy')}</p>`}
    ${prog !== null && html`<div class=${'bar' + (prog >= 1 && busy ? ' indet' : '')}><i style=${{ width: Math.round(prog * 100) + '%' }}></i></div>`}
    <div class="row savebar"><button type="button" disabled=${busy || !file || busyPumps} onClick=${start}>${t('mt.upload')}</button></div>
    ${msg.node}
  </section>`;
}

const isHttp = u => /^http:\/\//i.test(u.trim());
const fmtSize = b => (b >= 1048576 ? num(b / 1048576, 1) + ' MB' : num(b / 1024, 0) + ' kB');

// Pro view: the device downloads the image (POST /api/v1/ota/url) and
// reports on GET /api/v1/ota/status until it restarts.
function UrlUpdateCard({ busyPumps, opts }) {
  const [url, setUrl] = useState('');
  const [st, setSt] = useState(null);  // GET /api/v1/ota/status while running
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const poll = () => api('GET', '/api/v1/ota/status').then(j => {
    setSt(j);
    if (j.state === 'running') { setTimeout(poll, 1000); return; }
    if (j.state === 'rebooting') { msg.ok(t('mt.done')); waitForDevice(msg); return; }
    setBusy(false);
    if (j.state === 'failed') msg.err(t('mt.url.failed', { e: errText({ code: j.error }) }) + (j.detail ? ' (' + j.detail + ')' : ''));
  }).catch(() => setTimeout(poll, 2000));  // the device may be restarting
  const http = isHttp(url);
  const blocked = http && !opts.allow_http;
  const start = async e => {
    e.preventDefault();
    msg.clear();
    if (blocked) { msg.err(t('mt.url.http.off')); return; }
    if (http && !(await ask(t('mt.url.http.ask'), { ok: t('mt.url.start'), danger: true }))) return;
    setBusy(true);
    api('POST', '/api/v1/ota/url', { url: url.trim() })
      .then(() => { setSt({ state: 'running', received: 0, total: null }); setTimeout(poll, 500); })
      .catch(e => { setBusy(false); msg.err(e); });
  };
  const running = st && st.state === 'running';
  const pct = running && st.total ? Math.min(100, Math.floor(st.received / st.total * 100)) : null;
  return html`<form class="card" novalidate onSubmit=${start}><h2>${t('mt.url')}</h2>
    <p class="small mut">${t('mt.url.hint')}</p>
    <label class="field">${t('mt.url.field')}
      <input type="url" maxlength="512" autocomplete="off" spellcheck="false" placeholder="https://…/firmware.bin"
        disabled=${busy} value=${url} onInput=${e => setUrl(e.target.value)}/></label>
    ${http && html`<p class=${'small ' + (blocked ? 'err' : 'warn')}>${t(blocked ? 'mt.url.http.off' : 'mt.url.http')}</p>`}
    ${busyPumps && !busy && html`<p class="small warn">${t('mt.busy')}</p>`}
    ${running && html`<div class=${'bar' + (pct === null ? ' indet' : '')}><i style=${{ width: (pct ?? 100) + '%' }}></i></div>
      <p class="small">${pct === null ? t('mt.url.run', { r: fmtSize(st.received) }) : t('mt.url.run.pct', { p: pct, r: fmtSize(st.received) })}</p>`}
    <div class="row savebar"><button type="submit" disabled=${busy || !url.trim() || busyPumps || blocked}>${t('mt.url.start')}</button></div>
    ${msg.node}
  </form>`;
}

// Pro view: http for URL updates (warned) and updates over MQTT, both off from the factory
function UpdateOptsCard({ opts, setOpts, base }) {
  const msg = useMsg();
  const save = async (patch, box) => {
    if (patch.allow_http && !(await ask(t('mt.opt.http.ask'), { danger: true }))) { box.checked = false; return; }
    api('PUT', '/api/v1/ota/settings', patch).then(j => { setOpts(j); msg.ok(t('mt.opt.saved')); }).catch(e => msg.err(e));
  };
  return html`<section class="card"><h2>${t('mt.opt')}</h2>
    <label class="check"><input type="checkbox" checked=${opts.allow_http} onChange=${e => save({ allow_http: e.target.checked }, e.target)}/>
      <span>${t('mt.opt.http')}<br/><span class=${'small ' + (opts.allow_http ? 'warn' : 'mut')}>${t(opts.allow_http ? 'mt.opt.http.on' : 'mt.opt.http.h')}</span></span></label>
    <label class="check"><input type="checkbox" checked=${opts.mqtt_update} onChange=${e => save({ mqtt_update: e.target.checked })}/>
      <span>${t('mt.opt.mqtt')}<br/><span class=${'small ' + (opts.mqtt_update ? 'warn' : 'mut')}>${t('mt.opt.mqtt.h', { t: base + '/ota/set' })}</span></span></label>
    ${msg.node}
  </section>`;
}

function RestartCard() {
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const restart = async () => {
    if (!(await ask(t('mt.restart.confirm'), { ok: t('mt.restart') }))) return;
    setBusy(true);
    api('POST', '/api/v1/system/reboot')
      .then(() => { msg.ok(t('mt.restarting')); return waitForDevice(msg); })
      .catch(e => { setBusy(false); msg.err(e); });
  };
  return html`<section class="card"><h2>${t('mt.restart')}</h2>
    <p class="small mut">${t('mt.restart.hint')}</p>
    <div class="row savebar"><button type="button" class="sec" disabled=${busy} onClick=${restart}>${t('mt.restart')}</button></div>
    ${msg.node}
  </section>`;
}

// Factory reset for the admin: password again, then everything is erased
function FactoryCard() {
  const [open, setOpen] = useState(false);
  const [pw, setPw] = useState('');
  const [busy, setBusy] = useState(false);
  const [done, setDone] = useState(false);
  const msg = useMsg();
  const close = () => { setOpen(false); setPw(''); msg.clear(); };
  const go = async e => {
    e.preventDefault();
    if (!pw || !(await ask(t('mt.fac.ask'), { ok: t('mt.fac.go'), danger: true }))) return;
    setBusy(true);
    api('POST', '/api/v1/system/factory_reset', { password: pw })
      .then(() => { setPw(''); setDone(true); msg.ok(t('mt.fac.done')); })
      .catch(err => { setBusy(false); msg.err(err); });
  };
  return html`<section class="card danger-zone"><h2>${t('mt.fac')}</h2>
    <p class="small mut">${t('mt.fac.h')}</p>
    ${!open ? html`<div class="row savebar"><button type="button" class="sec" onClick=${() => setOpen(true)}>${t('mt.fac.go')}</button></div>`
      : !done && html`<form onSubmit=${go}>
        <label class="field">${t('mt.fac.pw')}
          <input type="password" autocomplete="current-password" maxlength="64" value=${pw} onInput=${e => setPw(e.target.value)}/></label>
        <div class="row savebar">
          <button type="submit" class="danger" disabled=${busy || !pw}>${t('mt.fac.yes')}</button>
          <button type="button" class="sec" disabled=${busy} onClick=${close}>${t('mt.fac.no')}</button>
        </div>
      </form>`}
    ${msg.node}
  </section>`;
}

function DiagCard({ info }) {
  const [log, setLog] = useState(null);
  const msg = useMsg();
  const pre = useRef(null);
  const loadLog = () => fetch('/api/v1/system/log', { cache: 'no-store' })
    .then(r => (r.ok ? r.text() : Promise.reject(new ApiError('http_' + r.status, 'HTTP ' + r.status, r.status))))
    .then(txt => { setLog(txt); setTimeout(() => { if (pre.current) pre.current.scrollTop = pre.current.scrollHeight; }, 0); })
    .catch(e => msg.err(e));
  const delCore = () => api('DELETE', '/api/v1/system/coredump').then(() => { msg.ok(t('mt.core.deleted')); reloadInfo(); }).catch(e => msg.err(e));
  const core = info.system.coredump;
  // two equal blocks: title and buttons in one row, the state below
  return html`<section class="card"><h2>${t('mt.diag')}</h2>
    <div class="diag-item">
      <div class="row between"><b>${t('mt.core.t')}</b>
        ${core && html`<div class="row"><a class="btn sec" href="/api/v1/system/coredump" download="coredump.elf">${t('mt.core.dl')}</a>
          <button type="button" class="sec" onClick=${delCore}>${t('mt.core.del')}</button></div>`}</div>
      <p class=${'small ' + (core ? 'warn' : 'mut')}>${t(core ? 'mt.core' : 'mt.core.none')}</p>
    </div>
    <div class="diag-item sub">
      <div class="row between"><b>${t('mt.log.t')}</b>
        <div class="row">${log === null
          ? html`<button type="button" class="sec" onClick=${loadLog}>${t('mt.log')}</button>`
          : html`<button type="button" class="sec" onClick=${loadLog}>${t('mt.log.reload')}</button>
            <button type="button" class="sec" onClick=${() => setLog(null)}>${t('mt.log.hide')}</button>`}</div></div>
      <p class="small mut">${t('mt.log.h')}</p>
      ${log !== null && html`<pre class="log" ref=${pre}>${log}</pre>`}
    </div>
    ${msg.node}
  </section>`;
}

export function MaintenanceView() {
  const st = useStore();
  const pro = usePrefs().mode === 'pro';
  useEffect(() => {
    reloadInfo();
    const id = setInterval(reloadInfo, 10000);
    return () => clearInterval(id);
  }, []);
  const info = st.info;
  const busyPumps = !!(st.pumps && st.pumps.some(p => p.activity !== 'idle'));
  const [opts, setOpts] = useState(null);  // GET /api/v1/ota/settings
  const [base, setBase] = useState('<base>');
  useEffect(() => {
    if (!pro) return;
    api('GET', '/api/v1/ota/settings').then(setOpts).catch(() => {});
    api('GET', '/api/v1/mqtt').then(m => { if (m.base_topic) setBase(m.base_topic); }).catch(() => {});
  }, [pro]);
  return html`<div class="page-head"><h1>${t('mt.title')}</h1></div>
    ${pro && opts && opts.allow_http && html`<p class="banner warn">${t('mt.opt.http.on')}</p>`}
    ${!info ? html`<p class="mut">…</p>` : html`<div class="grid settings">
      <${InfoCard} info=${info} pro=${pro}/>
      <${UpdateCard} busyPumps=${busyPumps}/>
      ${pro && opts && html`<${UrlUpdateCard} busyPumps=${busyPumps} opts=${opts}/>`}
      <${RestartCard}/>
      <${FactoryCard}/>
      ${pro && opts && html`<${UpdateOptsCard} opts=${opts} setOpts=${setOpts} base=${base}/>`}
      ${pro && html`<${DiagCard} info=${info}/>`}
    </div>`}`;
}
