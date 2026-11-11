// Engineering menu (functional spec 11.2), #/engineering. Only reachable via
// the hidden engineering dialog; every request needs the engineering unlock.
//   A  mechanics: dosing physics (global), driver per pump
//   B  limits: priming timeouts, minimum dose, daily limit window, schedule grid
//   C  maintenance: tube life and counters, RTC aging
//   D  diagnostics: raw stepper test, driver diagnosis, telemetry, resets
//   E  full backup and restore (views/backup.js)
// Live data is polled only while the user is active, so the device still
// locks the area 15 min after the last real use.
import { useEffect, useRef, useState } from 'preact/hooks';
import { api, qs } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, getLang, num, t } from '../lib/i18n.js';
import { hourCycle } from '../lib/prefs.js';
import { pumpColor, pumpName } from '../lib/pumps.js';
import { reloadPumps, useStore } from '../lib/store.js';
import { Seg, Toggle, useMsg } from '../lib/ui.js';
import { BackupSection } from './backup.js';
import { EngUnlock, lockEngineering } from './engineering.js';
import { UpdateSourceCard, UrlUpdateCard } from './maintenance.js';

add({
  en: {
    'u.days': 'days', 'u.rev': 'rev', 'u.rpm': 'rpm', 'u.sps': 'steps/s', 'u.sps2': 'steps/s²',
    'sv.title': 'Engineering', 'sv.left': 'unlocked · {m} min left', 'sv.lock': 'Lock now', 'sv.locked': 'The engineering area is locked.',
    'sv.save': 'Save', 'sv.saved': 'Saved.', 'sv.check': 'Please check: {f} ({min} … {max})', 'sv.loading': '…',
    'sv.a': 'A · Mechanics', 'sv.b': 'B · Limits and timeouts', 'sv.c': 'C · Maintenance', 'sv.d': 'D · Diagnostics',
    'sv.phys': 'Dosing physics (all pumps)',
    'sv.retract': 'Anti-drip retraction ("default")', 'sv.retract.h': '≈ {ml} ml', 'sv.retract.note': 'Anti-drip retraction: each pump uses off (factory setting), this default or its own value (pump settings, pro view).',
    'sv.calib': 'Calibration target', 'sv.tier': 'Speed {n}', 'sv.tier.n': ['slow', 'normal', 'fast'],
    'sv.tier.h': '≈ {ml} ml/min · {sps} steps/s', 'sv.tiers.asc': 'The speeds must not decrease from slow to fast.',
    'sv.drv': 'Stepper driver', 'sv.drv.h': 'TMC2209 via UART. Values are stored and written to the driver at once (only at standstill).',
    'sv.run_ma': 'Run current', 'sv.hold_ma': 'Hold current', 'sv.hold.h': '0 = motor free at standstill',
    'sv.micro': 'Microsteps', 'sv.full': 'Full steps per revolution', 'sv.accel': 'Acceleration', 'sv.vstart': 'Start speed',
    'sv.invert': 'Reverse direction (GCONF.shaft)', 'sv.all': 'Apply to all 5 pumps',
    'sv.spr': '{spr} steps/rev · max. {sps} steps/s', 'sv.present': 'driver OK', 'sv.absent': 'driver not answering',
    'sv.absent.h': 'No answer on the UART bus: driver module not plugged in, wrong slot or no 24 V supply.',
    'sv.lim': 'Timeouts, minimum dose and grid', 'sv.prime_max': 'Priming: max. duration', 'sv.prime_hb': 'Priming: dead-man timeout',
    'sv.min_dose': 'Smallest single dose', 'sv.limit': 'Daily limit window', 'sv.limit.calendar_day': 'Calendar day', 'sv.limit.rolling_24h': 'Rolling 24 h',
    'sv.grid': 'Schedule grid', 'sv.delay': 'Max. delay of a scheduled dose',
    'sv.tube': 'Tubes', 'sv.tube_life': 'Tube lifetime (maintenance warning)', 'sv.tube.set': 'Set', 'sv.tube.reset': 'Reset',
    'sv.tube.done': 'Pump {n}: counter set to {h} h.',
    'sv.thr': 'Container thresholds', 'sv.thr.h': 'Days of supply from the schedule; defaults for all pumps, a pump can set its own in its settings (pro mode). 0 = off.',
    'sv.thr.warn': 'Warning when it lasts fewer than', 'sv.thr.low': 'Alarm "almost empty" when it lasts fewer than',
    'sv.thr.order': 'The alarm threshold must not be above the warning threshold.',
    'sv.hist': 'History', 'sv.hist.keep': 'Keep events for', 'sv.hist.h': 'A shorter time deletes older days within a minute.',
    'sv.rtc': 'Clock module (DS3231)', 'sv.rtc.none': 'Clock module not found.', 'sv.rtc.temp': 'Temperature', 'sv.rtc.aging': 'Aging offset',
    'sv.rtc.h': 'About 0.1 ppm per step; positive values slow the clock down, negative speed it up. Range −128 … 127.',
    'sv.rtc.auto': 'Trim automatically from NTP', 'sv.rtc.auto.h': 'Measures the drift against the time server over at least a day and corrects the aging offset. A value entered by hand starts a new measurement.',
    'sv.rtc.since': 'Measuring since', 'sv.rtc.off': 'Deviation now', 'sv.rtc.last': 'Last correction',
    'sv.rtc.last.v': '{d}: {ppm} ppm, {from} → {to}', 'sv.rtc.wait': 'waits for the next NTP sync',
    'sv.rtc.lost': 'The clock module had lost the time while the device was off – check its backup battery.',
    'sv.rtc.on': 'Automatic trim switched on.', 'sv.rtc.offd': 'Automatic trim switched off.',
    'sv.raw': 'Raw stepper test', 'sv.raw.h': 'Moves a pump head by exact steps – bypasses container tracking, schedule and history. For hardware checks only.',
    'sv.raw.dir': 'Direction', 'sv.fwd': 'Forward', 'sv.rev': 'Backward', 'sv.steps': 'Steps', 'sv.sps': 'Frequency',
    'sv.force': 'Step even if the driver does not answer', 'sv.start': 'Start', 'sv.stop': 'Stop', 'sv.estop': 'Stop now',
    'sv.raw.calc': '= {rev} rev ≈ {ml} ml (nominal) · {s} s', 'sv.raw.run': 'running · {pos} / {tgt} steps · {rpm} rpm · StallGuard {sg}',
    'sv.raw.idle': 'idle · last move {pos} steps · StallGuard min. {sg}',
    'sv.e': 'E · Firmware update', 'sv.f': 'F · Backup',
    'sv.diag': 'Driver diagnosis', 'sv.diag.h': 'Status of the TMC2209 from the last read (continuously while moving, otherwise once per second).',
    'sv.fl.standstill': 'Standstill', 'sv.fl.stealth': 'StealthChop', 'sv.fl.overtemp_warn': 'Overtemperature warning (120 °C)',
    'sv.fl.overtemp': 'Overtemperature shutdown', 'sv.fl.short': 'Short circuit', 'sv.fl.open_load_a': 'Open load coil A',
    'sv.fl.open_load_b': 'Open load coil B', 'sv.fl.temp_over_120': 'Temperature above 120 °C', 'sv.fl.cs': 'Current scale (CS_ACTUAL)',
    'sv.yes': 'yes', 'sv.no': 'no', 'sv.regs': 'Read registers', 'sv.regs.none': 'no answer',
    'sv.eeprom': 'EEPROM self test', 'sv.eeprom.run': 'Run test', 'sv.eeprom.h': 'Writes and reads back the last page (content is restored).',
    'sv.eeprom.ok': 'EEPROM self test passed.', 'sv.eeprom.none': 'EEPROM not found – run state is kept in NVS.',
    'err.eeprom_test_failed': 'EEPROM self test failed',
    'sv.tel': 'System telemetry', 'sv.up': 'Uptime', 'sv.reset': 'Last reset', 'sv.chip': 'Chip temperature',
    'sv.heap': 'RAM free (min.)', 'sv.frag': 'Largest block · fragmentation', 'sv.psram': 'PSRAM free', 'sv.nvs': 'NVS entries',
    'sv.fs': 'Flash file system', 'sv.hist': 'History', 'sv.hist.v': '{d} days · {b}', 'sv.dropped': 'Events lost since start',
    'sv.tasks': 'Tasks', 'sv.uart': 'UART bus {b}', 'sv.uart.v': '{n} transfers · {e} errors',
    'sv.paused': 'Live values paused – move the mouse or press a key.',
    'sv.danger': 'Danger zone',
    'sv.dz.hist': 'Delete history', 'sv.dz.hist.h': 'Deletes all recorded events.',
    'sv.dz.net': 'Reset network settings', 'sv.dz.net.h': 'LAN back to DHCP, WLAN removed. The device may get a new address.',
    'sv.dz.sure': 'Really?', 'sv.dz.yes': 'Yes, do it', 'sv.dz.no': 'Cancel',
    'sv.dz.hist.ok': 'History deleted.', 'sv.dz.net.ok': 'Network settings reset – the device applies them in a moment.',
    'sv.f.driver_lost': 'stopped: driver stopped answering', 'sv.f.driver_reset': 'stopped: driver restarted (24 V dropped?)',
    'sv.f.driver_error': 'stopped: driver reports an error', 'sv.f.short_circuit': 'stopped: short circuit at the motor',
    'sv.f.overtemperature': 'stopped: driver overheated',
    'err.driver_config_failed': 'Saved, but the driver could not be configured',
    'sv.sps.range': 'Frequency out of range', 'err.rtc_unavailable': 'Clock module not reachable',
  },
  de: {
    'u.days': 'Tage', 'u.rev': 'U', 'u.rpm': 'U/min', 'u.sps': 'Schritte/s', 'u.sps2': 'Schritte/s²',
    'sv.title': 'Engineering', 'sv.left': 'freigeschaltet · noch {m} min', 'sv.lock': 'Jetzt sperren', 'sv.locked': 'Der Engineering-Bereich ist gesperrt.',
    'sv.save': 'Speichern', 'sv.saved': 'Gespeichert.', 'sv.check': 'Bitte prüfen: {f} ({min} … {max})', 'sv.loading': '…',
    'sv.a': 'A · Mechanik', 'sv.b': 'B · Grenzen und Timeouts', 'sv.c': 'C · Wartung', 'sv.d': 'D · Diagnose',
    'sv.phys': 'Dosier-Physik (alle Pumpen)',
    'sv.retract': 'Anti-Tropf-Rückzug („Standard“)', 'sv.retract.h': '≈ {ml} ml', 'sv.retract.note': 'Anti-Tropf-Rückzug: jede Pumpe nutzt aus (ab Werk), diesen Standard oder einen eigenen Wert (Pumpeneinstellungen, Profi-Ansicht).',
    'sv.calib': 'Kalibrier-Zielmenge', 'sv.tier': 'Stufe {n}', 'sv.tier.n': ['langsam', 'normal', 'schnell'],
    'sv.tier.h': '≈ {ml} ml/min · {sps} Schritte/s', 'sv.tiers.asc': 'Die Stufen dürfen von langsam nach schnell nicht kleiner werden.',
    'sv.drv': 'Schrittmotortreiber', 'sv.drv.h': 'TMC2209 per UART. Werte werden gespeichert und sofort in den Treiber geschrieben (nur im Stillstand).',
    'sv.run_ma': 'Laufstrom', 'sv.hold_ma': 'Haltestrom', 'sv.hold.h': '0 = Motor im Stillstand frei',
    'sv.micro': 'Mikroschritte', 'sv.full': 'Vollschritte pro Umdrehung', 'sv.accel': 'Beschleunigung', 'sv.vstart': 'Startgeschwindigkeit',
    'sv.invert': 'Drehrichtung umkehren (GCONF.shaft)', 'sv.all': 'Auf alle 5 Pumpen anwenden',
    'sv.spr': '{spr} Schritte/U · max. {sps} Schritte/s', 'sv.present': 'Treiber OK', 'sv.absent': 'Treiber antwortet nicht',
    'sv.absent.h': 'Keine Antwort am UART-Bus: Treibermodul nicht gesteckt, falscher Steckplatz oder keine 24-V-Versorgung.',
    'sv.lim': 'Timeouts, Mindestmenge und Raster', 'sv.prime_max': 'Entlüften: max. Dauer', 'sv.prime_hb': 'Entlüften: Totmann-Timeout',
    'sv.min_dose': 'Kleinste Einzeldosis', 'sv.limit': 'Fenster des Tageslimits', 'sv.limit.calendar_day': 'Kalendertag', 'sv.limit.rolling_24h': 'Rollierend 24 h',
    'sv.grid': 'Zeitplan-Raster', 'sv.delay': 'Max. Verzögerung einer geplanten Dosis',
    'sv.tube': 'Schläuche', 'sv.tube_life': 'Schlauch-Lebensdauer (Wartungswarnung)', 'sv.tube.set': 'Setzen', 'sv.tube.reset': 'Zurücksetzen',
    'sv.tube.done': 'Pumpe {n}: Zähler auf {h} h gesetzt.',
    'sv.thr': 'Behälter-Schwellen', 'sv.thr.h': 'Reichweite laut Zeitplan; Standardwerte für alle Pumpen, jede Pumpe kann in ihren Einstellungen eigene haben (Profi-Modus). 0 = aus.',
    'sv.thr.warn': 'Warnung, wenn er weniger reicht als', 'sv.thr.low': 'Alarm „fast leer“, wenn er weniger reicht als',
    'sv.thr.order': 'Die Alarmschwelle darf nicht über der Warnschwelle liegen.',
    'sv.hist': 'Verlauf', 'sv.hist.keep': 'Ereignisse aufbewahren für', 'sv.hist.h': 'Eine kürzere Zeit löscht ältere Tage innerhalb einer Minute.',
    'sv.rtc': 'Uhrenmodul (DS3231)', 'sv.rtc.none': 'Uhrenmodul nicht gefunden.', 'sv.rtc.temp': 'Temperatur', 'sv.rtc.aging': 'Aging-Offset',
    'sv.rtc.h': 'Etwa 0,1 ppm pro Schritt; positive Werte bremsen die Uhr, negative beschleunigen sie. Bereich −128 … 127.',
    'sv.rtc.auto': 'Automatisch per NTP abgleichen', 'sv.rtc.auto.h': 'Misst die Abweichung zum Zeitserver über mindestens einen Tag und korrigiert den Aging-Offset. Ein von Hand eingetragener Wert startet eine neue Messung.',
    'sv.rtc.since': 'Messung läuft seit', 'sv.rtc.off': 'Abweichung jetzt', 'sv.rtc.last': 'Letzte Korrektur',
    'sv.rtc.last.v': '{d}: {ppm} ppm, {from} → {to}', 'sv.rtc.wait': 'wartet auf den nächsten NTP-Abgleich',
    'sv.rtc.lost': 'Das Uhrenmodul hatte die Uhrzeit verloren, während das Gerät aus war – Pufferbatterie prüfen.',
    'sv.rtc.on': 'Automatischer Abgleich eingeschaltet.', 'sv.rtc.offd': 'Automatischer Abgleich ausgeschaltet.',
    'sv.raw': 'Raw-Stepper-Test', 'sv.raw.h': 'Fährt einen Pumpenkopf um exakt X Schritte – am Behälter-Tracking, Zeitplan und Verlauf vorbei. Nur zur Hardwareprüfung.',
    'sv.raw.dir': 'Richtung', 'sv.fwd': 'Vorwärts', 'sv.rev': 'Rückwärts', 'sv.steps': 'Schritte', 'sv.sps': 'Frequenz',
    'sv.force': 'Auch ohne Treiberantwort takten', 'sv.start': 'Start', 'sv.stop': 'Stopp', 'sv.estop': 'Sofort stoppen',
    'sv.raw.calc': '= {rev} U ≈ {ml} ml (Nennwert) · {s} s', 'sv.raw.run': 'läuft · {pos} / {tgt} Schritte · {rpm} U/min · StallGuard {sg}',
    'sv.raw.idle': 'steht · letzte Fahrt {pos} Schritte · StallGuard min. {sg}',
    'sv.e': 'E · Firmware-Update', 'sv.f': 'F · Datensicherung',
    'sv.diag': 'Treiberdiagnose', 'sv.diag.h': 'Zustand des TMC2209 aus der letzten Abfrage (während der Fahrt laufend, sonst jede Sekunde).',
    'sv.fl.standstill': 'Stillstand', 'sv.fl.stealth': 'StealthChop', 'sv.fl.overtemp_warn': 'Übertemperatur-Warnung (120 °C)',
    'sv.fl.overtemp': 'Übertemperatur-Abschaltung', 'sv.fl.short': 'Kurzschluss', 'sv.fl.open_load_a': 'Spule A offen',
    'sv.fl.open_load_b': 'Spule B offen', 'sv.fl.temp_over_120': 'Temperatur über 120 °C', 'sv.fl.cs': 'Stromstufe (CS_ACTUAL)',
    'sv.yes': 'ja', 'sv.no': 'nein', 'sv.regs': 'Register lesen', 'sv.regs.none': 'keine Antwort',
    'sv.eeprom': 'EEPROM-Selbsttest', 'sv.eeprom.run': 'Test starten', 'sv.eeprom.h': 'Schreibt und liest die letzte Seite (Inhalt wird wiederhergestellt).',
    'sv.eeprom.ok': 'EEPROM-Selbsttest bestanden.', 'sv.eeprom.none': 'EEPROM nicht gefunden – der Laufzustand liegt im NVS.',
    'err.eeprom_test_failed': 'EEPROM-Selbsttest fehlgeschlagen',
    'sv.tel': 'Systemtelemetrie', 'sv.up': 'Laufzeit', 'sv.reset': 'Letzter Neustart', 'sv.chip': 'Chip-Temperatur',
    'sv.heap': 'RAM frei (min.)', 'sv.frag': 'Größter Block · Fragmentierung', 'sv.psram': 'PSRAM frei', 'sv.nvs': 'NVS-Einträge',
    'sv.fs': 'Flash-Dateisystem', 'sv.hist': 'Verlauf', 'sv.hist.v': '{d} Tage · {b}', 'sv.dropped': 'Verlorene Ereignisse seit Start',
    'sv.tasks': 'Tasks', 'sv.uart': 'UART-Bus {b}', 'sv.uart.v': '{n} Transfers · {e} Fehler',
    'sv.paused': 'Live-Werte pausiert – Maus bewegen oder Taste drücken.',
    'sv.danger': 'Gefahrenzone',
    'sv.dz.hist': 'Verlauf löschen', 'sv.dz.hist.h': 'Löscht alle aufgezeichneten Ereignisse.',
    'sv.dz.net': 'Netzwerkeinstellungen zurücksetzen', 'sv.dz.net.h': 'LAN wieder per DHCP, WLAN entfernt. Das Gerät bekommt eventuell eine neue Adresse.',
    'sv.dz.sure': 'Wirklich?', 'sv.dz.yes': 'Ja, ausführen', 'sv.dz.no': 'Abbrechen',
    'sv.dz.hist.ok': 'Verlauf gelöscht.', 'sv.dz.net.ok': 'Netzwerkeinstellungen zurückgesetzt – das Gerät übernimmt sie gleich.',
    'sv.f.driver_lost': 'abgebrochen: Treiber antwortet nicht mehr', 'sv.f.driver_reset': 'abgebrochen: Treiber neu gestartet (24 V kurz weg?)',
    'sv.f.driver_error': 'abgebrochen: Treiber meldet Fehler', 'sv.f.short_circuit': 'abgebrochen: Kurzschluss am Motor',
    'sv.f.overtemperature': 'abgebrochen: Treiber überhitzt',
    'err.driver_config_failed': 'Gespeichert, aber der Treiber ließ sich nicht konfigurieren',
    'sv.sps.range': 'Frequenz außerhalb des Bereichs', 'err.rtc_unavailable': 'Uhrenmodul nicht erreichbar',
  },
});

