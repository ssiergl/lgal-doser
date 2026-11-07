// Schedules of all pumps.
import { html } from '../lib/html.js';
import { add, t } from '../lib/i18n.js';

add({
  en: { 'sched.title': 'Schedule' },
  de: { 'sched.title': 'Zeitplan' },
});

export function ScheduleView() {
  return html`<div class="page-head"><h1>${t('sched.title')}</h1></div>`;
}
