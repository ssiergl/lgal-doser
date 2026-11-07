// Detail view of one pump.
import { html } from '../lib/html.js';
import { add, t } from '../lib/i18n.js';
import { pumpName } from '../lib/pumps.js';
import { useStore } from '../lib/store.js';

add({
  en: { 'back': '← Overview', 'pump.none': 'There is no pump {n}.' },
  de: { 'back': '← Übersicht', 'pump.none': 'Pumpe {n} gibt es nicht.' },
});

export function PumpView({ pump }) {
  const s = useStore();
  const p = s.pumps && s.pumps[pump - 1];
  if (!s.pumps) return html`<div class="empty">…</div>`;
  if (!p) return html`<div class="empty">${t('pump.none', { n: pump })}</div>`;
  return html`<div class="page-head"><a href="#/">${t('back')}</a></div>
    <h1>${pumpName(p)}</h1>`;
}