// ---------------------------------------------------------------------------
// Helpers

let onLocked = () => {};
// API call of the engineering area: "engineering_locked" switches the view
function eapi(method, path, body) {
  return api(method, path, body).catch(e => {
    if (e.code === 'engineering_locked') onLocked();
    throw e;
  });
}

// "2,5" / "-3" -> number, NaN if invalid
const parse = s => { const v = String(s ?? '').trim().replace(',', '.'); return /^-?\d+(\.\d+)?$/.test(v) ? Number(v) : NaN; };
// Translation with fallback for codes the UI does not know
const tr = (k, fb) => { const x = t(k); return x === k ? fb : x; };
const unit = u => (u.startsWith('u.') ? t(u) : u);
const str = v => (v === undefined || v === null ? '' : String(v));

// Field: k key, l label, min, max, d decimals (0 = integer), u unit
function invalid(fields, d) {
  for (const f of fields) {
    const v = parse(d[f.k]);
    if (!(v >= f.min && v <= f.max) || (!f.d && !Number.isInteger(v))) return f;
  }
  return null;
}
const label = f => (f.n === undefined ? t(f.l) : t(f.l, { n: t('sv.tier.n')[f.n] }));
const checkText = f => t('sv.check', { f: label(f), min: num(f.min, f.d || 0), max: num(f.max, f.d || 0) });
// Changed fields as query members
function changed(fields, d, base) {
  const q = {};
  for (const f of fields) if (parse(d[f.k]) !== parse(base[f.k])) q[f.k] = parse(d[f.k]);
  return q;
}

