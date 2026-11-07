// Schedules: day overview of all pumps and the editor of one pump. Every
// pump has exactly one plan: "spread" (daily amount in N doses between two
// times) or "times" (single times, each with its own amount).
// Simple view: spread, every day. Pro view: both kinds, days (daily,
// weekdays, every N days), pause and the behaviour after a power loss.
import { useEffect, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, getLang, num, t } from '../lib/i18n.js';
import { setMode, usePrefs } from '../lib/prefs.js';
import { PumpTabs, pumpColor, pumpName } from '../lib/pumps.js';
import { reloadSched, useStore } from '../lib/store.js';
import { Amount, Seg, parseNum, useMsg } from '../lib/ui.js';

const WD = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'];
const kMaxTimes = 24;

add({
  en: {
    'sched.title': 'Schedule', 'sc.today': 'Today', 'sc.loading': 'Loading …',
    'sc.none': 'No schedule', 'sc.none.hint': 'This pump only doses when you start it by hand.',
    'sc.daily': 'Daily amount', 'sc.count': 'Doses per day', 'sc.from': 'From', 'sc.to': 'To', 'sc.at': 'Time',
    'sc.doses': 'doses', 'sc.every': 'every {m} min', 'sc.everyh': 'every {h} h', 'sc.each': '{n} × {ml} ml',
    'sc.save': 'Save', 'sc.discard': 'Discard', 'sc.saved': 'Schedule saved', 'sc.remove': 'Remove schedule',
    'sc.remove.ask': 'Remove the schedule of pump {n}?', 'sc.removed': 'Schedule removed', 'sc.create': 'Create schedule',
    'sc.pro.made': 'This schedule was set up in the pro view and stays active as it is.',
    'sc.pro.edit': 'Edit in pro view', 'sc.replace': 'Replace with a simple schedule',
    'sc.mode.manual_only': 'The pump is set to “manual only” – the schedule is paused.',
    'sc.mode.disabled': 'The pump is off – the schedule is paused.',
    'sc.limit': 'Daily limit {l} ml – planned {p} ml', 'sc.unsaved': 'Unsaved changes',
    'sc.kind': 'Plan', 'sc.kind.none': 'None', 'sc.kind.spread': 'Spread', 'sc.kind.times': 'Single times',
    'sc.kind.hint.spread': 'A daily amount, split evenly over several doses.',
    'sc.kind.hint.times': 'Fixed times, each with its own amount.',
    'sc.on': 'Active', 'sc.on.hint': 'Off pauses the schedule; it stays saved.',
    'sc.ml.total': 'Amount per day', 'sc.t.add': 'Add time', 'sc.t.del': 'Remove time', 'sc.t.sum': '{n} times · {ml} ml per day',
    'sc.days': 'Days', 'sc.d.daily': 'Daily', 'sc.d.weekdays': 'Weekdays', 'sc.d.interval': 'Every N days',
    'sc.d.every': 'Every', 'sc.d.days': 'days', 'sc.d.anchor': 'starting', 'sc.d.everyN': 'every {n} days',
    'sc.wd.mon': 'Mon', 'sc.wd.tue': 'Tue', 'sc.wd.wed': 'Wed', 'sc.wd.thu': 'Thu', 'sc.wd.fri': 'Fri', 'sc.wd.sat': 'Sat', 'sc.wd.sun': 'Sun',
    'sc.power': 'After a power loss', 'sc.pl.skip_missed': 'Skip missed doses', 'sc.pl.catch_up_window': 'Catch up',
    'sc.pl.hint.skip_missed': 'Doses missed while the device was off are left out.',
    'sc.pl.hint.catch_up_window': 'Missed doses are made up if they are at most this old:',
    'sc.total': 'Planned today: {ml} ml', 'sc.off': 'paused',
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
    'sched.title': 'Zeitplan', 'sc.today': 'Heute', 'sc.loading': 'Lädt …',
    'sc.none': 'Kein Zeitplan', 'sc.none.hint': 'Diese Pumpe dosiert nur, wenn du sie von Hand startest.',
    'sc.daily': 'Tagesmenge', 'sc.count': 'Dosen pro Tag', 'sc.from': 'Von', 'sc.to': 'Bis', 'sc.at': 'Uhrzeit',
    'sc.doses': 'Dosen', 'sc.every': 'alle {m} Min.', 'sc.everyh': 'alle {h} Std.', 'sc.each': '{n} × {ml} ml',
    'sc.save': 'Speichern', 'sc.discard': 'Verwerfen', 'sc.saved': 'Zeitplan gespeichert', 'sc.remove': 'Zeitplan entfernen',
    'sc.remove.ask': 'Zeitplan von Pumpe {n} entfernen?', 'sc.removed': 'Zeitplan entfernt', 'sc.create': 'Zeitplan anlegen',
    'sc.pro.made': 'Dieser Zeitplan wurde in der Profi-Ansicht eingerichtet und bleibt so aktiv.',
    'sc.pro.edit': 'In der Profi-Ansicht bearbeiten', 'sc.replace': 'Durch einfachen Zeitplan ersetzen',
    'sc.mode.manual_only': 'Die Pumpe steht auf „Nur manuell“ – der Zeitplan ruht.',
    'sc.mode.disabled': 'Die Pumpe ist aus – der Zeitplan ruht.',
    'sc.limit': 'Tageslimit {l} ml – geplant {p} ml', 'sc.unsaved': 'Ungespeicherte Änderungen',
    'sc.kind': 'Plan', 'sc.kind.none': 'Keiner', 'sc.kind.spread': 'Verteilt', 'sc.kind.times': 'Einzelzeiten',
    'sc.kind.hint.spread': 'Eine Tagesmenge, gleichmäßig auf mehrere Dosen verteilt.',
    'sc.kind.hint.times': 'Feste Uhrzeiten, jede mit eigener Menge.',
    'sc.on': 'Aktiv', 'sc.on.hint': 'Aus pausiert den Zeitplan; er bleibt gespeichert.',
    'sc.ml.total': 'Menge pro Tag', 'sc.t.add': 'Uhrzeit hinzufügen', 'sc.t.del': 'Uhrzeit entfernen', 'sc.t.sum': '{n} Uhrzeiten · {ml} ml pro Tag',
    'sc.days': 'Tage', 'sc.d.daily': 'Täglich', 'sc.d.weekdays': 'Wochentage', 'sc.d.interval': 'Alle N Tage',
    'sc.d.every': 'Alle', 'sc.d.days': 'Tage', 'sc.d.anchor': 'ab', 'sc.d.everyN': 'alle {n} Tage',
    'sc.wd.mon': 'Mo', 'sc.wd.tue': 'Di', 'sc.wd.wed': 'Mi', 'sc.wd.thu': 'Do', 'sc.wd.fri': 'Fr', 'sc.wd.sat': 'Sa', 'sc.wd.sun': 'So',
    'sc.power': 'Nach einem Stromausfall', 'sc.pl.skip_missed': 'Verpasste auslassen', 'sc.pl.catch_up_window': 'Nachholen',
    'sc.pl.hint.skip_missed': 'Dosen, die während des Ausfalls fällig waren, entfallen.',
    'sc.pl.hint.catch_up_window': 'Verpasste Dosen werden nachgeholt, wenn sie höchstens so alt sind:',
    'sc.total': 'Heute geplant: {ml} ml', 'sc.off': 'pausiert',
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
  if (d.kind === 'none') return { slots: [] };
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
const daySlots = (d, day, grid) => (d.kind !== 'none' && d.enabled && activeOn(d.days, day) ? expand(d, grid).slots : []);

// ---------------------------------------------------------------------------
// Draft (form values as text) <-> device JSON

const newRow = (time = '08:00') => ({ time, ml: '' });

function draftOf(s) {
  const d = s.days || { mode: 'daily' };
  return {
    kind: s.kind || 'none', enabled: s.enabled !== false,
    start: s.start || '08:00', end: s.end || '20:00', count: String(s.count || 4), ml: text(s.ml),
    times: (s.times || []).map(x => ({ time: x.time, ml: text(x.ml) })),
    days: { mode: d.mode, weekdays: d.weekdays || [...WD], every: String(d.every || 2), anchor: d.anchor || '' },
    power_loss: s.power_loss || 'skip_missed', catch_up: String(s.catch_up_min ?? 60),
  };
}

function bodyOf(d) {
  const common = { power_loss: d.power_loss, catch_up_min: parseInt(d.catch_up, 10) || 0 };
  if (d.kind === 'none') return { kind: 'none', ...common };
  const days = d.days.mode === 'weekdays' ? { mode: 'weekdays', weekdays: WD.filter(w => d.days.weekdays.includes(w)) }
    : d.days.mode === 'interval' ? { mode: 'interval', every: parseInt(d.days.every, 10), anchor: d.days.anchor }
      : { mode: 'daily' };
  const head = { kind: d.kind, enabled: d.enabled };
  if (d.kind === 'times') return { ...head, times: sortedTimes(d).map(x => ({ time: x.time, ml: x.ml })), days, ...common };
  const count = parseInt(d.count, 10);
  return { ...head, start: d.start, ...(count > 1 ? { end: d.end } : {}), count, ml: parseNum(d.ml), days, ...common };
}

// Simple form of a schedule: {ml, count, start, end}, {} without a plan, null if not representable
function simpleOf(s) {
  if (s.kind === 'none') return {};
  if (!s.enabled || s.days.mode !== 'daily') return null;
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
  const tail = `${daysText(s.days)}${s.enabled ? '' : ' · ' + t('sc.off')}`;
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
      p1: c.first.pump, t1: c.first.time, p2: c.second.pump, t2: c.second.time, g: c.gap_min,
      d: c.next_day ? t('sc.cf.next') : '',
    })).join('\n');
  }
  const base = errText({ code: e.code || 'invalid_param', message: e.message, vars: { g: grid } });
  const entry = info && info.entry;  // 1-based, in the sorted order of the body sent
  const time = entry && body && body.times && body.times[entry - 1] && body.times[entry - 1].time;
  return time ? t('sc.err.entry', { time, e: base }) : base;
}

