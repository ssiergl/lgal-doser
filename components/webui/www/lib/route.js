// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// In-app navigation between the #/… pages without loading the page again.
//
// The links stay plain href="#/…" (open in a new tab, copy the link, keyboard). A click on
// one is taken over and moves on with history.pushState, which never loads the document.
// Safari on the iPhone turned a plain hash link into a full page load after the address had
// been typed in: every change of page dropped the live connection and built the page anew.
// Back and forward (popstate) and a hash typed into the address bar (hashchange) still work.

const subs = new Set();
let last = location.hash;

function notify() {
  if (location.hash === last) return;  // back/forward fire popstate and hashchange
  last = location.hash;
  subs.forEach(f => f());
}

// Called when the #/route changes; returns the unsubscribe function
export function onRoute(f) {
  subs.add(f);
  return () => subs.delete(f);
}

// Go to a #/route ("#/settings") as a new history entry
export function navigate(hash) {
  if (hash === location.hash) return;
  history.pushState(null, '', hash);
  notify();
}

addEventListener('popstate', notify);
addEventListener('hashchange', notify);
document.addEventListener('click', e => {
  if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
  const a = e.target instanceof Element && e.target.closest('a[href^="#/"]');
  if (!a || (a.target && a.target !== '_self') || a.hasAttribute('download')) return;
  e.preventDefault();
  navigate(a.getAttribute('href'));
});
