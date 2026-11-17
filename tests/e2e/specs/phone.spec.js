// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, proMode, test } from './fixtures.js';

// Runs in the "phone" project (Pixel 7) only
test('@phone no sideways scrolling, bottom navigation', async ({ page, isMobile }) => {
  test.skip(!isMobile, 'phone project only');
  await open(page);
  await proMode(page);
  const bottom = page.locator('nav.bottom');
  await expect(bottom).toBeVisible();
  for (const hash of ['#/', '#/pump/1', '#/pump/3', '#/history', '#/settings', '#/maintenance']) {
    await page.goto('/' + hash);
    await expect(page.locator('main h1').first()).toBeVisible();
    const over = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
    expect(over, `horizontal overflow on ${hash}`).toBeLessThanOrEqual(0);
  }
  await bottom.getByRole('link', { name: 'History' }).click();
  await expect(page.getByRole('heading', { name: 'History', level: 1 })).toBeVisible();
});
