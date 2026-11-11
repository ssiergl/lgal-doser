// History page: daily amounts as one small bar chart per pump (everyone),
// pro view additionally the event list with filter and CSV export.
import { useEffect, useRef, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { load, save } from '../lib/hub.js';
import { add, errText, getLang, num, t } from '../lib/i18n.js';
import { usePrefs } from '../lib/prefs.js';
import { pumpColor, pumpName } from '../lib/pumps.js';
import { useStore } from '../lib/store.js';
import { Seg } from '../lib/ui.js';

add({
  en: {
    'hi.title': 'History', 'hi.daily': 'Dosed per day', 'hi.days': '{n} days',
    'hi.sum': '{s} ml in total · ⌀ {a} ml/day', 'hi.hover': 'Point at or tap the chart for the values of a day.',
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
    'hc.rtc_fault': 'clock module does not answer', 'hc.set': 'changed', 'hc.refilled': 'refilled', 'hc.other': 'other',
  },
  de: {
    'hi.title': 'Verlauf', 'hi.daily': 'Dosiert pro Tag', 'hi.days': '{n} Tage',
    'hi.sum': 'Gesamt {s} ml · ⌀ {a} ml/Tag', 'hi.hover': 'Auf das Diagramm zeigen oder tippen für die Werte eines Tages.',
    'hi.bar': 'Balken', 'hi.line': 'Linie',
    'hi.day': '{d}: {ml} ml · {n} Dosen', 'hi.day.skip': '{n} ausgelassen', 'hi.today': 'heute',
    'hi.skipnote': 'Punkt unter der Achse: An diesem Tag wurden Dosen ausgelassen.', 'hi.table': 'Als Tabelle zeigen', 'hi.date': 'Tag',
    'hi.notime': 'Das Gerät hat keine gültige Uhrzeit – Tageswerte folgen, sobald die Uhrzeit stimmt.',
    'hi.keep': 'Wird {n} Tage aufbewahrt.',
    'hi.events': 'Ereignisse', 'hi.all': 'Alle', 'hi.allpumps': 'Alle Pumpen', 'hi.more': 'Mehr laden', 'hi.csv': 'CSV-Export',
    'hi.none': 'Keine Ereignisse in diesem Zeitraum.', 'hi.device': 'Gerät',
    'hk.dose': 'Dosis', 'hk.prime': 'Schlauch füllen', 'hk.calib_run': 'Kalibrierlauf', 'hk.skip': 'Ausgelassen',
    'hk.container': 'Behälter', 'hk.calibration': 'Kalibrierung', 'hk.tube': 'Schlauch', 'hk.alarm': 'Alarm',
    'hs.schedule': 'Zeitplan', 'hs.manual': 'manuell', 'hs.api': 'API', 'hs.boost': 'Boost', 'hs.mqtt': 'MQTT',
    'hd.done': '{d} ml', 'hd.part': '{d} von {t} ml', 'hd.slots': '{n} Dosen', 'hd.ct': '{r} von {c} ml',
    'hd.cal': '{v} ml/U · gemessen {m} von {t} ml', 'hd.tube': '{h} h',
    'hc.success': 'fertig', 'hc.aborted': 'gestoppt', 'hc.fault': 'Treiberfehler', 'hc.locked': 'gesperrt (Update)',
    'hc.heartbeat_lost': 'gestoppt (Verbindung)', 'hc.max_duration': 'gestoppt (max. Dauer)',
    'hc.missed': 'verpasst (Gerät aus)', 'hc.daily_limit': 'Tageslimit erreicht',
    'hc.delay_timeout': 'Sperrzeit zu lang', 'hc.pump_disabled': 'Pumpe aus', 'hc.pump_manual_only': 'nur manuell',
    'hc.driver_unavailable': 'Treiber nicht erreichbar', 'hc.time_invalid': 'keine gültige Uhrzeit', 'hc.dose_too_small': 'Dosis zu klein',
    'hc.dose_too_large': 'Dosis zu groß', 'hc.pump_busy': 'Pumpe belegt', 'hc.collision_lock': 'Sperrzeit',
    'hc.container_low': 'Behälter fast leer', 'hc.tube_worn': 'Schlauch tauschen',
    'hc.rtc_fault': 'Uhrenmodul antwortet nicht', 'hc.set': 'geändert', 'hc.refilled': 'nachgefüllt', 'hc.other': 'sonstiges',
  },
});

const kRanges = [7, 14, 30, 60];
const kKinds = ['dose', 'skip', 'prime', 'calib_run', 'container', 'calibration', 'tube', 'alarm'];
const kPage = 50;
const kBad = ['aborted', 'fault', 'locked', 'heartbeat_lost', 'max_duration', 'rtc_fault', 'daily_limit'];

// "2026-11-07" -> "Fr., 07.11." in the UI language (calendar date, no time zone shift)
function dayLabel(date, opts = { weekday: 'short', day: '2-digit', month: '2-digit' }) {
  const [y, m, d] = date.split('-').map(Number);
  return new Date(Date.UTC(y, m - 1, d, 12)).toLocaleDateString(getLang(), { ...opts, timeZone: 'UTC' });
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
  const total = vals.reduce((a, b) => a + b, 0);
  const dayText = d => t('hi.day', { d: d.date === today ? t('hi.today') : dayLabel(d.date), ml: num(d.ml[i], 2), n: d.doses[i] })
    + (d.skipped[i] ? ' · ' + t('hi.day.skip', { n: d.skipped[i] }) : '');
  const sel = hover >= 0 ? days[hover] : null;
  const readout = sel ? dayText(sel) : t('hi.sum', { s: num(total, 1), a: num(total / days.length, 2) });

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
    <p class=${'hc-read small' + (sel ? '' : ' mut')}>${readout}</p>
    <div class="hc-plot" ref=${ref}>
      ${width > 0 && html`<svg width=${W} height=${base + kM.b} role="img" aria-label=${name + ': ' + readout}
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
  const [kind, setKind] = useState(load('lgal.hichart', 'line') === 'bar' ? 'bar' : 'line');
  const pickKind = k => { setKind(k); save('lgal.hichart', k); };
  const [D, setD] = useState(null);
  useEffect(() => {
    let live = true;
    const get = () => api('GET', '/api/v1/history/daily?days=' + range).then(j => live && setD(j)).catch(e => live && setD({ error: e }));
    get();
    const id = setInterval(get, 60000);
    return () => { live = false; clearInterval(id); };
  }, [range]);

  const head = html`<div class="row between hc-top"><h2>${t('hi.daily')}</h2>
    <div class="row hc-ctl">
      <${Seg} value=${kind} onChange=${pickKind} options=${[['line', t('hi.line')], ['bar', t('hi.bar')]]}/>
      <${Seg} value=${range} onChange=${setRange} options=${kRanges.map(n => [n, t('hi.days', { n })])}/>
    </div></div>`;
  if (!D) return html`<section class="card">${head}<p class="mut">…</p></section>`;
  if (D.error) return html`<section class="card">${head}<p class="msg err">${errText(D.error)}</p></section>`;
  if (!D.today || !D.days.length) return html`<section class="card">${head}<p class="mut">${t('hi.notime')}</p></section>`;

  const pumps = s.pumps || [];
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
    case 'calibration': return t('hd.cal', { v: num(e.ul_per_rev / 1000, 3), m: num(e.measured_ml, 1), t: num(e.target_ml, 1) });
    case 'tube': return t('hd.tube', { h: e.hours });
    default: return '';
  }
}

function codeCls(e) {
  if (e.kind === 'alarm') return e.code === 'container_low' || e.code === 'tube_worn' ? 'warn' : 'err';
  if (e.kind === 'skip') return 'warn';
  return kBad.includes(e.code) ? 'err' : 'mut';
}

const query = f => Object.entries(f).filter(([, v]) => v !== '' && v !== 0).map(([k, v]) => `${k}=${encodeURIComponent(v)}`).join('&');

function EventsCard({ pump }) {
  const s = useStore();
  const [f, setF] = useState({ pump: pump || 0, kind: '', days: 7 });
  const [list, setList] = useState(null);
  const [more, setMore] = useState(false);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState(null);
  const load = (offset, prev) => {
    setBusy(true);
    api('GET', '/api/v1/history?' + query({ ...f, limit: kPage, offset }))
      .then(j => { setList(prev.concat(j.events)); setMore(j.more); setErr(null); })
      .catch(setErr).finally(() => setBusy(false));
  };
  useEffect(() => { setList(null); load(0, []); }, [f.pump, f.kind, f.days]);
  const set = patch => setF(prev => ({ ...prev, ...patch }));
  const pumps = s.pumps || [];
  const nameOf = n => { const p = pumps.find(x => x.pump === n); return p ? pumpName(p) : t('pump', { n }); };

  return html`<section class="card">
    <div class="row between"><h2>${t('hi.events')}</h2>
      <a class="btn sec" href=${'/api/v1/history.csv?' + query(f)} download>${t('hi.csv')}</a></div>
    <div class="row hi-filter">
      <select class="small" value=${f.pump} onChange=${e => set({ pump: +e.target.value })} aria-label=${t('hi.allpumps')}>
        <option value="0">${t('hi.allpumps')}</option>
        ${[1, 2, 3, 4, 5].map(n => html`<option value=${n}>${nameOf(n)}</option>`)}
      </select>
      <select class="small" value=${f.kind} onChange=${e => set({ kind: e.target.value })} aria-label=${t('hi.events')}>
        <option value="">${t('hi.all')}</option>
        ${kKinds.map(k => html`<option value=${k}>${t('hk.' + k)}</option>`)}
      </select>
      <${Seg} value=${f.days} onChange=${v => set({ days: v })} options=${[7, 30, 60].map(n => [n, t('hi.days', { n })])}/>
    </div>
    ${err && html`<p class="msg err">${errText(err)}</p>`}
    ${!list ? html`<p class="mut">…</p>` : !list.length ? html`<p class="mut">${t('hi.none')}</p>` : html`
      <ul class="hi-list">${list.map(e => html`<li style=${{ '--pc': e.pump ? pumpColor(e.pump) : 'var(--mut)' }}>
        <span class="hi-t num small mut">${e.local.slice(8, 10)}.${e.local.slice(5, 7)}. ${e.local.slice(11, 16)}</span>
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
