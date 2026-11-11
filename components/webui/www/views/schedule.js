// Schedule card of the pump detail view: today's doses of this pump on a
// 24 h bar and the editor of its plan. Every pump has exactly one plan: "spread" (daily amount in N doses between two
// times) or "times" (single times, each with its own amount).
// Simple view: spread, every day. Pro view: both kinds, days (daily,
// weekdays, every N days), pause and the behaviour after a power loss.
import { useEffect, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, getLang, num, t } from '../lib/i18n.js';
import { hm, hourLabel, setMode } from '../lib/prefs.js';
import { pumpName } from '../lib/pumps.js';
import { reloadSched } from '../lib/store.js';
import { Amount, Seg, ask, parseNum, useMsg } from '../lib/ui.js';

const WD = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'];
const kMaxTimes = 24;

add({
  en: {
    'sched.title': 'Schedule', 'sc.loading': 'Loading …',
    'sc.export': 'Export', 'sc.import': 'Import', 'sc.io.hint': 'Save the schedule of this pump as a file or load one.',
    'sc.import.ask': 'Use the schedule of “{name}” from {d} for pump {n}? The current schedule is replaced.',
    'sc.import.ok': 'Schedule imported', 'sc.import.bad': 'This file is not a schedule export.',
    'sc.import.newer': 'This file is from a newer firmware and cannot be read.',
    'sc.none': 'No schedule', 'sc.none.hint': 'This pump only doses when you start it by hand.',
    'sc.daily': 'Daily amount', 'sc.count': 'Doses per day', 'sc.from': 'From', 'sc.to': 'To', 'sc.at': 'Time',
    'sc.doses': 'doses', 'sc.every': 'every {m} min', 'sc.everyh': 'every {h} h', 'sc.each': '{n} × {ml} ml',
    'sc.save': 'Save', 'sc.discard': 'Discard', 'sc.saved': 'Schedule saved',
    'sc.create': 'Create schedule',
    'sc.pro.made': 'This schedule was set up in the pro view and stays active as it is.',
    'sc.pro.edit': 'Edit in pro view', 'sc.replace': 'Replace with a simple schedule',
    'sc.mode.manual_only': 'The pump is set to “manual only” – the schedule is paused.',
    'sc.mode.disabled': 'The pump is off – the schedule is paused.',
    'sc.limit': 'Daily limit {l} ml – planned {p} ml', 'sc.unsaved': 'Unsaved changes',
    'sc.kind': 'Plan', 'sc.kind.spread': 'Spread', 'sc.kind.times': 'Single times',
    'sc.kind.hint.spread': 'A daily amount, split evenly over several doses.',
    'sc.kind.hint.times': 'Fixed times, each with its own amount.',

    'sc.ml.total': 'Amount per day', 'sc.t.add': 'Add time', 'sc.t.del': 'Remove time', 'sc.t.sum': '{n} times · {ml} ml per day',
    'sc.days': 'Days', 'sc.d.daily': 'Daily', 'sc.d.weekdays': 'Weekdays', 'sc.d.interval': 'Every N days',
    'sc.d.every': 'Every', 'sc.d.days': 'days', 'sc.d.anchor': 'starting', 'sc.d.everyN': 'every {n} days',
    'sc.wd.mon': 'Mon', 'sc.wd.tue': 'Tue', 'sc.wd.wed': 'Wed', 'sc.wd.thu': 'Thu', 'sc.wd.fri': 'Fri', 'sc.wd.sat': 'Sat', 'sc.wd.sun': 'Sun',
    'sc.power': 'After a power loss', 'sc.pl.skip_missed': 'Skip missed doses', 'sc.pl.catch_up_window': 'Catch up',
    'sc.pl.hint.skip_missed': 'Doses missed while the device was off are left out.',
    'sc.pl.hint.catch_up_window': 'Missed doses are made up if they are at most this old:',
    'sc.total': 'Planned today: {ml} ml',
    'sc.cf.overlap': 'Pump {p1} {t1} overlaps with pump {p2} {t2}{d}',
    'sc.cf.collision_lock': 'Pump {p1} {t1} and pump {p2} {t2}{d}: lock time {g} min',
    'sc.cf.next': ' (next day)', 'sc.err.entry': '{time}: {e}',
    'err.invalid_time': 'Invalid time', 'err.off_grid': 'Times must be on the {g}-minute grid',
    'err.invalid_range': 'End time must be after the start time', 'err.invalid_count': 'Invalid number of doses',
    'err.slots_too_dense': 'Too many doses for this time window', 'err.invalid_days': 'Choose at least one day',
    'err.invalid_catch_up': 'Catch-up window: 1 to 1440 minutes', 'err.too_many_slots': 'Too many doses per day',
    'err.duplicate_time': 'Each time only once', 'err.invalid_kind': 'Unknown kind of plan',
    'err.exceeds_daily_limit': 'The planned amount exceeds the daily limit', 'err.conflict': 'Doses collide',
  },
  de: {
    'sched.title': 'Zeitplan', 'sc.loading': 'Lädt …',
    'sc.export': 'Exportieren', 'sc.import': 'Importieren', 'sc.io.hint': 'Zeitplan dieser Pumpe als Datei sichern oder laden.',
    'sc.import.ask': 'Zeitplan von „{name}“ vom {d} für Pumpe {n} übernehmen? Der aktuelle Zeitplan wird ersetzt.',
    'sc.import.ok': 'Zeitplan importiert', 'sc.import.bad': 'Diese Datei ist kein Zeitplan-Export.',
    'sc.import.newer': 'Diese Datei stammt von einer neueren Firmware und kann nicht gelesen werden.',
    'sc.none': 'Kein Zeitplan', 'sc.none.hint': 'Diese Pumpe dosiert nur, wenn du sie von Hand startest.',
    'sc.daily': 'Tagesmenge', 'sc.count': 'Dosen pro Tag', 'sc.from': 'Von', 'sc.to': 'Bis', 'sc.at': 'Uhrzeit',
    'sc.doses': 'Dosen', 'sc.every': 'alle {m} Min.', 'sc.everyh': 'alle {h} Std.', 'sc.each': '{n} × {ml} ml',
    'sc.save': 'Speichern', 'sc.discard': 'Verwerfen', 'sc.saved': 'Zeitplan gespeichert',
    'sc.create': 'Zeitplan anlegen',
    'sc.pro.made': 'Dieser Zeitplan wurde in der Profi-Ansicht eingerichtet und bleibt so aktiv.',
    'sc.pro.edit': 'In der Profi-Ansicht bearbeiten', 'sc.replace': 'Durch einfachen Zeitplan ersetzen',
    'sc.mode.manual_only': 'Die Pumpe steht auf „Nur manuell“ – der Zeitplan ruht.',
    'sc.mode.disabled': 'Die Pumpe ist aus – der Zeitplan ruht.',
    'sc.limit': 'Tageslimit {l} ml – geplant {p} ml', 'sc.unsaved': 'Ungespeicherte Änderungen',
    'sc.kind': 'Plan', 'sc.kind.spread': 'Verteilt', 'sc.kind.times': 'Einzelzeiten',
    'sc.kind.hint.spread': 'Eine Tagesmenge, gleichmäßig auf mehrere Dosen verteilt.',
    'sc.kind.hint.times': 'Feste Uhrzeiten, jede mit eigener Menge.',

    'sc.ml.total': 'Menge pro Tag', 'sc.t.add': 'Uhrzeit hinzufügen', 'sc.t.del': 'Uhrzeit entfernen', 'sc.t.sum': '{n} Uhrzeiten · {ml} ml pro Tag',
    'sc.days': 'Tage', 'sc.d.daily': 'Täglich', 'sc.d.weekdays': 'Wochentage', 'sc.d.interval': 'Alle N Tage',
    'sc.d.every': 'Alle', 'sc.d.days': 'Tage', 'sc.d.anchor': 'ab', 'sc.d.everyN': 'alle {n} Tage',
    'sc.wd.mon': 'Mo', 'sc.wd.tue': 'Di', 'sc.wd.wed': 'Mi', 'sc.wd.thu': 'Do', 'sc.wd.fri': 'Fr', 'sc.wd.sat': 'Sa', 'sc.wd.sun': 'So',
    'sc.power': 'Nach einem Stromausfall', 'sc.pl.skip_missed': 'Verpasste auslassen', 'sc.pl.catch_up_window': 'Nachholen',
    'sc.pl.hint.skip_missed': 'Dosen, die während des Ausfalls fällig waren, entfallen.',
    'sc.pl.hint.catch_up_window': 'Verpasste Dosen werden nachgeholt, wenn sie höchstens so alt sind:',
    'sc.total': 'Heute geplant: {ml} ml',
    'sc.cf.overlap': 'Pumpe {p1} {t1} überschneidet sich mit Pumpe {p2} {t2}{d}',
    'sc.cf.collision_lock': 'Pumpe {p1} {t1} und Pumpe {p2} {t2}{d}: Sperrzeit {g} Min.',
    'sc.cf.next': ' (Folgetag)', 'sc.err.entry': '{time}: {e}',
    'err.invalid_time': 'Ungültige Uhrzeit', 'err.off_grid': 'Uhrzeiten müssen im {g}-Minuten-Raster liegen',
    'err.invalid_range': 'Ende muss nach dem Start liegen', 'err.invalid_count': 'Ungültige Anzahl Dosen',
    'err.slots_too_dense': 'Zu viele Dosen für dieses Zeitfenster', 'err.invalid_days': 'Mindestens einen Tag wählen',
    'err.invalid_catch_up': 'Nachholfenster: 1 bis 1440 Minuten', 'err.too_many_slots': 'Zu viele Dosen pro Tag',
    'err.duplicate_time': 'Jede Uhrzeit nur einmal', 'err.invalid_kind': 'Unbekannte Planart',
    'err.exceeds_daily_limit': 'Die geplante Menge überschreitet das Tageslimit', 'err.conflict': 'Dosen kollidieren',
  },
});

