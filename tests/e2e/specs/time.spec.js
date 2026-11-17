// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Time (settings page): the doses follow the device clock, so a wrong clock must be visible and
// easy to fix. The mock clock runs 10 min ahead of the host; /mock/time puts it back.
import { expect, open, proMode, test } from './fixtures.js';

test.use({ allowErrors: /status of 400 .*\/api\/v1\/system\/time/ });

const card = page => page.locator('form.card')
  .filter({ has: page.getByRole('heading', { name: 'Time', level: 2, exact: true }) });
const stat = (c, name) => c.locator('dt', { hasText: new RegExp(`^${name}$`) }).locator('xpath=following-sibling::dd[1]');
const message = c => c.locator(':scope > [role=status]');
// Before (the last sync is 3 h before the reset) and after the test
async function reset(page, cleanup) {
  await page.request.post('/mock/time');
  cleanup(() => page.request.post('/mock/time'));
}
const saved = async page => (await (await page.request.get('/api/v1/system/time')).json());

test('a clock that is off is shown and taken from the browser', async ({ page, cleanup }) => {
  await reset(page, cleanup);
  await open(page, '#/settings');
  const c = card(page);
  await expect(stat(c, 'Device time')).toHaveText(/^\d{4}-\d{2}-\d{2} \d{2}:\d{2}(:\d{2})? CET$/);
  await expect(stat(c, 'Source')).toHaveText('Clock module');
  await expect(stat(c, 'Last time server sync')).toHaveCount(0);  // pro view
  await expect(c.getByText('Differs from this browser by 10 min.')).toBeVisible();
  await c.getByRole('button', { name: 'Take the time from this browser' }).click();
  await expect(message(c)).toHaveText('Time taken from this browser.');
  await expect(c.getByText(/^Differs from this browser/)).toBeHidden();
  await expect(stat(c, 'Source')).toHaveText('Set by hand');
  expect(Math.abs((await saved(page)).utc - Date.now() / 1000)).toBeLessThan(60);
});

test.describe('in a browser in London', () => {
  test.use({ timezoneId: 'Europe/London' });

  test('time zone: suggested from the browser, own POSIX string', async ({ page, cleanup }) => {
    await reset(page, cleanup);
    await open(page, '#/settings');
    const c = card(page);
    const zone = c.getByRole('combobox', { name: 'Time zone' });
    await expect(zone.locator('option:checked')).toHaveText('Europe/Berlin, Vienna, Zurich, Paris, Rome, Madrid …');
    await expect(c.getByText('This browser uses Europe/London, Dublin.')).toBeVisible();
    await c.getByRole('button', { name: 'Use it' }).click();
    await expect(zone.locator('option:checked')).toHaveText('Europe/London, Dublin');
    await expect(c.getByText(/^This browser uses/)).toBeHidden();
    await c.getByRole('button', { name: 'Save' }).click();
    await expect(message(c)).toHaveText('Time settings saved.');
    expect((await saved(page)).timezone).toBe('GMT0BST,M3.5.0/1,M10.5.0');

    await zone.selectOption({ label: 'Other (POSIX string) …' });
    const posix = c.getByRole('textbox', { name: 'POSIX time zone' });
    await expect(posix).toHaveAttribute('placeholder', 'CET-1CEST,M3.5.0,M10.5.0/3');
    await posix.fill('3XYZ');
    await c.getByRole('button', { name: 'Save' }).click();
    await expect(message(c)).toHaveText('Please check: Time zone');
    await posix.fill('XYZ-3');
    await c.getByRole('button', { name: 'Save' }).click();
    await expect(message(c)).toHaveText('Time settings saved.');
    expect((await saved(page)).timezone).toBe('XYZ-3');
    await page.reload();
    await expect(card(page).getByRole('textbox', { name: 'POSIX time zone' })).toHaveValue('XYZ-3');
  });
});

test('pro view: time server, without one the time is set by hand', async ({ page, cleanup }) => {
  await reset(page, cleanup);
  await open(page, '#/settings');
  await proMode(page);
  const c = card(page);
  await expect(stat(c, 'Last time server sync')).toHaveText('3 hours ago');
  const ntp = c.getByRole('textbox', { name: 'Time server (NTP)' });
  const manual = c.locator('.sub').filter({ hasText: 'Set by hand' });
  await expect(ntp).toHaveValue('pool.ntp.org');
  await expect(manual).toHaveCount(0);

  await ntp.fill('pool ntp.org');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(message(c)).toHaveText('Please check: Time server (NTP)');
  await ntp.fill('');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(message(c)).toHaveText('Time settings saved.');

  // prefilled with the device time; date and time in the device's time zone
  await expect(manual.getByLabel('Date')).toHaveValue(/^\d{4}-\d{2}-\d{2}$/);
  await expect(manual.getByLabel('Time')).toHaveValue(/^\d{2}:\d{2}$/);
  await manual.getByLabel('Date').fill('2026-01-02');
  await manual.getByLabel('Time').fill('12:34');
  await manual.getByRole('button', { name: 'Set time' }).click();
  await expect(message(c)).toHaveText('Time set.');
  await expect(stat(c, 'Device time')).toContainText('2026-01-02 12:34');
  await expect(stat(c, 'Source')).toHaveText('Set by hand');
  await expect(c.getByText(/^Differs from this browser by \d+ min\.$/)).toBeVisible();
  expect((await saved(page)).ntp_server).toBe('');
});