function Num({ f, d, set, hint, bad, label }) {
  return html`<label class="field">${label || t(f.l)}
    <span class=${'amount' + (bad ? ' bad' : '')}><input type="text" inputmode="decimal" autocomplete="off"
      value=${d[f.k]} onInput=${e => set({ [f.k]: e.target.value })}/>${f.u && html`<span>${unit(f.u)}</span>`}</span>
    ${hint && html`<span class="small mut">${hint}</span>`}</label>`;
}

function Save({ busy, dirty }) {
  return html`<div class="row savebar"><button type="submit" disabled=${busy || !dirty}>${t('sv.save')}</button></div>`;
}

const fmtBytes = b => (b >= 1048576 ? num(b / 1048576, 1) + ' MB' : num(b / 1024, 0) + ' kB');
function fmtUptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return (d ? d + ' d ' : '') + h + ' h ' + m + ' min';
}

// ---------------------------------------------------------------------------
// A: dosing physics (global)

const kPhys = [
  { k: 'retract_rev', l: 'sv.retract', min: 0, max: 2, d: 3, u: 'u.rev' },
  { k: 'calib_ml', l: 'sv.calib', min: 1, max: 200, d: 1, u: 'ml' },
  { k: 'tier1_rpm', l: 'sv.tier', min: 10, max: 600, u: 'u.rpm', n: 0 },
  { k: 'tier2_rpm', l: 'sv.tier', min: 10, max: 600, u: 'u.rpm', n: 1 },
  { k: 'tier3_rpm', l: 'sv.tier', min: 10, max: 600, u: 'u.rpm', n: 2 },
];
const physOf = e => ({ retract_rev: str(e.retract_rev), calib_ml: str(e.calib_ml),
  tier1_rpm: str(e.tier_rpm[0]), tier2_rpm: str(e.tier_rpm[1]), tier3_rpm: str(e.tier_rpm[2]) });