const localError = (ex, grid) => {
  const base = t('err.' + ex.err, { g: grid });
  return ex.entry ? t('sc.err.entry', { time: ex.entry, e: base }) : base;
};

// ---------------------------------------------------------------------------
// Schedules of all pumps (GET /api/v1/pumps/schedule), shared while the page is open

export const cache = {};

export async function loadAll(setTick) {
  for (let n = 1; n <= 5; ++n) {
    try { cache[n] = await api('GET', '/api/v1/pumps/schedule?pump=' + n); } catch (e) { cache[n] = { error: e }; }
    setTick(x => x + 1);
  }
}

// ---------------------------------------------------------------------------
// Day overview: one row per pump, today's doses as dots

export function Timeline({ pumps, current, draft, today, grid, nowMin }) {
  const day = today ? dayNum(today) : null;
  return html`<section class="card timeline">
    <h2>${t('sc.today')}</h2>
    <div class="tl-head"><span></span>
      <div class="tl-axis">${[0, 6, 12, 18, 24].map(h => html`<span style=${{ left: (h / 24) * 100 + '%' }}>${h}</span>`)}</div></div>
    ${pumps.map(p => {
      const s = cache[p.pump];
      const slots = p.pump === current && draft && day !== null ? daySlots(draft, day, grid)
        : s && s.today ? s.today.slots.map(x => ({ min: toMin(x.time), ul: ul(x.ml) })) : [];
      const total = slots.reduce((a, x) => a + x.ul, 0);
      return html`<a class=${'tl-row' + (p.pump === current ? ' on' : '') + (p.mode !== 'auto' ? ' dim' : '')}
          href=${'#/schedule/' + p.pump} style=${{ '--pc': pumpColor(p.pump) }}>
        <span class="tl-name"><span class="badge">${p.pump}</span><span class="nm">${pumpName(p)}</span></span>
        <span class="tl-track">
          ${nowMin !== null && html`<i class="tl-now" style=${{ left: (nowMin / 1440) * 100 + '%' }}></i>`}
          ${slots.map(x => html`<i class="tl-dot" title=${hhmm(x.min) + ' · ' + num(x.ul / 1000, 2) + ' ml'}
            style=${{ left: (x.min / 1440) * 100 + '%' }}></i>`)}
        </span>
        <span class="tl-sum num">${total ? num(total / 1000, 1) + ' ml' : '–'}</span>
      </a>`;
    })}
  </section>`;
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
    <div class="times">${slots.map(s => html`<span class="chip">${hhmm(s.min)}</span>`)}</div>
  </div>`;
}

function useSave(pump, onSaved) {
  const msg = useMsg();
  const [busy, setBusy] = useState(false);
  const save = (body, okText, grid) => {
    setBusy(true);
    return api('PUT', '/api/v1/pumps/schedule?pump=' + pump, body)
      .then(j => { cache[pump] = j; msg.ok(okText); onSaved(j); reloadSched(); })
      .catch(e => msg.err(errorText(e, grid, body)))
      .finally(() => setBusy(false));
  };
  return { msg, busy, save };
}

// ---------------------------------------------------------------------------
// Simple view: daily amount in N doses, every day

function SimpleEditor({ pump, name, s, onSaved, limit }) {
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
    return html`<section class="card"><h2>${name}</h2>
      <p class="prohint small">${t('sc.pro.made')}</p>
      <ul class="plist">${planLines(s).map(l => html`<li>${l}</li>`)}</ul>
      ${s.today && html`<p class="small mut">${t('sc.total', { ml: num(s.today.ml, 2) })}</p>`}
      <div class="row"><button onClick=${() => setMode('pro')}>${t('sc.pro.edit')}</button>
        <button class="sec" onClick=${() => setReplace(true)}>${t('sc.replace')}</button></div>
    </section>`;
  }
  const draft = {
    kind: 'spread', enabled: true, start: f.start, end: f.end, count: f.count, ml: f.ml, times: [],
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
  const remove = () => {
    if (confirm(t('sc.remove.ask', { n: pump }))) save(bodyOf({ ...draft, kind: 'none' }), t('sc.removed'), grid);
  };
  const empty = s.kind === 'none';
  const planned = ex.slots.reduce((a, x) => a + x.ul, 0) / 1000;
  return html`<section class="card"><h2>${name}</h2>
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
        ${!empty && simple && html`<button type="button" class="sec" onClick=${remove} disabled=${busy}>${t('sc.remove')}</button>`}
        ${replace && html`<button type="button" class="sec" onClick=${() => setReplace(false)}>${t('sc.discard')}</button>`}
      </div>
    </form>
    ${msg.node}
  </section>`;
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

function ProEditor({ pump, name, s, draft: d, setDraft, onSaved, today, limit }) {
  const grid = s.grid_min || 5;
  const { msg, busy, save } = useSave(pump, onSaved);
  const set = patch => setDraft(prev => ({ ...prev, ...patch }));
  const setDays = patch => setDraft(prev => ({ ...prev, days: { ...prev.days, ...patch } }));
  const dirty = JSON.stringify(bodyOf(d)) !== JSON.stringify(bodyOf(draftOf(s)));
  const ex = expand(d, grid);
  const planned = today ? daySlots(d, dayNum(today), grid).reduce((a, x) => a + x.ul, 0) / 1000 : 0;
  const switchKind = kind => set({ kind, ...(kind === 'times' && !d.times.length ? { times: [newRow()] } : {}) });
  const switchDays = mode => setDays({ mode, anchor: d.days.anchor || today || '', weekdays: d.days.weekdays.length ? d.days.weekdays : [...WD] });
  const toggleWd = w => setDays({ weekdays: d.days.weekdays.includes(w) ? d.days.weekdays.filter(x => x !== w) : [...d.days.weekdays, w] });
  const typing = d.kind === 'spread' && Number.isNaN(parseNum(d.ml));
  const submit = e => {
    e.preventDefault();
    if (ex.err) { msg.err(localError(ex, grid)); return; }
    save(bodyOf(d), d.kind === 'none' ? t('sc.removed') : t('sc.saved'), grid);
  };
  return html`<form class="card plan" novalidate onSubmit=${submit}>
    <h2>${name}</h2>
    <div class="field"><span class="lbl">${t('sc.kind')}</span>
      <${Seg} value=${d.kind} onChange=${switchKind}
        options=${[['none', t('sc.kind.none')], ['spread', t('sc.kind.spread')], ['times', t('sc.kind.times')]]}/>
      <span class="mut small">${d.kind === 'none' ? t('sc.none.hint') : t('sc.kind.hint.' + d.kind)}</span>
    </div>
    ${d.kind !== 'none' && html`
      <label class="check"><input type="checkbox" checked=${d.enabled} onChange=${e => set({ enabled: e.target.checked })}/>
        <span>${t('sc.on')}<br/><span class="mut small">${t('sc.on.hint')}</span></span></label>
      <div class=${'plan-body' + (d.enabled ? '' : ' off')}>
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
      ${today && html`<p class="small mut">${t('sc.total', { ml: num(planned, 2) })}</p>`}
      ${limit > 0 && planned > limit && html`<p class="msg err">${t('sc.limit', { l: num(limit, 1), p: num(planned, 1) })}</p>`}`}
    <div class="row savebar">
      <button type="submit" disabled=${busy || !dirty}>${t('sc.save')}</button>
      <button type="button" class="sec" disabled=${busy || !dirty} onClick=${() => { setDraft(draftOf(s)); msg.clear(); }}>${t('sc.discard')}</button>
      ${dirty && html`<span class="chip warn">${t('sc.unsaved')}</span>`}
    </div>
    ${msg.node}
  </form>`;
}

// ---------------------------------------------------------------------------

export function ScheduleView({ pump }) {
  const st = useStore();
  const prefs = usePrefs();
  const [, setTick] = useState(0);
  const [draft, setDraft] = useState(null);
  const cur = pump >= 1 && pump <= 5 ? pump : 1;
  useEffect(() => { loadAll(setTick); }, []);
  const s = cache[cur];
  // Fresh draft when the pump or the saved schedule changes
  useEffect(() => { setDraft(s && !s.error ? draftOf(s) : null); }, [cur, s]);
  if (!st.pumps) return html`<div class="empty">…</div>`;
  const p = st.pumps[cur - 1];
  const now = st.sched && st.sched.now_local;
  const today = now ? now.slice(0, 10) : s && s.today ? s.today.date : null;
  const nowMin = now ? toMin(now.slice(11, 16)) : null;
  const pro = prefs.mode === 'pro';
  const grid = (s && s.grid_min) || 5;
  const onSaved = j => { setDraft(draftOf(j)); setTick(x => x + 1); };
  return html`<div class="page-head"><h1>${t('sched.title')}</h1>
      ${pro && html`<span class="spacer"></span><a class="btn sec" href="#/schedule/locks">${t('lk.title')}</a>`}</div>
    <${Timeline} pumps=${st.pumps} current=${cur} draft=${pro ? draft : null} today=${today} grid=${grid} nowMin=${nowMin}/>
    <${PumpTabs} current=${cur} pumps=${st.pumps} base="#/schedule/"/>
    <div class="sc" style=${{ '--pc': pumpColor(cur) }}>
    ${p.mode !== 'auto' && html`<div class="banner">${t('sc.mode.' + p.mode)}</div>`}
    ${!s ? html`<div class="empty">${t('sc.loading')}</div>`
      : s.error ? html`<div class="banner err">${errText(s.error)}</div>`
        : pro ? draft && html`<${ProEditor} key=${cur} pump=${cur} name=${pumpName(p)} s=${s} draft=${draft} setDraft=${setDraft} onSaved=${onSaved}
            today=${today} limit=${p.daily.limit_ml}/>`
          : html`<${SimpleEditor} key=${cur} pump=${cur} name=${pumpName(p)} s=${s} onSaved=${onSaved} limit=${p.daily.limit_ml}/>`}
    </div>`;
}