// ---------------------------------------------------------------------------
// Time and day helpers (same rules as schedule_core.cpp)

const toMin = s => { const m = /^(\d{1,2}):(\d{2})$/.exec(s || ''); return m && +m[1] < 24 && +m[2] < 60 ? +m[1] * 60 + +m[2] : NaN; };
const hhmm = m => String(Math.floor(m / 60)).padStart(2, '0') + ':' + String(m % 60).padStart(2, '0');
const dayNum = d => Math.floor(Date.UTC(+d.slice(0, 4), +d.slice(5, 7) - 1, +d.slice(8, 10)) / 864e5);
const weekday = day => (((day % 7) + 7 + 3) % 7);  // 0 = Monday
const ul = ml => Math.round(ml * 1000);
const text = v => (v === undefined || v === null ? '' : String(v).replace('.', getLang() === 'de' ? ',' : '.'));

function activeOn(days, day) {
  if (days.mode === 'weekdays') return days.weekdays.includes(WD[weekday(day)]);
  if (days.mode === 'interval') {
    const n = +days.every;
    return n > 0 && /^\d{4}-\d\d-\d\d$/.test(days.anchor || '') && (((day - dayNum(days.anchor)) % n) + n) % n === 0;
  }
  return true;
}

// Single times of a draft, sorted by time as the device stores them
const sortedTimes = d => d.times.map(x => ({ time: x.time, min: toMin(x.time), ml: parseNum(x.ml) }))
  .sort((a, b) => (Number.isNaN(a.min) ? 1e4 : a.min) - (Number.isNaN(b.min) ? 1e4 : b.min));

