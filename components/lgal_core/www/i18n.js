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
  const names = { en: 'English', de: 'Deutsch', fr: 'Français', es: 'Español', it: 'Italiano', nl: 'Nederlands', pl: 'Polski', tr: 'Türkçe', zh: '中文' };
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
    fr: {
      "lang.label": "Langue",
      "err.http": "Erreur (HTTP {status})",
      "err.unauthorized": "Connexion requise",
      "err.engineering_locked": "Zone engineering verrouillée – la déverrouiller sur /login?eng=1",
      "err.too_many_attempts": "Trop de tentatives échouées – veuillez patienter",
      "err.body_missing": "Requête sans données",
      "err.body_too_large": "Requête trop volumineuse",
      "err.invalid_json": "Données de requête invalides",
      "err.invalid_param": "Saisie invalide",
      "err.no_memory": "L’appareil manque de mémoire",
      "err.internal_error": "Erreur interne",
      "err.storage_error": "Impossible d’enregistrer le réglage",
      "err.no_coredump": "Aucun coredump enregistré",
      "err.ota_in_progress": "Une mise à jour est déjà en cours",
      "err.pump_running": "Une pompe tourne – réessayez quand elle aura terminé",
      "err.no_ota_partition": "Aucune partition de mise à jour disponible",
      "err.invalid_size": "Taille de fichier invalide",
      "err.transfer_aborted": "Transfert interrompu",
      "err.not_esp_image": "Ce n’est pas un fichier firmware",
      "err.wrong_chip": "Le firmware est destiné à une autre puce",
      "err.no_app_desc": "Le fichier firmware est incomplet",
      "err.wrong_project": "Le firmware est destiné à un autre appareil",
      "err.wrong_variant": "Le firmware est destiné à un module avec une autre taille de flash",
      "err.wrong_board": "Le firmware est destiné à une autre carte (modèle)",
      "err.flash_error": "Erreur d’écriture en flash",
      "err.file_too_short": "Fichier trop court",
      "err.image_invalid": "La vérification du firmware a échoué",
      "err.upload_failed": "La mise à jour a échoué",
    },
    es: {
      "lang.label": "Idioma",
      "err.http": "Error (HTTP {status})",
      "err.unauthorized": "Se requiere iniciar sesión",
      "err.engineering_locked": "Zona de ingeniería bloqueada – desbloquéela en /login?eng=1",
      "err.too_many_attempts": "Demasiados intentos fallidos – espere, por favor",
      "err.body_missing": "Solicitud sin datos",
      "err.body_too_large": "Solicitud demasiado grande",
      "err.invalid_json": "Datos de solicitud no válidos",
      "err.invalid_param": "Entrada no válida",
      "err.no_memory": "El dispositivo no tiene memoria suficiente",
      "err.internal_error": "Error interno",
      "err.storage_error": "No se pudo guardar el ajuste",
      "err.no_coredump": "No hay ningún coredump guardado",
      "err.ota_in_progress": "Ya hay una actualización en curso",
      "err.pump_running": "Una bomba está funcionando – inténtelo de nuevo cuando termine",
      "err.no_ota_partition": "No hay partición de actualización disponible",
      "err.invalid_size": "Tamaño de archivo no válido",
      "err.transfer_aborted": "Transferencia interrumpida",
      "err.not_esp_image": "No es un archivo de firmware",
      "err.wrong_chip": "El firmware es para otro chip",
      "err.no_app_desc": "El archivo de firmware está incompleto",
      "err.wrong_project": "El firmware es para otro dispositivo",
      "err.wrong_variant": "El firmware es para un módulo con otro tamaño de flash",
      "err.wrong_board": "El firmware es para otra placa (modelo)",
      "err.flash_error": "Error al escribir en la flash",
      "err.file_too_short": "Archivo demasiado corto",
      "err.image_invalid": "La verificación del firmware ha fallado",
      "err.upload_failed": "La actualización ha fallado",
    },
    it: {
      "lang.label": "Lingua",
      "err.http": "Errore (HTTP {status})",
      "err.unauthorized": "Accesso richiesto",
      "err.engineering_locked": "Area engineering bloccata – sbloccarla su /login?eng=1",
      "err.too_many_attempts": "Troppi tentativi falliti – attendere",
      "err.body_missing": "Richiesta senza dati",
      "err.body_too_large": "Richiesta troppo grande",
      "err.invalid_json": "Dati della richiesta non validi",
      "err.invalid_param": "Inserimento non valido",
      "err.no_memory": "Il dispositivo non ha memoria sufficiente",
      "err.internal_error": "Errore interno",
      "err.storage_error": "Impossibile salvare l’impostazione",
      "err.no_coredump": "Nessun coredump salvato",
      "err.ota_in_progress": "È già in corso un aggiornamento",
      "err.pump_running": "Una pompa è in funzione – riprovare quando avrà finito",
      "err.no_ota_partition": "Nessuna partizione di aggiornamento disponibile",
      "err.invalid_size": "Dimensione del file non valida",
      "err.transfer_aborted": "Trasferimento interrotto",
      "err.not_esp_image": "Non è un file firmware",
      "err.wrong_chip": "Il firmware è per un altro chip",
      "err.no_app_desc": "Il file firmware è incompleto",
      "err.wrong_project": "Il firmware è per un altro dispositivo",
      "err.wrong_variant": "Il firmware è per un modulo con un’altra dimensione di flash",
      "err.wrong_board": "Il firmware è per un’altra scheda (modello)",
      "err.flash_error": "Errore di scrittura nella flash",
      "err.file_too_short": "File troppo corto",
      "err.image_invalid": "Verifica del firmware non riuscita",
      "err.upload_failed": "Aggiornamento non riuscito",
    },
    nl: {
      "lang.label": "Taal",
      "err.http": "Fout (HTTP {status})",
      "err.unauthorized": "Aanmelden vereist",
      "err.engineering_locked": "Engineering-gedeelte vergrendeld – ontgrendel het op /login?eng=1",
      "err.too_many_attempts": "Te veel mislukte pogingen – even wachten",
      "err.body_missing": "Verzoek zonder gegevens",
      "err.body_too_large": "Verzoek te groot",
      "err.invalid_json": "Ongeldige verzoekgegevens",
      "err.invalid_param": "Ongeldige invoer",
      "err.no_memory": "Het apparaat heeft te weinig geheugen",
      "err.internal_error": "Interne fout",
      "err.storage_error": "Instelling kon niet worden opgeslagen",
      "err.no_coredump": "Geen coredump opgeslagen",
      "err.ota_in_progress": "Er loopt al een update",
      "err.pump_running": "Er draait een pomp – probeer het opnieuw als die klaar is",
      "err.no_ota_partition": "Geen updatepartitie beschikbaar",
      "err.invalid_size": "Ongeldige bestandsgrootte",
      "err.transfer_aborted": "Overdracht afgebroken",
      "err.not_esp_image": "Dit is geen firmwarebestand",
      "err.wrong_chip": "De firmware is voor een andere chip",
      "err.no_app_desc": "Het firmwarebestand is onvolledig",
      "err.wrong_project": "De firmware is voor een ander apparaat",
      "err.wrong_variant": "De firmware is voor een module met een andere flashgrootte",
      "err.wrong_board": "De firmware is voor een andere printplaat (model)",
      "err.flash_error": "Fout bij het schrijven naar de flash",
      "err.file_too_short": "Bestand te kort",
      "err.image_invalid": "Controle van de firmware mislukt",
      "err.upload_failed": "Update mislukt",
    },
    pl: {
      "lang.label": "Język",
      "err.http": "Błąd (HTTP {status})",
      "err.unauthorized": "Wymagane logowanie",
      "err.engineering_locked": "Obszar engineering zablokowany – odblokuj go na /login?eng=1",
      "err.too_many_attempts": "Zbyt wiele nieudanych prób – proszę czekać",
      "err.body_missing": "Żądanie bez danych",
      "err.body_too_large": "Żądanie zbyt duże",
      "err.invalid_json": "Nieprawidłowe dane żądania",
      "err.invalid_param": "Nieprawidłowe dane",
      "err.no_memory": "Urządzeniu brakuje pamięci",
      "err.internal_error": "Błąd wewnętrzny",
      "err.storage_error": "Nie udało się zapisać ustawienia",
      "err.no_coredump": "Brak zapisanego coredumpu",
      "err.ota_in_progress": "Aktualizacja już trwa",
      "err.pump_running": "Pompa pracuje – spróbuj ponownie, gdy skończy",
      "err.no_ota_partition": "Brak partycji aktualizacji",
      "err.invalid_size": "Nieprawidłowy rozmiar pliku",
      "err.transfer_aborted": "Przesyłanie przerwane",
      "err.not_esp_image": "To nie jest plik firmware",
      "err.wrong_chip": "Firmware jest dla innego układu",
      "err.no_app_desc": "Plik firmware jest niekompletny",
      "err.wrong_project": "Firmware jest dla innego urządzenia",
      "err.wrong_variant": "Firmware jest dla modułu z inną wielkością flash",
      "err.wrong_board": "Firmware jest dla innej płytki (modelu)",
      "err.flash_error": "Błąd zapisu do flash",
      "err.file_too_short": "Plik za krótki",
      "err.image_invalid": "Weryfikacja firmware nie powiodła się",
      "err.upload_failed": "Aktualizacja nie powiodła się",
    },
    tr: {
      "lang.label": "Dil",
      "err.http": "Hata (HTTP {status})",
      "err.unauthorized": "Oturum açmanız gerekiyor",
      "err.engineering_locked": "Mühendislik alanı kilitli – /login?eng=1 adresinden kilidini açın",
      "err.too_many_attempts": "Çok fazla başarısız deneme – lütfen bekleyin",
      "err.body_missing": "Veri içermeyen istek",
      "err.body_too_large": "İstek çok büyük",
      "err.invalid_json": "Geçersiz istek verisi",
      "err.invalid_param": "Geçersiz giriş",
      "err.no_memory": "Cihazın belleği yetersiz",
      "err.internal_error": "Dahili hata",
      "err.storage_error": "Ayar kaydedilemedi",
      "err.no_coredump": "Kayıtlı coredump yok",
      "err.ota_in_progress": "Zaten bir güncelleme sürüyor",
      "err.pump_running": "Bir pompa çalışıyor – bittiğinde tekrar deneyin",
      "err.no_ota_partition": "Güncelleme bölümü yok",
      "err.invalid_size": "Geçersiz dosya boyutu",
      "err.transfer_aborted": "Aktarım iptal edildi",
      "err.not_esp_image": "Bu bir firmware dosyası değil",
      "err.wrong_chip": "Firmware başka bir çip için",
      "err.no_app_desc": "Firmware dosyası eksik",
      "err.wrong_project": "Firmware başka bir cihaz için",
      "err.wrong_variant": "Firmware farklı flash boyutuna sahip bir modül için",
      "err.wrong_board": "Firmware başka bir kart (model) için",
      "err.flash_error": "Flash'a yazma hatası",
      "err.file_too_short": "Dosya çok kısa",
      "err.image_invalid": "Firmware doğrulaması başarısız",
      "err.upload_failed": "Güncelleme başarısız",
    },
    zh: {
      "lang.label": "语言",
      "err.http": "错误（HTTP {status}）",
      "err.unauthorized": "需要登录",
      "err.engineering_locked": "工程区已锁定 – 请在 /login?eng=1 解锁",
      "err.too_many_attempts": "失败次数过多 – 请稍候",
      "err.body_missing": "请求没有数据",
      "err.body_too_large": "请求过大",
      "err.invalid_json": "请求数据无效",
      "err.invalid_param": "输入无效",
      "err.no_memory": "设备内存不足",
      "err.internal_error": "内部错误",
      "err.storage_error": "无法保存设置",
      "err.no_coredump": "没有保存的 coredump",
      "err.ota_in_progress": "已有更新正在进行",
      "err.pump_running": "有泵正在运行 – 请在其结束后重试",
      "err.no_ota_partition": "没有可用的更新分区",
      "err.invalid_size": "文件大小无效",
      "err.transfer_aborted": "传输已中止",
      "err.not_esp_image": "这不是固件文件",
      "err.wrong_chip": "该固件适用于其他芯片",
      "err.no_app_desc": "固件文件不完整",
      "err.wrong_project": "该固件适用于其他设备",
      "err.wrong_variant": "该固件适用于闪存容量不同的模块",
      "err.wrong_board": "该固件适用于其他电路板（型号）",
      "err.flash_error": "写入闪存时出错",
      "err.file_too_short": "文件过短",
      "err.image_invalid": "固件校验失败",
      "err.upload_failed": "更新失败",
    },
  });

  window.i18n = { add, t, apply, error, init, setLang, flag, name, onChange: (f) => listeners.push(f),
                  get lang() { return lang; }, get languages() { return languages; } };
  window.t = t;
})();
