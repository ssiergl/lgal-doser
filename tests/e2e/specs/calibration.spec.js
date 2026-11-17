// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, proMode, test } from './fixtures.js';

// the implausible measurement is refused with 400 on purpose
test.use({ allowErrors: /status of 400 .*\/api\/v1\/pumps\/calibrate/ });

const card = page => page.locator('section.card').filter({ has: page.getByRole('heading', { name: 'Calibration', level: 2 }) });

// Start with a small target amount (pro view), the mock delivers it in a few seconds
async function run(page, target) {
  const c = card(page);
  await c.getByRole('textbox', { name: 'Target amount ml' }).fill(target);
  await c.getByRole('button', { name: 'Start calibration' }).click();
  const dlg = page.getByRole('dialog', { name: 'Calibrate pump 5' });
  await expect(dlg).toContainText(`about ${target}`);
  await dlg.getByRole('button', { name: 'Start', exact: true }).click();
  await expect(dlg).toBeHidden();
  await expect(c.getByText(/Calibration running/)).toBeVisible();
  await expect(c.getByText(`The pump should have delivered ${target} ml.`)).toBeVisible({ timeout: 15_000 });
  return c;
}

test('calibration: measure, apply, out of range, discard', async ({ page }) => {
  await open(page, '#/pump/5');
  await proMode(page);
  const before = await card(page).getByText(/^Calibrated: /).textContent();

  // 1.10 ml measured for 1.00 ml: the pump delivers 10 % more per revolution than assumed
  let c = await run(page, '1.00');
  await c.getByRole('textbox', { name: 'Measured amount' }).fill('1.1');
  await c.getByRole('button', { name: 'Apply' }).click();
  await expect(c.getByRole('status')).toHaveText('New calibration saved');
  // the line follows once the pump data is loaded again, a moment after the message
  await expect(c.getByText(/^Calibrated: /)).not.toHaveText(before);
  const after = await c.getByText(/^Calibrated: /).textContent();
  const mlrev = s => parseFloat(s.match(/([\d.]+) ml per revolution/)[1]);
  expect(mlrev(after) / mlrev(before)).toBeCloseTo(1.1, 2);

  // an implausible measurement (more than four times the nominal flow) is refused, the run can
  // then be discarded
  c = await run(page, '1.00');
  await c.getByRole('textbox', { name: 'Measured amount' }).fill('9');
  await c.getByRole('button', { name: 'Apply' }).click();
  await expect(c.locator('.msg.err')).toBeVisible();
  await c.getByRole('button', { name: 'Discard' }).click();
  await expect(c.getByRole('button', { name: 'Start calibration' })).toBeVisible();
  await expect(c.getByText(/^Calibrated: /)).toHaveText(after);
});

test('calibration: a pump that is off cannot be calibrated', async ({ page }) => {
  await open(page, '#/pump/4');
  await expect(card(page).getByRole('button', { name: 'Start calibration' })).toBeDisabled();
});
