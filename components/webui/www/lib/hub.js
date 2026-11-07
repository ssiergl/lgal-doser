// Tiny observable for module-level state shared by components.
import { useEffect, useState } from 'preact/hooks';

export function hub() {
  const subs = new Set();
  return {
    emit() { subs.forEach(f => f()); },
    // Re-render the calling component on every emit
    use() {
      const [, force] = useState(0);
      useEffect(() => {
        const f = () => force(x => x + 1);
        subs.add(f);
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
