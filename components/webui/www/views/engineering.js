// Hidden engineering access (spec 11.1): opened by clicking the firmware
// version in the footer 7 times. Unlocks the engineering area with the
// per-device key and lists the service pages. It locks itself after 15 min
// without engineering use.
import { useEffect, useRef, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, t } from '../lib/i18n.js';
import { useStore } from '../lib/store.js';
import { useMsg } from '../lib/ui.js';

add({
  en: {
    'eng.title': 'Engineering', 'eng.hint': 'Service area for the manufacturer. Needs the engineering key of this device.',
    'eng.key': 'Engineering key', 'eng.unlock': 'Unlock', 'eng.lock': 'Lock now', 'eng.close': 'Close',
    'eng.on': 'Unlocked – locks after {m} min without engineering use.', 'eng.pages': 'Service pages',
    'eng.wait': 'Too many attempts – please wait {s} s.',
    'link.dosing': 'Dosing test', 'link.schedule': 'Schedule test', 'link.hwtest': 'Hardware test',
    'err.wrong_key': 'Wrong engineering key', 
  },
  de: {
    'eng.title': 'Engineering', 'eng.hint': 'Servicebereich für den Hersteller. Braucht den Engineering-Schlüssel dieses Geräts.',
    'eng.key': 'Engineering-Schlüssel', 'eng.unlock': 'Freischalten', 'eng.lock': 'Jetzt sperren', 'eng.close': 'Schließen',
    'eng.on': 'Freigeschaltet – sperrt nach {m} min ohne Engineering-Nutzung.', 'eng.pages': 'Serviceseiten',
    'eng.wait': 'Zu viele Versuche – bitte {s} s warten.',
    'link.dosing': 'Dosiertest', 'link.schedule': 'Zeitplantest', 'link.hwtest': 'Hardwaretest',
    'err.wrong_key': 'Falscher Engineering-Schlüssel', 
  },
});

// Links of the system info that need the engineering area
const kServiceLinks = ['dosing', 'schedule', 'hwtest'];

export function EngDialog({ onClose }) {
  const st = useStore();
  const [S, setS] = useState(null);
  const [key, setKey] = useState('');
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const input = useRef(null);
  const load = () => api('GET', '/api/v1/auth/status').then(setS).catch(e => setS({ error: e }));
  useEffect(() => {
    load();
    const esc = e => { if (e.key === 'Escape') onClose(); };
    addEventListener('keydown', esc);
    return () => removeEventListener('keydown', esc);
  }, []);
  useEffect(() => { if (S && !S.engineering && input.current) input.current.focus(); }, [S]);

  const unlock = e => {
    e.preventDefault();
    setBusy(true);
    api('POST', '/api/v1/auth/engineering', { key: key.trim() })
      .then(() => { setKey(''); msg.clear(); return load(); })
      .catch(e => {
        const s = e.body && e.body.error && e.body.error.retry_after_s;
        msg.err(s ? t('eng.wait', { s }) : errText(e));
      })
      .finally(() => setBusy(false));
  };
  const lock = () => api('DELETE', '/api/v1/auth/engineering').then(load).catch(e => msg.err(e));
  const links = ((st.info && st.info.links) || []).filter(l => kServiceLinks.includes(l.id));
  const name = l => { const k = 'link.' + l.id, x = t(k); return x === k ? l.name : x; };

  let body;
  if (!S) body = html`<p class="mut">…</p>`;
  else if (S.error) body = html`<p class="msg err">${errText(S.error)}</p>`;
  else if (S.engineering) {
    body = html`<p class="small ok">${t('eng.on', { m: Math.max(1, Math.ceil(S.engineering_left_s / 60)) })}</p>
      ${links.length > 0 && html`<h3>${t('eng.pages')}</h3>
        <ul class="links">${links.map(l => html`<li><a href=${l.path}>${name(l)}</a></li>`)}</ul>`}
      <div class="row savebar"><button type="button" class="sec" onClick=${lock}>${t('eng.lock')}</button></div>`;
  } else {
    body = html`<p class="small mut">${t('eng.hint')}</p>
      <form class="row nowrap savebar" novalidate onSubmit=${unlock}>
        <input ref=${input} type="password" autocomplete="off" spellcheck="false" aria-label=${t('eng.key')}
          placeholder=${t('eng.key')} value=${key} onInput=${e => setKey(e.target.value)}/>
        <button type="submit" disabled=${busy || !key.trim()}>${t('eng.unlock')}</button>
      </form>`;
  }
  return html`<div class="overlay" onClick=${e => { if (e.target === e.currentTarget) onClose(); }}>
    <section class="card dialog" role="dialog" aria-modal="true" aria-label=${t('eng.title')}>
      <div class="row"><h2>${t('eng.title')}</h2><span class="spacer"></span>
        <button type="button" class="ghost" aria-label=${t('eng.close')} title=${t('eng.close')} onClick=${onClose}>✕</button></div>
      ${body}
      ${msg.node}
    </section>
  </div>`;
}
