// Overview of all pumps.
import { html } from '../lib/html.js';
import { add, num, t } from '../lib/i18n.js';
import { useStore } from '../lib/store.js';

add({
  en: { 'dash.title': 'Overview', 'pump': 'Pump {n}' },
  de: { 'dash.title': 'Übersicht', 'pump': 'Pumpe {n}' },
});

export function Dashboard() {
  const s = useStore();
  if (!s.pumps) return html`<div class="empty">…</div>`;
  return html`<div class="page-head"><h1>${t('dash.title')}</h1></div>
    <div class="grid">${s.pumps.map(p => html`
      <a class="card" href=${'#/pump/' + p.pump}>
        <h2>${p.name || t('pump', { n: p.pump })}</h2>
        <div class="mut">${p.activity} · ${num(p.container.remaining_ml, 0)} ml</div>
      </a>`)}</div>`;
}
