// Maintenance (#/maintenance): firmware and device info, firmware update by
// file upload, updates from the update source (only when one is set), restart,
// factory reset; log and coredump in the pro view. The update source, the
// update from a URL and the http option are exported for the engineering area.
// The emergency page /recovery is deliberately not linked (operating manual only).
import { useEffect, useRef, useState } from 'preact/hooks';
import { api, ApiError } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, getLang, num, t } from '../lib/i18n.js';
import { hourCycle, usePrefs } from '../lib/prefs.js';
import { reloadInfo, useStore } from '../lib/store.js';
import { ask, Seg, Toggle, useMsg } from '../lib/ui.js';

add({
  en: {
    'mt.title': 'Maintenance', 'mt.device': 'Device', 'mt.license': 'License', 'mt.license.v': 'GPLv3 or later', 'mt.third': 'third-party software', 'mt.fw': 'Firmware', 'mt.model': 'Model', 'mt.slot': 'Slot', 'mt.built': 'Built',
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
    'err.not_esp_image': 'This is not a firmware file', 'err.wrong_chip': 'Firmware is for a different chip', 'err.wrong_variant': 'Firmware is for a module with a different flash size (variant N8R8/N16R8/N32R8)',
    'err.no_app_desc': 'Firmware file is incomplete', 'err.wrong_project': 'Firmware is for a different device',
    'err.wrong_board': 'Firmware is for a different circuit board (model)',
    'err.flash_error': 'Flash write error', 'err.file_too_short': 'File too short', 'err.image_invalid': 'Firmware verification failed',
    'err.upload_failed': 'Update failed', 'err.no_memory': 'Device is out of memory', 'err.no_coredump': 'No crash report stored',
    'mt.url': 'Update from a URL', 'mt.url.hint': 'The device downloads the firmware file (.bin) itself, e.g. from a release page. HTTPS with a checked server certificate.',
    'mt.url.field': 'Address of the firmware file', 'mt.url.start': 'Download and install',
    'mt.url.run': 'Downloading … {r}', 'mt.url.run.pct': 'Downloading … {p} % ({r})',
    'mt.url.http': 'Unencrypted (http): the file can be altered on the way and nobody would notice. Only use it in your own network.',
    'mt.url.http.ask': 'Download the firmware over unencrypted http? It can be altered on the way.',
    'mt.url.http.off': 'http:// addresses are blocked – only https. You can allow http below (not recommended).',
    'mt.url.failed': 'Update failed: {e}',
    'mt.rel': 'Updates', 'mt.rel.cur': 'Installed', 'mt.rel.new': 'Available', 'mt.rel.at': 'Checked',
    'mt.rel.none': 'not checked yet', 'mt.rel.ok': 'Up to date', 'mt.rel.avail': 'New: {v}', 'mt.rel.checking': 'Checking …',
    'mt.rel.check': 'Check for updates', 'mt.rel.install': 'Install {v}', 'mt.rel.notes': 'What is new',
    'mt.rel.ask': 'Install firmware {v}? Pumps are locked while it is written; the device then restarts.',
    'mt.rel.failed': 'Check failed: {e}', 'mt.rel.auto': 'Check daily', 'mt.rel.auto.h': 'Only shows a notice – installing always needs you.',
    'mt.rel.hint': 'Official releases. The device checks the file against the release (SHA-256) before it switches to it.',
    'mt.src': 'Update source', 'mt.src.field': 'Address of the release list (manifest.json)',
    'mt.src.h': 'Maintenance offers "Check for updates" while it is set; empty = no update source. Module variant: {v}.',
    'mt.src.save': 'Save', 'mt.src.saved': 'Saved – checking now.', 'mt.src.cleared': 'Update source removed.',
    'mt.opt.http': 'Allow unencrypted http',
    'mt.opt.http.h': 'Off: only https with a checked certificate (recommended).',
    'mt.opt.http.on': 'http is allowed: firmware downloads over http are not protected against tampering. Switch it off again when you do not need it.',
    'mt.opt.http.ask': 'Allow firmware downloads over unencrypted http? Anyone in the network path could swap the firmware. Only do this if you know why.',
    'mt.opt.saved': 'Saved.',
    'mt.ch': 'Update channel', 'mt.ch.stable': 'Stable', 'mt.ch.beta': 'Beta',
    'mt.ch.h': 'Stable: releases only. Beta: release candidates as well – for testers only.',
    'mt.ch.on': 'Beta channel: release candidates may contain bugs that lead to wrong doses. Test the water more often after an update.',
    'mt.ch.ask.t': 'Switch to the beta channel?',
    'mt.ch.ask': 'Release candidates are not fully tested yet and may contain bugs that lead to wrong doses.',
    'mt.ch.ask.1': 'Only for testers who know what they are letting themselves in for.',
    'mt.ch.ask.2': 'Nothing is ever installed by itself – every update is started by hand.',
    'mt.ch.ask.3': 'Test the water more often after an update.',
    'mt.ch.ask.4': 'Back to stable does not install an older version: the device keeps the release candidate until a newer release is out.',
    'mt.ch.ok': 'Switch to beta',
    'mt.ch.pre': 'A release candidate is installed. It stays until a newer release is out; to go back at once, install the release file under "Firmware update".',
    'err.no_channel': 'This update source has no channel to choose',
    'err.no_source': 'No update source set', 'err.no_update': 'No newer version available',
    'err.sha256_mismatch': 'The downloaded file does not match the release (SHA-256) – discarded',
    'err.manifest_invalid': 'The update source sent an invalid release list', 'err.invalid_version': 'The release list has an invalid version',
    'err.invalid_sha256': 'The release list has an invalid checksum', 'err.no_build': 'The release has no firmware for this module variant',
    'err.unknown_variant': 'Unknown module variant (flash size)',
    'err.invalid_url': 'Invalid address (http:// or https://)', 'err.http_not_allowed': 'Unencrypted http is not allowed',
    'err.download_failed': 'Download failed', 'err.http_status': 'The server refused the download',
    'err.too_many_redirects': 'Too many redirects',
  },
  de: {
    'mt.title': 'Wartung', 'mt.device': 'Gerät', 'mt.license': 'Lizenz', 'mt.license.v': 'GPLv3 oder neuer', 'mt.third': 'Fremdsoftware', 'mt.fw': 'Firmware', 'mt.model': 'Modell', 'mt.slot': 'Speicherplatz', 'mt.built': 'Erstellt',
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
    'err.not_esp_image': 'Das ist keine Firmware-Datei', 'err.wrong_chip': 'Firmware ist für einen anderen Chip', 'err.wrong_variant': 'Firmware ist für ein Modul mit anderer Flash-Größe (Variante N8R8/N16R8/N32R8)',
    'err.no_app_desc': 'Firmware-Datei ist unvollständig', 'err.wrong_project': 'Firmware ist für ein anderes Gerät',
    'err.wrong_board': 'Firmware ist für eine andere Platine (anderes Modell)',
    'err.flash_error': 'Fehler beim Schreiben in den Flash', 'err.file_too_short': 'Datei zu kurz', 'err.image_invalid': 'Prüfung der Firmware fehlgeschlagen',
    'err.upload_failed': 'Update fehlgeschlagen', 'err.no_memory': 'Kein freier Speicher im Gerät', 'err.no_coredump': 'Kein Absturzbericht gespeichert',
    'mt.url': 'Update von einer Adresse', 'mt.url.hint': 'Das Gerät lädt die Firmware-Datei (.bin) selbst herunter, z. B. von einer Release-Seite. HTTPS mit geprüftem Server-Zertifikat.',
    'mt.url.field': 'Adresse der Firmware-Datei', 'mt.url.start': 'Herunterladen und installieren',
    'mt.url.run': 'Lade herunter … {r}', 'mt.url.run.pct': 'Lade herunter … {p} % ({r})',
    'mt.url.http': 'Unverschlüsselt (http): Die Datei kann unterwegs verändert werden, ohne dass es jemand merkt. Nur im eigenen Netz verwenden.',
    'mt.url.http.ask': 'Firmware über unverschlüsseltes http laden? Sie kann unterwegs verändert werden.',
    'mt.url.http.off': 'http://-Adressen sind gesperrt – nur https. Unten lässt sich http erlauben (nicht empfohlen).',
    'mt.url.failed': 'Update fehlgeschlagen: {e}',
    'mt.rel': 'Updates', 'mt.rel.cur': 'Installiert', 'mt.rel.new': 'Verfügbar', 'mt.rel.at': 'Geprüft',
    'mt.rel.none': 'noch nicht geprüft', 'mt.rel.ok': 'Aktuell', 'mt.rel.avail': 'Neu: {v}', 'mt.rel.checking': 'Prüfe …',
    'mt.rel.check': 'Nach Updates suchen', 'mt.rel.install': '{v} installieren', 'mt.rel.notes': 'Was ist neu',
    'mt.rel.ask': 'Firmware {v} installieren? Die Pumpen sind währenddessen gesperrt, danach startet das Gerät neu.',
    'mt.rel.failed': 'Prüfung fehlgeschlagen: {e}', 'mt.rel.auto': 'Täglich prüfen', 'mt.rel.auto.h': 'Zeigt nur einen Hinweis – installiert wird nie von selbst.',
    'mt.rel.hint': 'Offizielle Versionen. Das Gerät prüft die Datei gegen das Release (SHA-256), bevor es auf sie umschaltet.',
    'mt.src': 'Update-Quelle', 'mt.src.field': 'Adresse der Release-Liste (manifest.json)',
    'mt.src.h': 'Solange sie gesetzt ist, bietet die Wartung „Nach Updates suchen“ an; leer = keine Update-Quelle. Modulvariante: {v}.',
    'mt.src.save': 'Speichern', 'mt.src.saved': 'Gespeichert – wird jetzt geprüft.', 'mt.src.cleared': 'Update-Quelle entfernt.',
    'mt.opt.http': 'Unverschlüsseltes http erlauben',
    'mt.opt.http.h': 'Aus: nur https mit geprüftem Zertifikat (empfohlen).',
    'mt.opt.http.on': 'http ist erlaubt: Firmware-Downloads über http sind nicht gegen Manipulation geschützt. Wieder ausschalten, wenn es nicht mehr gebraucht wird.',
    'mt.opt.http.ask': 'Firmware-Downloads über unverschlüsseltes http erlauben? Jeder auf dem Weg durchs Netz könnte die Firmware austauschen. Nur einschalten, wenn klar ist, warum.',
    'mt.opt.saved': 'Gespeichert.',
    'mt.ch': 'Update-Kanal', 'mt.ch.stable': 'Stabil', 'mt.ch.beta': 'Beta',
    'mt.ch.h': 'Stabil: nur fertige Versionen. Beta: auch Vorabversionen – nur für Tester.',
    'mt.ch.on': 'Beta-Kanal: Vorabversionen können Fehler enthalten, die zu Fehldosierungen führen. Nach einem Update die Wasserwerte öfter messen.',
    'mt.ch.ask.t': 'Auf den Beta-Kanal wechseln?',
    'mt.ch.ask': 'Vorabversionen sind noch nicht vollständig getestet und können Fehler enthalten, die zu Fehldosierungen führen.',
    'mt.ch.ask.1': 'Nur für Tester, die wissen, worauf sie sich einlassen.',
    'mt.ch.ask.2': 'Installiert wird nie von selbst – jedes Update wird von Hand gestartet.',
    'mt.ch.ask.3': 'Nach einem Update die Wasserwerte öfter messen.',
    'mt.ch.ask.4': 'Zurück auf Stabil spielt keine ältere Version ein: Das Gerät behält die Vorabversion, bis eine neuere fertige Version erscheint.',
    'mt.ch.ok': 'Auf Beta wechseln',
    'mt.ch.pre': 'Installiert ist eine Vorabversion. Sie bleibt, bis eine neuere fertige Version erscheint; wer sofort zurück will, installiert die fertige Version unter „Firmware-Update“ als Datei.',
    'err.no_channel': 'Diese Update-Quelle hat keinen Kanal zur Auswahl',
    'err.no_source': 'Keine Update-Quelle eingestellt', 'err.no_update': 'Keine neuere Version verfügbar',
    'err.sha256_mismatch': 'Die geladene Datei passt nicht zum Release (SHA-256) – verworfen',
    'err.manifest_invalid': 'Die Update-Quelle lieferte eine ungültige Release-Liste', 'err.invalid_version': 'Die Release-Liste hat eine ungültige Version',
    'err.invalid_sha256': 'Die Release-Liste hat eine ungültige Prüfsumme', 'err.no_build': 'Das Release enthält keine Firmware für diese Modulvariante',
    'err.unknown_variant': 'Unbekannte Modulvariante (Flash-Größe)',
    'err.invalid_url': 'Ungültige Adresse (http:// oder https://)', 'err.http_not_allowed': 'Unverschlüsseltes http ist nicht erlaubt',
    'err.download_failed': 'Download fehlgeschlagen', 'err.http_status': 'Der Server hat den Download abgelehnt',
    'err.too_many_redirects': 'Zu viele Weiterleitungen',
  },
});

