// Shared pump helpers: names, colours, state, warnings, the container graphic.
import { html } from './html.js';
import { add, getLang, num, t } from './i18n.js';
import { hm as clock } from './prefs.js';

add({
  en: {
    'pump': 'Pump {n}',
    'st.idle': 'Ready', 'st.dosing': 'Dosing', 'st.priming': 'Priming', 'st.calibrating': 'Calibrating',
    'st.disabled': 'Off', 'st.manual_only': 'Manual only',
    'al.low': 'Container almost empty', 'al.days': 'Lasts {d} more days',
    'al.uncal': 'Not calibrated', 'al.recal': 'Calibration older than {m} months', 'al.limit': 'Daily limit reached', 'al.tube': 'Replace the tube',
    'al.fault': 'Last run stopped: driver fault',
    'al.delayed': 'Dose waiting (lock time)', 'al.skipped': '{n} doses skipped today', 'al.calib': 'Enter the measured amount',
    'day.tomorrow': 'tomorrow',
    'ct.untracked': 'not tracked',
    'ov.today': 'Today', 'ov.range': 'Lasts', 'ov.next': 'Next', 'ov.none': '–',
    'ov.days': '{d} days', 'ov.day1': '< 1 day', 'ov.next.boost': 'incl. {ml} ml boost', 'ov.boost.note': 'incl. boost (+{ml} ml per dose)',
  },
  de: {
    'pump': 'Pumpe {n}',
    'st.idle': 'Bereit', 'st.dosing': 'Dosiert', 'st.priming': 'Entlüftet', 'st.calibrating': 'Kalibriert',
    'st.disabled': 'Aus', 'st.manual_only': 'Nur manuell',
    'al.low': 'Behälter fast leer', 'al.days': 'Reicht noch {d} Tage',
    'al.uncal': 'Nicht kalibriert', 'al.recal': 'Kalibrierung älter als {m} Monate', 'al.limit': 'Tageslimit erreicht', 'al.tube': 'Schlauch tauschen',
    'al.fault': 'Letzter Lauf abgebrochen: Treiberfehler',
    'al.delayed': 'Dosis wartet (Sperrzeit)', 'al.skipped': 'Heute {n} Dosen ausgelassen', 'al.calib': 'Gemessene Menge eintragen',
    'day.tomorrow': 'morgen',
    'ct.untracked': 'nicht erfasst',
    'ov.today': 'Heute', 'ov.range': 'Reicht', 'ov.next': 'Nächste', 'ov.none': '–',
    'ov.days': '{d} Tage', 'ov.day1': '< 1 Tag', 'ov.next.boost': 'inkl. {ml} ml Boost', 'ov.boost.note': 'inkl. Boost (+{ml} ml je Dosis)',
  },
});

export const pumpName = p => p.name || t('pump', { n: p.pump });
export const pumpColor = n => `var(--p${n})`;

// Status chip: activity first, then mode
export function status(p) {
  if (p.activity !== 'idle') return { key: 'st.' + p.activity, cls: p.activity === 'dosing' ? 'acc' : 'svc' };
  if (p.mode === 'disabled') return { key: 'st.disabled', cls: '' };
  if (p.mode === 'manual_only') return { key: 'st.manual_only', cls: 'warn' };
  return { key: 'st.idle', cls: 'ok' };
}

// Days the container lasts with the schedule (null = unknown)
export const daysLeft = sp => (sp && sp.remaining_days !== null && sp.remaining_days !== undefined ? sp.remaining_days : null);

// Colour class of a supply in days against the container thresholds in
// effect (device values, 0 = off; defaults for older firmware)
export const daysCls = (d, c) => d === null ? '' : d < (c.low_days ?? 4) ? 'err' : d < (c.warn_days ?? 7) ? 'warn' : '';

