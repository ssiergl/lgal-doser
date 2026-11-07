// Schedules: day overview of all pumps and the editor of one pump.
// Simple view: daily amount in N doses between two times, every day.
// Pro view: up to 8 programs (single time or spread doses; daily, weekdays
// or every N days) and the behaviour after a power loss.
import { useEffect, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, getLang, num, t } from '../lib/i18n.js';
import { setMode, usePrefs } from '../lib/prefs.js';
import { PumpTabs, pumpColor, pumpName } from '../lib/pumps.js';
import { reloadSched, useStore } from '../lib/store.js';
import { Amount, Seg, parseNum, useMsg } from '../lib/ui.js';

const WD = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'];
const kMaxPrograms = 8;

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
    'sc.prog': 'Program {n}', 'sc.on': 'Active', 'sc.add': 'Add program', 'sc.del': 'Delete program',
    'sc.kind.single': 'Single time', 'sc.kind.generator': 'Spread', 'sc.ml.single': 'Amount', 'sc.ml.total': 'Amount per day',
    'sc.days': 'Days', 'sc.d.daily': 'Daily', 'sc.d.weekdays': 'Weekdays', 'sc.d.interval': 'Every N days',
    'sc.d.every': 'Every', 'sc.d.days': 'days', 'sc.d.anchor': 'starting', 'sc.d.everyN': 'every {n} days',
    'sc.wd.mon': 'Mon', 'sc.wd.tue': 'Tue', 'sc.wd.wed': 'Wed', 'sc.wd.thu': 'Thu', 'sc.wd.fri': 'Fri', 'sc.wd.sat': 'Sat', 'sc.wd.sun': 'Sun',
    'sc.power': 'After a power loss', 'sc.pl.skip_missed': 'Skip missed doses', 'sc.pl.catch_up_window': 'Catch up',
    'sc.pl.hint.skip_missed': 'Doses missed while the device was off are left out.',
    'sc.pl.hint.catch_up_window': 'Missed doses are made up if they are at most this old:',
    'sc.total': 'Planned today: {ml} ml', 'sc.off': 'paused',
    'sc.cf.overlap': 'Pump {p1} {t1} overlaps with pump {p2} {t2}{d}',
    'sc.cf.collision_lock': 'Pump {p1} {t1} and pump {p2} {t2}{d}: lock time {g} min',
    'sc.cf.next': ' (next day)', 'sc.err.prog': 'Program {n}: {e}',
    'err.invalid_time': 'Invalid time', 'err.off_grid': 'Times must be on the {g}-minute grid',
    'err.invalid_range': 'End time must be after the start time', 'err.invalid_count': 'Invalid number of doses',
    'err.slots_too_dense': 'Too many doses for this time window', 'err.invalid_days': 'Choose at least one day',
    'err.invalid_catch_up': 'Catch-up window: 1 to 1440 minutes', 'err.too_many_slots': 'Too many doses per day',
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
    'sc.prog': 'Programm {n}', 'sc.on': 'Aktiv', 'sc.add': 'Programm hinzufügen', 'sc.del': 'Programm löschen',
    'sc.kind.single': 'Einzelzeit', 'sc.kind.generator': 'Verteilt', 'sc.ml.single': 'Menge', 'sc.ml.total': 'Menge pro Tag',
    'sc.days': 'Tage', 'sc.d.daily': 'Täglich', 'sc.d.weekdays': 'Wochentage', 'sc.d.interval': 'Alle N Tage',
    'sc.d.every': 'Alle', 'sc.d.days': 'Tage', 'sc.d.anchor': 'ab', 'sc.d.everyN': 'alle {n} Tage',
    'sc.wd.mon': 'Mo', 'sc.wd.tue': 'Di', 'sc.wd.wed': 'Mi', 'sc.wd.thu': 'Do', 'sc.wd.fri': 'Fr', 'sc.wd.sat': 'Sa', 'sc.wd.sun': 'So',
    'sc.power': 'Nach einem Stromausfall', 'sc.pl.skip_missed': 'Verpasste auslassen', 'sc.pl.catch_up_window': 'Nachholen',
    'sc.pl.hint.skip_missed': 'Dosen, die während des Ausfalls fällig waren, entfallen.',
    'sc.pl.hint.catch_up_window': 'Verpasste Dosen werden nachgeholt, wenn sie höchstens so alt sind:',
    'sc.total': 'Heute geplant: {ml} ml', 'sc.off': 'pausiert',
    'sc.cf.overlap': 'Pumpe {p1} {t1} überschneidet sich mit Pumpe {p2} {t2}{d}',
    'sc.cf.collision_lock': 'Pumpe {p1} {t1} und Pumpe {p2} {t2}{d}: Sperrzeit {g} Min.',
    'sc.cf.next': ' (Folgetag)', 'sc.err.prog': 'Programm {n}: {e}',
    'err.invalid_time': 'Ungültige Uhrzeit', 'err.off_grid': 'Uhrzeiten müssen im {g}-Minuten-Raster liegen',
    'err.invalid_range': 'Ende muss nach dem Start liegen', 'err.invalid_count': 'Ungültige Anzahl Dosen',
    'err.slots_too_dense': 'Zu viele Dosen für dieses Zeitfenster', 'err.invalid_days': 'Mindestens einen Tag wählen',
    'err.invalid_catch_up': 'Nachholfenster: 1 bis 1440 Minuten', 'err.too_many_slots': 'Zu viele Dosen pro Tag',
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