// Draft -> {slots: [{min, ul}], err, entry (time text of the faulty entry)}
function expand(d, grid) {
  const fail = (err, entry) => ({ slots: [], err, entry });
  const days = d.days;
  if (days.mode === 'weekdays' && !days.weekdays.length) return fail('invalid_days');
  if (days.mode === 'interval' && !(+days.every >= 1 && +days.every <= 30 && days.anchor)) return fail('invalid_days');
  if (d.kind === 'times') {
    const list = sortedTimes(d);
    if (!list.length || list.length > kMaxTimes) return fail('invalid_count');
    const slots = [];
    for (let i = 0; i < list.length; ++i) {
      const x = list[i];
      if (Number.isNaN(x.min)) return fail('invalid_time', x.time || '–');
      if (x.min % grid) return fail('off_grid', x.time);
      if (i > 0 && x.min === list[i - 1].min) return fail('duplicate_time', x.time);
      if (!(x.ml > 0)) return fail('invalid_param', x.time);
      slots.push({ min: x.min, ul: ul(x.ml) });
    }
    return { slots };
  }
  const start = toMin(d.start), end = toMin(d.end), n = parseInt(d.count, 10), total = ul(parseNum(d.ml));
  if (Number.isNaN(start)) return fail('invalid_time');
  if (start % grid) return fail('off_grid');
  if (!(total > 0)) return fail('invalid_param');
  if (!(n >= 1 && n <= 96)) return fail('invalid_count');
  if (n > 1) {
    if (Number.isNaN(end)) return fail('invalid_time');
    if (end % grid) return fail('off_grid');
    if (end <= start) return fail('invalid_range');
  }
  const base = Math.floor(total / n), rest = total % n, span = n > 1 ? end - start : 0;
  const slots = [];
  for (let i = 0; i < n; ++i) {
    const min = n > 1 ? start + Math.floor((2 * i * span + (n - 1) * grid) / (2 * (n - 1) * grid)) * grid : start;
    if (i > 0 && min <= slots[i - 1].min) return fail('slots_too_dense');
    slots.push({ min, ul: base + (i < rest ? 1 : 0) });
  }
  return { slots };
}

