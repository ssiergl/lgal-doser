// Translations. English is the base; views add their strings with add().
// The language is a device setting (POST /api/v1/system/language), cached
// per browser so the first paint is already right.
import { hub, load, save } from './hub.js';

export const languages = [['en', 'English'], ['de', 'Deutsch']];
const dict = { en: {}, de: {} };
const changes = hub();
let lang = load('lgal.lang', (navigator.language || 'en').slice(0, 2));
if (!dict[lang]) lang = 'en';
document.documentElement.lang = lang;

export function add(d) {
  for (const l in d) Object.assign(dict[l] || (dict[l] = {}), d[l]);
}

export function t(key, vars) {
  let s = dict[lang][key] ?? dict.en[key] ?? key;
  if (vars) s = s.replace(/\{(\w+)\}/g, (m, k) => (vars[k] ?? m));
  return s;
}

export const getLang = () => lang;
export const useLang = () => { changes.use(); return lang; };

export function setLang(l, persist = true) {
  if (!dict[l] || l === lang) return;
  lang = l;
  save('lgal.lang', l);
  document.documentElement.lang = l;
  changes.emit();
  if (persist) fetch('/api/v1/system/language?lang=' + l, { method: 'POST' }).catch(() => {});
}

export async function initLang() {
  try {
    const j = await (await fetch('/api/v1/system/language', { cache: 'no-store' })).json();
    if (j.language) setLang(j.language, false);
  } catch (e) { /* keep the cached language */ }
}

// Error (ApiError or anything else) -> readable text
export function errText(e) {
  if (!e) return '';
  const key = 'err.' + e.code;
  const txt = dict[lang][key] ?? dict.en[key];
  if (txt) return t(key, e.vars);
  return e.message || String(e.code || e);
}

// Numbers in the UI language
export function num(v, digits = 0) {
  if (v === null || v === undefined || Number.isNaN(+v)) return '–';
  return Number(v).toLocaleString(lang, { minimumFractionDigits: digits, maximumFractionDigits: digits });
}

add({
  en: {
    'err.network': 'No connection to the device',
    'err.invalid_param': 'Invalid input',
    'err.pump_busy': 'Pump is busy',
    'err.pump_disabled': 'Pump is disabled',
    'err.pump_manual_only': 'Pump is set to manual only',
    'err.locked': 'Pumps are locked (update in progress)',
    'err.daily_limit': 'Daily limit would be exceeded',
    'err.time_invalid': 'No valid time',
    'err.driver_unavailable': 'Driver not reachable',
    'err.dose_too_small': 'Amount below the minimum dose',
    'err.dose_too_large': 'Amount too large',
    'err.not_priming': 'Priming has already stopped',
    'err.collision_lock': 'Lock time running',
    'err.storage_error': 'Could not save',
    'err.unauthorized': 'Please log in again',
    'err.engineering_locked': 'Service area is locked',
    'err.no_calibration_run': 'No calibration run to apply',
    'err.measured_out_of_range': 'Measured amount is implausible – please measure again',
    'err.speed_out_of_range': 'Speed not possible with this calibration',
    'err.queue_full': 'Too many waiting doses',
    'err.internal_error': 'Internal error',
  },
  de: {
    'err.network': 'Keine Verbindung zum Gerät',
    'err.invalid_param': 'Ungültige Eingabe',
    'err.pump_busy': 'Pumpe ist beschäftigt',
    'err.pump_disabled': 'Pumpe ist deaktiviert',
    'err.pump_manual_only': 'Pumpe steht auf „nur manuell“',
    'err.locked': 'Pumpen gesperrt (Update läuft)',
    'err.daily_limit': 'Tageslimit würde überschritten',
    'err.time_invalid': 'Keine gültige Uhrzeit',
    'err.driver_unavailable': 'Treiber nicht erreichbar',
    'err.dose_too_small': 'Menge unter der kleinsten Dosis',
    'err.dose_too_large': 'Menge zu groß',
    'err.not_priming': 'Entlüften ist bereits beendet',
    'err.collision_lock': 'Sperrzeit läuft',
    'err.storage_error': 'Speichern fehlgeschlagen',
    'err.unauthorized': 'Bitte erneut anmelden',
    'err.engineering_locked': 'Servicebereich ist gesperrt',
    'err.no_calibration_run': 'Kein Kalibrierlauf zum Übernehmen',
    'err.measured_out_of_range': 'Gemessene Menge unplausibel – bitte erneut messen',
    'err.speed_out_of_range': 'Geschwindigkeit mit dieser Kalibrierung nicht möglich',
    'err.queue_full': 'Zu viele wartende Dosen',
    'err.internal_error': 'Interner Fehler',
  },
});