// Program (parsed, see toProgram) -> {slots: [{min, ul}], err}
function expand(p, grid) {
  const fail = err => ({ slots: [], err });
  if (p.days.mode === 'weekdays' && !p.days.weekdays.length) return fail('invalid_days');
  if (p.days.mode === 'interval' && !(p.days.every >= 1 && p.days.every <= 30 && p.days.anchor)) return fail('invalid_days');
  if (Number.isNaN(p.start)) return fail('invalid_time');
  if (p.start % grid) return fail('off_grid');
  if (!(p.ul > 0)) return fail('invalid_param');
  if (p.kind === 'single') return { slots: [{ min: p.start, ul: p.ul }] };
  const n = p.count;
  if (!(n >= 1 && n <= 96)) return fail('invalid_count');
  if (n > 1) {
    if (Number.isNaN(p.end)) return fail('invalid_time');
    if (p.end % grid) return fail('off_grid');
    if (p.end <= p.start) return fail('invalid_range');
  }
  const base = Math.floor(p.ul / n), rest = p.ul % n, span = n > 1 ? p.end - p.start : 0;
  const slots = [];
  for (let i = 0; i < n; ++i) {
    const min = n > 1 ? p.start + Math.floor((2 * i * span + (n - 1) * grid) / (2 * (n - 1) * grid)) * grid : p.start;
    if (i > 0 && min <= slots[i - 1].min) return fail('slots_too_dense');
    slots.push({ min, ul: base + (i < rest ? 1 : 0) });
  }
  return { slots };
}

// ---------------------------------------------------------------------------
// Draft (form values as text) <-> device JSON

function draftOf(s) {
  return {
    power_loss: s.power_loss, catch_up: String(s.catch_up_min),
    programs: s.programs.map(p => ({
      enabled: p.enabled, kind: p.kind,
      start: p.kind === 'single' ? p.time : p.start, end: p.kind === 'generator' ? p.end : '',
      count: String(p.kind === 'generator' ? p.count : 1), ml: text(p.ml),
      days: { mode: p.days.mode, weekdays: p.days.weekdays || [], every: String(p.days.every || 2), anchor: p.days.anchor || '' },
    })),
  };
}

function toProgram(d) {
  return {
    enabled: d.enabled, kind: d.kind, start: toMin(d.start), end: toMin(d.end), count: parseInt(d.count, 10),
    ul: ul(parseNum(d.ml)), days: { ...d.days, every: parseInt(d.days.every, 10) },
  };
}

