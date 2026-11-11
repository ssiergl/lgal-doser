// Account (settings page): change the admin password and sign out for
// everyone; API keys for apps, Home Assistant and scripts in the pro view.
import { useEffect, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { hub } from '../lib/hub.js';
import { add, errText, getLang, t } from '../lib/i18n.js';
import { useMsg } from '../lib/ui.js';

add({
  en: {
    'ac.title': 'Login', 'ac.info': 'Signed in at {d}.', 'ac.logout': 'Sign out',
    'ac.pw.set': 'Choose your admin password', 'ac.pw.set.hint': 'You signed in with the device password. Please choose your own password (at least 8 characters).',
    'ac.pw.change': 'Change password', 'ac.pw.hint': 'All other browsers are signed out afterwards. API keys stay valid.',
    'ac.pw.current': 'Current password', 'ac.pw.new': 'New password', 'ac.pw.repeat': 'Repeat new password',
    'ac.pw.mismatch': 'The passwords do not match.', 'ac.pw.short': 'At least 8 characters', 'ac.pw.ok': 'Password saved.', 'ac.pw.save': 'Save password',
    'ac.wait': 'Too many attempts – please wait {s} s.',
    'ac.keys': 'API keys', 'ac.keys.hint': 'For the app, Home Assistant or scripts. Send it as header "X-API-Key: …" or "Authorization: Bearer …".',
    'ac.keys.none': 'No API keys yet.', 'ac.keys.created': 'created {d}', 'ac.keys.name': 'Name of the new key',
    'ac.keys.add': 'Create key', 'ac.keys.full': 'All {n} key slots are in use.', 'ac.keys.del': 'Delete key "{n}"? Programs using it lose access.',
    'ac.keys.delete': 'Delete', 'ac.keys.once': 'Copy the key now – it is shown only once:', 'ac.keys.copy': 'Copy', 'ac.keys.copied': 'Copied',
    'ac.keys.done': 'Done', 'ac.keys.deleted': 'Key "{n}" deleted.',
    'err.wrong_password': 'Wrong password', 'err.too_many_attempts': 'Too many attempts – please wait',
    'err.password_too_short': 'At least 8 characters', 'err.password_too_long': 'At most 64 characters',
    'err.too_many_keys': 'All key slots are in use',
  },
  de: {
    'ac.title': 'Anmeldung', 'ac.info': 'Angemeldet bei {d}.', 'ac.logout': 'Abmelden',
    'ac.pw.set': 'Eigenes Admin-Passwort festlegen', 'ac.pw.set.hint': 'Du bist mit dem Geräte-Passwort angemeldet. Bitte wähle ein eigenes Passwort (mindestens 8 Zeichen).',
    'ac.pw.change': 'Passwort ändern', 'ac.pw.hint': 'Alle anderen Browser werden danach abgemeldet. API-Schlüssel bleiben gültig.',
    'ac.pw.current': 'Aktuelles Passwort', 'ac.pw.new': 'Neues Passwort', 'ac.pw.repeat': 'Neues Passwort wiederholen',
    'ac.pw.mismatch': 'Die Passwörter stimmen nicht überein.', 'ac.pw.short': 'Mindestens 8 Zeichen', 'ac.pw.ok': 'Passwort gespeichert.', 'ac.pw.save': 'Passwort speichern',
    'ac.wait': 'Zu viele Versuche – bitte {s} s warten.',
    'ac.keys': 'API-Schlüssel', 'ac.keys.hint': 'Für App, Home Assistant oder Skripte. Mitsenden als Header „X-API-Key: …“ oder „Authorization: Bearer …“.',
    'ac.keys.none': 'Noch keine API-Schlüssel.', 'ac.keys.created': 'erstellt {d}', 'ac.keys.name': 'Name des neuen Schlüssels',
    'ac.keys.add': 'Schlüssel erzeugen', 'ac.keys.full': 'Alle {n} Schlüsselplätze sind belegt.', 'ac.keys.del': 'Schlüssel „{n}“ löschen? Programme, die ihn nutzen, verlieren den Zugriff.',
    'ac.keys.delete': 'Löschen', 'ac.keys.once': 'Schlüssel jetzt kopieren – er wird nur einmal angezeigt:', 'ac.keys.copy': 'Kopieren', 'ac.keys.copied': 'Kopiert',
    'ac.keys.done': 'Fertig', 'ac.keys.deleted': 'Schlüssel „{n}“ gelöscht.',
    'err.wrong_password': 'Falsches Passwort', 'err.too_many_attempts': 'Zu viele Versuche – bitte warten',
    'err.password_too_short': 'Mindestens 8 Zeichen', 'err.password_too_long': 'Höchstens 64 Zeichen',
    'err.too_many_keys': 'Alle Schlüsselplätze sind belegt',
  },
});

const authErr = e => {
  const s = e.body && e.body.error && e.body.error.retry_after_s;
  return s ? t('ac.wait', { s }) : errText(e);
};

// Copy text; the Clipboard API needs HTTPS, the device speaks plain HTTP
function copy(text) {
  if (navigator.clipboard && window.isSecureContext) return navigator.clipboard.writeText(text);
  const ta = document.createElement('textarea');
  ta.value = text;
  ta.setAttribute('readonly', '');
  ta.style.position = 'fixed';
  ta.style.opacity = '0';
  document.body.appendChild(ta);
  ta.select();
  const ok = document.execCommand('copy');
  ta.remove();
  return ok ? Promise.resolve() : Promise.reject(new Error('copy'));
}

function PasswordForm({ set, onDone }) {
  const [v, setV] = useState({ cur: '', pw: '', pw2: '' });
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const field = (k, label, ac) => html`<label class="field">${label}
    <input type="password" autocomplete=${ac} maxlength="64" value=${v[k]} onInput=${e => { const x = e.target.value; setV(prev => ({ ...prev, [k]: x })); }}/></label>`;
  const submit = e => {
    e.preventDefault();
    if (v.pw.length < 8) { msg.err(t('ac.pw.short')); return; }
    if (v.pw !== v.pw2) { msg.err(t('ac.pw.mismatch')); return; }
    setBusy(true);
    const body = { new: v.pw };
    if (set) body.current = v.cur;
    api('POST', '/api/v1/auth/password', body)
      .then(() => { setV({ cur: '', pw: '', pw2: '' }); msg.ok(t('ac.pw.ok')); onDone(); })
      .catch(e => msg.err(authErr(e)))
      .finally(() => setBusy(false));
  };
  return html`<form class="sub" novalidate onSubmit=${submit}>
    <h3>${t(set ? 'ac.pw.change' : 'ac.pw.set')}</h3>
    <p class="small mut">${t(set ? 'ac.pw.hint' : 'ac.pw.set.hint')}</p>
    ${set && field('cur', t('ac.pw.current'), 'current-password')}
    ${field('pw', t('ac.pw.new'), 'new-password')}
    ${field('pw2', t('ac.pw.repeat'), 'new-password')}
    <div class="row savebar"><button type="submit" disabled=${busy || !v.pw || (set && !v.cur)}>${t('ac.pw.save')}</button></div>
    ${msg.node}
  </form>`;
}

function KeysCard() {
  const [k, setK] = useState(null);
  const [name, setName] = useState('');
  const [fresh, setFresh] = useState(null);   // {name, key} shown once
  const [copied, setCopied] = useState(false);
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const load = () => api('GET', '/api/v1/auth/keys').then(setK).catch(e => setK({ error: e }));
  useEffect(() => { load(); }, []);

  const head = html`<h2>${t('ac.keys')}</h2><p class="small mut">${t('ac.keys.hint')}</p>`;
  if (!k) return html`<section class="card">${head}<p class="mut">…</p></section>`;
  if (k.error) return html`<section class="card">${head}<p class="msg err">${errText(k.error)}</p></section>`;
  const full = k.keys.length >= k.max;
  const date = s => new Date(s * 1000).toLocaleDateString(getLang());
  const create = e => {
    e.preventDefault();
    setBusy(true);
    msg.clear();
    api('POST', '/api/v1/auth/keys', { name: name.trim() })
      .then(j => { setFresh({ name: j.name, key: j.key }); setCopied(false); setName(''); load(); })
      .catch(e => msg.err(e))
      .finally(() => setBusy(false));
  };
  const del = key => {
    if (!confirm(t('ac.keys.del', { n: key.name }))) return;
    api('DELETE', '/api/v1/auth/keys?slot=' + key.slot)
      .then(() => { msg.ok(t('ac.keys.deleted', { n: key.name })); load(); })
      .catch(e => msg.err(e));
  };
  return html`<section class="card">
    ${head}
    ${k.keys.length ? html`<ul class="keys">${k.keys.map(x => html`<li key=${x.slot}>
        <b>${x.name}</b>${x.created > 0 && html`<span class="small mut">${t('ac.keys.created', { d: date(x.created) })}</span>`}
        <button type="button" class="ghost" onClick=${() => del(x)}>${t('ac.keys.delete')}</button></li>`)}</ul>`
      : html`<p class="small mut">${t('ac.keys.none')}</p>`}
    ${fresh && html`<div class="newkey" role="status">
      <span class="small">${t('ac.keys.once')}</span>
      <code>${fresh.key}</code>
      <div class="row">
        <button type="button" onClick=${() => copy(fresh.key).then(() => setCopied(true)).catch(() => {})}>${copied ? t('ac.keys.copied') : t('ac.keys.copy')}</button>
        <button type="button" class="sec" onClick=${() => setFresh(null)}>${t('ac.keys.done')}</button>
      </div></div>`}
    <form class="row nowrap savebar" novalidate onSubmit=${create}>
      <input aria-label=${t('ac.keys.name')} placeholder=${t('ac.keys.name')} maxlength="23" autocomplete="off"
        value=${name} disabled=${full} onInput=${e => setName(e.target.value)}/>
      <button type="submit" disabled=${busy || full || !name.trim()}>${t('ac.keys.add')}</button>
    </form>
    ${full && html`<p class="small mut">${t('ac.keys.full', { n: k.max })}</p>`}
    ${msg.node}
  </section>`;
}

// Login status shared by the login card and the API key card
const authHub = hub();
let auth = null;
const loadAuth = () => api('GET', '/api/v1/auth/status').then(j => { auth = j; }, e => { auth = { error: e }; })
  .then(() => authHub.emit());
function useAuth() {
  authHub.use();
  useEffect(() => { loadAuth(); }, []);
  return auth;
}

export function AccountCard() {
  const S = useAuth();
  const head = html`<h2>${t('ac.title')}</h2>`;
  if (!S) return html`<section class="card">${head}<p class="mut">…</p></section>`;
  if (S.error) return html`<section class="card">${head}<p class="msg err">${errText(S.error)}</p></section>`;
  const logout = () => api('POST', '/api/v1/auth/logout').catch(() => {}).then(() => { location.href = '/login'; });
  return html`<section class="card">
    ${head}
    <div class="row"><span class="small mut">${t('ac.info', { d: S.device })}</span>
      <button type="button" class="sec" onClick=${logout}>${t('ac.logout')}</button></div>
    <${PasswordForm} set=${S.password_set} onDone=${loadAuth}/>
  </section>`;
}

// API keys need an own admin password first
export function ApiCard() {
  const S = useAuth();
  return S && S.password_set ? html`<${KeysCard}/>` : null;
}
