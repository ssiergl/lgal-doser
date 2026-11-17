// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, test } from './fixtures.js';

// Board v1.1 with 24 V sense: two more mocks, one without supply (USB power only), one with a
// supply that is too low. Every pump start is refused like on the device.
const cases = [
  { name: 'no 24 V supply', url: 'http://127.0.0.1:8773', banner: /^No 24 V supply – pumps, LAN and clock module are off/, err: 'No 24 V supply',
    volts: '8.0 V · missing', cls: /\berr\b/ },
  { name: '24 V supply too low', url: 'http://127.0.0.1:8774', banner: /^The 24 V supply is too low \(18\.0 V\)/, err: '24 V supply too low',
    volts: '18.0 V · too low', cls: /\bwarn\b/ },
];

for (const c of cases) {
  test.describe(c.name, () => {
    test.use({ baseURL: c.url, allowErrors: /status of 409 .*\/api\/v1\/pumps\// });
    test(`${c.name}: banner, starts are refused`, async ({ page }) => {
      await open(page, '#/pump/1');
      await expect(page.locator('.banner').filter({ hasText: c.banner })).toBeVisible();

      const dose = page.locator('section.card').filter({ has: page.getByRole('heading', { name: 'Dose now', level: 2 }) });
      await dose.getByRole('button', { name: 'Dose', exact: true }).click();
      await expect(dose.locator('.msg.err')).toHaveText(c.err);

      const cal = page.locator('section.card').filter({ has: page.getByRole('heading', { name: 'Calibration', level: 2 }) });
      await cal.getByRole('button', { name: 'Start calibration' }).click();
      await page.getByRole('dialog').getByRole('button', { name: 'Start', exact: true }).click();
      await expect(cal.locator('.msg.err')).toHaveText(c.err);

      const pumps = (await (await page.request.get('/api/v1/pumps')).json()).pumps;
      expect(pumps.every(p => p.activity === 'idle')).toBe(true);
      await page.getByRole('banner').getByRole('link', { name: 'Overview' }).click();
      await expect(page.locator('.banner').filter({ hasText: c.banner })).toBeVisible();
      // the maintenance page shows the measured voltage and its state
      await open(page, '#/maintenance');
      const v = page.locator('dt', { hasText: /^Supply$/ }).locator('xpath=following-sibling::dd[1]');
      await expect(v).toHaveText(c.volts);
      await expect(v).toHaveClass(c.cls);
    });
  });
}

test('board v1.0 has no supply sense: no banner, no voltage', async ({ page }) => {
  await open(page, '#/maintenance');
  await expect(page.getByRole('heading', { name: 'Device', level: 2 })).toBeVisible();
  await expect(page.locator('dt', { hasText: /^Supply$/ })).toHaveCount(0);
  await expect(page.locator('.banner')).toHaveCount(0);
});