function bodyOf(draft) {
  return {
    power_loss: draft.power_loss, catch_up_min: parseInt(draft.catch_up, 10) || 0,
    programs: draft.programs.map(d => {
      const ml = parseNum(d.ml);
      const days = d.days.mode === 'weekdays' ? { mode: 'weekdays', weekdays: WD.filter(w => d.days.weekdays.includes(w)) }
        : d.days.mode === 'interval' ? { mode: 'interval', every: parseInt(d.days.every, 10), anchor: d.days.anchor }
          : { mode: 'daily' };
      return d.kind === 'single'
        ? { enabled: d.enabled, kind: 'single', time: d.start, ml, days }
        : { enabled: d.enabled, kind: 'generator', start: d.start, end: d.end, count: parseInt(d.count, 10), ml, days };
    }),
  };
}

// Slots of a draft on a day, sorted
function daySlots(draft, day, grid) {
  const out = [];
  draft.programs.forEach((d, i) => {
    const p = toProgram(d);
    if (!p.enabled || !activeOn(p.days, day)) return;
    for (const s of expand(p, grid).slots) out.push({ ...s, program: i + 1 });
  });
  return out.sort((a, b) => a.min - b.min);
}

// Simple form of a schedule: {ml, count, start, end}, {} without programs, null if not representable
function simpleOf(s) {
  if (!s.programs.length) return {};
  const p = s.programs[0];
  if (s.programs.length > 1 || !p.enabled || p.days.mode !== 'daily') return null;
  return p.kind === 'single' ? { ml: p.ml, count: 1, start: p.time, end: p.time }
    : { ml: p.ml, count: p.count, start: p.start, end: p.end };
}

function daysText(days) {
  if (days.mode === 'weekdays') return days.weekdays.length === 7 ? t('sc.d.daily') : WD.filter(w => days.weekdays.includes(w)).map(w => t('sc.wd.' + w)).join(', ');
  if (days.mode === 'interval') return t('sc.d.everyN', { n: days.every });
  return t('sc.d.daily');
}

function programText(p) {
  const amount = p.kind === 'single' ? `${p.time} · ${num(p.ml, 2)} ml`
    : `${t('sc.each', { n: p.count, ml: num(p.ml / p.count, 2) })} · ${p.start}–${p.end} (${num(p.ml, 1)} ml)`;
  return `${amount} · ${daysText(p.days)}${p.enabled ? '' : ' · ' + t('sc.off')}`;
}

function errorText(e, grid) {
  if (!e) return '';
  if (e.code === 'conflict' && e.body && e.body.error && e.body.error.conflicts) {
    return e.body.error.conflicts.map(c => t('sc.cf.' + (c.code === 'collision_lock' ? 'collision_lock' : 'overlap'), {
      p1: c.first.pump, t1: c.first.time, p2: c.second.pump, t2: c.second.time, g: c.gap_min,
      d: c.next_day ? t('sc.cf.next') : '',
    })).join('\n');
  }
  const base = errText({ code: e.code || 'invalid_param', message: e.message, vars: { g: grid } });
  const prog = e.body && e.body.error && e.body.error.program;
  return prog ? t('sc.err.prog', { n: prog, e: base }) : base;
}

// ---------------------------------------------------------------------------
// Schedules of all pumps (GET /api/v1/pumps/schedule), shared while the page is open

const cache = {};

async function loadAll(setTick) {
  for (let n = 1; n <= 5; ++n) {
    try { cache[n] = await api('GET', '/api/v1/pumps/schedule?pump=' + n); } catch (e) { cache[n] = { error: e }; }
    setTick(x => x + 1);
  }
}

// ---------------------------------------------------------------------------
// Day overview: one row per pump, today's doses as dots

function Timeline({ pumps, current, draft, today, grid, nowMin }) {
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
      .catch(e => msg.err(errorText(e, grid)))
      .finally(() => setBusy(false));
  };
  return { msg, busy, save };
}

// ---------------------------------------------------------------------------
// Simple view

