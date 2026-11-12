// Lock times between pumps (settings page, pro view): pairs of pumps that must not dose
// close together (e.g. calcium and alkalinity), and what a manual dose does
// while a lock time is running. Saved schedules are checked against the
// rules by the device; collisions come back as conflicts.
import { useEffect, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, t } from '../lib/i18n.js';
import { pumpColor, pumpName, fittedOf } from '../lib/pumps.js';
import { reloadSched, useStore } from '../lib/store.js';
import { Amount, Seg, useMsg } from '../lib/ui.js';
import { errorText } from './schedule.js';

const kMaxLocks = 10;

add({
  en: {
    'lk.title': 'Lock times between pumps',
    'lk.intro': 'A lock time keeps two pumps apart: after a dose of one pump ends, the other one starts at the earliest once the lock time has passed. Use it for additives that must not meet in the same water flow.',
    'lk.none': 'No lock times – all pumps may dose at the same time.',
    'lk.add': 'Add rule', 'lk.del': 'Delete rule',
    'lk.manual': 'Manual dose during a lock time', 'lk.m.reject': 'Reject', 'lk.m.queue': 'Wait',
    'lk.m.hint.reject': 'The dose is refused with a note when it may start.',
    'lk.m.hint.queue': 'The dose waits and starts by itself once the lock time has passed.',
    'lk.saved': 'Lock times saved', 'lk.rule': 'Rule {n}: {e}',
    'lk.err.same': 'Choose two different pumps', 'lk.err.gap': 'Lock time: 1 to 1440 minutes',
    'lk.err.dup': 'These pumps already have a rule',
    'err.invalid_lock': 'Invalid lock rule',
  },
});

const draftOf = l => ({ manual: l.manual, rules: l.rules.map(r => ({ a: r.pumps[0], b: r.pumps[1], gap: String(r.gap_min) })) });
const bodyOf = d => ({ manual: d.manual, rules: d.rules.map(r => ({ pumps: [+r.a, +r.b], gap_min: parseInt(r.gap, 10) || 0 })) });

// First faulty rule: {i, key} or null
function check(d) {
  const seen = new Set();
  for (let i = 0; i < d.rules.length; ++i) {
    const r = d.rules[i], gap = Number(r.gap);
    if (+r.a === +r.b) return { i, key: 'lk.err.same' };
    if (!(Number.isInteger(gap) && gap >= 1 && gap <= 1440)) return { i, key: 'lk.err.gap' };
    const pair = Math.min(r.a, r.b) + '-' + Math.max(r.a, r.b);
    if (seen.has(pair)) return { i, key: 'lk.err.dup' };
    seen.add(pair);
  }
  return null;
}

function PumpSelect({ value, pumps, onChange }) {
  return html`<select value=${value} style=${{ '--pc': pumpColor(+value) }} onChange=${e => onChange(+e.target.value)}>
    ${pumps.map(p => html`<option value=${p.pump}>${p.pump} · ${pumpName(p)}</option>`)}</select>`;
}

export function LocksCard() {
  const st = useStore();
  const [saved, setSaved] = useState(null);
  const [d, setDraft] = useState(null);
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  useEffect(() => {
    api('GET', '/api/v1/schedule/locks').then(l => { setSaved(l); setDraft(draftOf(l)); }).catch(e => setSaved({ error: e }));
  }, []);
  const head = html`<h2>${t('lk.title')}</h2><p class="small mut">${t('lk.intro')}</p>`;
  if (!st.pumps || !saved) return html`<section class="card">${head}<p class="mut">…</p></section>`;
  if (saved.error) return html`<section class="card">${head}<p class="msg err">${errText(saved.error)}</p></section>`;

  const fit = fittedOf(st.pumps);
  const set = patch => setDraft(prev => ({ ...prev, ...patch }));
  const setRule = (i, patch) => setDraft(prev => ({ ...prev, rules: prev.rules.map((r, k) => (k === i ? { ...r, ...patch } : r)) }));
  const addRule = () => {
    // first pair without a rule
    const used = new Set(d.rules.map(r => Math.min(r.a, r.b) + '-' + Math.max(r.a, r.b)));
    const pairs = [];
    const ns = fit.map(p => p.pump);
    ns.forEach((x, i) => ns.slice(i + 1).forEach(y => pairs.push([x, y])));
    const [a, b] = pairs.find(([x, y]) => !used.has(x + '-' + y)) || [1, 2];
    set({ rules: [...d.rules, { a, b, gap: '5' }] });
  };
  const bad = check(d);
  const dirty = JSON.stringify(bodyOf(d)) !== JSON.stringify(bodyOf(draftOf(saved)));
  const submit = e => {
    e.preventDefault();
    if (bad) { msg.err(t('lk.rule', { n: bad.i + 1, e: t(bad.key) })); return; }
    setBusy(true);
    const body = bodyOf(d);
    api('PUT', '/api/v1/schedule/locks', body)
      .then(l => { setSaved(l); setDraft(draftOf(l)); msg.ok(t('lk.saved')); reloadSched(); })
      .catch(e => {
        const rule = e.body && e.body.error && e.body.error.rule;
        msg.err(rule ? t('lk.rule', { n: rule, e: errText(e) }) : errorText(e, 5, body));
      })
      .finally(() => setBusy(false));
  };
  return html`<form class="card locks" novalidate onSubmit=${submit}>
      ${head}
      ${!d.rules.length && html`<p class="small mut">${t('lk.none')}</p>`}
      <div class="lrules">${d.rules.map((r, i) => html`<div class=${'lrow' + (bad && bad.i === i ? ' bad' : '')} key=${i}>
        <${PumpSelect} value=${r.a} pumps=${fit} onChange=${v => setRule(i, { a: v })}/>
        <span class="mut" aria-hidden="true">↔</span>
        <${PumpSelect} value=${r.b} pumps=${fit} onChange=${v => setRule(i, { b: v })}/>
        <${Amount} value=${r.gap} onInput=${v => setRule(i, { gap: v })} unit="min"/>
        <button type="button" class="ghost" title=${t('lk.del')} aria-label=${t('lk.del')}
          onClick=${() => set({ rules: d.rules.filter((_, k) => k !== i) })}>✕</button>
      </div>`)}</div>
      ${bad && html`<p class="msg err">${t('lk.rule', { n: bad.i + 1, e: t(bad.key) })}</p>`}
      <button type="button" class="sec" onClick=${addRule} disabled=${d.rules.length >= kMaxLocks}>+ ${t('lk.add')}</button>
      <div class="field"><span class="lbl">${t('lk.manual')}</span>
        <${Seg} value=${d.manual} onChange=${v => set({ manual: v })}
          options=${[['reject', t('lk.m.reject')], ['queue', t('lk.m.queue')]]}/>
        <span class="mut small">${t('lk.m.hint.' + d.manual)}</span>
      </div>
      <div class="row savebar">
        <button type="submit" disabled=${busy || !dirty}>${t('sc.save')}</button>
        <button type="button" class="sec" disabled=${busy || !dirty} onClick=${() => { setDraft(draftOf(saved)); msg.clear(); }}>${t('sc.discard')}</button>
        ${dirty && html`<span class="chip warn">${t('sc.unsaved')}</span>`}
      </div>
      ${msg.node}
    </form>`;
}
