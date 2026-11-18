// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, proMode, test } from './fixtures.js';

// The reload at the end cuts off requests the settings page has just started; WebKit and Firefox report them
test.use({ allowErrors: /due to access control checks|Load failed|NetworkError when attempting/ });

// What only the pro view shows; the simple view must not show any of it
const proOnly = {
  '#/pump/1': [
    p => p.getByRole('radio', { name: 'Single times' }),
    p => p.getByRole('button', { name: 'Export' }),
    p => p.getByRole('radio', { name: 'Doses' }),
    p => p.getByRole('button', { name: 'Tube replaced' }),
    p => p.getByRole('textbox', { name: 'Target amount ml' }),
    p => p.getByRole('group', { name: 'Fine-tuning' }),
    p => p.locator('main').getByRole('radio', { name: 'Fast' }),
  ],
  '#/settings': [
    p => p.getByRole('heading', { name: 'Lock times between pumps' }),
  ],
};

test('the simple view hides the pro functions, the pro view shows them', async ({ page }) => {
  await open(page);
  for (const [hash, items] of Object.entries(proOnly)) {
    await page.goto('/' + hash);
    await expect(page.locator('main h1').first()).toBeVisible();
    for (const item of items) await expect(item(page), `${hash} in the simple view`).toHaveCount(0);
  }
  await proMode(page);
  for (const [hash, items] of Object.entries(proOnly)) {
    await page.goto('/' + hash);
    await expect(page.locator('main h1').first()).toBeVisible();
    for (const item of items) await expect(item(page).first(), `${hash} in the pro view`).toBeVisible();
  }
  // the choice stays in this browser
  await page.reload();
  await expect(page.locator('header .seg button').nth(1)).toHaveClass(/on/);
});
