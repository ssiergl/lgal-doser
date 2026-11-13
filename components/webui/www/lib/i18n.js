// Translations. English is the base: views add their strings with add().
// Every other language is one file lang/<code>.json with all keys of the
// web UI, loaded only when that language is used; a missing key falls back
// to English. New language: the file, an entry here and in the firmware
// (settings::kLanguages). The language is a device setting
// (POST /api/v1/system/language), cached per browser so the first paint is
// already right (app.js waits for ready()).
import { hub, load, save } from './hub.js';

export const languages = [['en', 'English'], ['de', 'Deutsch'], ['fr', 'Français'], ['es', 'Español'], ['it', 'Italiano']];
const known = l => languages.some(([c]) => c === l);
const dict = { en: {} };
const changes = hub();
let lang = load('lgal.lang', (navigator.language || 'en').slice(0, 2));
if (!known(lang)) lang = 'en';
document.documentElement.lang = lang;

// Loads lang/<l>.json once; false if it cannot be had (then English stays)
async function loadDict(l) {
  if (dict[l]) return true;
  try {
    const r = await fetch('/ui/lang/' + l + '.json');
    if (!r.ok) return false;
    dict[l] = await r.json();
    return true;
  } catch (e) {
    return false;
  }
}

// Before the first paint: the dictionary of the cached language
export async function ready() {
  if (!(await loadDict(lang))) lang = 'en';
  document.documentElement.lang = lang;
}

export function add(d) {
  Object.assign(dict.en, d.en);
}

export function t(key, vars) {
  let s = dict[lang]?.[key] ?? dict.en[key] ?? key;
  if (vars) s = s.replace(/\{(\w+)\}/g, (m, k) => (vars[k] ?? m));
  return s;
}

export const getLang = () => lang;
export const useLang = () => { changes.use(); return lang; };

export async function setLang(l, persist = true) {
  if (!known(l) || l === lang || !(await loadDict(l))) return;
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
  const txt = dict[lang]?.[key] ?? dict.en[key];
  if (txt) return t(key, e.vars);
  return e.message || String(e.code || e);
}

// Decimal separator of the UI language ("," or ".")
export const decimalSep = () => (1.5).toLocaleString(lang).charAt(1);

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
});
