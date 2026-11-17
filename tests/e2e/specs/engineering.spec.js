// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, test } from './fixtures.js';

// The service area stays closed without the engineering key of the device
test('engineering needs the key', async ({ page }) => {
  await open(page);
  const ver = page.getByRole('contentinfo').getByRole('button', { name: /^Firmware/ });
  // 7 presses within 3 s open the dialog; by keyboard, because a click waits for the page to be
  // still, and WebKit without a graphics card (Linux CI) needs about 2 s for that
  await ver.focus();
  for (let i = 0; i < 7; i++) await page.keyboard.press('Enter');
  const dlg = page.getByRole('dialog', { name: 'Engineering' });
  await expect(dlg).toBeVisible();
  const unlock = dlg.getByRole('button', { name: 'Unlock' });
  await expect(unlock).toBeDisabled();
  await dlg.getByRole('textbox', { name: 'Engineering key' }).fill('not-the-key');
  await unlock.click();
  await expect(dlg.locator('.msg.err, [role=status]')).toBeVisible();
  await dlg.getByRole('button', { name: 'Close' }).click();
  await expect(dlg).toBeHidden();

  // direct link without unlocking: no service functions
  expect((await page.request.get('/api/v1/engineering/dosing')).status()).toBe(403);
});
