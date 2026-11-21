// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// History page: daily amounts as one small bar chart per pump (everyone),
// pro view additionally the event list with filter and CSV export.
import { useEffect, useRef, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { load, save } from '../lib/hub.js';
import { add, errText, getLang, num, t } from '../lib/i18n.js';
import { hm, usePrefs } from '../lib/prefs.js';
import { pumpColor, pumpName, fittedOf } from '../lib/pumps.js';
import { useStore } from '../lib/store.js';
import { Seg } from '../lib/ui.js';

add({
  en: {
    'hi.title': 'History', 'hi.daily': 'Dosed per day', 'hi.days': '{n} days',
    'hi.custom': 'Period', 'hi.from': 'From', 'hi.to': 'To',
    'hi.hover': 'Point at or tap the chart for the values of a day.',
    'hi.bar': 'Bars', 'hi.line': 'Line',
    'hi.day': '{d}: {ml} ml · {n} doses', 'hi.day.skip': '{n} skipped', 'hi.today': 'today',
    'hi.skipnote': 'Dot under the axis: doses were skipped that day.', 'hi.table': 'Show as table', 'hi.date': 'Day',
    'hi.notime': 'The device has no valid time – daily values follow once the time is set.',
    'hi.keep': 'Kept for {n} days.',
    'hi.events': 'Events', 'hi.all': 'All', 'hi.allpumps': 'All pumps', 'hi.more': 'Load more', 'hi.csv': 'CSV export',
    'hi.none': 'No events in this period.', 'hi.device': 'Device',
    'hk.dose': 'Dose', 'hk.prime': 'Filling', 'hk.calib_run': 'Calibration run', 'hk.skip': 'Skipped',
    'hk.container': 'Container', 'hk.calibration': 'Calibration', 'hk.tube': 'Tube', 'hk.alarm': 'Alarm',
    'hs.schedule': 'schedule', 'hs.manual': 'manual', 'hs.api': 'API', 'hs.boost': 'boost', 'hs.mqtt': 'MQTT',
    'hd.done': '{d} ml', 'hd.part': '{d} of {t} ml', 'hd.slots': '{n} doses', 'hd.ct': '{r} of {c} ml',
    'hd.cal': '{v} ml/rev. · measured {m} of {t} ml', 'hd.tube': '{h} h',
    'hc.success': 'done', 'hc.aborted': 'stopped', 'hc.fault': 'driver fault', 'hc.locked': 'locked (update)',
    'hc.heartbeat_lost': 'stopped (connection)', 'hc.max_duration': 'stopped (max. time)',
    'hc.missed': 'missed (device off)', 'hc.daily_limit': 'daily limit reached',
    'hc.delay_timeout': 'lock time too long', 'hc.pump_disabled': 'pump off', 'hc.pump_manual_only': 'manual only',
    'hc.driver_unavailable': 'driver not reachable', 'hc.time_invalid': 'no valid time', 'hc.dose_too_small': 'dose too small',
    'hc.dose_too_large': 'dose too large', 'hc.pump_busy': 'pump busy', 'hc.collision_lock': 'lock time',
    'hc.container_low': 'container almost empty', 'hc.tube_worn': 'replace the tube',
    'hc.rtc_fault': 'clock module does not answer', 'hc.supply_missing': 'no 24 V supply', 'hc.supply_low': '24 V supply too low',
    'hc.set': 'changed', 'hc.refilled': 'refilled', 'hc.other': 'other',
    'hk.boost': 'Boost', 'hk.mode': 'Mode', 'hk.schedule': 'Schedule', 'hk.settings': 'Settings', 'hk.locks': 'Lock rules',
    'hk.alarm_end': 'Alarm over', 'hk.system': 'System',
    'hc.started': 'started', 'hc.cancelled': 'cancelled', 'hc.finished': 'finished', 'hc.cleared': 'deleted',
    'hc.boot': 'started', 'hc.updated': 'firmware updated', 'hc.restored': 'backup restored', 'hc.time_set': 'time set',
    'hc.settings': 'settings changed', 'hc.channels': 'fitted pumps changed', 'hc.engineering': 'engineering changed',
    'hc.network': 'network changed', 'hc.mqtt': 'MQTT changed', 'hc.password': 'password changed',
    'hc.key_added': 'API key added', 'hc.key_removed': 'API key deleted',
    'hd.bo.days': '{t} ml over {n} days', 'hd.bo.doses': '{t} ml over {n} doses', 'hd.plan': '{d} ml/day · {n} doses',
    'hd.rules': '{n} rules', 'hd.pumps': 'pumps {p}', 'hd.ver': '{from} → {to}',
    'hd.ar.language': 'Language', 'hd.ar.time': 'Time zone / time server', 'hd.ar.led': 'Status LED', 'hd.ar.name': 'Device name',
    'hd.ch.name': 'Name', 'hd.ch.speed': 'Speed', 'hd.ch.retract': 'Anti-drip retraction', 'hd.ch.daily_limit': 'Daily limit',
    'hd.ch.container': 'Container size', 'hd.ch.thresholds': 'Container thresholds', 'hd.ch.calibration': 'Calibration',
    'hd.ch.reminder': 'Calibration reminder', 'hd.ch.direction': 'Direction of rotation',
    'hd.ch.dir': 'Direction of rotation → {d}', 'hd.dir.cw': 'Clockwise (CW)', 'hd.dir.ccw': 'Counter-clockwise (CCW)',
    'hd.rs.power_on': 'power on', 'hd.rs.external': 'reset', 'hd.rs.restart': 'restart', 'hd.rs.crash': 'after a crash',
    'hd.rs.watchdog': 'watchdog', 'hd.rs.brownout': 'voltage dip', 'hd.rs.other': '',
  },
});

const kRanges = [7, 14, 30];
const kMaxSpan = 90;  // the longest period and how far back it can start (history keeps at most 90 days)
const kKinds = ['dose', 'skip', 'boost', 'prime', 'calib_run', 'container', 'calibration', 'tube', 'mode', 'schedule',
  'settings', 'locks', 'alarm', 'alarm_end', 'system'];
const kPage = 50;
const kBad = ['aborted', 'fault', 'locked', 'heartbeat_lost', 'max_duration', 'rtc_fault', 'daily_limit', 'supply_missing', 'supply_low'];

// "2026-11-07" -> "Fr., 07.11." in the UI language (calendar date, no time zone shift)
function dayLabel(date, opts = { weekday: 'short', day: '2-digit', month: '2-digit' }) {
  const [y, m, d] = date.split('-').map(Number);
  return new Date(Date.UTC(y, m - 1, d, 12)).toLocaleDateString(getLang(), { ...opts, timeZone: 'UTC' });
}

// "YYYY-MM-DD" k days after a date (k < 0: before), calendar days
function addDays(date, k) {
  const [y, m, d] = date.split('-').map(Number);
  return new Date(Date.UTC(y, m - 1, d + k)).toISOString().slice(0, 10);
}
const pad2 = n => String(n).padStart(2, '0');
const browserToday = () => { const d = new Date(); return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())}`; };
// Query string of the non-empty filter values
const query = f => Object.entries(f).filter(([, v]) => v !== '' && v !== 0).map(([k, v]) => `${k}=${encodeURIComponent(v)}`).join('&');
// Query of a period: a number of days up to today or { from, to } (local days, both inclusive)
const periodQuery = p => (typeof p === 'object' ? { from_date: p.from, to_date: p.to } : { days: p });
const periodKey = p => (typeof p === 'object' ? p.from + '/' + p.to : String(p));

// Period of a card: 7, 14 or 30 days up to today, or "Period": any days within the last 90,
// with two date fields below, as wide as the buttons together. "Period" starts with the days
// shown so far. today: the device's day, if known.
function Period({ value, onChange, today }) {
  const custom = typeof value === 'object';
  const end = today || browserToday(), min = addDays(end, 1 - kMaxSpan);
  const clamp = v => (v < min ? min : v > end ? end : v);
  const pick = v => onChange(v !== 'custom' ? v : custom ? value : { from: addDays(end, 1 - value), to: end });
  // a date typed in part is empty: wait for the whole date
  const setFrom = v => { if (v) { v = clamp(v); onChange({ from: v, to: value.to < v ? v : value.to }); } };
  const setTo = v => { if (v) { v = clamp(v); onChange({ from: value.from > v ? v : value.from, to: v }); } };
  return html`<div class="period">
    <${Seg} value=${custom ? 'custom' : value} onChange=${pick}
      options=${[...kRanges.map(n => [n, t('hi.days', { n })]), ['custom', t('hi.custom')]]}/>
    ${custom && html`<div class="period-dates">
      <label>${t('hi.from')}<input type="date" min=${min} max=${value.to} value=${value.from} onChange=${e => setFrom(e.target.value)}/></label>
      <label>${t('hi.to')}<input type="date" min=${value.from} max=${end} value=${value.to} onChange=${e => setTo(e.target.value)}/></label>
    </div>`}
  </div>`;
}

// Axis maximum: 1, 2 or 5 × 10^k at or above v
function niceMax(v) {
  if (!(v > 0)) return 1;
  const p = 10 ** Math.floor(Math.log10(v));
  return [1, 2, 5, 10].map(f => f * p).find(x => x >= v - 1e-9);
}

// Width of an element in CSS pixels, updated on resize
function useWidth() {
  const ref = useRef(null);
  const [w, setW] = useState(0);
  useEffect(() => {
    const el = ref.current;
    if (!el || typeof ResizeObserver === 'undefined') return;
    const ro = new ResizeObserver(([e]) => setW(Math.floor(e.contentRect.width)));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);
  return [ref, w];
}

// Plot frame: left room for the ml labels, bottom room for the dates
const kM = { l: 34, r: 8, t: 26, b: 30 };
const kPlotH = 112;

// x labels: every day for a week, otherwise at least ~56 px apart (whole
// weeks from 30 days on), counted back from today so that today always
// carries a label
function xTicks(n, slot) {
  let step = n <= 7 ? 1 : Math.max(1, Math.ceil(56 / slot));
  if (n >= 30) step = Math.ceil(step / 7) * 7;
  const out = [];
  for (let k = n - 1; k >= 0; k -= step) out.push(k);
  return out;
}

function DayChart({ n, name, days, today, kind }) {
  const [hover, setHover] = useState(-1);
  const [ref, width] = useWidth();
  const i = n - 1;
  const vals = days.map(d => d.ml[i]);
  const max = niceMax(Math.max(...vals));
  const dayText = d => t('hi.day', { d: d.date === today ? t('hi.today') : dayLabel(d.date), ml: num(d.ml[i], 2), n: d.doses[i] })
    + (d.skipped[i] ? ' · ' + t('hi.day.skip', { n: d.skipped[i] }) : '');
  const sel = hover >= 0 ? days[hover] : null;
  const readout = sel ? dayText(sel) : '';  // the line stays reserved, the chart does not move

  const W = Math.max(width, 160), pw = W - kM.l - kM.r, base = kM.t + kPlotH;
  // Bars sit in one slot per day; the line runs from axis to axis, its
  // first day on the y axis and today at the right edge.
  const bars = kind === 'bar', last = days.length - 1;
  const slot = pw / days.length, step = last > 0 ? pw / last : 0;
  const xc = k => (bars ? kM.l + slot * (k + 0.5) : last > 0 ? kM.l + step * k : kM.l + pw / 2);
  const yv = v => base - (kPlotH * v) / max;
  const dec = max < 1 ? 2 : max < 10 ? 1 : 0;  // halves of 1/2/5 × 10^k stay exact
  const pick = e => {
    const x = e.clientX - e.currentTarget.getBoundingClientRect().left - kM.l;
    const k = bars ? Math.floor(x / slot) : last > 0 ? Math.round(x / step) : 0;
    setHover(x >= (bars ? 0 : -step / 2) && k >= 0 && k <= last ? k : -1);
  };
  const xLabel = k => days[k].date === today ? t('hi.today')
    : days.length <= 7 ? dayLabel(days[k].date, { weekday: 'short' }) : dayLabel(days[k].date, { day: '2-digit', month: '2-digit' });

  let marks;
  if (bars) {
    const gap = slot >= 8 ? 2 : 1, bw = Math.max(1, slot - gap);
    marks = days.map((d, k) => {
      const v = d.ml[i];
      if (!(v > 0)) return null;
      const x = kM.l + slot * k + gap / 2, y = yv(v), h = base - y, r = Math.min(4, bw / 2, h);
      return html`<path class="hc-bar" d=${`M${x} ${base}V${y + r}q0 ${-r} ${r} ${-r}h${bw - 2 * r}q${r} 0 ${r} ${r}V${base}Z`}/>`;
    });
  } else {
    const pts = days.map((d, k) => `${xc(k).toFixed(1)},${yv(d.ml[i]).toFixed(1)}`);
    marks = html`<path class="hc-area" d=${`M${xc(0)},${base}L${pts.join('L')}L${xc(days.length - 1)},${base}Z`}/>
      <polyline class="hc-line" points=${pts.join(' ')}/>
      ${days.length <= 14 && days.map((d, k) => html`<circle class="hc-pt" cx=${xc(k)} cy=${yv(d.ml[i])} r="3"/>`)}`;
  }

  return html`<div class="hc" style=${{ '--pc': pumpColor(n) }}>
    <div class="hc-head"><i class="sw"></i><b>${name}</b></div>
    <p class="hc-read small">${readout}</p>
    <div class="hc-plot" ref=${ref}>
      ${width > 0 && html`<svg width=${W} height=${base + kM.b} role="img" aria-label=${readout ? name + ': ' + readout : name}
          onPointerMove=${pick} onPointerDown=${pick} onPointerLeave=${() => setHover(-1)}>
        ${hover >= 0 && bars && html`<rect class="hc-hl" x=${kM.l + slot * hover} y=${kM.t} width=${slot} height=${kPlotH}/>`}
        ${[0, 0.5, 1].map(f => html`<line class=${f ? 'hc-grid-l' : 'hc-base'} x1=${kM.l} x2=${W - kM.r} y1=${yv(max * f)} y2=${yv(max * f)}/>
          <text class="hc-y" x=${kM.l - 6} y=${yv(max * f)}>${num(max * f, dec)}</text>`)}
        <text class="hc-unit" x=${kM.l - 6} y="7">ml</text>
        ${marks}
        ${hover >= 0 && !bars && html`<line class="hc-cross" x1=${xc(hover)} x2=${xc(hover)} y1=${kM.t} y2=${base}/>
          <circle class="hc-dot" cx=${xc(hover)} cy=${yv(days[hover].ml[i])} r="4.5"/>`}
        ${days.map((d, k) => d.skipped[i] > 0 && html`<circle class="hc-skip" cx=${xc(k)} cy=${base + 6} r="3"/>`)}
        ${xTicks(days.length, slot).map(k => html`<line class="hc-tick" x1=${xc(k)} x2=${xc(k)} y1=${base} y2=${base + 3}/>
          <text class="hc-x" x=${xc(k)} y=${base + 20}
            text-anchor=${xc(k) + 24 > W ? 'end' : xc(k) - 24 < 0 ? 'start' : 'middle'}>${xLabel(k)}</text>`)}
      </svg>`}
    </div>
  </div>`;
}

function DailyCard() {
  const s = useStore();
  const [range, setRange] = useState(14);
  const [kind, setKind] = useState(load('lgal.hichart', 'bar') === 'line' ? 'line' : 'bar');
  const pickKind = k => { setKind(k); save('lgal.hichart', k); };
  const [D, setD] = useState(null);
  useEffect(() => {
    let live = true;
    const get = () => api('GET', '/api/v1/history/daily?' + query(periodQuery(range))).then(j => live && setD(j)).catch(e => live && setD({ error: e }));
    get();
    const id = setInterval(get, 60000);
    return () => { live = false; clearInterval(id); };
  }, [periodKey(range)]);

  const head = html`<div class="row between hc-top"><h2>${t('hi.daily')}</h2>
    <div class="row hc-ctl">
      <${Seg} value=${kind} onChange=${pickKind} options=${[['line', t('hi.line')], ['bar', t('hi.bar')]]}/>
      <${Period} value=${range} onChange=${setRange} today=${D && D.today}/>
    </div></div>`;
  if (!D) return html`<section class="card">${head}<p class="mut">…</p></section>`;
  if (D.error) return html`<section class="card">${head}<p class="msg err">${errText(D.error)}</p></section>`;
  if (!D.today || !D.days.length) return html`<section class="card">${head}<p class="mut">${t('hi.notime')}</p></section>`;

  const pumps = fittedOf(s.pumps);
  const anySkip = D.days.some(d => d.skipped.some(x => x > 0));
  return html`<section class="card">${head}
    <div class="hc-grid">${pumps.map(p => html`<${DayChart} key=${p.pump} n=${p.pump} name=${pumpName(p)} days=${D.days} today=${D.today} kind=${kind}/>`)}</div>
    <p class="small mut">${t('hi.hover')} ${anySkip ? t('hi.skipnote') + ' ' : ''}${t('hi.keep', { n: D.keep_days })}</p>
    <details class="hc-table"><summary class="small">${t('hi.table')}</summary>
      <div class="scroll"><table class="num small">
        <thead><tr><th>${t('hi.date')}</th>${pumps.map(p => html`<th>${pumpName(p)}</th>`)}</tr></thead>
        <tbody>${D.days.slice().reverse().map(d => html`<tr><td>${dayLabel(d.date)}</td>
          ${pumps.map(p => html`<td>${num(d.ml[p.pump - 1], 2)}${d.skipped[p.pump - 1] ? html` <span class="warn">(${d.skipped[p.pump - 1]}×)</span>` : ''}</td>`)}</tr>`)}</tbody>
      </table></div>
    </details>
  </section>`;
}

function detail(e) {
  switch (e.kind) {
    case 'dose': case 'prime': case 'calib_run':
      return e.code === 'success' || e.kind === 'prime' ? t('hd.done', { d: num(e.done_ml, 2) })
        : t('hd.part', { d: num(e.done_ml, 2), t: num(e.target_ml, 2) });
    case 'skip': return e.count > 1 ? t('hd.slots', { n: e.count }) : e.target_ml ? t('hd.done', { d: num(e.target_ml, 2) }) : '';
    case 'container': return t('hd.ct', { r: num(e.remaining_ml, 0), c: num(e.capacity_ml, 0) });
    case 'calibration': return t('hd.cal', { v: num(e.ul_per_rev / 1000, 3), m: num(e.measured_ml, 2), t: num(e.target_ml, 2) });
    case 'tube': return t('hd.tube', { h: e.hours });
    case 'boost': {
      if (e.code !== 'started') return t('hd.part', { d: num(e.done_ml, 2), t: num(e.total_ml, 2) });
      return e.days ? t('hd.bo.days', { t: num(e.total_ml, 2), n: e.days }) : t('hd.bo.doses', { t: num(e.total_ml, 2), n: e.doses });
    }
    case 'mode': return t('mode.' + e.mode);
    case 'schedule': return e.code === 'set' ? t('hd.plan', { d: num(e.daily_ml, 2), n: e.doses }) : '';
    case 'locks': return t('hd.rules', { n: e.rules });
    case 'settings': return (e.changed || []).map(c => c === 'direction' && e.direction
      ? t('hd.ch.dir', { d: t('hd.dir.' + e.direction) }) : t('hd.ch.' + c)).join(', ');
    case 'system':
      if (e.code === 'boot') return t('hd.rs.' + e.reason);
      if (e.code === 'updated') return e.from ? t('hd.ver', { from: e.from, to: e.version }) : e.version || '';
      if (e.code === 'channels') return t('hd.pumps', { p: e.pumps.join(', ') });
      if (e.code === 'settings' && e.area) return t('hd.ar.' + e.area);
      return '';
    default: return '';
  }
}

function codeCls(e) {
  if (e.kind === 'alarm') return e.code === 'container_low' || e.code === 'tube_worn' ? 'warn' : 'err';
  if (e.kind === 'alarm_end') return 'ok';
  if (e.kind === 'system' && e.code === 'boot') return e.reason === 'crash' || e.reason === 'watchdog' || e.reason === 'brownout' ? 'warn' : 'mut';
  if (e.kind === 'skip') return 'warn';
  return kBad.includes(e.code) ? 'err' : 'mut';
}

function EventsCard({ pump }) {
  const s = useStore();
  const [f, setF] = useState({ pump: pump || 0, kind: '', period: 7 });
  const q = { pump: f.pump, kind: f.kind, ...periodQuery(f.period) };
  const [list, setList] = useState(null);
  const [more, setMore] = useState(false);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState(null);
  // Answers can overtake each other when the filter changes quickly: only the last request counts
  const last = useRef(0);
  const load = (offset, prev) => {
    const n = ++last.current;
    setBusy(true);
    api('GET', '/api/v1/history?' + query({ ...q, limit: kPage, offset }))
      .then(j => { if (n === last.current) { setList(prev.concat(j.events)); setMore(j.more); setErr(null); } })
      .catch(e => { if (n === last.current) setErr(e); })
      .finally(() => { if (n === last.current) setBusy(false); });
  };
  useEffect(() => { setList(null); load(0, []); }, [f.pump, f.kind, periodKey(f.period)]);
  const set = patch => setF(prev => ({ ...prev, ...patch }));
  const pumps = fittedOf(s.pumps);
  const nameOf = n => { const p = pumps.find(x => x.pump === n); return p ? pumpName(p) : t('pump', { n }); };

  return html`<section class="card">
    <div class="row between"><h2>${t('hi.events')}</h2>
      <a class="btn sec" href=${'/api/v1/history.csv?' + query(q)} download>${t('hi.csv')}</a></div>
    <div class="row hi-filter">
      <select class="small" value=${f.pump} onChange=${e => set({ pump: +e.target.value })} aria-label=${t('hi.allpumps')}>
        <option value="0">${t('hi.allpumps')}</option>
        ${pumps.map(p => html`<option value=${p.pump}>${pumpName(p)}</option>`)}
      </select>
      <select class="small" value=${f.kind} onChange=${e => set({ kind: e.target.value })} aria-label=${t('hi.events')}>
        <option value="">${t('hi.all')}</option>
        ${kKinds.map(k => html`<option value=${k}>${t('hk.' + k)}</option>`)}
      </select>
      <${Period} value=${f.period} onChange=${v => set({ period: v })}/>
    </div>
    ${err && html`<p class="msg err">${errText(err)}</p>`}
    ${!list ? html`<p class="mut">…</p>` : !list.length ? html`<p class="mut">${t('hi.none')}</p>` : html`
      <ul class="hi-list">${list.map(e => html`<li style=${{ '--pc': e.pump ? pumpColor(e.pump) : 'var(--mut)' }}>
        <span class="hi-t num small mut">${e.local.slice(8, 10)}.${e.local.slice(5, 7)}. ${hm(e.local.slice(11, 16))}</span>
        <span class="hi-p"><i class="sw"></i>${e.pump ? nameOf(e.pump) : t('hi.device')}</span>
        <span class="hi-k">${t('hk.' + e.kind)}${e.source && e.kind !== 'skip' ? html` <span class="small mut">· ${t('hs.' + e.source)}</span>` : ''}</span>
        <span class="hi-d num">${detail(e)}</span>
        <span class=${'hi-c small ' + codeCls(e)}>${t('hc.' + e.code)}</span>
      </li>`)}</ul>`}
    ${more && html`<button class="sec" disabled=${busy} onClick=${() => load(list.length, list)}>${t('hi.more')}</button>`}
  </section>`;
}

export function HistoryView({ pump }) {
  const prefs = usePrefs();
  return html`<div class="page-head"><h1>${t('hi.title')}</h1></div>
    <div class="grid">
      <${DailyCard}/>
      ${prefs.mode === 'pro' && html`<${EventsCard} pump=${pump}/>`}
    </div>`;
}