// Slots of a draft on a day
const daySlots = (d, day, grid) => (activeOn(d.days, day) ? expand(d, grid).slots : []);

// ---------------------------------------------------------------------------
// Draft (form values as text) <-> device JSON

const newRow = (time = '08:00') => ({ time, ml: '' });

function draftOf(s) {
  const d = s.days || { mode: 'daily' };
  return {
    kind: s.kind === 'times' ? 'times' : 'spread',  // without a plan the form starts as spread
    start: s.start || '08:00', end: s.end || '20:00', count: String(s.count || 4), ml: text(s.ml),
    times: (s.times || []).map(x => ({ time: x.time, ml: text(x.ml) })),
    days: { mode: d.mode, weekdays: d.weekdays || [...WD], every: String(d.every || 2), anchor: d.anchor || '' },
    power_loss: s.power_loss || 'skip_missed', catch_up: String(s.catch_up_min ?? 60),
  };
}

function bodyOf(d) {
  const common = { power_loss: d.power_loss, catch_up_min: parseInt(d.catch_up, 10) || 0 };
  const days = d.days.mode === 'weekdays' ? { mode: 'weekdays', weekdays: WD.filter(w => d.days.weekdays.includes(w)) }
    : d.days.mode === 'interval' ? { mode: 'interval', every: parseInt(d.days.every, 10), anchor: d.days.anchor }
      : { mode: 'daily' };
  const head = { kind: d.kind };
  if (d.kind === 'times') return { ...head, times: sortedTimes(d).map(x => ({ time: x.time, ml: x.ml })), days, ...common };
  const count = parseInt(d.count, 10);
  return { ...head, start: d.start, ...(count > 1 ? { end: d.end } : {}), count, ml: parseNum(d.ml), days, ...common };
}

// Simple form of a schedule: {ml, count, start, end}, {} without a plan, null if not representable
function simpleOf(s) {
  if (s.kind === 'none') return {};
  if (s.days.mode !== 'daily') return null;
  if (s.kind === 'spread') return { ml: s.ml, count: s.count, start: s.start, end: s.end };
  return s.times.length === 1 ? { ml: s.times[0].ml, count: 1, start: s.times[0].time, end: '20:00' } : null;
}

function daysText(days) {
  if (days.mode === 'weekdays') return days.weekdays.length === 7 ? t('sc.d.daily') : WD.filter(w => days.weekdays.includes(w)).map(w => t('sc.wd.' + w)).join(', ');
  if (days.mode === 'interval') return t('sc.d.everyN', { n: days.every });
  return t('sc.d.daily');
}

// One-line summary lines of a saved plan
function planLines(s) {
  const tail = daysText(s.days);
  if (s.kind === 'spread') {
    return [`${t('sc.each', { n: s.count, ml: num(s.ml / s.count, 2) })} · ${s.count > 1 ? s.start + '–' + s.end : s.start} (${num(s.ml, 1)} ml)`, tail];
  }
  return [s.times.map(x => `${x.time} · ${num(x.ml, 2)} ml`).join(', '), tail];
}

export function errorText(e, grid, body) {
  if (!e) return '';
  const info = e.body && e.body.error;
  if (e.code === 'conflict' && info && info.conflicts) {
    return info.conflicts.map(c => t('sc.cf.' + (c.code === 'collision_lock' ? 'collision_lock' : 'overlap'), {
      p1: c.first.pump, t1: hm(c.first.time), p2: c.second.pump, t2: hm(c.second.time), g: c.gap_min,
      d: c.next_day ? t('sc.cf.next') : '',
    })).join('\n');
  }
  const base = errText({ code: e.code || 'invalid_param', message: e.message, vars: { g: grid } });
  const entry = info && info.entry;  // 1-based, in the sorted order of the body sent
  const time = entry && body && body.times && body.times[entry - 1] && body.times[entry - 1].time;
  return time ? t('sc.err.entry', { time: hm(time), e: base }) : base;
}

const localError = (ex, grid) => {
  const base = t('err.' + ex.err, { g: grid });
  return ex.entry ? t('sc.err.entry', { time: hm(ex.entry), e: base }) : base;
};

