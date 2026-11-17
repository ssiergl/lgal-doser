// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Tiny observable for module-level state shared by components.
import { useEffect, useState } from 'preact/hooks';

export function hub() {
  const subs = new Set();
  let version = 0;  // counts the emits
  return {
    emit() { version++; subs.forEach(f => f()); },
    // Re-render the calling component on every emit. The subscription starts after the first
    // paint; an emit in between (fast answers at start-up) would be lost, so catch up on it.
    use() {
      const [, force] = useState(0);
      const seen = version;
      useEffect(() => {
        const f = () => force(x => x + 1);
        subs.add(f);
        if (version !== seen) f();
        return () => subs.delete(f);
      }, []);
    },
    on(f) { subs.add(f); return () => subs.delete(f); },
  };
}

export function load(key, fallback) {
  try { return localStorage.getItem(key) ?? fallback; } catch (e) { return fallback; }
}

export function save(key, value) {
  try { localStorage.setItem(key, value); } catch (e) { /* private mode */ }
}