// Line icons (chips on the dashboard, notice cards)
const ICON = {
  err: 'M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18zM12 7.5v5.5M12 16.5v.01',
  warn: 'M12 3.5 2.5 20h19L12 3.5zM12 10v4.5M12 17.5v.01',
  boost: 'M6 12.5l6-6 6 6M6 19l6-6 6 6',  // double chevron: dosing raised
};
export const Ic = ({ n }) => html`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"
  stroke-linejoin="round" stroke-linecap="round" aria-hidden="true"><path d=${ICON[n]}/></svg>`;

// Warnings of a pump, most important first: [{cls: 'err'|'warn', text}]
export function alerts(p, sp) {
  const out = [];
  const c = p.container;
  const d = daysLeft(sp);
  if (p.last && p.last.result === 'fault') out.push({ cls: 'err', text: t('al.fault') });
  if (p.calibration.pending) out.push({ cls: 'warn', text: t('al.calib') });
  const dc = c.tracked ? daysCls(d, c) : '';
  if (dc === 'err') out.push({ cls: 'err', text: t('al.low') });
  else if (dc === 'warn') out.push({ cls: 'warn', text: t('al.days', { d: num(d, 0) }) });
  if (sp && sp.delayed) out.push({ cls: 'warn', text: t('al.delayed') });
  if (p.daily.limit_ml && p.daily.used_ml >= p.daily.limit_ml) out.push({ cls: 'warn', text: t('al.limit') });
  if (sp && sp.skipped) out.push({ cls: 'warn', text: t('al.skipped', { n: sp.skipped }) });
  if (!p.calibration.calibrated && p.mode !== 'disabled') out.push({ cls: 'warn', text: t('al.uncal') });
  else if (p.calibration.due && p.mode !== 'disabled') out.push({ cls: 'warn', text: t('al.recal', { m: p.calibration.remind_months }) });
  if (p.tube && p.tube.percent >= 100) out.push({ cls: 'warn', text: t('al.tube') });
  return out;
}

// "2026-11-12 08:00" -> "08:00" today, "tomorrow 08:00", "Wed 08:00" within
// a week, otherwise "19.11. 08:00" (calendar dates, no time zone shift;
// times in the chosen clock)
export function whenLocal(local, nowLocal) {
  if (!local) return '–';
  const [d, time] = local.split(' ');
  const hm = clock(time);
  const today = nowLocal ? nowLocal.split(' ')[0] : '';
  if (d === today) return hm;
  if (today) {
    const days = Math.round((Date.parse(d + 'T12:00:00Z') - Date.parse(today + 'T12:00:00Z')) / 86400000);
    if (days === 1) return t('day.tomorrow') + ' ' + hm;
    if (days > 1 && days < 7) return new Date(d + 'T12:00:00Z').toLocaleDateString(getLang(), { weekday: 'short', timeZone: 'UTC' }) + ' ' + hm;
  }
  const [, m, day] = d.split('-');
  return `${+day}.${+m}. ${hm}`;
}

// Pump switcher in the pump colours; base is the link prefix ('#/pump/')
export function PumpTabs({ current, pumps, base = '#/pump/' }) {
  return html`<nav class="pump-tabs">${pumps.map(p => html`
    <a href=${base + p.pump} class=${p.pump === current ? 'on' : ''} style=${{ '--pc': pumpColor(p.pump) }}>
      <span class="badge">${p.pump}</span><span class="nm">${pumpName(p)}</span></a>`)}</nav>`;
}

// ---------------------------------------------------------------------------
// Container graphic: canister with liquid level in the pump colour

let clipSeq = 0;

// Liquid surface: a wave with a period of 60 units, long enough to slide one
// full period (CSS .wave) without a gap at either side of the canister
const wavePath = (y, bottom) => `M-70 ${y} q15 -5 30 0 t30 0 t30 0 t30 0 t30 0 t30 0 t30 0 t30 0 V${bottom} H-70 Z`;