// ---------------------------------------------------------------------------
// Today's doses of this pump on a 24 h bar (live while editing in pro)

function DayBar({ slots, nowMin }) {
  const total = slots.reduce((a, x) => a + x.ul, 0);
  return html`<div class="daybar">
    <div class="tl-axis">${[0, 6, 12, 18, 24].map(h => html`<span style=${{ left: (h / 24) * 100 + '%' }}>${hourLabel(h)}</span>`)}</div>
    <div class="tl-track">
      ${nowMin !== null && html`<i class="tl-now" style=${{ left: (nowMin / 1440) * 100 + '%' }}></i>`}
      ${slots.map(x => html`<i class="tl-dot" title=${hm(hhmm(x.min)) + ' · ' + num(x.ul / 1000, 2) + ' ml'}
        style=${{ left: (x.min / 1440) * 100 + '%' }}></i>`)}
    </div>
    <span class="small mut">${t('sc.total', { ml: num(total / 1000, 2) })}</span>
  </div>`;
}

// ---------------------------------------------------------------------------
// Shared form pieces

function TimeInput({ value, onInput, grid }) {
  return html`<input type="time" step=${grid * 60} value=${value} onInput=${e => onInput(e.target.value)}/>`;
}

function Preview({ slots, err, grid }) {
  if (err) return html`<p class="msg err">${t('err.' + err, { g: grid })}</p>`;
  if (!slots.length) return null;
  const gap = slots.length > 1 ? slots[1].min - slots[0].min : 0;
  const even = slots.every((s, i) => i === 0 || s.min - slots[i - 1].min === gap);
  return html`<div class="preview small">
    <span class="mut">${t('sc.each', { n: slots.length, ml: num(slots[0].ul / 1000, 2) })}${even && gap ? ' · ' + (gap % 60 ? t('sc.every', { m: gap }) : t('sc.everyh', { h: gap / 60 })) : ''}</span>
    <div class="times">${slots.map(s => html`<span class="chip">${hm(hhmm(s.min))}</span>`)}</div>
  </div>`;
}

function useSave(pump, onSaved) {
  const msg = useMsg();
  const [busy, setBusy] = useState(false);
  const save = (body, okText, grid) => {
    setBusy(true);
    return api('PUT', '/api/v1/pumps/schedule?pump=' + pump, body)
      .then(j => { msg.ok(okText); onSaved(j); reloadSched(); })
      .catch(e => msg.err(errorText(e, grid, body)))
      .finally(() => setBusy(false));
  };
  return { msg, busy, save };
}

// ---------------------------------------------------------------------------
// Simple view: daily amount in N doses, every day

