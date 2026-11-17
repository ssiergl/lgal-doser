// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, proMode, test } from './fixtures.js';

// Runs in the "phone" project (Pixel 7) only. Every language, because German, French or Polish
// texts are much longer than English ones.
const languages = ['en', 'de', 'fr', 'es', 'it', 'nl', 'pl', 'tr', 'zh'];
const pages = ['#/', '#/pump/1', '#/pump/3', '#/history', '#/settings', '#/maintenance'];

// Page wider than the screen, or a button, chip or tab whose text does not fit
const overflow = () => {
  const out = [];
  const de = document.documentElement;
  if (de.scrollWidth > de.clientWidth) out.push(`page ${de.scrollWidth} > ${de.clientWidth} px`);
  for (const el of document.querySelectorAll('button, .chip, nav a, .seg > *, label, h1, h2')) {
    if (!el.offsetParent) continue;
    if (el.scrollWidth > el.clientWidth + 1 && getComputedStyle(el).overflowX !== 'visible') {
      out.push(`${el.tagName.toLowerCase()}.${el.className} "${el.textContent.trim().slice(0, 40)}"`);
    }
    // rows that scroll sideways on purpose (pump tabs) may reach past the edge
    let box = el.parentElement;
    while (box && !['auto', 'scroll'].includes(getComputedStyle(box).overflowX)) box = box.parentElement;
    const r = el.getBoundingClientRect();
    if (!box && r.right > de.clientWidth + 1) out.push(`off screen: ${el.tagName.toLowerCase()} "${el.textContent.trim().slice(0, 40)}"`);
  }
  return out;
};

for (const lang of languages) {
  test(`@phone layout fits (${lang})`, async ({ page, request, isMobile }) => {
    test.skip(!isMobile, 'phone project only');
    await request.post('/api/v1/system/language?lang=' + lang);
    try {
      await open(page);
      await expect(page.locator('html')).toHaveAttribute('lang', lang);
      await proMode(page);
      const found = [];
      for (const hash of pages) {
        await page.goto('/' + hash);
        await expect(page.locator('main h1').first()).toBeVisible();
        found.push(...(await page.evaluate(overflow)).map(o => `${hash}: ${o}`));
      }
      expect(found.join('\n')).toBe('');
      await expect(page.locator('nav.bottom')).toBeVisible();
    } finally {
      await request.post('/api/v1/system/language?lang=en');
    }
  });
}

test('@phone bottom navigation', async ({ page, isMobile }) => {
  test.skip(!isMobile, 'phone project only');
  await open(page);
  const bottom = page.locator('nav.bottom');
  await expect(bottom).toBeVisible();
  await bottom.getByRole('link', { name: 'History' }).click();
  await expect(page.getByRole('heading', { name: 'History', level: 1 })).toBeVisible();
  await bottom.getByRole('link', { name: 'Settings' }).click();
  await expect(page.getByRole('heading', { name: 'Settings', level: 1 })).toBeVisible();
});
