// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// The unlocked engineering area: every form checks its values, saves them and fills itself again
// from the device. Each test puts back what it changed and locks the area again.
import { engineeringKey, expect, open, test, unlockEngineering } from './fixtures.js';

const card = (page, title) => page.locator('form.card, section.card')
  .filter({ has: page.getByRole('heading', { name: title, level: 2, exact: true }) });
const get = async (page, path) => (await page.request.get(path)).json();
const dosing = page => get(page, '/api/v1/engineering/dosing');
const hw = page => get(page, '/api/v1/hw/status');

async function unlock(page, cleanup) {
  await unlockEngineering(page, cleanup);
  await expect(page.getByText(/^unlocked · \d+ min left$/)).toBeVisible();
}

// Saves a card and waits for its message
async function save(c, text = 'Saved.') {
  await c.getByRole('button', { name: 'Save', exact: true }).click();
  await expect(c.getByRole('status')).toHaveText(text);
}

test('dosing physics: values are checked, speeds must rise, saved values stay', async ({ page, cleanup }) => {
  await unlock(page, cleanup);
  const before = await dosing(page);
  cleanup(() => page.request.post('/api/v1/engineering/dosing?' + new URLSearchParams({
    retract_rev: before.retract_rev, tier3_rpm: before.tier_rpm[2] })));
  const c = card(page, 'Dosing physics (all pumps)');
  const save_ = c.getByRole('button', { name: 'Save', exact: true });
  await expect(save_).toBeDisabled();

  const retract = c.getByRole('textbox', { name: /^Anti-drip retraction/ });
  await retract.fill('3');
  await save_.click();
  await expect(c.getByRole('status')).toHaveText(/^Please check: Anti-drip retraction \("default"\) \(0\.000 … 2\.000\)$/);
  await retract.fill('0.5');
  await expect(c.getByText('≈ 0.19 ml')).toBeVisible();  // 0.5 revolutions × 0.37 ml

  const fast = c.getByRole('textbox', { name: /^Speed fast/ });
  await fast.fill('150');  // slower than "normal" (200)
  await save_.click();
  await expect(c.getByRole('status')).toHaveText('The speeds must not decrease from slow to fast.');
  await fast.fill('320');
  await save(c);
  await expect(save_).toBeDisabled();  // the form holds the saved values again
  const after = await dosing(page);
  expect(after.retract_rev).toBe(0.5);
  expect(after.tier_rpm).toEqual([before.tier_rpm[0], before.tier_rpm[1], 320]);
  await page.reload();
  await expect(card(page, 'Dosing physics (all pumps)').getByRole('textbox', { name: /^Speed fast/ })).toHaveValue('320');
});

test('stepper driver: limits, hold not above run, a driver that does not answer', async ({ page, cleanup }) => {
  await unlock(page, cleanup);
  const before = (await hw(page)).pumps[1].config;
  cleanup(() => page.request.post(`/api/v1/hw/pump/config?pump=2&run_ma=${before.run_ma}&hold_ma=${before.hold_ma}`));
  const c = card(page, 'Stepper driver');
  await c.getByRole('radio', { name: '2 Calcium' }).click();
  await expect(c.getByText('driver OK')).toBeVisible();
  const run = c.getByRole('textbox', { name: /^Run current/ });
  const hold = c.getByRole('textbox', { name: /^Hold current/ });
  await run.fill('2000');
  await c.getByRole('button', { name: 'Save', exact: true }).click();
  await expect(c.getByRole('status')).toHaveText('Please check: Run current (50 … 1,500)');
  await run.fill('700');
  await hold.fill('800');
  await c.getByRole('button', { name: 'Save', exact: true }).click();
  await expect(c.getByRole('status')).toHaveText('Please check: Hold current (0 … 700)');
  await hold.fill('100');
  await save(c);
  expect((await hw(page)).pumps[1].config).toMatchObject({ run_ma: 700, hold_ma: 100 });

  // pump 4: the driver does not answer; the value is kept, the device says so
  await c.getByRole('radio', { name: /^4 / }).click();
  await expect(c.getByText('driver not answering')).toBeVisible();
  await expect(c.getByText(/^No answer on the UART bus/)).toBeVisible();
});