function SimpleEditor({ pump, s, onSaved, limit }) {
  const grid = s.grid_min || 5;
  const simple = simpleOf(s);
  const [replace, setReplace] = useState(false);
  const init = () => {
    const sm = simple || {};
    const sl = s.today ? s.today.slots : [];
    return {
      ml: text(sm.ml ?? (s.today && s.today.ml) ?? ''), count: String(sm.count ?? (sl.length || 1)),
      start: sm.start ?? (sl.length ? sl[0].time : '08:00'), end: sm.end ?? (sl.length > 1 ? sl[sl.length - 1].time : '20:00'),
    };
  };
  const [f, setF] = useState(init);
  useEffect(() => { setF(init()); setReplace(false); }, [s]);
  const { msg, busy, save } = useSave(pump, onSaved);
  const set = patch => setF(prev => ({ ...prev, ...patch }));

  if (simple === null && !replace) {
    return html`<div>
      <p class="prohint small">${t('sc.pro.made')}</p>
      <ul class="plist">${planLines(s).map(l => html`<li>${l}</li>`)}</ul>
      <div class="row"><button onClick=${() => setMode('pro')}>${t('sc.pro.edit')}</button>
        <button class="sec" onClick=${() => setReplace(true)}>${t('sc.replace')}</button></div>
    </div>`;
  }
  const draft = {
    kind: 'spread', start: f.start, end: f.end, count: f.count, ml: f.ml, times: [],
    days: { mode: 'daily', weekdays: [...WD], every: '2', anchor: '' },
    power_loss: s.power_loss, catch_up: String(s.catch_up_min),
  };
  const typing = Number.isNaN(parseNum(f.ml)) || !(parseInt(f.count, 10) >= 1);
  const ex = typing ? { slots: [] } : expand(draft, grid);
  const count = parseInt(f.count, 10);
  const submit = e => {
    e.preventDefault();
    if (ex.err || !ex.slots.length) { msg.err(t('err.' + (ex.err || 'invalid_param'), { g: grid })); return; }
    save(bodyOf(draft), t('sc.saved'), grid);
  };
  const empty = s.kind === 'none';
  const planned = ex.slots.reduce((a, x) => a + x.ul, 0) / 1000;
  return html`<div>
    ${empty && html`<p class="small mut">${t('sc.none')}. ${t('sc.none.hint')}</p>`}
    <form novalidate onSubmit=${submit}>
      <div class="form-row">
        <label>${t('sc.daily')}<${Amount} value=${f.ml} onInput=${v => set({ ml: v })}/></label>
        <label>${t('sc.count')}<${Amount} value=${f.count} onInput=${v => set({ count: v })} unit=${t('sc.doses')}/></label>
      </div>
      <div class="form-row">
        <label>${count > 1 ? t('sc.from') : t('sc.at')}<${TimeInput} value=${f.start} grid=${grid} onInput=${v => set({ start: v })}/></label>
        ${count > 1 && html`<label>${t('sc.to')}<${TimeInput} value=${f.end} grid=${grid} onInput=${v => set({ end: v })}/></label>`}
      </div>
      <${Preview} slots=${ex.slots} err=${ex.err} grid=${grid}/>
      ${limit > 0 && planned > limit && html`<p class="msg err">${t('sc.limit', { l: num(limit, 1), p: num(planned, 1) })}</p>`}
      <div class="row">
        <button type="submit" disabled=${busy}>${empty ? t('sc.create') : t('sc.save')}</button>
        ${replace && html`<button type="button" class="sec" onClick=${() => setReplace(false)}>${t('sc.discard')}</button>`}
      </div>
    </form>
    ${msg.node}
  </div>`;
}

// ---------------------------------------------------------------------------
// Pro view: the whole plan of one pump

function TimesList({ d, grid, set }) {
  const setRow = (i, patch) => set({ times: d.times.map((x, k) => (k === i ? { ...x, ...patch } : x)) });
  const addRow = () => {
    const last = d.times.length ? toMin(d.times[d.times.length - 1].time) : NaN;
    const next = Number.isNaN(last) ? '08:00' : hhmm(Math.min(last + 60, 1440 - grid) - (Math.min(last + 60, 1440 - grid) % grid));
    set({ times: [...d.times, newRow(next)] });
  };
  const sum = d.times.reduce((a, x) => a + (parseNum(x.ml) > 0 ? ul(parseNum(x.ml)) : 0), 0);
  return html`<div class="tlist">
    ${d.times.map((x, i) => html`<div class="trow" key=${i}>
      <${TimeInput} value=${x.time} grid=${grid} onInput=${v => setRow(i, { time: v })}/>
      <${Amount} value=${x.ml} onInput=${v => setRow(i, { ml: v })}/>
      <button type="button" class="ghost" title=${t('sc.t.del')} aria-label=${t('sc.t.del')}
        onClick=${() => set({ times: d.times.filter((_, k) => k !== i) })}>✕</button>
    </div>`)}
    <div class="row">
      <button type="button" class="sec" onClick=${addRow} disabled=${d.times.length >= kMaxTimes}>+ ${t('sc.t.add')}</button>
      ${d.times.length > 0 && html`<span class="mut small">${t('sc.t.sum', { n: d.times.length, ml: num(sum / 1000, 2) })}</span>`}
    </div>
  </div>`;
}

