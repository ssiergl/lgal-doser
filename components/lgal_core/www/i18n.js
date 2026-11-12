// LG Aquatic Life – shared translation base for all device pages.
//
// Firmware/API are always English (errors: {"error":{"code","message"}}).
// The pages translate via keys; English is the default and fallback.
//
//   i18n.add({en:{k:'Text {x}'}, de:{k:'Text {x}'}});
//   t('k', {x: 1});                         // get text
//   <span data-i18n="k"></span>             // element text, via i18n.apply()
//   <input data-i18n-placeholder="k">       // attributes: title, placeholder
//   i18n.error(json, status)                // translate API error
//   await i18n.init('lang');                // load language, picker in <div id="lang">
//   i18n.flag('de'), i18n.name('de'), i18n.languages
//
// New language: entry in settings::kLanguages (firmware) + dictionaries here
// and in the pages. Missing keys fall back to English. The picker is a pill
// with the flag of the language (/flags.js, loaded by the page before this
// file, with the picker's styles; without it the picker shows the names only)
// and opens a list.
(function () {
  const dict = { en: {}, de: {} };
  const names = { en: 'English', de: 'Deutsch' };
  let lang = 'en';
  let languages = ['en', 'de'];
  const listeners = [];

  function add(d) {
    for (const l in d) dict[l] = Object.assign(dict[l] || {}, d[l]);
  }

  function t(key, vars) {
    let s = (dict[lang] && dict[lang][key]) ?? dict.en[key] ?? key;
    if (vars) s = s.replace(/\{(\w+)\}/g, (m, k) => (k in vars ? vars[k] : m));
    return s;
  }

  function apply(root) {
    root = root || document;
    root.querySelectorAll('[data-i18n]').forEach((e) => (e.textContent = t(e.dataset.i18n)));
    root.querySelectorAll('[data-i18n-html]').forEach((e) => (e.innerHTML = t(e.dataset.i18nHtml)));
    root.querySelectorAll('[data-i18n-title]').forEach((e) => (e.title = t(e.dataset.i18nTitle)));
    root.querySelectorAll('[data-i18n-placeholder]').forEach((e) => (e.placeholder = t(e.dataset.i18nPlaceholder)));
    document.documentElement.lang = lang;
    if (dict[lang]['page.title'] || dict.en['page.title']) document.title = t('page.title');
  }

  // Error response -> readable text. Unknown codes: the firmware's English message.
  function error(j, status) {
    const e = j && j.error;
    if (!e) return t('err.http', { status: status || '?' });
    if (typeof e === 'string') return e;
    const key = 'err.' + e.code;
    const txt = dict[lang][key] ?? dict.en[key];
    if (!txt) return e.message || e.code;
    // Parameter error: append the detail (which parameter) from the firmware
    return e.code === 'invalid_param' && e.message ? `${t(key)} (${e.message})` : t(key);
  }

  function setLang(l, save) {
    if (!languages.includes(l)) l = 'en';
    lang = l;
    try { localStorage.setItem('lgal.lang', l); } catch (e) {}
    apply();
    renderPicker();
    listeners.forEach((f) => f(l));
    if (save) fetch('/api/v1/system/language?lang=' + encodeURIComponent(l), { method: 'POST' }).catch(() => {});
  }

  let pickerId = null;
  async function init(id) {
    pickerId = id || null;
    // First the last used value (no flicker), then the device setting
    try { const l = localStorage.getItem('lgal.lang'); if (l) lang = l; } catch (e) {}
    apply();
    try {
      const r = await fetch('/api/v1/system/language', { cache: 'no-store' });
      if (r.ok) {
        const j = await r.json();
        languages = j.languages || languages;
        lang = j.language || lang;
      }
    } catch (e) {}
    setLang(lang, false);
  }

  const flag = (l) => {
    const svg = window.lgalFlags && window.lgalFlags[l];
    return svg ? `<span class="flag">${svg}</span>` : '';
  };
  const name = (l) => names[l] || l;

  // Picker: <div id="lang"> becomes a pill button with a list below it
  const chev = '<svg class="chev" viewBox="0 0 12 8" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M1 1.5l5 5 5-5"/></svg>';

  function renderPicker() {
    const box = pickerId && document.getElementById(pickerId);
    if (!box) return;
    if (window.lgalLangCss && !document.getElementById('lgp-css')) {
      const st = document.createElement('style');
      st.id = 'lgp-css';
      st.textContent = window.lgalLangCss;
      document.head.appendChild(st);
    }
    box.classList.add('lgp');
    box.innerHTML = `<button type="button" class="lgp-btn" aria-haspopup="listbox" aria-expanded="false" title="${t('lang.label')}">` +
      `${flag(lang)}<span>${name(lang)}</span>${chev}</button>` +
      `<div class="lgp-list" role="listbox" aria-label="${t('lang.label')}" hidden>` +
      languages.map((l) => `<button type="button" role="option" lang="${l}" aria-selected="${l === lang}" data-l="${l}">${flag(l)}<span>${name(l)}</span></button>`).join('') +
      '</div>';
    const btn = box.firstElementChild, list = box.lastElementChild;
    const open = (on) => {
      list.hidden = !on;
      btn.setAttribute('aria-expanded', on);
      if (on) (list.querySelector('[aria-selected=true]') || list.firstElementChild).focus();
    };
    btn.onclick = () => open(list.hidden);
    list.onclick = (e) => {
      const b = e.target.closest('button');
      if (!b) return;
      open(false);
      if (b.dataset.l !== lang) setLang(b.dataset.l, true);
      box.firstElementChild.focus();
    };
    box.onkeydown = (e) => {
      if (e.key === 'Escape' && !list.hidden) { open(false); btn.focus(); }
      if ((e.key === 'ArrowDown' || e.key === 'ArrowUp') && !list.hidden) {
        e.preventDefault();
        const items = [...list.children], i = items.indexOf(document.activeElement);
        items[(i + (e.key === 'ArrowDown' ? 1 : items.length - 1)) % items.length].focus();
      }
    };
  }
  document.addEventListener('click', (e) => {
    const box = pickerId && document.getElementById(pickerId);
    if (box && !box.contains(e.target)) {
      const list = box.querySelector('.lgp-list');
      if (list && !list.hidden) { list.hidden = true; box.firstElementChild.setAttribute('aria-expanded', 'false'); }
    }
  });

  add({
    en: {
      'lang.label': 'Language',
      'err.http': 'Error (HTTP {status})',
      'err.unauthorized': 'Login required',
      'err.engineering_locked': 'Engineering area locked – unlock it on /login?eng=1',
      'err.too_many_attempts': 'Too many failed attempts – please wait',
      'err.body_missing': 'Request without data',
      'err.body_too_large': 'Request too large',
      'err.invalid_json': 'Invalid request data',
      'err.invalid_param': 'Invalid input',
      'err.no_memory': 'Device is out of memory',
      'err.internal_error': 'Internal error',
      'err.storage_error': 'Could not save setting',
      'err.no_coredump': 'No coredump stored',
      'err.ota_in_progress': 'An update is already in progress',
      'err.pump_running': 'A pump is running – try again when it has finished',
      'err.no_ota_partition': 'No update partition available',
      'err.invalid_size': 'Invalid file size',
      'err.transfer_aborted': 'Transfer aborted',
      'err.not_esp_image': 'This is not a firmware file',
      'err.wrong_chip': 'Firmware is for a different chip',
      'err.no_app_desc': 'Firmware file is incomplete',
      'err.wrong_project': 'Firmware is for a different device',
      'err.wrong_variant': 'Firmware is for a module with a different flash size',
      'err.wrong_board': 'Firmware is for a different circuit board (model)',
      'err.flash_error': 'Flash write error',
      'err.file_too_short': 'File too short',
      'err.image_invalid': 'Firmware verification failed',
      'err.upload_failed': 'Update failed',
    },
    de: {
      'lang.label': 'Sprache',
      'err.http': 'Fehler (HTTP {status})',
      'err.unauthorized': 'Anmeldung erforderlich',
      'err.engineering_locked': 'Engineering-Bereich gesperrt – unter /login?eng=1 freischalten',
      'err.too_many_attempts': 'Zu viele Fehlversuche – bitte warten',
      'err.body_missing': 'Anfrage ohne Daten',
      'err.body_too_large': 'Anfrage zu groß',
      'err.invalid_json': 'Ungültige Anfragedaten',
      'err.invalid_param': 'Ungültige Eingabe',
      'err.no_memory': 'Kein freier Speicher im Gerät',
      'err.internal_error': 'Interner Fehler',
      'err.storage_error': 'Einstellung konnte nicht gespeichert werden',
      'err.no_coredump': 'Kein Coredump vorhanden',
      'err.ota_in_progress': 'Es läuft bereits ein Update',
      'err.pump_running': 'Eine Pumpe läuft – nach dem Ende erneut versuchen',
      'err.no_ota_partition': 'Keine Update-Partition verfügbar',
      'err.invalid_size': 'Ungültige Dateigröße',
      'err.transfer_aborted': 'Übertragung abgebrochen',
      'err.not_esp_image': 'Das ist keine Firmware-Datei',
      'err.wrong_chip': 'Firmware ist für einen anderen Chip',
      'err.no_app_desc': 'Firmware-Datei ist unvollständig',
      'err.wrong_project': 'Firmware ist für ein anderes Gerät',
      'err.wrong_variant': 'Firmware ist für ein Modul mit anderer Flash-Größe',
      'err.wrong_board': 'Firmware ist für eine andere Platine (anderes Modell)',
      'err.flash_error': 'Fehler beim Schreiben in den Flash',
      'err.file_too_short': 'Datei zu kurz',
      'err.image_invalid': 'Prüfung der Firmware fehlgeschlagen',
      'err.upload_failed': 'Update fehlgeschlagen',
    },
  });

  window.i18n = { add, t, apply, error, init, setLang, flag, name, onChange: (f) => listeners.push(f),
                  get lang() { return lang; }, get languages() { return languages; } };
  window.t = t;
})();
