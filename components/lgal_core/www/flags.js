// Language picker assets, shared by the device pages and the web UI
// (GET /flags.js, no sign-in): window.lgalFlags[code] is a round flag icon
// as inline SVG (square, cropped to a circle by .flag; English shows the
// United Kingdom, Chinese (simplified) China), window.lgalLangCss the styles
// of the picker (a pill .lgp-btn that opens the list .lgp-list).
window.lgalFlags = {
  en: '<svg xmlns="http://www.w3.org/2000/svg" aria-hidden="true" viewBox="15 0 30 30"><path d="M0 0h60v30H0z" fill="#012169"/><path d="M0 0l60 30M60 0L0 30" stroke="#fff" stroke-width="7"/><path d="M0 0l60 30M60 0L0 30" stroke="#C8102E" stroke-width="3"/><path d="M30 0v30M0 15h60" stroke="#fff" stroke-width="10"/><path d="M30 0v30M0 15h60" stroke="#C8102E" stroke-width="6"/></svg>',
  de: '<svg xmlns="http://www.w3.org/2000/svg" aria-hidden="true" viewBox="0 0 30 30"><path d="M0 0h30v10H0z"/><path d="M0 10h30v10H0z" fill="#D00"/><path d="M0 20h30v10H0z" fill="#FFCE00"/></svg>',
  fr: '<svg xmlns="http://www.w3.org/2000/svg" aria-hidden="true" viewBox="0 0 30 30"><path d="M0 0h10v30H0z" fill="#0055A4"/><path d="M10 0h10v30H10z" fill="#fff"/><path d="M20 0h10v30H20z" fill="#EF4135"/></svg>',
  es: '<svg xmlns="http://www.w3.org/2000/svg" aria-hidden="true" viewBox="0 0 30 30"><path d="M0 0h30v30H0z" fill="#AA151B"/><path d="M0 7.5h30v15H0z" fill="#F1BF00"/></svg>',
  it: '<svg xmlns="http://www.w3.org/2000/svg" aria-hidden="true" viewBox="0 0 30 30"><path d="M0 0h10v30H0z" fill="#009246"/><path d="M10 0h10v30H10z" fill="#fff"/><path d="M20 0h10v30H20z" fill="#CE2B37"/></svg>',
  nl: '<svg xmlns="http://www.w3.org/2000/svg" aria-hidden="true" viewBox="0 0 30 30"><path d="M0 0h30v10H0z" fill="#AE1C28"/><path d="M0 10h30v10H0z" fill="#fff"/><path d="M0 20h30v10H0z" fill="#21468B"/></svg>',
  pl: '<svg xmlns="http://www.w3.org/2000/svg" aria-hidden="true" viewBox="0 0 30 30"><path d="M0 0h30v15H0z" fill="#fff"/><path d="M0 15h30v15H0z" fill="#DC143C"/></svg>',
  tr: '<svg xmlns="http://www.w3.org/2000/svg" aria-hidden="true" viewBox="0 0 30 30"><path d="M0 0h30v30H0z" fill="#E30A17"/><circle cx="12.5" cy="15" r="7.5" fill="#fff"/><circle cx="14.4" cy="15" r="6" fill="#E30A17"/><polygon points="17.20,15.00 19.55,14.24 19.55,11.77 21.00,13.76 23.35,13.00 21.90,15.00 23.35,17.00 21.00,16.24 19.55,18.23 19.55,15.76" fill="#fff"/></svg>',
  zh: '<svg xmlns="http://www.w3.org/2000/svg" aria-hidden="true" viewBox="0 0 30 30"><path d="M0 0h30v30H0z" fill="#EE1C25"/><polygon points="9.00,5.00 10.12,8.45 13.76,8.45 10.82,10.59 11.94,14.05 9.00,11.91 6.06,14.05 7.18,10.59 4.24,8.45 7.88,8.45" fill="#FFDE00"/><polygon points="15.13,5.51 15.85,4.50 15.12,3.51 16.30,3.88 17.02,2.88 17.02,4.12 18.20,4.49 17.03,4.88 17.03,6.11 16.30,5.12" fill="#FFDE00"/><polygon points="17.84,7.98 18.90,7.34 18.63,6.14 19.56,6.95 20.62,6.32 20.13,7.46 21.06,8.27 19.83,8.16 19.35,9.29 19.07,8.09" fill="#FFDE00"/><polygon points="17.84,11.85 19.06,11.72 19.32,10.51 19.82,11.64 21.05,11.50 20.14,12.33 20.64,13.46 19.57,12.85 18.65,13.67 18.91,12.47" fill="#FFDE00"/><polygon points="15.11,14.32 16.29,14.69 17.00,13.68 17.02,14.91 18.20,15.28 17.03,15.67 17.05,16.91 16.31,15.92 15.14,16.32 15.85,15.31" fill="#FFDE00"/></svg>',
};

window.lgalLangCss = `.lgp{position:relative;display:inline-block}
.lgp .flag{flex:none;display:inline-block;width:20px;height:20px;border-radius:50%;overflow:hidden;box-shadow:0 0 0 1px rgba(255,255,255,.25)}
.lgp .flag svg{display:block;width:100%;height:100%}
.lgp-btn{display:flex;align-items:center;gap:8px;margin:0;font:inherit;font-size:13px;font-weight:600;cursor:pointer;
 color:var(--text,var(--fg));padding:6px 12px 6px 7px;border:1px solid var(--line,#2c4757);border-radius:999px;box-shadow:0 4px 14px rgba(0,0,0,.25);
 background:color-mix(in srgb,var(--card) var(--glass,75%),transparent);-webkit-backdrop-filter:blur(8px);backdrop-filter:blur(8px)}
.lgp-btn:hover{border-color:var(--acc)}.lgp-btn:focus-visible,.lgp-list button:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
.lgp-btn .chev{width:10px;height:7px;margin-left:2px;color:var(--mut);transition:transform .15s}
.lgp-btn[aria-expanded=true] .chev{transform:rotate(180deg)}
.lgp-list{position:absolute;right:0;top:calc(100% + 6px);z-index:30;min-width:190px;max-height:min(420px,70vh);overflow-y:auto;padding:6px;
 border:1px solid var(--line,#2c4757);border-radius:14px;background:var(--card);box-shadow:0 12px 32px rgba(0,0,0,.35)}
.lgp-list[hidden]{display:none}
.lgp-list button{display:flex;align-items:center;gap:12px;width:100%;margin:0;padding:8px 10px;border:0;border-radius:9px;background:none;
 color:var(--text,var(--fg));font:inherit;font-size:15px;font-weight:500;text-align:left;cursor:pointer}
.lgp-list button .flag{width:26px;height:26px}
.lgp-list button:hover{background:color-mix(in srgb,var(--acc) 12%,transparent)}
.lgp-list button[aria-selected=true]{font-weight:700;background:color-mix(in srgb,var(--acc) 18%,transparent)}
.lgp.left .lgp-list{left:0;right:auto}
.card:has(.lgp-list:not([hidden])){position:relative;z-index:5}`;