// sp: schedule status of the pump (range in days for the outline colour)
export function Container({ p, sp, size = 'm', style }) {
  const c = p.container;
  const id = 'ct' + (++clipSeq);
  const pct = c.tracked ? Math.max(0, Math.min(100, c.percent || 0)) : 0;
  const top = 24, bottom = 154, h = bottom - top;
  const y = bottom - (h * pct) / 100;
  const color = pumpColor(p.pump);
  const level = !c.tracked ? 'none' : daysCls(daysLeft(sp), c);
  return html`<svg class=${'canister ' + size} style=${style} viewBox="0 0 120 160" role="img"
      aria-label=${c.tracked ? num(pct, 0) + ' %' : t('ct.untracked')}>
    <defs><clipPath id=${id}><rect x="14" y="24" width="92" height="130" rx="16"/></clipPath></defs>
    <rect x="44" y="3" width="32" height="12" rx="3" class="cap"/>
    <rect x="49" y="13" width="22" height="12" class="neck"/>
    <rect x="14" y="24" width="92" height="130" rx="16" class="body"/>
    ${c.tracked && html`<g clip-path=${`url(#${id})`}>
      <rect x="0" y=${y} width="120" height=${bottom - y + 2} fill=${color} opacity=".38"/>
      ${pct > 0 && pct < 100 && ['wave back', 'wave'].map(cls => html`<path class=${cls + (p.activity !== 'idle' ? ' run' : '')}
        d=${wavePath(y, bottom)} fill=${color} opacity=${cls === 'wave' ? '.55' : '.3'}/>`)}
    </g>`}
    ${[0.25, 0.5, 0.75].map(f => html`<line x1="94" x2="104" y1=${bottom - h * f} y2=${bottom - h * f} class="tick"/>`)}
    <rect x="14" y="24" width="92" height="130" rx="16" class=${'outline ' + level}/>
    <text x="60" y="96" class="pct">${c.tracked ? num(pct, 0) + '%' : '–'}</text>
  </svg>`;
}

// Today's amount with bar, range and next dose (dashboard card, container card).
// With a boost today's plan and the next dose include it (turquoise);
// boostNote: say so in a line below (else only a tooltip)
export function PumpStats({ p, sp, nowLocal, boostNote }) {
  const b = sp && sp.boost && sp.boost.active ? sp.boost : null;
  const bt = b ? b.today_ml : 0;
  const used = p.daily.used_ml, plan = (sp ? sp.planned_today_ml : 0) + bt;
  const pct = plan > 0 ? Math.min(100, (used / plan) * 100) : used > 0 ? 100 : 0;
  const d = daysLeft(sp), nx = sp && sp.next;
  const none = html`<span class="mut">${t('ov.none')}</span>`;
  return html`<div class="today">
      <div class="kv"><span class="mut">${t('ov.today')}</span>
        <b class="num">${num(used, 1)}<span class=${bt > 0 ? 'acc' : 'mut'}
          title=${bt > 0 ? t('ov.next.boost', { ml: num(bt, 1) }) : null}> / ${num(plan, 1)}</span> ml</b></div>
      <div class="bar"><i style=${{ width: pct + '%', background: pumpColor(p.pump) }}></i></div>
    </div>
    <div class="kv"><span class="mut">${t('ov.range')}</span><b class="num">${d === null ? none
      : html`<span class=${daysCls(d, p.container)}>${d < 1 ? t('ov.day1') : t('ov.days', { d: num(d, 0) })}</span>`}</b></div>
    <div class="kv"><span class="mut">${t('ov.next')}</span>
      <b class="num">${nx ? html`${whenLocal(nx.local, nowLocal)} <span class=${nx.boost_ml ? 'acc' : 'mut'}
        title=${nx.boost_ml ? t('ov.next.boost', { ml: num(nx.boost_ml, 1) }) : null}>· ${num(nx.ml, 1)} ml</span>` : none}</b></div>
    ${boostNote && b && html`<div class="small acc num nx-boost">${t('ov.boost.note', { ml: num(b.per_dose_ml, 2) })}</div>`}`;
}
