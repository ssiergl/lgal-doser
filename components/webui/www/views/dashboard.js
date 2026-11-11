// Overview of all pumps: container, today's amount, range, next dose, warnings.
import { html } from '../lib/html.js';
import { add, num, t } from '../lib/i18n.js';
import { Container, PumpStats, alerts, pumpColor, pumpName, status } from '../lib/pumps.js';
import { useStore } from '../lib/store.js';

add({
  en: {
    'dash.title': 'Overview', 'dash.left': '{ml} ml left',
    'dash.run': '{done} of {target} ml', 'dash.prime': '{s} s · {ml} ml', 'dash.ok': 'Everything is fine',
    'dash.alerts': '{n} notes', 'dash.boost': 'Boost: {ml} ml to go',
  },
  de: {
    'dash.title': 'Übersicht', 'dash.left': 'noch {ml} ml',
    'dash.run': '{done} von {target} ml', 'dash.prime': '{s} s · {ml} ml', 'dash.ok': 'Alles in Ordnung',
    'dash.alerts': '{n} Hinweise', 'dash.boost': 'Boost: noch {ml} ml',
  },
});

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
      <${Container} p=${p} sp=${sp}/>
      <div class="pc-stats">
        <${PumpStats} p=${p} sp=${sp} nowLocal=${nowLocal}/>
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