test('channel population: at least one pump, asks before the restart', async ({ page, cleanup }) => {
  await unlock(page, cleanup);
  const c = card(page, 'Channel population');
  const apply = c.getByRole('button', { name: 'Save and restart' });
  await expect(apply).toBeDisabled();
  for (const sw of await c.getByRole('switch').all()) await sw.uncheck();
  await apply.click();
  await expect(c.getByRole('status')).toHaveText('At least one pump must be fitted');

  await c.getByRole('switch').first().check();
  await apply.click();
  const ask = page.getByRole('alertdialog');
  await expect(ask).toContainText('Save the channel population and restart the device?');
  await ask.getByRole('button', { name: 'Cancel' }).click();
  await expect(ask).toBeHidden();
  expect((await hw(page)).fitted_next).toEqual([1, 2, 3, 4, 5]);  // nothing saved
});

test.describe('with a restart', () => {
  // While the device restarts, requests fail and the live connection drops
  test.use({ allowErrors: /ERR_|WebSocket|network connection was lost|Connection terminated unexpectedly|Could not connect|Load failed|NetworkError when attempting|access control checks/ });

  test('channel population: saved, the device restarts, the pump is gone everywhere', async ({ page, cleanup }) => {
    test.slow();
    await unlock(page, cleanup);
    // all five back, with a restart of its own (the change takes effect only then)
    cleanup(async () => {
      await page.request.post('/api/v1/auth/engineering', { data: { key: engineeringKey } });
      await page.request.post('/api/v1/hw/channels?pumps=1,2,3,4,5');
      await page.request.post('/api/v1/system/reboot');
      await expect.poll(async () => (await page.request.get('/api/v1/system/language').catch(() => null))?.ok() ?? false,
        { timeout: 30_000 }).toBe(true);
      await page.request.post('/api/v1/auth/engineering', { data: { key: engineeringKey } });  // the restart locked it
      expect((await hw(page)).fitted).toEqual([1, 2, 3, 4, 5]);
      await page.request.delete('/api/v1/auth/engineering');
    });
    const c = card(page, 'Channel population');
    await c.getByRole('switch').nth(4).uncheck();
    await c.getByRole('button', { name: 'Save and restart' }).click();
    const ask = page.getByRole('alertdialog');
    await expect(ask).toContainText('Save the channel population and restart the device?');
    await ask.getByRole('button', { name: 'Save and restart' }).click();
    await expect(c.getByRole('status')).toHaveText(/^Waiting for the device/, { timeout: 15_000 });
    // the page reloads; the restart locks the engineering area
    await expect(page.getByText('The engineering area is locked.')).toBeVisible({ timeout: 30_000 });

    await open(page, '#/');
    await expect(page.locator('a.pump-card')).toHaveCount(4);
    await page.goto('/#/pump/5');  // no heading on this page
    await expect(page.getByText('Pump 5 is not fitted.')).toBeVisible();
    await page.goto('/#/pump/9');
    await expect(page.getByText('There is no pump 9.')).toBeVisible();
  });
});

test('clock module without a time server: the aging offset is set by hand', async ({ page, cleanup }) => {
  cleanup(() => page.request.post('/mock/time'));
  expect((await page.request.put('/api/v1/system/time', { data: { ntp_server: '' } })).ok()).toBe(true);
  await unlock(page, cleanup);
  // after the unlock, so it runs before the area is locked again (cleanups run in reverse order)
  cleanup(() => page.request.post('/api/v1/hw/rtc/aging?offset=0'));
  const c = card(page, 'Clock module (DS3231)');
  await expect(c.getByText('No time server: the aging offset is set by hand here.')).toBeVisible();
  const aging = c.getByRole('textbox', { name: /^Aging offset/ });
  await expect(c.getByRole('button', { name: 'Save', exact: true })).toBeDisabled();
  await aging.fill('200');
  await c.getByRole('button', { name: 'Save', exact: true }).click();
  await expect(c.getByRole('status')).toHaveText(/^Please check: Aging offset \(−?-?128 … 127\)$/);
  await aging.fill('-12');
  await save(c);
  expect((await hw(page)).rtc.aging).toBe(-12);
  await expect(c.locator('dd').filter({ hasText: /^-12$/ })).toBeVisible();
});

test('history kept longer; EEPROM self test', async ({ page, cleanup }) => {
  const before = (await get(page, '/api/v1/history/settings')).keep_days;
  const longer = before < 60 ? 60 : 59;
  await unlock(page, cleanup);
  cleanup(() => page.request.post(`/api/v1/history/settings?keep_days=${before}`));  // while unlocked
  const hist = card(page, 'History');
  await hist.getByRole('textbox', { name: /^Keep events for/ }).fill(String(longer));
  await save(hist);
  expect((await get(page, '/api/v1/history/settings')).keep_days).toBe(longer);

  const tel = card(page, 'System telemetry');
  await tel.getByRole('button', { name: 'Run test' }).click();
  await expect(tel.getByRole('status')).toHaveText('EEPROM self test passed.');
});

