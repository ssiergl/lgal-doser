// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Status LED (settings page): what it shows now, the colour legend and the night mode. The mock
// LED shows a warning.
import { expect, open, test } from './fixtures.js';

const card = page => page.locator('form.card')
  .filter({ has: page.getByRole('heading', { name: 'Status LED', level: 2, exact: true }) });

test('now, legend and night mode', async ({ page, cleanup }) => {
  const { night: { active, ...night } } = await (await page.request.get('/api/v1/system/led')).json();
  cleanup(() => page.request.put('/api/v1/system/led', { data: { night } }));
  await open(page, '#/settings');
  const c = card(page);
  await expect(c.locator('.led-now')).toHaveText(/^Now: Yellow blinking – warning\s*The overview shows which pump it is about\.$/);
  await c.getByText('What the colours mean').click();
  await expect(c.locator('.led-legend li')).toHaveCount(7);  // LAN and Wi-Fi share the green light
  await expect(c.locator('.led-legend li').first()).toHaveText(/^Red, fast blinking – alarm/);
  await expect(c.locator('.led-legend li').last()).toHaveText('Green – ready, connected by LAN or Wi-Fi');

  const mode = c.locator('.field').filter({ hasText: /^Night mode/ });
  const from = c.getByLabel('From');
  const to = c.getByLabel('To');
  const save = c.getByRole('button', { name: 'Save' });
  await expect(mode.getByRole('radio', { name: 'Off' })).toHaveAttribute('aria-checked', 'true');
  await expect(from).toHaveCount(0);
  await mode.getByRole('radio', { name: 'On' }).click();
  await expect(from).toHaveValue('22:00');
  await expect(to).toHaveValue('07:00');
  const bright = c.locator('.field').filter({ hasText: /^Ready light at night/ }).getByRole('radio');
  await expect(bright).toHaveText(['Off', '10 %', '25 %', '50 %']);
  await expect(bright.first()).toHaveAttribute('aria-checked', 'true');

  await to.fill('22:00');
  await save.click();
  await expect(c.getByRole('status')).toHaveText('Start and end must differ.');
  await from.fill('21:30');
  await to.fill('06:45');
  await bright.filter({ hasText: '10 %' }).click();
  await save.click();
  await expect(c.getByRole('status')).toHaveText('Status LED saved.');
  await expect(save).toBeDisabled();
  const saved = (await (await page.request.get('/api/v1/system/led')).json()).night;
  expect(saved).toMatchObject({ enabled: true, start: '21:30', end: '06:45', brightness: 10 });

  // switching it off again and discarding keeps the saved night mode
  await mode.getByRole('radio', { name: 'Off' }).click();
  await expect(from).toHaveCount(0);
  await c.getByRole('button', { name: 'Discard' }).click();
  await expect(from).toHaveValue('21:30');
});
