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

const card = (page, title) => page.locator('section.card').filter({ has: page.getByRole('heading', { name: title, level: 2, exact: true }) });
const pumpOf = async (page, n) => (await (await page.request.get('/api/v1/pumps')).json()).pumps[n - 1];

test('a running dose can be stopped', async ({ page }) => {
  await open(page, '#/pump/3');
  const c = card(page, 'Dose now');
  await c.getByRole('textbox', { name: 'Amount ml', exact: true }).fill('40');
  await c.getByRole('button', { name: 'Dose', exact: true }).click();
  await expect(c.getByText(/^[\d.]+ of 40\.00 ml$/)).toBeVisible();
  await expect(c.getByRole('button', { name: 'Dose', exact: true })).toBeDisabled();
  await c.getByRole('button', { name: /Stop$/ }).click();
  await expect(c.getByRole('button', { name: 'Dose', exact: true })).toBeEnabled();
  expect((await pumpOf(page, 3)).activity).toBe('idle');
  await expect(card(page, 'Consumption').getByText(/^Dose · [\d.]+ ml · stopped$/)).toBeVisible();
});

test('tube replaced (pro view): asks first, then resets the counter', async ({ page, cleanup }) => {
  const hours = (await pumpOf(page, 2)).tube.hours;
  cleanup(() => page.request.post(`/api/v1/pumps/tube?pump=2&hours=${hours}`));
  await page.request.post('/api/v1/pumps/tube?pump=2&hours=120');
  await open(page, '#/pump/2');
  await proMode(page);
  const c = card(page, 'Consumption');
  await expect(c.getByText(/^120\.0 of \d+ h$/)).toBeVisible();
  const ask = page.getByRole('alertdialog');
  await c.getByRole('button', { name: 'Tube replaced' }).click();
  await expect(ask).toContainText('Reset the tube counter of pump 2?');
  await ask.getByRole('button', { name: 'Cancel' }).click();
  expect((await pumpOf(page, 2)).tube.hours).toBe(120);

  await c.getByRole('button', { name: 'Tube replaced' }).click();
  await ask.getByRole('button', { name: 'Tube replaced' }).click();
  await expect(c.getByRole('status')).toHaveText('Tube counter reset – please calibrate again');
  await expect(c.getByText(/^0\.0 of \d+ h$/)).toBeVisible();
  expect((await pumpOf(page, 2)).tube.hours).toBe(0);
});

test('a boost above the daily limit warns before it starts', async ({ page, cleanup }) => {
  const limit = (await pumpOf(page, 1)).daily.limit_ml;
  cleanup(() => page.request.post(`/api/v1/pumps/settings?pump=1&daily_limit_ml=${limit}`));
  const planned = (await (await page.request.get('/api/v1/schedule/status')).json()).pumps[0].planned_today_ml;
  expect(planned).toBeGreaterThan(0);
  await page.request.post(`/api/v1/pumps/settings?pump=1&daily_limit_ml=${planned + 5}`);
  await open(page, '#/pump/1');
  const c = card(page, 'Boost');
  await c.getByRole('textbox', { name: 'Extra amount ml' }).fill('30');
  await c.getByRole('textbox', { name: 'Spread over days' }).fill('3');
  // 10 ml a day on top of the plan, 5 ml are allowed
  await expect(c.getByText(`With the boost about ${(planned + 10).toFixed(2)} ml per day – more than the daily limit of ${(planned + 5).toFixed(2)} ml. Doses above the limit are skipped.`)).toBeVisible();
  await c.getByRole('textbox', { name: 'Spread over days' }).fill('30');  // 1 ml a day fits
  await expect(c.getByText(/more than the daily limit/)).toHaveCount(0);
});