test('limits, container thresholds and history: checked and saved', async ({ page, cleanup }) => {
  await unlock(page, cleanup);
  const before = await dosing(page);
  const sch = await get(page, '/api/v1/engineering/scheduler');
  cleanup(async () => {
    await page.request.post('/api/v1/engineering/dosing?' + new URLSearchParams({ prime_max_s: before.prime_max_s,
      limit_mode: before.limit_mode, warn_days: before.warn_days, low_days: before.low_days }));
    await page.request.post('/api/v1/engineering/scheduler?' + new URLSearchParams({ max_delay_min: sch.max_delay_min }));
  });

  const lim = card(page, 'Timeouts, minimum dose and grid');
  await lim.getByRole('textbox', { name: /^Smallest single dose/ }).fill('0');
  await lim.getByRole('button', { name: 'Save', exact: true }).click();
  await expect(lim.getByRole('status')).toHaveText('Please check: Smallest single dose (0.01 … 10.00)');
  await lim.getByRole('textbox', { name: /^Smallest single dose/ }).fill(String(before.min_dose_ml));
  await lim.getByRole('textbox', { name: /^Priming: max\. duration/ }).fill('120');
  await lim.getByRole('textbox', { name: /^Max\. delay of a scheduled dose/ }).fill('90');
  await lim.getByRole('radio', { name: 'Rolling 24 h' }).click();
  await save(lim);
  expect(await dosing(page)).toMatchObject({ prime_max_s: 120, limit_mode: 'rolling_24h' });
  expect((await get(page, '/api/v1/engineering/scheduler')).max_delay_min).toBe(90);

  const thr = card(page, 'Container thresholds');
  await thr.getByRole('textbox', { name: /^Warning when/ }).fill('5');
  await thr.getByRole('textbox', { name: /^Alarm "almost empty"/ }).fill('6');
  await thr.getByRole('button', { name: 'Save', exact: true }).click();
  await expect(thr.getByRole('status')).toHaveText('The alarm threshold must not be above the warning threshold.');
  await thr.getByRole('textbox', { name: /^Alarm "almost empty"/ }).fill('2');
  await save(thr);
  expect(await dosing(page)).toMatchObject({ warn_days: 5, low_days: 2 });

  // shortening the history deletes days, so only the check is tried here
  const hist = card(page, 'History');
  await hist.getByRole('textbox', { name: /^Keep events for/ }).fill('0');
  await hist.getByRole('button', { name: 'Save', exact: true }).click();
  await expect(hist.getByRole('status')).toHaveText('Please check: Keep events for (1 … 60)');
});

test('saving one card keeps what is typed in another', async ({ page, cleanup }) => {
  await unlock(page, cleanup);
  const before = await dosing(page);
  cleanup(() => page.request.post('/api/v1/engineering/dosing?' + new URLSearchParams({
    prime_max_s: before.prime_max_s, warn_days: before.warn_days })));
  const thr = card(page, 'Container thresholds');
  const warn = thr.getByRole('textbox', { name: /^Warning when/ });
  const typed = String(before.warn_days + 3);
  await warn.fill(typed);
  const lim = card(page, 'Timeouts, minimum dose and grid');
  await lim.getByRole('textbox', { name: /^Priming: max\. duration/ }).fill(String(before.prime_max_s + 10));
  await save(lim);
  // the shared values came again: the typed threshold is still there and still to be saved
  await expect(warn).toHaveValue(typed);
  await expect(thr.getByRole('button', { name: 'Save', exact: true })).toBeEnabled();
  await save(thr);
  expect(await dosing(page)).toMatchObject({ prime_max_s: before.prime_max_s + 10, warn_days: before.warn_days + 3 });
  await expect(thr.getByRole('button', { name: 'Save', exact: true })).toBeDisabled();
});