function ProEditor({ pump, s, draft: d, setDraft, onSaved, today, limit }) {
  const grid = s.grid_min || 5;
  const { msg, busy, save } = useSave(pump, onSaved);
  const set = patch => setDraft(prev => ({ ...prev, ...patch }));
  const setDays = patch => setDraft(prev => ({ ...prev, days: { ...prev.days, ...patch } }));
  const dirty = JSON.stringify(bodyOf(d)) !== JSON.stringify(bodyOf(draftOf(s)));
  const ex = expand(d, grid);
  const planned = today ? daySlots(d, dayNum(today), grid).reduce((a, x) => a + x.ul, 0) / 1000 : 0;
  // Spread -> single times: start from the doses the spread plan gives
  const fromSpread = () => {
    const sl = d.kind === 'spread' ? ex.slots : [];
    return sl.length && sl.length <= kMaxTimes ? sl.map(x => ({ time: hhmm(x.min), ml: text(x.ul / 1000) })) : [newRow()];
  };
  const switchKind = kind => set({ kind, ...(kind === 'times' && !d.times.length ? { times: fromSpread() } : {}) });
  const switchDays = mode => setDays({ mode, anchor: d.days.anchor || today || '', weekdays: d.days.weekdays.length ? d.days.weekdays : [...WD] });
  const toggleWd = w => setDays({ weekdays: d.days.weekdays.includes(w) ? d.days.weekdays.filter(x => x !== w) : [...d.days.weekdays, w] });
  const typing = d.kind === 'spread' && Number.isNaN(parseNum(d.ml));
  const submit = e => {
    e.preventDefault();
    if (ex.err) { msg.err(localError(ex, grid)); return; }
    save(bodyOf(d), t('sc.saved'), grid);
  };
  return html`<form class="plan" novalidate onSubmit=${submit}>
    <div class="field"><span class="lbl">${t('sc.kind')}</span>
      <${Seg} value=${d.kind} onChange=${switchKind}
        options=${[['spread', t('sc.kind.spread')], ['times', t('sc.kind.times')]]}/>
      <span class="mut small">${t('sc.kind.hint.' + d.kind)}</span>
    </div>
      <div class="plan-body">
        ${d.kind === 'spread' ? html`
          <div class="form-row">
            <label>${t('sc.ml.total')}<${Amount} value=${d.ml} onInput=${v => set({ ml: v })}/></label>
            <label>${t('sc.count')}<${Amount} value=${d.count} onInput=${v => set({ count: v })} unit=${t('sc.doses')}/></label>
          </div>
          <div class="form-row">
            <label>${parseInt(d.count, 10) > 1 ? t('sc.from') : t('sc.at')}<${TimeInput} value=${d.start} grid=${grid} onInput=${v => set({ start: v })}/></label>
            ${parseInt(d.count, 10) > 1 && html`<label>${t('sc.to')}<${TimeInput} value=${d.end} grid=${grid} onInput=${v => set({ end: v })}/></label>`}
          </div>
          <${Preview} slots=${typing ? [] : ex.slots} err=${typing ? null : ex.err} grid=${grid}/>`
        : html`<${TimesList} d=${d} grid=${grid} set=${set}/>`}
        <div class="field"><span class="lbl">${t('sc.days')}</span>
          <${Seg} value=${d.days.mode} onChange=${switchDays}
            options=${[['daily', t('sc.d.daily')], ['weekdays', t('sc.d.weekdays')], ['interval', t('sc.d.interval')]]}/>
          ${d.days.mode === 'weekdays' && html`<div class="wd">${WD.map(w => html`
            <button type="button" class=${d.days.weekdays.includes(w) ? 'on' : ''} aria-pressed=${d.days.weekdays.includes(w)}
              onClick=${() => toggleWd(w)}>${t('sc.wd.' + w)}</button>`)}</div>`}
          ${d.days.mode === 'interval' && html`<div class="form-row">
            <label>${t('sc.d.every')}<${Amount} value=${d.days.every} onInput=${v => setDays({ every: v })} unit=${t('sc.d.days')}/></label>
            <label>${t('sc.d.anchor')}<input type="date" value=${d.days.anchor} onInput=${e => setDays({ anchor: e.target.value })}/></label>
          </div>`}
        </div>
        <div class="field"><span class="lbl">${t('sc.power')}</span>
          <${Seg} value=${d.power_loss} onChange=${v => set({ power_loss: v })}
            options=${[['skip_missed', t('sc.pl.skip_missed')], ['catch_up_window', t('sc.pl.catch_up_window')]]}/>
          <span class="mut small">${t('sc.pl.hint.' + d.power_loss)}</span>
          ${d.power_loss === 'catch_up_window' && html`<${Amount} value=${d.catch_up} onInput=${v => set({ catch_up: v })} unit="min"/>`}
        </div>
      </div>
      ${d.kind === 'times' && ex.err && ex.err !== 'invalid_param' && d.times.length > 0 && html`<p class="msg err">${localError(ex, grid)}</p>`}
      ${limit > 0 && planned > limit && html`<p class="msg err">${t('sc.limit', { l: num(limit, 1), p: num(planned, 1) })}</p>`}
    <div class="row savebar">
      <button type="submit" disabled=${busy || !dirty}>${t('sc.save')}</button>
      <button type="button" class="sec" disabled=${busy || !dirty} onClick=${() => { setDraft(draftOf(s)); msg.clear(); }}>${t('sc.discard')}</button>
      ${dirty && html`<span class="chip warn">${t('sc.unsaved')}</span>`}
    </div>
    ${msg.node}
  </form>`;
}

