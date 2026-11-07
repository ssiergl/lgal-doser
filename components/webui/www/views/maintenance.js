// Maintenance (#/maintenance): firmware and device info, firmware update by
// file upload, restart; log and coredump in the pro view. /recovery stays
// as the emergency page without the web UI.
import { useEffect, useRef, useState } from 'preact/hooks';
import { api, ApiError } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, num, t } from '../lib/i18n.js';
import { usePrefs } from '../lib/prefs.js';
import { reloadInfo, useStore } from '../lib/store.js';
import { useMsg } from '../lib/ui.js';

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
    'mt.diag': 'Diagnostics', 'mt.log': 'Show log', 'mt.log.hide': 'Hide log', 'mt.log.reload': 'Reload',
    'mt.core': 'A crash report (coredump) is stored.', 'mt.core.dl': 'Download', 'mt.core.del': 'Delete', 'mt.core.deleted': 'Crash report deleted.',
    'mt.core.none': 'No crash report stored.', 'mt.emergency': 'Emergency page without the web interface: {u}',
    'err.ota_in_progress': 'An update is already in progress', 'err.pump_running': 'A pump is running – try again when it has finished',
    'err.no_ota_partition': 'No update partition available', 'err.invalid_size': 'Invalid file size', 'err.transfer_aborted': 'Transfer aborted',
    'err.not_esp_image': 'This is not a firmware file', 'err.wrong_chip': 'Firmware is for a different chip',
    'err.no_app_desc': 'Firmware file is incomplete', 'err.wrong_project': 'Firmware is for a different device',
    'err.flash_error': 'Flash write error', 'err.file_too_short': 'File too short', 'err.image_invalid': 'Firmware verification failed',
    'err.upload_failed': 'Update failed', 'err.no_memory': 'Device is out of memory', 'err.no_coredump': 'No crash report stored',
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
    'mt.diag': 'Diagnose', 'mt.log': 'Log anzeigen', 'mt.log.hide': 'Log ausblenden', 'mt.log.reload': 'Neu laden',
    'mt.core': 'Ein Absturzbericht (Coredump) ist gespeichert.', 'mt.core.dl': 'Herunterladen', 'mt.core.del': 'Löschen', 'mt.core.deleted': 'Absturzbericht gelöscht.',
    'mt.core.none': 'Kein Absturzbericht gespeichert.', 'mt.emergency': 'Notfallseite ohne Weboberfläche: {u}',
    'err.ota_in_progress': 'Es läuft bereits ein Update', 'err.pump_running': 'Eine Pumpe läuft – nach dem Ende erneut versuchen',
    'err.no_ota_partition': 'Keine Update-Partition verfügbar', 'err.invalid_size': 'Ungültige Dateigröße', 'err.transfer_aborted': 'Übertragung abgebrochen',
    'err.not_esp_image': 'Das ist keine Firmware-Datei', 'err.wrong_chip': 'Firmware ist für einen anderen Chip',
    'err.no_app_desc': 'Firmware-Datei ist unvollständig', 'err.wrong_project': 'Firmware ist für ein anderes Gerät',
    'err.flash_error': 'Fehler beim Schreiben in den Flash', 'err.file_too_short': 'Datei zu kurz', 'err.image_invalid': 'Prüfung der Firmware fehlgeschlagen',
    'err.upload_failed': 'Update fehlgeschlagen', 'err.no_memory': 'Kein freier Speicher im Gerät', 'err.no_coredump': 'Kein Absturzbericht gespeichert',
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

function RestartCard() {
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const restart = () => {
    if (!confirm(t('mt.restart.confirm'))) return;
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

function DiagCard({ info }) {
  const [log, setLog] = useState(null);
  const msg = useMsg();
  const pre = useRef(null);
  const loadLog = () => fetch('/api/v1/system/log', { cache: 'no-store' })
    .then(r => (r.ok ? r.text() : Promise.reject(new ApiError('http_' + r.status, 'HTTP ' + r.status, r.status))))
    .then(txt => { setLog(txt); setTimeout(() => { if (pre.current) pre.current.scrollTop = pre.current.scrollHeight; }, 0); })
    .catch(e => msg.err(e));
  const delCore = () => api('DELETE', '/api/v1/system/coredump').then(() => { msg.ok(t('mt.core.deleted')); reloadInfo(); }).catch(e => msg.err(e));
  return html`<section class="card"><h2>${t('mt.diag')}</h2>
    ${info.system.coredump
      ? html`<p class="small warn">${t('mt.core')}</p>
        <div class="row"><a class="btn sec small" href="/api/v1/system/coredump" download="coredump.elf">${t('mt.core.dl')}</a>
          <button type="button" class="sec small" onClick=${delCore}>${t('mt.core.del')}</button></div>`
      : html`<p class="small mut">${t('mt.core.none')}</p>`}
    <div class="row savebar">
      ${log === null
        ? html`<button type="button" class="sec" onClick=${loadLog}>${t('mt.log')}</button>`
        : html`<button type="button" class="sec" onClick=${loadLog}>${t('mt.log.reload')}</button>
          <button type="button" class="sec" onClick=${() => setLog(null)}>${t('mt.log.hide')}</button>`}
    </div>
    ${log !== null && html`<pre class="log" ref=${pre}>${log}</pre>`}
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
  return html`<div class="page-head"><h1>${t('mt.title')}</h1></div>
    ${!info ? html`<p class="mut">…</p>` : html`<div class="grid settings">
      <${InfoCard} info=${info} pro=${pro}/>
      <${UpdateCard} busyPumps=${busyPumps}/>
      <${RestartCard}/>
      ${pro && html`<${DiagCard} info=${info}/>`}
    </div>`}
    <p class="small mut emergency">${t('mt.emergency', { u: '' })}<a href="/recovery">/recovery</a></p>`;
}
