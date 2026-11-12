// Hidden engineering access (spec 11.1): clicking the firmware version in the
// footer 7 times opens this dialog. It asks for the per-device key and then
// opens the engineering menu (views/service.js); when the area is already
// unlocked it goes there at once. The device locks the area after 15 min
// without engineering use; a tab that did not unlock it locks it at start
// (tab closed = locked).
import { useEffect, useRef, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, t } from '../lib/i18n.js';
import { useMsg } from '../lib/ui.js';

add({
  en: {
    'eng.title': 'Engineering', 'eng.hint': 'Service area for the manufacturer. Needs the engineering key of this device.',
    'eng.key': 'Engineering key', 'eng.unlock': 'Unlock', 'eng.close': 'Close',
    'eng.wait': 'Too many attempts – please wait {s} s.',
    'err.wrong_key': 'Wrong engineering key',
  },
});

// The unlock belongs to this browser tab: sessionStorage survives a reload
// but not closing the tab.
const kTabKey = 'lgal.eng';
const markTab = on => { try { on ? sessionStorage.setItem(kTabKey, '1') : sessionStorage.removeItem(kTabKey); } catch (e) { /* no storage */ } };
const tabMarked = () => { try { return sessionStorage.getItem(kTabKey) === '1'; } catch (e) { return true; } };

export const lockEngineering = () => { markTab(false); return api('DELETE', '/api/v1/auth/engineering'); };

// At start: an unlock this tab did not make (tab closed and opened again,
// other tab) is locked.
export function lockForeignUnlock() {
  if (tabMarked()) return;
  api('GET', '/api/v1/auth/status')
    .then(s => { if (s.engineering && !tabMarked()) return lockEngineering(); })
    .catch(() => {});
}

// Key form; onDone after a successful unlock
export function EngUnlock({ onDone, autoFocus }) {
  const [key, setKey] = useState('');
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const input = useRef(null);
  useEffect(() => { if (autoFocus && input.current) input.current.focus(); }, []);
  const unlock = e => {
    e.preventDefault();
    setBusy(true);
    api('POST', '/api/v1/auth/engineering', { key: key.trim() })
      .then(() => { markTab(true); setKey(''); msg.clear(); onDone(); })
      .catch(e => {
        const s = e.body && e.body.error && e.body.error.retry_after_s;
        msg.err(s ? t('eng.wait', { s }) : errText(e));
      })
      .finally(() => setBusy(false));
  };
  return html`<p class="small mut">${t('eng.hint')}</p>
    <form class="row nowrap savebar" novalidate onSubmit=${unlock}>
      <input ref=${input} type="password" autocomplete="off" spellcheck="false" aria-label=${t('eng.key')}
        placeholder=${t('eng.key')} value=${key} onInput=${e => setKey(e.target.value)}/>
      <button type="submit" disabled=${busy || !key.trim()}>${t('eng.unlock')}</button>
    </form>
    ${msg.node}`;
}

export function EngDialog({ onClose }) {
  const [S, setS] = useState(null);
  const open = () => { location.hash = '#/engineering'; onClose(); };
  useEffect(() => {
    api('GET', '/api/v1/auth/status')
      .then(j => { if (j.engineering) open(); else setS(j); })
      .catch(e => setS({ error: e }));
    const esc = e => { if (e.key === 'Escape') onClose(); };
    addEventListener('keydown', esc);
    return () => removeEventListener('keydown', esc);
  }, []);

  let body;
  if (!S) body = html`<p class="mut">…</p>`;
  else if (S.error) body = html`<p class="msg err">${errText(S.error)}</p>`;
  else body = html`<${EngUnlock} autoFocus onDone=${open}/>`;
  return html`<div class="overlay" onClick=${e => { if (e.target === e.currentTarget) onClose(); }}>
    <section class="card dialog" role="dialog" aria-modal="true" aria-label=${t('eng.title')}>
      <div class="row"><h2>${t('eng.title')}</h2><span class="spacer"></span>
        <button type="button" class="ghost" aria-label=${t('eng.close')} title=${t('eng.close')} onClick=${onClose}>✕</button></div>
      ${body}
    </section>
  </div>`;
}