// ---------------------------------------------------------------------------
// Export / import of this pump's schedule as a JSON file (pro view)

const kFormat = 'lgal-doser-schedule';
const kKeys = ['kind', 'start', 'end', 'count', 'ml', 'times', 'days', 'power_loss', 'catch_up_min'];

function exportFile(p, s) {
  const schedule = Object.fromEntries(kKeys.filter(k => s[k] !== undefined).map(k => [k, s[k]]));
  const doc = { format: kFormat, version: 1, exported: new Date().toISOString(), pump: p.pump, name: pumpName(p), schedule };
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([JSON.stringify(doc, null, 2) + '\n'], { type: 'application/json' }));
  const slug = (p.name || '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
  a.download = `doser-pump${p.pump}${slug ? '-' + slug : ''}-schedule.json`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

function ScheduleIO({ p, s, onSaved, grid }) {
  const { msg, busy, save } = useSave(p.pump, onSaved);
  const pick = e => {
    const file = e.target.files && e.target.files[0];
    e.target.value = '';
    if (!file) return;
    file.text().then(txt => {
      let doc;
      try { doc = JSON.parse(txt); } catch { doc = null; }
      if (!doc || doc.format !== kFormat || !doc.schedule || typeof doc.schedule !== 'object') { msg.err(t('sc.import.bad')); return; }
      if (doc.version > 1) { msg.err(t('sc.import.newer')); return; }
      const d = doc.exported ? new Date(doc.exported).toLocaleDateString(getLang()) : '–';
      return ask(t('sc.import.ask', { name: doc.name || t('pump', { n: doc.pump }), d, n: p.pump }), { ok: t('sc.import') })
        .then(yes => yes && save(doc.schedule, t('sc.import.ok'), grid));
    }).catch(() => msg.err(t('sc.import.bad')));
  };
  return html`<div class="sched-io">
    <p class="small mut">${t('sc.io.hint')}</p>
    <div class="row">
      <button type="button" class="sec" onClick=${() => exportFile(p, s)}>${t('sc.export')}</button>
      <label class=${'btn sec' + (busy ? ' disabled' : '')}>${t('sc.import')}
        <input type="file" accept="application/json,.json" hidden disabled=${busy} onChange=${pick}/></label>
    </div>
    ${msg.node}
  </div>`;
}

// ---------------------------------------------------------------------------

export function ScheduleCard({ p, pro, nowLocal }) {
  const [s, setS] = useState(null);
  const [draft, setDraft] = useState(null);
  const pump = p.pump;
  useEffect(() => {
    setS(null);
    api('GET', '/api/v1/pumps/schedule?pump=' + pump).then(setS).catch(e => setS({ error: e }));
  }, [pump]);
  // Fresh draft when the saved schedule changes
  useEffect(() => { setDraft(s && !s.error ? draftOf(s) : null); }, [s]);
  const today = nowLocal ? nowLocal.slice(0, 10) : s && s.today ? s.today.date : null;
  const nowMin = nowLocal ? toMin(nowLocal.slice(11, 16)) : null;
  const grid = (s && s.grid_min) || 5;
  const slots = !s || s.error ? []
    : pro && draft && today ? daySlots(draft, dayNum(today), grid)
      : s.today ? s.today.slots.map(x => ({ min: toMin(x.time), ul: ul(x.ml) })) : [];
  return html`<section class="card sched"><h2>${t('sched.title')}</h2>
    ${p.mode !== 'auto' && html`<p class="small warn">${t('sc.mode.' + p.mode)}</p>`}
    ${!s ? html`<p class="mut">${t('sc.loading')}</p>`
      : s.error ? html`<p class="msg err">${errText(s.error)}</p>`
        : html`<${DayBar} slots=${slots} nowMin=${nowMin}/>
          ${pro ? draft && html`<${ProEditor} pump=${pump} s=${s} draft=${draft} setDraft=${setDraft} onSaved=${setS}
              today=${today} limit=${p.daily.limit_ml}/>`
            : html`<${SimpleEditor} pump=${pump} s=${s} onSaved=${setS} limit=${p.daily.limit_ml}/>`}
          ${pro && html`<${ScheduleIO} p=${p} s=${s} onSaved=${setS} grid=${grid}/>`}`}
  </section>`;
}
