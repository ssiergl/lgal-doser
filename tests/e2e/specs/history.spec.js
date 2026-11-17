// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, test } from './fixtures.js';

test('history: period and chart type', async ({ page }) => {
  await open(page, '#/history');
  const range = page.getByRole('radio', { name: '14 days' });
  await expect(range).toHaveAttribute('aria-checked', 'true');
  const chart = page.getByRole('img', { name: 'KH' });
  // date labels on the axis: dd/mm or mm/dd
  const labels = async () => (await chart.textContent()).match(/\d\d\/\d\d/g)?.length ?? 0;
  const at14 = await labels();
  await page.getByRole('radio', { name: '7 days' }).click();
  await expect(page.getByRole('radio', { name: '7 days' })).toHaveAttribute('aria-checked', 'true');
  await expect.poll(labels).toBeLessThan(at14);
  await page.getByRole('radio', { name: 'Line' }).click();
  await expect(page.getByRole('radio', { name: 'Line' })).toHaveAttribute('aria-checked', 'true');
  await expect(chart).toBeVisible();

  await page.getByText('Show as table').click();
  await expect(page.getByRole('table').first()).toBeVisible();
});