function PhysCard({ eng, spr, reload }) {
  const [d, setD] = useState(physOf(eng));
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  useEffect(() => setD(physOf(eng)), [eng]);
  const set = p => setD(x => ({ ...x, ...p }));
  const q = changed(kPhys, d, physOf(eng));
  const bad = invalid(kPhys, d);
  const nom = eng.nominal_ml_per_rev;
  const submit = e => {
    e.preventDefault();
    if (bad) return msg.err(checkText(bad));
    const r = [1, 2, 3].map(i => parse(d['tier' + i + '_rpm']));
    if (r[1] < r[0] || r[2] < r[1]) return msg.err(t('sv.tiers.asc'));
    setBusy(true);
    eapi('POST', '/api/v1/engineering/dosing?' + qs(q))
      .then(() => { msg.ok(t('sv.saved')); reload(); reloadPumps(); })
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  return html`<form class="card" novalidate onSubmit=${submit}>
    <h2>${t('sv.phys')}</h2>
    <div class="ipgrid">
      ${kPhys.map(f => {
        const v = parse(d[f.k]);
        let hint = null;
        if (f.k === 'retract_rev') hint = t('sv.retract.h', { ml: num(v * nom, 3) });
        else if (f.n !== undefined) hint = t('sv.tier.h', { ml: num(v * nom, 0), sps: spr ? num(v * spr / 60, 0) : '–' });
        return html`<${Num} f=${f} label=${label(f)} d=${d} set=${set} hint=${hint} bad=${bad === f}/>`;
      })}
    </div>
    <p class="small mut">${t('sv.retract.note')}</p>
    <${Save} busy=${busy} dirty=${Object.keys(q).length > 0}/>
    ${msg.node}
  </form>`;
}

// ---------------------------------------------------------------------------
// A: driver per pump

const kMicro = [1, 2, 4, 8, 16, 32, 64, 128, 256];
const kDrv = [
  { k: 'run_ma', l: 'sv.run_ma', min: 50, max: 1500, u: 'mA' },
  { k: 'hold_ma', l: 'sv.hold_ma', min: 0, max: 1500, u: 'mA' },
  { k: 'full_steps', l: 'sv.full', min: 1, max: 10000, u: '' },
  { k: 'accel', l: 'sv.accel', min: 1, max: 10000000, u: 'u.sps2' },
  { k: 'vstart', l: 'sv.vstart', min: 1, max: 1000000, u: 'u.sps' },
];
const drvOf = c => ({ run_ma: str(c.run_ma), hold_ma: str(c.hold_ma), full_steps: str(c.full_steps),
  accel: str(c.accel), vstart: str(c.v_start), microsteps: c.microsteps, invert: c.invert });

function DriverCard({ hw, pumps, reload }) {
  const [n, setN] = useState(1);
  const p = hw.pumps[n - 1];
  const [d, setD] = useState(drvOf(p.config));
  const [all, setAll] = useState(false);
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  useEffect(() => { setD(drvOf(hw.pumps[n - 1].config)); }, [n]);
  const set = x => setD(o => ({ ...o, ...x }));
  const base = drvOf(p.config);
  const q = changed(kDrv, d, base);
  if (d.microsteps !== base.microsteps) q.microsteps = d.microsteps;
  if (d.invert !== base.invert) q.invert = d.invert ? 1 : 0;
  const bad = invalid(kDrv, d);
  const submit = e => {
    e.preventDefault();
    if (bad) return msg.err(checkText(bad));
    if (parse(d.hold_ma) > parse(d.run_ma)) return msg.err(checkText({ ...kDrv[1], max: parse(d.run_ma) }));
    setBusy(true);
    const targets = all ? [1, 2, 3, 4, 5] : [n];
    targets.reduce((pr, k) => pr.then(() => eapi('POST', '/api/v1/hw/pump/config?' + qs({ pump: k, ...q }))), Promise.resolve())
      .then(() => { msg.ok(t('sv.saved')); return reload(); })
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  const spr = parse(d.full_steps) * d.microsteps;
  return html`<form class="card" novalidate onSubmit=${submit}>
    <h2>${t('sv.drv')}</h2>
    <p class="small mut">${t('sv.drv.h')}</p>
    <${PumpPick} n=${n} set=${setN} pumps=${pumps}/>
    <div class="row">
      <span class=${'chip ' + (p.present ? 'ok' : 'err')}>${t(p.present ? 'sv.present' : 'sv.absent')}</span>
      ${p.fault && html`<span class="chip err">${tr('sv.f.' + p.fault, p.fault)}</span>`}
      <span class="small mut">${p.stepgen} · ${t('sv.run_ma')} ${p.run_ma} mA · ${t('sv.hold_ma')} ${p.hold_ma} mA</span>
    </div>
    ${!p.present && html`<p class="small mut">${t('sv.absent.h')}</p>`}
    <div class="ipgrid">
      ${kDrv.map(f => html`<${Num} f=${f} d=${d} set=${set} bad=${bad === f} hint=${f.k === 'hold_ma' ? t('sv.hold.h') : null}/>`)}
      <label class="field">${t('sv.micro')}
        <select value=${d.microsteps} onChange=${e => set({ microsteps: +e.target.value })}>
          ${kMicro.map(m => html`<option value=${m}>${m}</option>`)}</select></label>
    </div>
    <p class="small mut">${t('sv.spr', { spr: num(spr), sps: num(p.config.max_sps) })}</p>
    <${Toggle} checked=${d.invert} onChange=${v => set({ invert: v })}>${t('sv.invert')}<//>
    <label class="check"><input type="checkbox" checked=${all} onChange=${e => setAll(e.target.checked)}/>${t('sv.all')}</label>
    <${Save} busy=${busy} dirty=${Object.keys(q).length > 0}/>
    ${msg.node}
  </form>`;
}

function PumpPick({ n, set, pumps }) {
  return html`<${Seg} value=${n} onChange=${set} options=${[1, 2, 3, 4, 5].map(k =>
    [k, pumps && pumps[k - 1] ? k + ' ' + pumpName(pumps[k - 1]) : String(k)])}/>`;
}

// ---------------------------------------------------------------------------
// B: limits and timeouts

const kLim = [
  { k: 'prime_max_s', l: 'sv.prime_max', min: 5, max: 600, u: 's' },
  { k: 'prime_heartbeat_ms', l: 'sv.prime_hb', min: 200, max: 5000, u: 'ms' },
  { k: 'min_dose_ml', l: 'sv.min_dose', min: 0.001, max: 10, d: 3, u: 'ml' },
];
const kSch = [{ k: 'max_delay_min', l: 'sv.delay', min: 1, max: 720, u: 'min' }];
const kGrids = [1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 16, 18, 20, 24, 30, 32, 36, 40, 45, 48, 60];
const limOf = (e, s) => ({ prime_max_s: str(e.prime_max_s), prime_heartbeat_ms: str(e.prime_heartbeat_ms),
  min_dose_ml: str(e.min_dose_ml), limit_mode: e.limit_mode, grid_min: s.grid_min, max_delay_min: str(s.max_delay_min) });

function LimitsCard({ eng, sch, reload }) {
  const [d, setD] = useState(limOf(eng, sch));
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  useEffect(() => setD(limOf(eng, sch)), [eng, sch]);
  const set = x => setD(o => ({ ...o, ...x }));
  const base = limOf(eng, sch);
  const qd = changed(kLim, d, base);
  if (d.limit_mode !== base.limit_mode) qd.limit_mode = d.limit_mode;
  const qs2 = changed(kSch, d, base);
  if (d.grid_min !== base.grid_min) qs2.grid_min = d.grid_min;
  const bad = invalid(kLim, d) || invalid(kSch, d);
  const submit = e => {
    e.preventDefault();
    if (bad) return msg.err(checkText(bad));
    setBusy(true);
    (Object.keys(qd).length ? eapi('POST', '/api/v1/engineering/dosing?' + qs(qd)) : Promise.resolve())
      .then(() => Object.keys(qs2).length && eapi('POST', '/api/v1/engineering/scheduler?' + qs(qs2)))
      .then(() => { msg.ok(t('sv.saved')); reload(); })
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  return html`<form class="card" novalidate onSubmit=${submit}>
    <h2>${t('sv.lim')}</h2>
    <div class="ipgrid">
      ${kLim.map(f => html`<${Num} f=${f} d=${d} set=${set} bad=${bad === f}/>`)}
      <label class="field">${t('sv.grid')}
        <select value=${d.grid_min} onChange=${e => set({ grid_min: +e.target.value })}>
          ${kGrids.map(g => html`<option value=${g}>${g} min</option>`)}</select></label>
      ${kSch.map(f => html`<${Num} f=${f} d=${d} set=${set} bad=${bad === f}/>`)}
    </div>
    <div class="field"><span class="lbl">${t('sv.limit')}</span>
      <${Seg} value=${d.limit_mode} onChange=${v => set({ limit_mode: v })}
        options=${['calendar_day', 'rolling_24h'].map(v => [v, t('sv.limit.' + v)])}/></div>
    <${Save} busy=${busy} dirty=${Object.keys(qd).length + Object.keys(qs2).length > 0}/>
    ${msg.node}
  </form>`;
}

// Container thresholds (defaults of all pumps)
const kThr = [
  { k: 'warn_days', l: 'sv.thr.warn', min: 0, max: 60, u: 'u.days' },
  { k: 'low_days', l: 'sv.thr.low', min: 0, max: 60, u: 'u.days' },
];
const thrOf = e => ({ warn_days: str(e.warn_days), low_days: str(e.low_days) });

function ThresholdCard({ eng, reload }) {
  const [d, setD] = useState(thrOf(eng));
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  useEffect(() => setD(thrOf(eng)), [eng]);
  const q = changed(kThr, d, thrOf(eng));
  const bad = invalid(kThr, d);
  const submit = e => {
    e.preventDefault();
    if (bad) return msg.err(checkText(bad));
    if (parse(d.low_days) > parse(d.warn_days)) return msg.err(t('sv.thr.order'));
    setBusy(true);
    eapi('POST', '/api/v1/engineering/dosing?' + qs(q))
      .then(() => { msg.ok(t('sv.saved')); reload(); reloadPumps(); })
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  return html`<form class="card" novalidate onSubmit=${submit}>
    <h2>${t('sv.thr')}</h2>
    <p class="small mut">${t('sv.thr.h')}</p>
    ${kThr.map(f => html`<${Num} f=${f} d=${d} set=${x => setD(o => ({ ...o, ...x }))} bad=${bad === f}/>`)}
    <${Save} busy=${busy} dirty=${Object.keys(q).length > 0}/>
    ${msg.node}
  </form>`;
}

// ---------------------------------------------------------------------------
// C: tubes and clock module

const kTube = { k: 'tube_life_h', l: 'sv.tube_life', min: 10, max: 65535, u: 'h' };

function TubeCard({ eng, pumps, reload }) {
  const [d, setD] = useState({ tube_life_h: str(eng.tube_life_h) });
  const [hours, setHours] = useState(['', '', '', '', '']);
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  useEffect(() => setD({ tube_life_h: str(eng.tube_life_h) }), [eng]);
  const q = changed([kTube], d, { tube_life_h: str(eng.tube_life_h) });
  const bad = invalid([kTube], d);
  const submit = e => {
    e.preventDefault();
    if (bad) return msg.err(checkText(bad));
    setBusy(true);
    eapi('POST', '/api/v1/engineering/dosing?' + qs(q))
      .then(() => { msg.ok(t('sv.saved')); reload(); reloadPumps(); })
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  const setTube = (n, h) => {
    if (!(h >= 0 && Number.isInteger(h))) return msg.err(checkText({ l: 'sv.tube', min: 0, max: 65535 }));
    setBusy(true);
    eapi('POST', '/api/v1/pumps/tube?' + qs({ pump: n, hours: h }))
      .then(() => { msg.ok(t('sv.tube.done', { n, h })); setHours(o => o.map((x, k) => (k === n - 1 ? '' : x))); reloadPumps(); })
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  return html`<form class="card" novalidate onSubmit=${submit}>
    <h2>${t('sv.tube')}</h2>
    <${Num} f=${kTube} d=${d} set=${x => setD(o => ({ ...o, ...x }))} bad=${!!bad}/>
    <${Save} busy=${busy} dirty=${Object.keys(q).length > 0}/>
    <ul class="eng-list">${(pumps || []).map((p, i) => html`<li>
      <span class="badge" style=${{ '--pc': pumpColor(p.pump) }}>${p.pump}</span>
      <span class="grow">${pumpName(p)}<br/><span class="small mut num">${num(p.tube.hours, 1)} h · ${num(p.tube.percent, 0)} %</span></span>
      <span class="amount"><input type="text" inputmode="numeric" aria-label=${t('sv.tube')} placeholder=${num(Math.floor(p.tube.hours))}
        value=${hours[i]} onInput=${e => { const v = e.target.value; setHours(o => o.map((x, k) => (k === i ? v : x))); }}/><span>h</span></span>
      <button type="button" class="sec" disabled=${busy || hours[i] === ''} onClick=${() => setTube(p.pump, parse(hours[i]))}>${t('sv.tube.set')}</button>
      <button type="button" class="sec" disabled=${busy} onClick=${() => setTube(p.pump, 0)}>${t('sv.tube.reset')}</button>
    </li>`)}</ul>
    ${msg.node}
  </form>`;
}

const kAging = { k: 'aging', l: 'sv.rtc.aging', min: -128, max: 127, u: '' };

function RtcCard({ hw, reload }) {
  const rtc = hw.rtc;
  const [d, setD] = useState({ aging: str(rtc.aging) });
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  useEffect(() => setD({ aging: str(rtc.aging) }), [rtc.aging]);
  const bad = invalid([kAging], d);
  const submit = e => {
    e.preventDefault();
    if (bad) return msg.err(checkText(kAging));
    setBusy(true);
    eapi('POST', '/api/v1/hw/rtc/aging?' + qs({ offset: parse(d.aging) }))
      .then(() => { msg.ok(t('sv.saved')); return reload(); })
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  const tr = rtc.trim || {};
  const setAuto = on => {
    setBusy(true);
    eapi('POST', '/api/v1/hw/rtc/aging?' + qs({ auto: on ? 1 : 0 }))
      .then(() => { msg.ok(t(on ? 'sv.rtc.on' : 'sv.rtc.offd')); return reload(); })
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  const day = utc => new Date(utc * 1000).toLocaleString(getLang(), { dateStyle: 'short', timeStyle: 'short', ...hourCycle() });
  return html`<form class="card" novalidate onSubmit=${submit}>
    <h2>${t('sv.rtc')}</h2>
    ${!rtc.present ? html`<p class="warn">${t('sv.rtc.none')}</p>` : html`
      ${rtc.battery_warning && html`<p class="small warn">${t('sv.rtc.lost')}</p>`}
      <dl class="stats"><dt>${t('sv.rtc.temp')}</dt><dd class="num">${num(rtc.temp_c, 2)} °C</dd>
        <dt>${t('sv.rtc.aging')}</dt><dd class="num">${rtc.aging}</dd>
        ${tr.automatic && html`
          <dt>${t('sv.rtc.since')}</dt><dd class="num">${tr.measuring ? day(tr.since) : html`<span class="mut">${t('sv.rtc.wait')}</span>`}</dd>
          ${tr.offset_ms !== undefined && html`<dt>${t('sv.rtc.off')}</dt><dd class="num">${tr.offset_ms > 0 ? '+' : ''}${num(tr.offset_ms, 0)} ms</dd>`}`}
        ${tr.last && html`<dt>${t('sv.rtc.last')}</dt><dd class="num">${t('sv.rtc.last.v',
          { d: day(tr.last.at), ppm: (tr.last.ppm > 0 ? '+' : '') + num(tr.last.ppm, 2), from: tr.last.from, to: tr.last.to })}</dd>`}</dl>
      <${Toggle} checked=${!!tr.automatic} disabled=${busy} onChange=${setAuto}>${t('sv.rtc.auto')}<//>
      <p class="small mut">${t('sv.rtc.auto.h')}</p>
      <${Num} f=${kAging} d=${d} set=${x => setD(x)} bad=${!!bad} hint=${t('sv.rtc.h')}/>
      <${Save} busy=${busy} dirty=${parse(d.aging) !== rtc.aging}/>`}
    ${msg.node}
  </form>`;
}

function HistCard() {
  const [cur, setCur] = useState(null);  // GET /api/v1/history/settings
  const [d, setD] = useState({ keep_days: '' });
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const take = j => { setCur(j); setD({ keep_days: str(j.keep_days) }); };
  useEffect(() => { api('GET', '/api/v1/history/settings').then(take).catch(e => msg.err(e)); }, []);
  if (!cur) return html`<section class="card"><h2>${t('sv.hist')}</h2>${msg.node}</section>`;
  const f = { k: 'keep_days', l: 'sv.hist.keep', min: cur.min_days, max: cur.max_days, u: 'u.days' };
  const bad = invalid([f], d);
  const submit = e => {
    e.preventDefault();
    if (bad) return msg.err(checkText(f));
    setBusy(true);
    eapi('POST', '/api/v1/history/settings?' + qs({ keep_days: parse(d.keep_days) }))
      .then(j => { take(j); msg.ok(t('sv.saved')); })
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  return html`<form class="card" novalidate onSubmit=${submit}>
    <h2>${t('sv.hist')}</h2>
    <${Num} f=${f} d=${d} set=${x => setD(x)} bad=${!!bad} hint=${t('sv.hist.h')}/>
    <${Save} busy=${busy} dirty=${parse(d.keep_days) !== cur.keep_days}/>
    ${msg.node}
  </form>`;
}

// ---------------------------------------------------------------------------
// D: raw stepper test

function RawCard({ hw, pumps, nominal, reload }) {
  const [n, setN] = useState(1);
  const [d, setD] = useState({ steps: '3200', sps: '1600', dir: 1, force: false });
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const p = hw.pumps[n - 1];
  const c = p.config;
  const fields = [{ k: 'steps', l: 'sv.steps', min: 1, max: 20000000 }, { k: 'sps', l: 'sv.sps', min: 1, max: c.max_sps, u: 'u.sps' }];
  const bad = invalid(fields, d);
  const set = x => setD(o => ({ ...o, ...x }));
  const steps = parse(d.steps), sps = parse(d.sps);
  const start = e => {
    e.preventDefault();
    if (bad) return msg.err(checkText(bad));
    setBusy(true);
    eapi('POST', '/api/v1/hw/pump/run?' + qs({ pump: n, steps: d.dir * steps, sps, force: d.force ? 1 : undefined }))
      .then(() => { msg.clear(); return reload(); })
      // the raw test reports a step frequency, not a dosing speed
      .catch(e => msg.err(e && e.code === 'speed_out_of_range' ? t('sv.sps.range') : e)).finally(() => setBusy(false));
  };
  const stop = mode => eapi('POST', '/api/v1/hw/pump/stop?' + qs({ pump: n, mode })).then(reload).catch(e => msg.err(e));
  return html`<form class="card" novalidate onSubmit=${start}>
    <h2>${t('sv.raw')}</h2>
    <p class="small warn">${t('sv.raw.h')}</p>
    <${PumpPick} n=${n} set=${setN} pumps=${pumps}/>
    <div class="field"><span class="lbl">${t('sv.raw.dir')}</span>
      <${Seg} value=${d.dir} onChange=${v => set({ dir: v })} options=${[[1, t('sv.fwd')], [-1, t('sv.rev')]]}/></div>
    <div class="ipgrid">${fields.map(f => html`<${Num} f=${f} d=${d} set=${set} bad=${bad === f}/>`)}</div>
    ${!bad && c.steps_per_rev > 0 && html`<p class="small mut">${t('sv.raw.calc', {
      rev: num(steps / c.steps_per_rev, 2), ml: num(steps / c.steps_per_rev * nominal, 2), s: num(steps / sps, 1) })}</p>`}
    <label class="check"><input type="checkbox" checked=${d.force} onChange=${e => set({ force: e.target.checked })}/>${t('sv.force')}</label>
    <p class="small num">${p.running
      ? t('sv.raw.run', { pos: num(p.position), tgt: num(p.target), rpm: num(p.rpm, 0), sg: p.sg_result })
      : t('sv.raw.idle', { pos: num(p.position), sg: p.sg_min })}</p>
    <div class="row savebar">
      <button type="submit" disabled=${busy || p.running}>${t('sv.start')}</button>
      <button type="button" class="sec" disabled=${!p.running} onClick=${() => stop('soft')}>${t('sv.stop')}</button>
      <button type="button" class="danger" onClick=${() => stop('hard')}>${t('sv.estop')}</button>
    </div>
    ${msg.node}
  </form>`;
}

// ---------------------------------------------------------------------------
// D: driver diagnosis (status flags, raw TMC2209 registers)

const kFlags = ['short', 'overtemp', 'overtemp_warn', 'temp_over_120', 'open_load_a', 'open_load_b', 'standstill', 'stealth'];
const kBad = ['short', 'overtemp', 'overtemp_warn', 'temp_over_120', 'open_load_a', 'open_load_b'];

function DiagCard({ hw, pumps }) {
  const [n, setN] = useState(1);
  const [regs, setRegs] = useState(null);
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const p = hw.pumps[n - 1];
  const f = p.flags || {};
  const pick = k => { setN(k); setRegs(null); msg.clear(); };
  const read = () => {
    setBusy(true);
    eapi('GET', '/api/v1/hw/tmc?pump=' + n).then(j => setRegs(j.registers))
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  return html`<section class="card">
    <h2>${t('sv.diag')}</h2>
    <p class="small mut">${t('sv.diag.h')}</p>
    <${PumpPick} n=${n} set=${pick} pumps=${pumps}/>
    <dl class="stats">
      ${kFlags.map(k => html`<dt>${t('sv.fl.' + k)}</dt>
        <dd class=${kBad.includes(k) && f[k] ? 'err' : ''}>${t(f[k] ? 'sv.yes' : 'sv.no')}</dd>`)}
      <dt>${t('sv.fl.cs')}</dt><dd class="num">${f.cs_actual ?? '–'}</dd>
      <dt>DRV_STATUS · GSTAT</dt><dd class="mono">${p.drv_status} · ${p.gstat}</dd>
    </dl>
    <button type="button" class="sec" disabled=${busy} onClick=${read}>${t('sv.regs')}</button>
    ${regs && html`<dl class="stats mono" style="margin-top:12px">${Object.entries(regs).map(([k, v]) =>
      html`<dt>${k}</dt><dd>${v === null ? t('sv.regs.none') : v}</dd>`)}</dl>`}
    ${msg.node}
  </section>`;
}

// ---------------------------------------------------------------------------
// D: telemetry

function TelemetryCard({ tel, hw, paused }) {
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const eeTest = () => {
    setBusy(true);
    eapi('POST', '/api/v1/hw/eeprom/test').then(() => msg.ok(t('sv.eeprom.ok')))
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  const rows = [];
  if (tel) {
    const h = tel.heap, n = tel.nvs, s = tel.storage;
    rows.push(['sv.up', fmtUptime(tel.uptime_s)], ['sv.reset', tel.reset_reason],
      ['sv.chip', tel.chip_temp_c === null ? '–' : num(tel.chip_temp_c, 1) + ' °C'],
      ['sv.heap', fmtBytes(h.free) + ' / ' + fmtBytes(h.total) + ' (' + fmtBytes(h.min_free) + ')'],
      ['sv.frag', fmtBytes(h.largest_block) + ' · ' + num(h.fragmentation_pct, 1) + ' %'],
      ['sv.psram', fmtBytes(tel.psram.free) + ' / ' + fmtBytes(tel.psram.total)],
      ['sv.nvs', n.total_entries ? num(n.used_entries) + ' / ' + num(n.total_entries) + ' (' + num(100 * n.used_entries / n.total_entries, 0) + ' %)' : '–'],
      ['sv.fs', s.total ? fmtBytes(s.used) + ' / ' + fmtBytes(s.total) : '–'],
      ['sv.hist', t('sv.hist.v', { d: s.history_days, b: fmtBytes(s.history_bytes) })],
      ['sv.dropped', num(s.history_dropped)], ['sv.tasks', num(tel.tasks)]);
  }
  return html`<section class="card">
    <h2>${t('sv.tel')}</h2>
    ${!tel ? html`<p class="mut">${t('sv.loading')}</p>` : html`<dl class="stats">
      ${rows.map(([k, v]) => html`<dt>${t(k)}</dt><dd class="num">${v}</dd>`)}
      ${hw.uart.map((u, b) => html`<dt>${t('sv.uart', { b: b ? 'B' : 'A' })}</dt>
        <dd class="num">${t('sv.uart.v', { n: num(u.transfers), e: num(u.timeouts + u.crc_errors + u.echo_errors) })}</dd>`)}
    </dl>`}
    ${paused && html`<p class="small mut">${t('sv.paused')}</p>`}
    <div class="sub">
      <div class="row between"><b>${t('sv.eeprom')}</b>
        <button type="button" class="sec" disabled=${busy || !hw.eeprom.present} onClick=${eeTest}>${t('sv.eeprom.run')}</button></div>
      <p class="small mut">${t(hw.eeprom.present ? 'sv.eeprom.h' : 'sv.eeprom.none')}</p>
      ${msg.node}
    </div>
  </section>`;
}

// ---------------------------------------------------------------------------
// D: resets

function Danger({ id, run }) {
  const [ask, setAsk] = useState(false);
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const go = () => {
    setBusy(true);
    run().then(() => { setAsk(false); msg.ok(t(id + '.ok')); })
      .catch(e => msg.err(e)).finally(() => setBusy(false));
  };
  return html`<div class="sub">
    <div class="row between"><b>${t(id)}</b>
      ${!ask && html`<button type="button" class="danger" onClick=${() => setAsk(true)}>${t(id)}</button>`}</div>
    <p class="small mut">${t(id + '.h')}</p>
    ${ask && html`<div class="row">
      <span>${t('sv.dz.sure')}</span>
      <button type="button" class="danger" disabled=${busy} onClick=${go}>${t('sv.dz.yes')}</button>
      <button type="button" class="sec" onClick=${() => setAsk(false)}>${t('sv.dz.no')}</button>
    </div>`}
    ${msg.node}
  </div>`;
}

function DangerCard() {
  return html`<section class="card danger-zone">
    <h2>${t('sv.danger')}</h2>
    <${Danger} id="sv.dz.hist" run=${() => eapi('DELETE', '/api/v1/history')}/>
    <${Danger} id="sv.dz.net" run=${() => eapi('POST', '/api/v1/engineering/reset?what=network')}/>
  </section>`;
}

// ---------------------------------------------------------------------------
// View

const kIdleMs = 60000;  // live polling stops this long after the last input

export function ServiceView() {
  const st = useStore();
  const [auth, setAuth] = useState(null);  // GET /api/v1/auth/status
  const [eng, setEng] = useState(null);
  const [sch, setSch] = useState(null);
  const [hw, setHw] = useState(null);
  const [tel, setTel] = useState(null);
  const [ota, setOta] = useState(null);  // GET /api/v1/ota/settings
  const [err, setErr] = useState(null);
  const [paused, setPaused] = useState(false);
  const active = useRef(Date.now());
  const hwRef = useRef(null);
  hwRef.current = hw;
  const locked = auth && !auth.engineering;

  const loadAuth = () => api('GET', '/api/v1/auth/status').then(setAuth).catch(setErr);
  const loadEng = () => Promise.all([eapi('GET', '/api/v1/engineering/dosing'), eapi('GET', '/api/v1/engineering/scheduler')])
    .then(([e, s]) => { setEng(e); setSch(s); });
  const loadHw = () => eapi('GET', '/api/v1/hw/status').then(setHw);
  const loadTel = () => eapi('GET', '/api/v1/engineering/telemetry').then(setTel);
  const loadOta = () => api('GET', '/api/v1/ota/settings').then(setOta);
  const loadAll = () => Promise.all([loadEng(), loadHw(), loadTel(), loadOta()]).then(() => setErr(null)).catch(e => { if (e.code !== 'engineering_locked') setErr(e); });

  onLocked = () => setAuth(a => ({ ...(a || {}), engineering: false }));
  useEffect(() => {
    loadAuth().then(() => loadAll());
    const mark = () => { active.current = Date.now(); };
    const evs = ['pointerdown', 'pointermove', 'keydown', 'wheel', 'touchstart'];
    evs.forEach(e => addEventListener(e, mark, { passive: true }));
    let tick = 0;
    const id = setInterval(() => {
      tick++;
      const idle = document.hidden || Date.now() - active.current > kIdleMs;
      setPaused(idle);
      if (tick % 15 === 0) loadAuth();  // remaining time; not an engineering request
      if (idle) return;
      const running = hwRef.current && hwRef.current.pumps.some(p => p.running);
      if (running || tick % 3 === 0) loadHw().catch(() => {});
      if (tick % 5 === 0) loadTel().catch(() => {});
    }, 1000);
    return () => { clearInterval(id); evs.forEach(e => removeEventListener(e, mark)); onLocked = () => {}; };
  }, []);

  const lock = () => lockEngineering().then(loadAuth).catch(setErr);
  const head = html`<div class="page-head"><h1>${t('sv.title')}</h1><span class="spacer"></span>
    ${auth && auth.engineering && html`<span class="chip svc">${t('sv.left', { m: Math.max(1, Math.ceil(auth.engineering_left_s / 60)) })}</span>
      <button type="button" class="sec" onClick=${lock}>${t('sv.lock')}</button>`}</div>`;

  if (!auth) return html`${head}${err ? html`<p class="msg err">${errText(err)}</p>` : html`<p class="mut">${t('sv.loading')}</p>`}`;
  if (locked) {
    return html`${head}<section class="card eng-locked">
      <p>${t('sv.locked')}</p>
      <${EngUnlock} autoFocus onDone=${() => loadAuth().then(loadAll)}/>
    </section>`;
  }
  if (!eng || !sch || !hw) return html`${head}${err ? html`<p class="msg err">${errText(err)}</p>` : html`<p class="mut">${t('sv.loading')}</p>`}`;

  const pumps = st.pumps;
  const reloadEng = () => loadEng().catch(() => {});
  const reloadHw = () => loadHw().catch(() => {});
  return html`${head}
    ${err && html`<p class="msg err">${errText(err)}</p>`}
    <h2 class="eng-sec">${t('sv.a')}</h2>
    <div class="grid settings">
      <${PhysCard} eng=${eng} spr=${hw.pumps[0].config.steps_per_rev} reload=${reloadEng}/>
      <${DriverCard} hw=${hw} pumps=${pumps} reload=${reloadHw}/>
    </div>
    <h2 class="eng-sec">${t('sv.b')}</h2>
    <div class="grid settings">
      <${LimitsCard} eng=${eng} sch=${sch} reload=${reloadEng}/>
      <${ThresholdCard} eng=${eng} reload=${reloadEng}/>
    </div>
    <h2 class="eng-sec">${t('sv.c')}</h2>
    <div class="grid settings">
      <${TubeCard} eng=${eng} pumps=${pumps} reload=${reloadEng}/>
      <${RtcCard} hw=${hw} reload=${reloadHw}/>
      <${HistCard}/>
    </div>
    <h2 class="eng-sec">${t('sv.d')}</h2>
    <div class="grid settings">
      <${RawCard} hw=${hw} pumps=${pumps} nominal=${eng.nominal_ml_per_rev} reload=${reloadHw}/>
      <${DiagCard} hw=${hw} pumps=${pumps}/>
      <${TelemetryCard} tel=${tel} hw=${hw} paused=${paused}/>
      <${DangerCard}/>
    </div>
    <h2 class="eng-sec">${t('sv.e')}</h2>
    <div class="grid settings">
      ${ota && html`<${UpdateSourceCard} opts=${ota} setOpts=${setOta} call=${eapi}/>
        <${UrlUpdateCard} busyPumps=${pumps.some(p => p.activity !== 'idle')} opts=${ota} call=${eapi}/>`}
    </div>
    <h2 class="eng-sec">${t('sv.f')}</h2>
    <div class="grid settings"><${BackupSection}/></div>`;
}
