// LG Aquatic Life web UI: layout, hash router, start-up.
import { render } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { api } from './lib/api.js';
import { html } from './lib/html.js';
import { add, initLang, t, useLang } from './lib/i18n.js';
import { setMode, usePrefs } from './lib/prefs.js';
import { start, useStore } from './lib/store.js';
import { Dashboard } from './views/dashboard.js';
import { EngDialog } from './views/engineering.js';
import { MaintenanceView } from './views/maintenance.js';
import { PumpView } from './views/pump.js';
import { SettingsView } from './views/settings.js';

add({
  en: {
    'nav.home': 'Overview', 'nav.settings': 'Settings',
    'mode.beginner': 'Simple', 'mode.pro': 'Pro', 'mode.title': 'View',
    'live.on': 'Live', 'live.off': 'Connecting …',
    'stop.all': 'Stop all', 'stop.title': 'Stop all pumps now',
    'notime': 'The device has no valid time – schedules and daily limits are paused.',
    'offline': 'No connection to the device ({e}).',
    'footer.maint': 'Maintenance', 'notfound': 'Page not found',
  },
  de: {
    'nav.home': 'Übersicht', 'nav.settings': 'Einstellungen',
    'mode.beginner': 'Einfach', 'mode.pro': 'Profi', 'mode.title': 'Ansicht',
    'live.on': 'Live', 'live.off': 'Verbinde …',
    'stop.all': 'Alle stoppen', 'stop.title': 'Alle Pumpen sofort stoppen',
    'notime': 'Das Gerät hat keine gültige Uhrzeit – Zeitpläne und Tageslimits ruhen.',
    'offline': 'Keine Verbindung zum Gerät ({e}).',
    'footer.maint': 'Wartung', 'notfound': 'Seite nicht gefunden',
  },
});

// ---------------------------------------------------------------------------
// Router: #/, #/pump/3, #/settings, #/maintenance (#/schedule/3 opens the pump)

function parse() {
  const parts = location.hash.replace(/^#\/?/, '').split('/').filter(Boolean);
  return { name: parts[0] || 'home', arg: parts[1] };
}

function useRoute() {
  const [r, set] = useState(parse());
  useEffect(() => {
    const f = () => { set(parse()); window.scrollTo(0, 0); };
    addEventListener('hashchange', f);
    return () => removeEventListener('hashchange', f);
  }, []);
  return r;
}

const icons = {
  home: 'M4 4h7v7H4zM13 4h7v4h-7zM13 10h7v10h-7zM4 13h7v7H4z',
  settings: 'M12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6zm8 3-2-.6-.6-1.5 1-1.8-1.5-1.5-1.8 1-1.5-.6L13 4h-2l-.6 2-1.5.6-1.8-1L5.6 7.1l1 1.8L6 10.4 4 11v2l2 .6.6 1.5-1 1.8 1.5 1.5 1.8-1 1.5.6.6 2h2l.6-2 1.5-.6 1.8 1 1.5-1.5-1-1.8.6-1.5 2-.6z',
};
const navItems = [['home', '#/'], ['settings', '#/settings']];

function Nav({ route, cls }) {
  const active = route.name === 'pump' || route.name === 'schedule' ? 'home' : route.name === 'maintenance' ? 'settings' : route.name;
  return html`<nav class=${cls}>${navItems.map(([n, href]) => html`
    <a href=${href} class=${active === n ? 'on' : ''}>
      ${cls === 'bottom' && html`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round" stroke-linecap="round"><path d=${icons[n]}/></svg>`}
      ${t('nav.' + n)}</a>`)}</nav>`;
}

function Header({ route }) {
  const s = useStore();
  const prefs = usePrefs();
  const busy = s.pumps && s.pumps.some(p => p.activity !== 'idle');
  const host = s.info && s.info.device ? s.info.device.hostname : '';
  return html`<header class="top"><div class="top-in">
    <a class="brand" href="#/"><img src="/ui/icon.svg" alt=""/><div><b>LG Aquatic Life</b><span>Doser${host ? ' · ' + host : ''}</span></div></a>
    <span class="spacer"></span>
    <span class=${'live hide-s' + (s.live ? ' on' : '')}><i></i>${t(s.live ? 'live.on' : 'live.off')}</span>
    <div class="seg" title=${t('mode.title')}>
      ${['beginner', 'pro'].map(m => html`<button class=${prefs.mode === m ? 'on' : ''} onClick=${() => setMode(m)}>${t('mode.' + m)}</button>`)}
    </div>
    <button class=${busy ? 'danger small' : 'sec small'} title=${t('stop.title')}
      onClick=${() => api('POST', '/api/v1/pumps/stop?pump=0').catch(() => {})}>■<span class="hide-s">${t('stop.all')}</span></button>
  </div><${Nav} route=${route} cls="tabs"/></header>`;
}

// Hidden service access (spec 11.1): click the version 7 times quickly
function Footer() {
  const s = useStore();
  const clicks = useRef([]);
  const [eng, setEng] = useState(false);
  const ver = s.info && s.info.firmware ? s.info.firmware.version : '';
  const tap = () => {
    const now = Date.now();
    clicks.current = clicks.current.filter(c => now - c < 3000).concat(now);
    if (clicks.current.length >= 7) { clicks.current = []; setEng(true); }
  };
  return html`<footer>
    <span>© LG Aquatic Life</span>
    <button class="ver" onClick=${tap}>Firmware ${ver}</button>
    <a href="#/maintenance">${t('footer.maint')}</a>
  </footer>
  ${eng && html`<${EngDialog} onClose=${() => setEng(false)}/>`}`;
}

function Banners() {
  const s = useStore();
  return html`
    ${s.error && s.error.code === 'network' && html`<div class="banner err">${t('offline', { e: s.error.message })}</div>`}
    ${!s.timeValid && html`<div class="banner">${t('notime')}</div>`}`;
}

function App() {
  useLang();
  const route = useRoute();
  let page;
  switch (route.name) {
    case 'home': page = html`<${Dashboard}/>`; break;
    case 'pump': page = html`<${PumpView} pump=${+route.arg}/>`; break;
    case 'schedule': page = html`<${PumpView} pump=${+route.arg || 1}/>`; break;  // old links
    case 'settings': page = html`<${SettingsView}/>`; break;
    case 'maintenance': page = html`<${MaintenanceView}/>`; break;
    default: page = html`<div class="empty">${t('notfound')}</div>`;
  }
  return html`
    <${Header} route=${route}/>
    <main><${Banners}/>${page}</main>
    <${Footer}/>
    <${Nav} route=${route} cls="bottom"/>`;
}

initLang();
start();
const root = document.getElementById('app');
root.textContent = '';  // boot placeholder
render(html`<${App}/>`, root);