const sleep = ms => new Promise(r => setTimeout(r, ms));

// After an update or restart: wait until the device answers again, then reload
// (sessions live in RAM, so the reload usually ends on the login page)
export async function waitForDevice(msg) {
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
      ${d.model && html`<dt>${t('mt.model')}</dt><dd>${d.model}${pro && d.board ? html`<br/><span class="mut small">${d.board}</span>` : ''}</dd>`}
      <dt>${t('mt.fw')}</dt><dd>${f.version}</dd>
      ${pro && html`<dt>${t('mt.slot')}</dt><dd>${f.partition} · ${f.ota_state}</dd>`}
      <dt>${t('mt.built')}</dt><dd>${f.built}${pro && html`<br/><span class="mut small">IDF ${f.idf}</span>`}</dd>
      <dt>${t('mt.host')}</dt><dd>${d.hostname}.local</dd>
      ${pro && html`<dt>${t('mt.mac')}</dt><dd>${d.mac}</dd>`}
      <dt>${t('mt.uptime')}</dt><dd>${uptime(s.uptime_s)}</dd>
      ${pro && html`<dt>${t('mt.reset')}</dt><dd>${s.reset_reason}</dd>
        <dt>${t('mt.mem')}</dt><dd>${num(s.heap_free / 1024)} / ${num(s.heap_min_free / 1024)} KiB</dd>
        <dt>${t('mt.psram')}</dt><dd>${num(s.psram_free / 1048576, 1)} MiB</dd>`}
      <dt>${t('mt.license')}</dt><dd><a href="/ui/licenses/gpl-3.0.txt" target="_blank" rel="noopener">${t('mt.license.v')}</a><br/>
        <a href="/ui/licenses/third-party.txt" target="_blank" rel="noopener">${t('mt.third')}</a></dd>
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

// Update the device downloads itself: follows GET /api/v1/ota/status until
// the restart. run(promise) starts it with the POST that begins the download.
function useDownload(msg) {
  const [st, setSt] = useState(null);  // status while running
  const [busy, setBusy] = useState(false);
  const poll = () => api('GET', '/api/v1/ota/status').then(j => {
    setSt(j);
    if (j.state === 'running') { setTimeout(poll, 1000); return; }
    if (j.state === 'rebooting') { msg.ok(t('mt.done')); waitForDevice(msg); return; }
    setBusy(false);
    if (j.state === 'failed') msg.err(t('mt.url.failed', { e: errText({ code: j.error }) }) + (j.detail ? ' (' + j.detail + ')' : ''));
  }).catch(() => setTimeout(poll, 2000));  // the device may be restarting
  const run = p => {
    msg.clear();
    setBusy(true);
    return p.then(() => { setSt({ state: 'running', received: 0, total: null }); setTimeout(poll, 500); })
      .catch(e => { setBusy(false); msg.err(e); });
  };
  const running = st && st.state === 'running';
  const pct = running && st.total ? Math.min(100, Math.floor(st.received / st.total * 100)) : null;
  const node = running && html`<div class=${'bar' + (pct === null ? ' indet' : '')}><i style=${{ width: (pct ?? 100) + '%' }}></i></div>
    <p class="small">${pct === null ? t('mt.url.run', { r: fmtSize(st.received) }) : t('mt.url.run.pct', { p: pct, r: fmtSize(st.received) })}</p>`;
  return { busy, run, node };
}

// Maintenance (all modes, only with an update source): last check, check now,
// install the newer release, daily check on/off; pro view: update channel
// (beta only after a warning). The beta notice shows in every view.
function ReleaseCard({ rel, setRel, busyPumps, pro }) {
  const msg = useMsg();
  const dl = useDownload(msg);
  const watch = () => api('GET', '/api/v1/ota/release').then(j => { setRel(j); if (j.checking) setTimeout(watch, 1000); })
    .catch(() => setTimeout(watch, 2000));
  useEffect(() => { if (rel.checking) watch(); }, []);
  const check = () => {
    msg.clear();
    api('POST', '/api/v1/ota/release/check').then(() => { setRel({ ...rel, checking: true }); setTimeout(watch, 700); }).catch(e => msg.err(e));
  };
  const install = async () => {
    if (!(await ask(t('mt.rel.ask', { v: rel.latest }), { ok: t('mt.rel.install', { v: rel.latest }) }))) return;
    dl.run(api('POST', '/api/v1/ota/release/install'));
  };
  const auto = on => api('PUT', '/api/v1/ota/settings', { auto_check: on })
    .then(j => setRel({ ...rel, auto_check: j.auto_check })).catch(e => msg.err(e));
  const channel = async c => {
    if (c === 'beta' && !(await ask(t('mt.ch.ask'), {
      title: t('mt.ch.ask.t'), warn: true, points: [1, 2, 3, 4].map(i => t('mt.ch.ask.' + i)), ok: t('mt.ch.ok'), danger: true,
    }))) return;
    msg.clear();
    api('PUT', '/api/v1/ota/settings', { channel: c })
      .then(j => { setRel({ ...rel, channel: j.channel, latest: null, available: false, notes: null, error: null, checking: true }); setTimeout(watch, 700); })
      .catch(e => msg.err(e));
  };
  const pre = rel.current.includes('-');  // a release candidate is running
  const at = rel.checked_at ? new Date(rel.checked_at * 1000).toLocaleString(getLang(), { dateStyle: 'short', timeStyle: 'short', ...hourCycle() }) : null;
  const busy = dl.busy || rel.checking;
  return html`<section class="card">
    <div class="row between"><h2>${t('mt.rel')}</h2>
      <span class="row">${rel.channel === 'beta' && html`<span class="chip warn">${t('mt.ch.beta')}</span>`}
      ${rel.latest && html`<span class=${'chip ' + (rel.available ? 'acc' : 'ok')}>${rel.available ? t('mt.rel.avail', { v: rel.latest }) : t('mt.rel.ok')}</span>`}</span></div>
    <p class="small mut">${t('mt.rel.hint')}</p>
    <dl class="stats">
      <dt>${t('mt.rel.cur')}</dt><dd>${rel.current}</dd>
      <dt>${t('mt.rel.new')}</dt><dd>${rel.checking ? t('mt.rel.checking') : rel.latest || t('mt.rel.none')}
        ${rel.notes && !rel.checking && html` · <a href=${rel.notes} target="_blank" rel="noopener noreferrer">${t('mt.rel.notes')}</a>`}</dd>
      ${at && html`<dt>${t('mt.rel.at')}</dt><dd>${at}</dd>`}
    </dl>
    ${rel.channel === 'beta' && html`<p class="small warn">${t('mt.ch.on')}</p>`}
    ${rel.channel === 'stable' && pre && !rel.available && html`<p class="small mut">${t('mt.ch.pre')}</p>`}
    ${rel.error && !rel.checking && html`<p class="small err">${t('mt.rel.failed', { e: errText({ code: rel.error }) })}${rel.detail ? ' (' + rel.detail + ')' : ''}</p>`}
    <${Toggle} checked=${rel.auto_check} onChange=${auto}>
      <span>${t('mt.rel.auto')}<br/><span class="small mut">${t('mt.rel.auto.h')}</span></span><//>
    ${pro && rel.channel && html`<div class="field"><span class="lbl">${t('mt.ch')}</span>
      <${Seg} value=${rel.channel} disabled=${busy} onChange=${channel}
        options=${[['stable', t('mt.ch.stable')], ['beta', t('mt.ch.beta')]]}/>
      <span class="small mut">${t('mt.ch.h')}</span></div>`}
    ${rel.available && busyPumps && !dl.busy && html`<p class="small warn">${t('mt.busy')}</p>`}
    ${dl.node}
    <div class="row savebar">
      ${rel.available && html`<button type="button" disabled=${busy || busyPumps} onClick=${install}>${t('mt.rel.install', { v: rel.latest })}</button>`}
      <button type="button" class=${rel.available ? 'sec' : ''} disabled=${busy} onClick=${check}>${t('mt.rel.check')}</button>
    </div>
    ${msg.node}
  </section>`;
}

// Engineering: the device downloads the image (POST /api/v1/ota/url) and
// reports on GET /api/v1/ota/status until it restarts. call = API function
// of the engineering area.
export function UrlUpdateCard({ busyPumps, opts, call }) {
  const [url, setUrl] = useState('');
  const msg = useMsg();
  const dl = useDownload(msg);
  const http = isHttp(url);
  const blocked = http && !opts.allow_http;
  const start = async e => {
    e.preventDefault();
    msg.clear();
    if (blocked) { msg.err(t('mt.url.http.off')); return; }
    if (http && !(await ask(t('mt.url.http.ask'), { ok: t('mt.url.start'), danger: true }))) return;
    dl.run(call('POST', '/api/v1/ota/url', { url: url.trim() }));
  };
  const busy = dl.busy;
  return html`<form class="card" novalidate onSubmit=${start}><h2>${t('mt.url')}</h2>
    <p class="small mut">${t('mt.url.hint')}</p>
    <label class="field">${t('mt.url.field')}
      <input type="url" maxlength="512" autocomplete="off" spellcheck="false" placeholder="https://…/firmware.bin"
        disabled=${busy} value=${url} onInput=${e => setUrl(e.target.value)}/></label>
    ${http && html`<p class=${'small ' + (blocked ? 'err' : 'warn')}>${t(blocked ? 'mt.url.http.off' : 'mt.url.http')}</p>`}
    ${busyPumps && !busy && html`<p class="small warn">${t('mt.busy')}</p>`}
    ${dl.node}
    <div class="row savebar"><button type="submit" disabled=${busy || !url.trim() || busyPumps || blocked}>${t('mt.url.start')}</button></div>
    ${msg.node}
  </form>`;
}

// Engineering: update source (release manifest) and plain http for updates
// (warned, off from the factory)
export function UpdateSourceCard({ opts, setOpts, call }) {
  const [src, setSrc] = useState(opts.source);
  const [variant, setVariant] = useState(null);
  useEffect(() => { api('GET', '/api/v1/ota/release').then(j => setVariant(j.variant)).catch(() => {}); }, []);
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const put = body => call('PUT', '/api/v1/ota/settings', body).then(j => { setOpts(j); return j; });
  const save = e => {
    e.preventDefault();
    msg.clear();
    setBusy(true);
    put({ source: src.trim() }).then(j => { setSrc(j.source); msg.ok(t(j.source ? 'mt.src.saved' : 'mt.src.cleared')); })
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  const http = async on => {
    if (on && !(await ask(t('mt.opt.http.ask'), { danger: true }))) return;
    put({ allow_http: on }).then(() => msg.ok(t('mt.opt.saved'))).catch(e => msg.err(e));
  };
  return html`<form class="card" novalidate onSubmit=${save}><h2>${t('mt.src')}</h2>
    <label class="field">${t('mt.src.field')}
      <input type="url" maxlength="512" autocomplete="off" spellcheck="false" disabled=${busy}
        placeholder="https://github.com/…/releases/latest/download/manifest.json" value=${src} onInput=${e => setSrc(e.target.value)}/></label>
    <p class="small mut">${t('mt.src.h', { v: variant || '–' })}</p>
    <div class="row savebar"><button type="submit" disabled=${busy || src.trim() === opts.source}>${t('mt.src.save')}</button></div>
    <${Toggle} checked=${opts.allow_http} onChange=${http}>
      <span>${t('mt.opt.http')}<br/><span class=${'small ' + (opts.allow_http ? 'warn' : 'mut')}>${t(opts.allow_http ? 'mt.opt.http.on' : 'mt.opt.http.h')}</span></span><//>
    ${msg.node}
  </form>`;
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
  const [rel, setRel] = useState(null);  // GET /api/v1/ota/release (incl. the http warning)
  const [http, setHttp] = useState(false);
  useEffect(() => {
    api('GET', '/api/v1/ota/release').then(setRel).catch(() => {});
    api('GET', '/api/v1/ota/settings').then(j => setHttp(j.allow_http)).catch(() => {});
  }, []);
  return html`<div class="page-head"><h1>${t('mt.title')}</h1></div>
    ${pro && http && html`<p class="banner warn">${t('mt.opt.http.on')}</p>`}
    ${!info ? html`<p class="mut">…</p>` : html`<div class="grid settings">
      <${InfoCard} info=${info} pro=${pro}/>
      ${rel && rel.source && html`<${ReleaseCard} rel=${rel} setRel=${setRel} busyPumps=${busyPumps} pro=${pro}/>`}
      <${UpdateCard} busyPumps=${busyPumps}/>
      <${RestartCard}/>
      <${FactoryCard}/>
      ${pro && html`<${DiagCard} info=${info}/>`}
    </div>`}`;
}
