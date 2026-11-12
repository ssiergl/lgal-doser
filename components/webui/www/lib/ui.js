// Small shared UI pieces: messages, decimal input, segmented control, switch,
// save bar, hold button, confirmation dialog.
import { useEffect, useRef, useState } from 'preact/hooks';
import { html } from './html.js';
import { hub } from './hub.js';
import { add, errText, t } from './i18n.js';
import { Ic } from './pumps.js';

add({
  en: { 'ui.save': 'Save', 'ui.discard': 'Discard', 'ui.unsaved': 'Unsaved changes', 'ui.ok': 'Confirm', 'ui.cancel': 'Cancel' },
});

// Decimal text ("2,5" or "2.5") -> number, NaN if invalid
export function parseNum(s) {
  const v = String(s ?? '').trim().replace(',', '.');
  return /^\d+(\.\d+)?$/.test(v) ? Number(v) : NaN;
}

// Message line under a form: {ok, text} or an error
export function useMsg() {
  const [m, set] = useState(null);
  return {
    node: m ? html`<div class=${'msg ' + (m.ok ? 'ok' : 'err')} role="status">${m.text}</div>` : null,
    ok: text => set({ ok: true, text }),
    err: e => set({ ok: false, text: typeof e === 'string' ? e : errText(e) }),
    clear: () => set(null),
  };
}

export function Seg({ value, options, onChange, disabled }) {
  return html`<div class="seg" role="radiogroup">${options.map(([v, label]) => html`
    <button type="button" role="radio" aria-checked=${value === v} class=${value === v ? 'on' : ''}
      disabled=${disabled} onClick=${() => onChange(v)}>${label}</button>`)}</div>`;
}

// Language picker: a pill with the flag that opens a list of all languages.
// Flags and styles come from /flags.js (shared with the device pages);
// without it the picker shows the names only. options = [[code, name], …]
const flagOf = l => (window.lgalFlags && window.lgalFlags[l]
  ? html`<span class="flag" dangerouslySetInnerHTML=${{ __html: window.lgalFlags[l] }}/>` : null);
export function LangPicker({ value, options, onChange, left = false }) {
  const [open, setOpen] = useState(false);
  const box = useRef(null);
  useEffect(() => {
    if (window.lgalLangCss && !document.getElementById('lgp-css')) {
      const st = document.createElement('style');
      st.id = 'lgp-css';
      st.textContent = window.lgalLangCss;
      document.head.appendChild(st);
    }
  }, []);
  useEffect(() => {
    if (!open) return undefined;
    const sel = box.current.querySelector('[aria-selected=true]');
    if (sel) sel.focus();
    const away = e => { if (!box.current.contains(e.target)) setOpen(false); };
    document.addEventListener('click', away);
    return () => document.removeEventListener('click', away);
  }, [open]);
  const key = e => {
    if (e.key === 'Escape' && open) { setOpen(false); box.current.firstElementChild.focus(); }
    if ((e.key === 'ArrowDown' || e.key === 'ArrowUp') && open) {
      e.preventDefault();
      const items = [...box.current.querySelectorAll('.lgp-list button')], i = items.indexOf(document.activeElement);
      items[(i + (e.key === 'ArrowDown' ? 1 : items.length - 1)) % items.length].focus();
    }
  };
  const pick = l => { setOpen(false); box.current.firstElementChild.focus(); if (l !== value) onChange(l); };
  const name = (options.find(([c]) => c === value) || [value, value])[1];
  return html`<div class=${'lgp' + (left ? ' left' : '')} ref=${box} onKeyDown=${key}>
    <button type="button" class="lgp-btn" aria-haspopup="listbox" aria-expanded=${open} onClick=${() => setOpen(!open)}>
      ${flagOf(value)}<span>${name}</span>
      <svg class="chev" viewBox="0 0 12 8" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M1 1.5l5 5 5-5"/></svg>
    </button>
    <div class="lgp-list" role="listbox" hidden=${!open}>
      ${options.map(([c, n]) => html`<button type="button" role="option" lang=${c} aria-selected=${c === value} onClick=${() => pick(c)}>${flagOf(c)}<span>${n}</span></button>`)}
    </div>
  </div>`;
}

// On/off switch with its label on the left (a checkbox with role=switch)
export function Toggle({ checked, onChange, disabled, children }) {
  return html`<label class=${'switch' + (disabled ? ' off' : '')}><span class="sw-l">${children}</span>
    <input type="checkbox" role="switch" checked=${checked} disabled=${disabled} onChange=${e => onChange(e.target.checked)}/></label>`;
}

// Decimal input with unit; value is the text
export function Amount({ value, onInput, unit = 'ml', id, placeholder }) {
  return html`<div class="amount"><input id=${id} type="text" inputmode="decimal" autocomplete="off"
    value=${value} placeholder=${placeholder} onInput=${e => onInput(e.target.value)}/><span>${unit}</span></div>`;
}

