// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, proMode, test } from './fixtures.js';

const schedule = page => page.locator('section, .card').filter({ has: page.getByRole('heading', { name: 'Schedule', level: 2 }) }).last();

test('change the daily amount, save, reload', async ({ page, cleanup }) => {
  await open(page, '#/pump/2');
  const amount = page.getByRole('textbox', { name: 'Daily amount ml' });
  const save = async value => {
    await amount.fill(value);
    const done = page.waitForResponse(r => r.url().includes('/api/v1/pumps/schedule') && r.request().method() === 'PUT');
    await schedule(page).getByRole('button', { name: 'Save' }).click();
    expect((await done).ok()).toBe(true);
    await expect(page.getByRole('status').filter({ hasText: 'Schedule saved' })).toBeVisible();
  };
  const mlOf = async () => (await (await page.request.get('/api/v1/pumps/schedule?pump=2')).json()).ml;
  const before = await amount.inputValue();
  const restore = cleanup(() => save(before));
  await save('24');
  expect(await mlOf()).toBe(24);
  await expect(page.getByText(/4\.00 ml/).first()).toBeVisible();  // 6 doses × 4.00 ml
  await page.reload();
  await expect(amount).toHaveValue('24');
  await restore();
  expect(String(await mlOf())).toBe(before);
});

test('pro view: unsaved changes can be discarded', async ({ page }) => {
  await open(page, '#/pump/1');
  await proMode(page);
  const amount = page.getByRole('textbox', { name: 'Amount per day ml' });
  const save = schedule(page).getByRole('button', { name: 'Save' });
  await expect(save).toBeDisabled();
  await amount.fill('66');
  await expect(save).toBeEnabled();
  await expect(schedule(page).getByText('Unsaved')).toBeVisible();
  await schedule(page).getByRole('button', { name: 'Discard' }).click();
  await expect(amount).toHaveValue('60');
  await expect(save).toBeDisabled();
});

test('dose now runs and shows on the overview', async ({ page }) => {
  await open(page, '#/pump/1');
  await page.getByRole('textbox', { name: 'Amount ml', exact: true }).fill('3');
  await page.getByRole('button', { name: 'Dose', exact: true }).click();
  await expect(page.getByText('3.00 ml started')).toBeVisible();
  await page.getByRole('banner').getByRole('link', { name: 'Overview' }).click();
  await expect(page.locator('a.pump-card').first().locator('.running')).toBeVisible();
});

test('hold to fill primes only while held', async ({ page }) => {
  await open(page, '#/pump/5');
  const hold = page.locator('main button.hold');
  await expect(hold).toHaveText('Hold to fill');
  await hold.hover();
  await page.mouse.down();
  await expect(hold).toHaveClass(/held/);
  await expect.poll(async () => (await (await page.request.get('/api/v1/pumps')).json()).pumps[4].activity).toBe('priming');
  await page.mouse.up();
  await expect(hold).not.toHaveClass(/held/);
  await expect.poll(async () => (await (await page.request.get('/api/v1/pumps')).json()).pumps[4].activity).toBe('idle');
});
