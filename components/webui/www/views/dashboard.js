// Overview of all pumps: container, today's amount, range, next dose; errors,
// warnings and a running boost only as small icon chips beside the status
// (the texts are on the pump page, one tap away).
import { html } from '../lib/html.js';
import { add, num, t } from '../lib/i18n.js';
import { Container, Ic, PumpStats, alerts, pumpColor, pumpName, status } from '../lib/pumps.js';
import { useStore } from '../lib/store.js';

add({
  en: {
    'dash.title': 'Overview', 'dash.left': '{ml} ml left',
    'dash.run': '{done} of {target} ml', 'dash.prime': '{s} s · {ml} ml', 'dash.boost': 'Boost running',
    'dash.err1': '1 error', 'dash.errs': '{n} errors', 'dash.warn1': '1 warning', 'dash.warns': '{n} warnings',
  },
  de: {
    'dash.title': 'Übersicht', 'dash.left': 'noch {ml} ml',
    'dash.run': '{done} von {target} ml', 'dash.prime': '{s} s · {ml} ml', 'dash.boost': 'Boost läuft',
    'dash.err1': '1 Fehler', 'dash.errs': '{n} Fehler', 'dash.warn1': '1 Warnung', 'dash.warns': '{n} Warnungen',
  },
});

function Running({ p }) {
  if (p.activity === 'idle') return null;
  const text = p.activity === 'priming'
    ? t('dash.prime', { s: num(p.prime_s, 0), ml: num(p.done_ml, 2) })
    : t('dash.run', { done: num(p.done_ml, 2), target: num(p.target_ml, 2) });
  const pct = p.target_ml ? Math.min(100, (p.done_ml / p.target_ml) * 100) : null;
  return html`<div class="running">
    <div class="small num">${text}</div>
    <div class=${'bar' + (pct === null ? ' indet' : '')}><i style=${{ width: (pct ?? 100) + '%' }}></i></div>
  </div>`;
}

const count = (n, one, many) => (n === 1 ? t(one) : t(many, { n }));

function IcChip({ cls, icon, label, n }) {
  return html`<span class=${'chip ic ' + cls} title=${label} aria-label=${label}><${Ic} n=${icon}/>${n}</span>`;
}

function PumpCard({ p, sp, nowLocal }) {
  const st = status(p);
  const al = alerts(p, sp);
  const errs = al.filter(a => a.cls === 'err').length, warns = al.length - errs;
  const boost = !!(sp && sp.boost);
  const running = p.activity !== 'idle';
  const dim = p.mode === 'disabled';
  return html`<a class=${'card pump-card' + (dim ? ' dim' : '')} href=${'#/pump/' + p.pump}
      style=${{ '--pc': pumpColor(p.pump) }}>
    <div class="pc-head">
      <span class="badge">${p.pump}</span>
      <h2 title=${pumpName(p)}>${pumpName(p)}</h2>
      ${errs > 0 && html`<${IcChip} cls="err" icon="err" n=${errs} label=${count(errs, 'dash.err1', 'dash.errs')}/>`}
      ${warns > 0 && html`<${IcChip} cls="warn" icon="warn" n=${warns} label=${count(warns, 'dash.warn1', 'dash.warns')}/>`}
      ${boost && html`<${IcChip} cls="acc" icon="boost" label=${t('dash.boost')}/>`}
      <span class=${'chip ' + st.cls}>${t(st.key)}</span>
    </div>
    <div class="pc-body">
      <${Container} p=${p} sp=${sp}/>
      <div class="pc-stats">
        <${PumpStats} p=${p} sp=${sp} nowLocal=${nowLocal}/>
        ${p.container.tracked && html`<div class="mut small num">${t('dash.left', { ml: num(p.container.remaining_ml, 0) })}</div>`}
      </div>
    </div>
    ${running && html`<div class="pc-foot"><${Running} p=${p}/></div>`}
  </a>`;
}

export function Dashboard() {
  const s = useStore();
  if (!s.pumps) return html`<div class="empty">…</div>`;
  const sched = s.sched ? s.sched.pumps : [];
  const nowLocal = s.sched && s.sched.now_local;
  return html`<div class="page-head"><h1>${t('dash.title')}</h1></div>
    <div class="grid pumps">${s.pumps.map((p, i) => html`<${PumpCard} key=${p.pump} p=${p} sp=${sched[i]} nowLocal=${nowLocal}/>`)}</div>`;
}