// Save / discard row at the end of a settings form (type=submit saves)
export function SaveBar({ busy, dirty, onDiscard }) {
  return html`<div class="row savebar">
    <button type="submit" disabled=${busy || !dirty}>${t('ui.save')}</button>
    <button type="button" class="sec" disabled=${busy || !dirty} onClick=${onDiscard}>${t('ui.discard')}</button>
    ${dirty && html`<span class="chip warn">${t('ui.unsaved')}</span>`}
  </div>`;
}

// Hold-to-run button: onStart once on press, onBeat every `every` ms while
// held, onStop on release / cancel / unmount. onStart may return a promise;
// a rejection ends the hold.
export function HoldButton({ label, activeLabel, onStart, onBeat, onStop, every = 150, disabled, cls = '' }) {
  const [held, setHeld] = useState(false);
  const st = useRef({ held: false, timer: null, started: false });
  const stop = () => {
    const s = st.current;
    if (!s.held) return;
    s.held = false;
    clearInterval(s.timer);
    s.timer = null;
    setHeld(false);
    if (s.started) onStop();
    s.started = false;
  };
  const start = e => {
    e.preventDefault();
    const s = st.current;
    if (s.held || disabled) return;
    try { e.currentTarget.setPointerCapture(e.pointerId); } catch (x) { /* synthetic event */ }
    s.held = true;
    setHeld(true);
    Promise.resolve(onStart()).then(() => {
      if (!s.held) { onStop(); return; }  // released before the start was confirmed
      s.started = true;
      s.timer = setInterval(() => Promise.resolve(onBeat()).catch(stop), every);
    }).catch(() => { s.held = false; setHeld(false); });
  };
  useEffect(() => stop, []);
  const key = e => { if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) start(e); };
  const keyUp = e => { if (e.key === ' ' || e.key === 'Enter') stop(); };
  return html`<button type="button" class=${'hold ' + cls + (held ? ' held' : '')} disabled=${disabled}
    onPointerDown=${start} onPointerUp=${stop} onPointerCancel=${stop} onLostPointerCapture=${stop}
    onKeyDown=${key} onKeyUp=${keyUp} onBlur=${stop} onContextMenu=${e => e.preventDefault()}>
    ${held ? activeLabel : label}</button>`;
}

// Confirmation dialog in place of window.confirm(): ask(text, {ok, danger,
// title, warn, points}) resolves true or false. Optional: a title, the text
// as a highlighted warning, and a list of points below it. <DialogHost/>
// renders it once for the whole app.
const dialogs = hub();
let asking = null;   // {text, ok, danger, title, warn, points, resolve}

export function ask(text, { ok, danger = false, title = '', warn = false, points = null } = {}) {
  if (asking) asking.resolve(false);
  return new Promise(resolve => { asking = { text, ok, danger, title, warn, points, resolve }; dialogs.emit(); });
}

function answer(v) {
  if (!asking) return;
  const r = asking.resolve;
  asking = null;
  dialogs.emit();
  r(v);
}

export function DialogHost() {
  dialogs.use();
  const first = useRef(null);
  const a = asking;
  useEffect(() => {
    if (!a) return undefined;
    const back = document.activeElement;
    if (first.current) first.current.focus();
    const esc = e => { if (e.key === 'Escape') answer(false); };
    addEventListener('keydown', esc);
    return () => { removeEventListener('keydown', esc); if (back && back.focus) back.focus(); };
  }, [a]);
  if (!a) return null;
  // a dangerous action starts focused on "cancel", so Enter cannot trigger it by accident
  return html`<div class="overlay" onClick=${e => { if (e.target === e.currentTarget) answer(false); }}>
    <section class="card dialog ask" role="alertdialog" aria-modal="true" aria-describedby="ask-text"
      aria-labelledby=${a.title ? 'ask-title' : null}>
      ${a.title && html`<h2 id="ask-title">${a.title}</h2>`}
      <p id="ask-text" class=${a.warn ? 'ask-warn' : ''}>${a.warn && html`<${Ic} n="warn"/>`}<span>${a.text}</span></p>
      ${a.points && html`<ul class="ask-points">${a.points.map(x => html`<li>${x}</li>`)}</ul>`}
      <div class="row end">
        <button type="button" class="sec" ref=${a.danger ? first : null} onClick=${() => answer(false)}>${t('ui.cancel')}</button>
        <button type="button" class=${a.danger ? 'danger' : ''} ref=${a.danger ? null : first} onClick=${() => answer(true)}>${a.ok || t('ui.ok')}</button>
      </div>
    </section>
  </div>`;
}
