// Small shared UI pieces: messages, decimal input, segmented control, hold button.
import { useEffect, useRef, useState } from 'preact/hooks';
import { html } from './html.js';
import { errText } from './i18n.js';

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

// Decimal input with unit; value is the text
export function Amount({ value, onInput, unit = 'ml', id, placeholder }) {
  return html`<div class="amount"><input id=${id} type="text" inputmode="decimal" autocomplete="off"
    value=${value} placeholder=${placeholder} onInput=${e => onInput(e.target.value)}/><span>${unit}</span></div>`;
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
