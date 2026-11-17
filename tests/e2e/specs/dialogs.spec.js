// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, proMode, test } from './fixtures.js';

// The dialog covers the whole window, sits in the middle and nothing lies on top of it
async function expectModal(page, dialog) {
  const vp = page.viewportSize();
  const overlay = page.locator('.overlay');
  await expect(overlay).toBeVisible();
  const o = await overlay.boundingBox();
  expect([o.x, o.y, o.width, o.height]).toEqual([0, 0, vp.width, vp.height]);
  const d = await dialog.boundingBox();
  expect(Math.abs(d.x + d.width / 2 - vp.width / 2)).toBeLessThan(2);
  expect(d.y).toBeGreaterThanOrEqual(0);
  expect(d.y + d.height).toBeLessThanOrEqual(vp.height + 1);
  const onTop = await dialog.evaluate(el => {
    const r = el.getBoundingClientRect();
    return el.contains(document.elementFromPoint(r.x + r.width / 2, r.y + 8));
  });
  expect(onTop).toBe(true);
}

test('calibration dialog is modal and can be cancelled', async ({ page }) => {
  await open(page, '#/pump/1');
  await page.getByRole('button', { name: 'Start calibration' }).click();
  const dlg = page.getByRole('dialog', { name: 'Calibrate pump 1' });
  await expect(dlg).toBeVisible();
  const start = dlg.getByRole('button', { name: 'Start', exact: true });
  await expect(start).toBeFocused();  // focus moves into the dialog once it is ready
  await expectModal(page, dlg);
  await dlg.getByRole('button', { name: 'Cancel' }).click();
  await expect(dlg).toBeHidden();

  // by keyboard (Safari does not focus a clicked button): focus goes back to the opener
  const trigger = page.getByRole('button', { name: 'Start calibration' });
  await trigger.focus();
  await page.keyboard.press('Enter');
  await expect(start).toBeFocused();
  await dlg.getByRole('button', { name: 'Cancel' }).press('Enter');
  await expect(dlg).toBeHidden();
  await expect(trigger).toBeFocused();
  await page.keyboard.press('Enter');
  await expect(start).toBeFocused();
  await page.keyboard.press('Escape');
  await expect(dlg).toBeHidden();
});

test('confirmation: focus, Escape cancels, focus goes back', async ({ page }) => {
  await open(page, '#/pump/3');
  await proMode(page);
  const trigger = page.locator('main').getByRole('button', { name: 'Tube replaced' });
  const hoursBefore = await page.getByRole('definition').filter({ hasText: /of \d+ h/ }).textContent();
  await trigger.focus();
  await page.keyboard.press('Enter');
  const ask = page.getByRole('alertdialog');
  await expect(ask).toContainText('Reset the tube counter of pump 3?');
  await expectModal(page, ask);
  await expect(ask.getByRole('button', { name: 'Tube replaced' })).toBeFocused();
  await page.keyboard.press('Escape');
  await expect(ask).toBeHidden();
  await expect(trigger).toBeFocused();
  await expect(page.getByRole('definition').filter({ hasText: /of \d+ h/ })).toHaveText(hoursBefore);

  // a click beside the dialog cancels as well
  await trigger.click();
  await page.mouse.click(5, 5);
  await expect(ask).toBeHidden();
});