function SimpleEditor({ pump, name, s, onSaved, limit }) {
  const grid = s.grid_min || 5;
  const simple = simpleOf(s);
  const [replace, setReplace] = useState(false);
  const init = () => {
    const sm = simple || {};
    const sl = s.today ? s.today.slots : [];
    return {
      ml: text(sm.ml ?? (s.today && s.today.ml) ?? ''), count: String(sm.count ?? (sl.length || 1)),
      start: sm.start ?? (sl.length ? sl[0].time : '08:00'), end: sm.end ?? (sl.length ? sl[sl.length - 1].time : '20:00'),
    };
  };
  const [f, setF] = useState(init);
  useEffect(() => { setF(init()); setReplace(false); }, [s]);
  const { msg, busy, save } = useSave(pump, onSaved);
  const set = patch => setF(prev => ({ ...prev, ...patch }));

  if (simple === null && !replace) {
    return html`<section class="card"><h2>${name}</h2>
      <p class="prohint small">${t('sc.pro.made')}</p>
      <ul class="plist">${s.programs.map(p => html`<li class=${p.enabled ? '' : 'mut'}>${programText(p)}</li>`)}</ul>
      ${s.today && html`<p class="small mut">${t('sc.total', { ml: num(s.today.ml, 2) })}</p>`}
      <div class="row"><button onClick=${() => setMode('pro')}>${t('sc.pro.edit')}</button>
        <button class="sec" onClick=${() => setReplace(true)}>${t('sc.replace')}</button></div>
    </section>`;
  }
  const count = parseInt(f.count, 10);
  const prog = { enabled: true, kind: count > 1 ? 'generator' : 'single', start: toMin(f.start), end: toMin(f.end), count, ul: ul(parseNum(f.ml)), days: { mode: 'daily' } };
  const ex = Number.isNaN(parseNum(f.ml)) || !(count >= 1) ? { slots: [] } : expand(prog, grid);
  const submit = e => {
    e.preventDefault();
    if (ex.err || !ex.slots.length) { msg.err(t('err.' + (ex.err || 'invalid_param'), { g: grid })); return; }
    const ml = parseNum(f.ml);
    const p = count > 1 ? { kind: 'generator', start: f.start, end: f.end, count, ml, days: { mode: 'daily' } }
      : { kind: 'single', time: f.start, ml, days: { mode: 'daily' } };
    save({ power_loss: s.power_loss, catch_up_min: s.catch_up_min, programs: [{ enabled: true, ...p }] }, t('sc.saved'), grid);
  };
  const remove = () => {
    if (confirm(t('sc.remove.ask', { n: pump }))) save({ power_loss: s.power_loss, catch_up_min: s.catch_up_min, programs: [] }, t('sc.removed'), grid);
  };
  const empty = simple && !s.programs.length;
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
// Pro view

function ProgramCard({ d, index, grid, today, onChange, onDelete }) {
  const set = patch => onChange({ ...d, ...patch });
  const setDays = patch => onChange({ ...d, days: { ...d.days, ...patch } });
  const p = toProgram(d);
  const ex = expand(p, grid);
  const toggleWd = w => setDays({ weekdays: d.days.weekdays.includes(w) ? d.days.weekdays.filter(x => x !== w) : [...d.days.weekdays, w] });
  const switchDays = mode => setDays({ mode, anchor: d.days.anchor || today || '', weekdays: d.days.weekdays.length ? d.days.weekdays : [...WD] });
  return html`<div class=${'prog' + (d.enabled ? '' : ' off')}>
    <div class="prog-head">
      <b>${t('sc.prog', { n: index + 1 })}</b>
      <label class="check"><input type="checkbox" checked=${d.enabled} onChange=${e => set({ enabled: e.target.checked })}/> ${t('sc.on')}</label>
      <span class="spacer"></span>
      <button type="button" class="ghost" title=${t('sc.del')} aria-label=${t('sc.del')} onClick=${onDelete}>✕</button>
    </div>
    <${Seg} value=${d.kind} onChange=${kind => set({ kind, end: d.end || '20:00', count: kind === 'generator' && d.count === '1' ? '4' : d.count })}
      options=${[['single', t('sc.kind.single')], ['generator', t('sc.kind.generator')]]}/>
    <div class="form-row">
      ${d.kind === 'single' ? html`
        <label>${t('sc.at')}<${TimeInput} value=${d.start} grid=${grid} onInput=${v => set({ start: v })}/></label>
        <label>${t('sc.ml.single')}<${Amount} value=${d.ml} onInput=${v => set({ ml: v })}/></label>` : html`
        <label>${t('sc.from')}<${TimeInput} value=${d.start} grid=${grid} onInput=${v => set({ start: v })}/></label>
        <label>${t('sc.to')}<${TimeInput} value=${d.end} grid=${grid} onInput=${v => set({ end: v })}/></label>
        <label>${t('sc.count')}<${Amount} value=${d.count} onInput=${v => set({ count: v })} unit=${t('sc.doses')}/></label>
        <label>${t('sc.ml.total')}<${Amount} value=${d.ml} onInput=${v => set({ ml: v })}/></label>`}
    </div>
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
    <${Preview} slots=${Number.isNaN(parseNum(d.ml)) || d.kind === 'single' ? [] : ex.slots} err=${Number.isNaN(parseNum(d.ml)) ? null : ex.err} grid=${grid}/>
  </div>`;
}

function ProEditor({ pump, name, s, draft, setDraft, onSaved, today, limit }) {
  const grid = s.grid_min || 5;
  const { msg, busy, save } = useSave(pump, onSaved);
  const dirty = JSON.stringify(bodyOf(draft)) !== JSON.stringify(bodyOf(draftOf(s)));
  const setProg = (i, d) => setDraft({ ...draft, programs: draft.programs.map((x, k) => (k === i ? d : x)) });
  const addProg = () => setDraft({
    ...draft, programs: [...draft.programs, {
      enabled: true, kind: 'single', start: '08:00', end: '', count: '1', ml: '', days: { mode: 'daily', weekdays: [...WD], every: '2', anchor: today || '' },
    }],
  });
  const planned = today ? daySlots(draft, dayNum(today), grid).reduce((a, x) => a + x.ul, 0) / 1000 : 0;
  const submit = e => {
    e.preventDefault();
    const bad = draft.programs.findIndex(d => Number.isNaN(parseNum(d.ml)) || expand(toProgram(d), grid).err);
    if (bad >= 0) {
      const d = draft.programs[bad];
      msg.err(t('sc.err.prog', { n: bad + 1, e: t('err.' + (Number.isNaN(parseNum(d.ml)) ? 'invalid_param' : expand(toProgram(d), grid).err), { g: grid }) }));
      return;
    }
    save(bodyOf(draft), t('sc.saved'), grid);
  };
  return html`<form class="card" novalidate onSubmit=${submit}>
    <h2>${name}</h2>
    ${!draft.programs.length && html`<p class="small mut">${t('sc.none')}. ${t('sc.none.hint')}</p>`}
    <div class="progs">${draft.programs.map((d, i) => html`<${ProgramCard} key=${i} d=${d} index=${i} grid=${grid} today=${today}
      onChange=${x => setProg(i, x)} onDelete=${() => setDraft({ ...draft, programs: draft.programs.filter((_, k) => k !== i) })}/>`)}</div>
    <button type="button" class="sec" onClick=${addProg} disabled=${draft.programs.length >= kMaxPrograms}>+ ${t('sc.add')}</button>
    <div class="field"><span class="lbl">${t('sc.power')}</span>
      <${Seg} value=${draft.power_loss} onChange=${v => setDraft({ ...draft, power_loss: v })}
        options=${[['skip_missed', t('sc.pl.skip_missed')], ['catch_up_window', t('sc.pl.catch_up_window')]]}/>
      <span class="mut small">${t('sc.pl.hint.' + draft.power_loss)}</span>
      ${draft.power_loss === 'catch_up_window' && html`<${Amount} value=${draft.catch_up} onInput=${v => setDraft({ ...draft, catch_up: v })} unit="min"/>`}
    </div>
    ${today && html`<p class="small mut">${t('sc.total', { ml: num(planned, 2) })}</p>`}
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
  return html`<div class="page-head"><h1>${t('sched.title')}</h1></div>
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
