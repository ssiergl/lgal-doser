// Overview of all pumps: container, today's amount, range, next dose, warnings.
import { html } from '../lib/html.js';
import { add, num, t } from '../lib/i18n.js';
import { Container, alerts, daysLeft, pumpColor, pumpName, status, whenLocal } from '../lib/pumps.js';
import { useStore } from '../lib/store.js';

add({
  en: {
    'dash.title': 'Overview', 'dash.today': 'Today', 'dash.range': 'Lasts', 'dash.next': 'Next',
    'dash.days': '{d} days', 'dash.day1': '< 1 day', 'dash.none': '–', 'dash.left': '{ml} ml left',
    'dash.run': '{done} of {target} ml', 'dash.prime': '{s} s · {ml} ml', 'dash.ok': 'Everything is fine',
    'dash.alerts': '{n} notes', 'dash.boost': 'Boost: {ml} ml to go',
  },
  de: {
    'dash.title': 'Übersicht', 'dash.today': 'Heute', 'dash.range': 'Reicht', 'dash.next': 'Nächste',
    'dash.days': '{d} Tage', 'dash.day1': '< 1 Tag', 'dash.none': '–', 'dash.left': 'noch {ml} ml',
    'dash.run': '{done} von {target} ml', 'dash.prime': '{s} s · {ml} ml', 'dash.ok': 'Alles in Ordnung',
    'dash.alerts': '{n} Hinweise', 'dash.boost': 'Boost: noch {ml} ml',
  },
});

function Range({ d }) {
  if (d === null) return html`<span class="mut">${t('dash.none')}</span>`;
  const cls = d < 3 ? 'err' : d < 7 ? 'warn' : '';
  return html`<span class=${cls}>${d < 1 ? t('dash.day1') : t('dash.days', { d: num(d, 0) })}</span>`;
}

function Today({ p, sp }) {
  const used = p.daily.used_ml, plan = sp ? sp.planned_today_ml : 0;
  const pct = plan > 0 ? Math.min(100, (used / plan) * 100) : used > 0 ? 100 : 0;
  return html`<div class="today">
    <div class="kv"><span class="mut">${t('dash.today')}</span>
      <b class="num">${num(used, 1)}<span class="mut"> / ${num(plan, 1)}</span> ml</b></div>
    <div class="bar"><i style=${{ width: pct + '%', background: pumpColor(p.pump) }}></i></div>
  </div>`;
}

function Running({ p }) {
  if (p.activity === 'idle') return null;
  const text = p.activity === 'priming'
    ? t('dash.prime', { s: num(p.prime_s, 0), ml: num(p.done_ml, 1) })
    : t('dash.run', { done: num(p.done_ml, 2), target: num(p.target_ml, 2) });
  const pct = p.target_ml ? Math.min(100, (p.done_ml / p.target_ml) * 100) : null;
  return html`<div class="running">
    <div class="small num">${text}</div>
    <div class=${'bar' + (pct === null ? ' indet' : '')}><i style=${{ width: (pct ?? 100) + '%' }}></i></div>
  </div>`;
}

function PumpCard({ p, sp, nowLocal }) {
  const st = status(p);
  const al = alerts(p, sp);
  const dim = p.mode === 'disabled';
  return html`<a class=${'card pump-card' + (dim ? ' dim' : '')} href=${'#/pump/' + p.pump}
      style=${{ '--pc': pumpColor(p.pump) }}>
    <div class="pc-head">
      <span class="badge">${p.pump}</span>
      <h2 title=${pumpName(p)}>${pumpName(p)}</h2>
      <span class=${'chip ' + st.cls}>${t(st.key)}</span>
    </div>
    <div class="pc-body">
      <${Container} p=${p}/>
      <div class="pc-stats">
        <${Today} p=${p} sp=${sp}/>
        <div class="kv"><span class="mut">${t('dash.range')}</span><b class="num"><${Range} d=${daysLeft(sp)}/></b></div>
        <div class="kv"><span class="mut">${t('dash.next')}</span>
          <b class="num">${sp && sp.next ? html`${whenLocal(sp.next.local, nowLocal)} <span class="mut">· ${num(sp.next.ml, 1)} ml</span>` : t('dash.none')}</b></div>
        ${p.container.tracked && html`<div class="mut small num">${t('dash.left', { ml: num(p.container.remaining_ml, 0) })}</div>`}
        ${sp && sp.boost && html`<div class="small num acc">${t('dash.boost', { ml: num(sp.boost.remaining_ml, 1) })}</div>`}
      </div>
    </div>
    <${Running} p=${p}/>
    ${al.length > 0 && html`<ul class="alerts">${al.slice(0, 2).map(a => html`<li class=${a.cls}>${a.text}</li>`)}
      ${al.length > 2 && html`<li class="more">+${al.length - 2}</li>`}</ul>`}
  </a>`;
}

export function Dashboard() {
  const s = useStore();
  if (!s.pumps) return html`<div class="empty">…</div>`;
  const sched = s.sched ? s.sched.pumps : [];
  const nowLocal = s.sched && s.sched.now_local;
  const all = s.pumps.flatMap((p, i) => alerts(p, sched[i]));
  const errs = all.filter(a => a.cls === 'err').length;
  return html`<div class="page-head">
      <h1>${t('dash.title')}</h1><span class="spacer"></span>
      ${all.length ? html`<span class=${'chip ' + (errs ? 'err' : 'warn')}>${t('dash.alerts', { n: all.length })}</span>`
        : html`<span class="chip ok">${t('dash.ok')}</span>`}
    </div>
    <div class="grid pumps">${s.pumps.map((p, i) => html`<${PumpCard} key=${p.pump} p=${p} sp=${sched[i]} nowLocal=${nowLocal}/>`)}</div>`;
}
