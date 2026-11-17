// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Device card (settings page): the device name gives the host name in the network (same rule as
// the firmware: umlauts as ae/oe/ue/ss), an own host name in the pro view. A wrong host name makes
// the device hard to find, so the page checks it before saving.
import { expect, open, proMode, test } from './fixtures.js';

const card = page => page.locator('form.card, section.card')
  .filter({ has: page.getByRole('heading', { name: 'Device', level: 2, exact: true }) });
const reach = c => c.locator('dt', { hasText: /^Reachable as$/ }).locator('xpath=following-sibling::dd[1]');
const device = async page => (await (await page.request.get('/api/v1/system/device')).json());
const keep = (page, cleanup) => cleanup(() => page.request.put('/api/v1/system/device', { data: { name: '', own_hostname: '' } }));

test('device name: host name follows, too long is refused, header shows it', async ({ page, cleanup }) => {
  keep(page, cleanup);
  await open(page, '#/settings');
  const c = card(page);
  const name = c.getByRole('textbox', { name: 'Device name' });
  await expect(name).toHaveAttribute('placeholder', 'e.g. Reef tank');
  await expect(reach(c)).toHaveText('lgal-doser-e4f5.local');
  await expect(c.getByRole('textbox', { name: 'Own host name' })).toHaveCount(0);  // pro view

  await name.fill('Riffbecken Süd (Ost) 2');
  await expect(reach(c)).toHaveText('riffbecken-sued-ost-2.local');
  // 17 umlauts are 34 bytes
  await name.fill('ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ');
  await expect(name).toHaveClass(/bad/);
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(c.getByRole('status')).toHaveText('At most 32 bytes (umlauts count twice)');
  await c.getByRole('button', { name: 'Discard' }).click();
  await expect(name).toHaveValue('');

  await name.fill('Riffbecken Süd');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(c.getByRole('status')).toHaveText(/^Saved\. The router shows the new name/);
  await expect(page.locator('header')).toContainText('Riffbecken Süd');
  expect(await device(page)).toMatchObject({ name: 'Riffbecken Süd', hostname: 'riffbecken-sued' });

  await expect(c.getByRole('link', { name: 'Setup wizard' })).toHaveAttribute('href', '/setup');
  await expect(c.getByRole('link', { name: 'Maintenance and firmware update' })).toHaveAttribute('href', '#/maintenance');
});

test('own host name (pro view): checked, then used', async ({ page, cleanup }) => {
  keep(page, cleanup);
  await page.request.put('/api/v1/system/device', { data: { name: 'Reef', own_hostname: '' } });
  await open(page, '#/settings');
  await proMode(page);
  const c = card(page);
  const own = c.getByRole('textbox', { name: 'Own host name' });
  await expect(own).toHaveAttribute('placeholder', 'reef');
  await expect(reach(c)).toHaveText('reef.local');

  await own.fill('-reef');
  await expect(own).toHaveClass(/bad/);
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(c.getByRole('status')).toHaveText('Host name: only a–z, 0–9 and “-”, not at the start or end');

  await own.fill('Doser-Keller');
  await expect(reach(c)).toHaveText('doser-keller.local');  // lower case
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(c.getByRole('status')).toHaveText(/^Saved\./);
  await expect(own).toHaveValue('doser-keller');
  expect(await device(page)).toMatchObject({ name: 'Reef', hostname: 'doser-keller', own_hostname: 'doser-keller' });

  // empty again: from the device name
  await own.fill('');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(reach(c)).toHaveText('reef.local');
  expect((await device(page)).hostname).toBe('reef');
});