test('tubes: set and reset a counter, lifetime', async ({ page, cleanup }) => {
  await unlock(page, cleanup);
  const pumps = (await get(page, '/api/v1/pumps')).pumps;
  const life = (await dosing(page)).tube_life_h;
  cleanup(async () => {
    await page.request.post(`/api/v1/pumps/tube?pump=1&hours=${pumps[0].tube.hours}`);
    await page.request.post(`/api/v1/engineering/dosing?tube_life_h=${life}`);
  });
  const c = card(page, 'Tubes');
  const row = c.getByRole('row').filter({ hasText: 'KH' });
  const set = row.getByRole('button', { name: 'Set', exact: true });
  await expect(set).toBeDisabled();
  await row.getByRole('textbox', { name: 'Set counter' }).fill('100');
  await set.click();
  await expect(c.getByRole('status')).toHaveText('Pump 1: counter set to 100 h.');
  await expect(row.getByRole('cell', { name: '100.0 h' })).toBeVisible();
  await row.getByRole('button', { name: 'Reset', exact: true }).click();
  await expect(c.getByRole('status')).toHaveText('Pump 1: counter set to 0 h.');
  await expect(row.getByRole('cell', { name: '0.0 h' })).toBeVisible();

  const lt = c.getByRole('textbox', { name: /^Tube lifetime/ });
  await lt.fill('5');
  await c.getByRole('button', { name: 'Save', exact: true }).click();
  await expect(c.getByRole('status')).toHaveText('Please check: Tube lifetime (maintenance warning) (10 … 65,535)');
  await lt.fill('1500');
  await save(c);
  expect((await dosing(page)).tube_life_h).toBe(1500);
});

test('raw stepper test, driver diagnosis, clock module', async ({ page, cleanup }) => {
  await unlock(page, cleanup);
  cleanup(() => page.request.post('/api/v1/hw/pump/stop?pump=0&mode=hard'));
  const raw = card(page, 'Raw stepper test');
  await expect(raw.getByText('= 1.00 rev ≈ 0.37 ml (nominal) · 2.0 s')).toBeVisible();  // 3200 steps at 1600/s
  await raw.getByRole('textbox', { name: /^Frequency/ }).fill('0');
  await raw.getByRole('button', { name: 'Start' }).click();
  await expect(raw.getByRole('status')).toHaveText(/^Please check: Frequency \(1 … 32,000\)$/);
  await raw.getByRole('textbox', { name: /^Steps/ }).fill('320000');
  await raw.getByRole('textbox', { name: /^Frequency/ }).fill('1600');
  await raw.getByRole('button', { name: 'Start' }).click();
  await expect(raw.getByText(/^running · /)).toBeVisible();
  await expect(raw.getByRole('button', { name: 'Start' })).toBeDisabled();
  await raw.getByRole('button', { name: 'Stop', exact: true }).click();
  await expect(raw.getByText(/^idle · last move /)).toBeVisible();

  // pump 4 does not answer: refused unless forced
  await raw.getByRole('radio', { name: /^4 / }).click();
  await raw.getByRole('textbox', { name: /^Steps/ }).fill('100');
  await raw.getByRole('button', { name: 'Start' }).click();
  await expect(raw.getByRole('status')).toHaveText('Driver not reachable');

  const diag = card(page, 'Driver diagnosis');
  await diag.getByRole('radio', { name: /^4 / }).click();
  await diag.getByRole('button', { name: 'Read registers' }).click();
  await expect(diag.getByText('no answer').first()).toBeVisible();
  await diag.getByRole('radio', { name: '1 KH' }).click();
  await diag.getByRole('button', { name: 'Read registers' }).click();
  await expect(diag.getByText('0x80000000').first()).toBeVisible();

  // with a time server the aging offset is trimmed automatically, there is no field
  const rtc = card(page, 'Clock module (DS3231)');
  await expect(rtc.getByText(/^Trimmed automatically/)).toBeVisible();
  await expect(rtc.getByRole('textbox')).toHaveCount(0);
});

test('danger zone asks first; lock now', async ({ page, cleanup }) => {
  await unlock(page, cleanup);
  const dz = card(page, 'Danger zone');
  await dz.getByRole('button', { name: 'Delete history' }).click();
  await expect(dz.getByText('Really?')).toBeVisible();
  await dz.getByRole('button', { name: 'Cancel' }).click();  // keeps the history for the other tests
  await expect(dz.getByText('Really?')).toBeHidden();
  await dz.getByRole('button', { name: 'Reset network settings' }).click();
  await dz.getByRole('button', { name: 'Yes, do it' }).click();
  await expect(dz.getByRole('status')).toHaveText('Network settings reset – the device applies them in a moment.');

  await page.getByRole('button', { name: 'Lock now' }).click();
  await expect(page.getByText('The engineering area is locked.')).toBeVisible();
  expect((await page.request.get('/api/v1/engineering/dosing')).status()).toBe(403);
});
