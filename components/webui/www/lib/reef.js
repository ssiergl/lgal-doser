// Background reef: branching corals, sea fans, seaweed and brain corals in
// three depth layers. Drawn in the browser from a fixed seed, so it looks the
// same on every load while the firmware ships a few lines of code instead of
// ~120 KB of path data. Colours come from style.css (--reef-*, .h0–.h5);
// seaweed sways and bubbles rise up the page unless reduced motion is asked
// for.

const f = (v) => v.toFixed(1);

function rng(s) {  // mulberry32
  return () => {
    s = s + 0x6D2B79F5 | 0;
    let t = Math.imul(s ^ s >>> 15, 1 | s);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}

// Growth rules: trunks from the base, each branch forks or bends on,
// gets shorter and thinner and leans back towards upright.
const BRANCH = { trunks: 3, fanout: 1.1, len: 30, w: 10, min: 3, taper: .76, depth: 4, split: .6, spread: .55, pull: .85 };
const FAN = { trunks: 4, fanout: 1.5, len: 20, w: 3.6, min: .9, taper: .8, depth: 6, split: .7, spread: .42, pull: .9 };
const BUSH = { trunks: 5, fanout: 1.6, len: 18, w: 6, min: 2, taper: .8, depth: 4, split: .75, spread: .6, pull: .8 };

function scene(seed) {
  const r = rng(seed), R = (a, b) => a + r() * (b - a);

  function tree(x, y, s, o) {
    const segs = [];
    const grow = (x, y, a, len, w, d) => {
      const ex = x + Math.sin(a) * len, ey = y - Math.cos(a) * len, k = R(-.22, .22) * len;
      segs.push([`M${f(x)} ${f(y)}Q${f((x + ex) / 2 + Math.cos(a) * k)} ${f((y + ey) / 2 + Math.sin(a) * k)} ${f(ex)} ${f(ey)}`, w]);
      if (d <= 0) return;
      const nl = len * R(.72, .9), nw = Math.max(w * o.taper, o.min * s);
      a *= o.pull;
      if (r() < o.split) {
        const sp = o.spread * R(.6, 1.1);
        grow(ex, ey, a - sp * R(.4, 1), nl, nw, d - 1);
        grow(ex, ey, a + sp * R(.4, 1), nl, nw, d - 1);
      } else grow(ex, ey, a + R(-.3, .3), nl, nw, d - 1);
    };
    for (let i = 0; i < o.trunks; i++) {
      const a = o.trunks > 1 ? (i / (o.trunks - 1) - .5) * o.fanout + R(-.15, .15) : R(-.2, .2);
      grow(x + R(-4, 4) * s, y, a, o.len * s * R(.85, 1.1), o.w * s, o.depth);
    }
    return { segs };
  }

  function weed(x, y, s) {  // four tapering blades
    const segs = [];
    for (let b = 0; b < 4; b++) {
      const h = R(70, 120) * s, w = R(5, 8) * s, ph = R(0, 6), bx = x + R(-10, 10) * s;
      let px = bx, py = y;
      for (let i = 1; i <= 12; i++) {
        const t = i / 12, nx = bx + Math.sin(t * 4 + ph) * h * .09 * t + (bx - x) * t * .6, ny = y - h * t;
        segs.push([`M${f(px)} ${f(py)}L${f(nx)} ${f(ny)}`, Math.max(1, w * Math.sin(Math.PI * Math.min(.95, .12 + t * .85)))]);
        px = nx; py = ny;
      }
    }
    return { segs, sway: true };
  }

  function brain(x, y, s) {  // dome with meandering grooves
    const rx = R(34, 46) * s, ry = rx * R(.6, .75);
    let d = `M${f(x - rx)} ${f(y)}`;
    for (let i = 1; i <= 18; i++) {
      const a = Math.PI - Math.PI * i / 18, k = 1 + R(-.05, .05);
      d += `L${f(x + Math.cos(a) * rx * k)} ${f(y - Math.sin(a) * ry * k)}`;
    }
    let gr = '';
    for (let j = 1; j <= 3; j++) {
      const yy = y - ry * j * .24, hw = rx * Math.sqrt(1 - ((y - yy) / ry) ** 2) * .8;
      gr += `M${f(x - hw)} ${f(yy)}`;
      for (let t = 1; t <= 8; t++) gr += `L${f(x - hw + 2 * hw * t / 8)} ${f(yy + Math.sin(t * 1.7 + j) * 3 * s)}`;
    }
    return { fill: d + 'Z', groove: gr };
  }

  function rock(base, amp) {
    let d = `M0 240L0 ${base}`;
    for (let x = 0; x <= 1240; x += 40) d += `Q${x + 20} ${f(base + R(-amp, amp))} ${x + 40} ${f(base + R(-amp, amp) * .5)}`;
    return { fill: d + 'L1200 240Z' };
  }

  // The draft had sponge tubes at two spots; their random numbers are still
  // drawn so every other coral keeps its approved shape.
  function skip(s) { for (let i = 0; i < 4; i++) { R(28, 58); R(-3, 3); } return null; }

  const make = (k, x, y, s) => k === 'weed' ? weed(x, y, s) : k === 'gap' ? skip(s) : k === 'brain' ? brain(x, y, s)
    : tree(x, y, s, k === 'fan' ? FAN : k === 'branch' ? BRANCH : BUSH);
  const items = [];
  const add = (cls, o) => { if (o) items.push(Object.assign(o, { cls })); };

  add('far', rock(200, 8));
  [[40, FAN], [150, BRANCH], [260, BUSH], [380, FAN], [470, BRANCH], [600, BUSH], [700, FAN],
    [820, BRANCH], [930, BUSH], [1040, FAN], [1150, BRANCH]]
    .forEach(([x, o]) => add('far', tree(x + R(-15, 15), 204, .55, o)));
  add('rk', rock(218, 7));
  [[90, 'fan'], [200, 'branch'], [300, 'weed'], [430, 'bush'], [540, 'branch'], [660, 'fan'], [760, 'gap'],
    [880, 'branch'], [990, 'weed'], [1100, 'fan'], [1190, 'bush']]
    .forEach(([x, k], i) => { x += R(-12, 12); add(`m h${(i * 2 + 1) % 6}`, make(k, x, 222, .75)); });
  add('f h0', rock(234, 4));
  [[18, 'weed'], [95, 'branch'], [205, 'brain'], [320, 'fan'], [415, 'weed'], [505, 'bush'], [600, 'gap'],
    [700, 'brain'], [810, 'branch'], [920, 'fan'], [1010, 'weed'], [1090, 'branch'], [1175, 'brain']]
    .forEach(([x, k], i) => { x += R(-10, 10); add(`f h${i % 6}`, make(k, x, 238, 1)); });
  return items;
}

// Bubbles rise from the reef over the whole page: [left %, start above the
// reef base in px, size px, rise time s, delay s]. Negative delays put them
// mid-way already on load.
const BUBBLES = [[2.5, 30, 6, 19, -2], [3.6, 22, 4, 23, -11], [17.8, 40, 5, 21, -15], [35.2, 34, 4, 26, -6],
  [58.5, 26, 6, 18, -9], [60.4, 32, 3.5, 24, -19], [84.5, 38, 5, 22, -4], [97.6, 24, 4.5, 20, -13]];

export function reefSvg(seed = 7) {
  let out = '', n = 0;
  for (const it of scene(seed)) {
    let body = '';
    if (it.fill) body += `<path class="b" d="${it.fill}"/>`;
    if (it.groove) body += `<path class="gr" d="${it.groove}"/>`;
    if (it.segs) {
      const byWidth = new Map();
      for (const [d, w] of it.segs) {
        const k = w.toFixed(1);
        byWidth.set(k, (byWidth.get(k) || '') + d);
      }
      for (const [w, d] of byWidth) body += `<path stroke-width="${w}" d="${d}"/>`;
    }
    out += `<g class="${it.cls}${it.sway ? ` sw s${n++ % 3}` : ''}">${body}</g>`;
  }
  return `<svg class="reef" viewBox="0 0 1200 240" preserveAspectRatio="xMidYMax slice" focusable="false">${out}</svg>`;
}

function bubbles() {
  return '<div class="bubbles">' + BUBBLES.map(([x, y, s, d, delay]) =>
    `<i style="left:${x}%;bottom:${y}px;width:${s}px;height:${s}px;animation-duration:${d}s;animation-delay:${delay}s"></i>`).join('') + '</div>';
}

export function drawReef(el) {
  if (el && !el.firstChild) el.innerHTML = reefSvg() + bubbles();
}
