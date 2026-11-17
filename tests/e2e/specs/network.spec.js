// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Network settings: status for everyone, interface, static addresses, Wi-Fi search and test in
// the pro view. A wrong network setting cuts the device off, so the page must point at the field
// the device refused. Each test puts the saved settings back.
import { expect, open, proMode, test } from './fixtures.js';

test.use({ allowErrors: /status of 400 .*\/api\/v1\/net\/settings/ });

const card = page => page.locator('form.card, section.card')
  .filter({ has: page.getByRole('heading', { name: 'Network', level: 2, exact: true }) });
const stat = (c, name) => c.locator('dt', { hasText: new RegExp(`^${name}$`) }).locator('xpath=following-sibling::dd[1]');
const message = c => c.locator(':scope > [role=status]');

// The settings as at the start (the Wi-Fi password stays when it is not sent)
async function keep(page, cleanup) {
  const { mode, lan, wlan: { password_set, ...wlan } } = await (await page.request.get('/api/v1/net/settings')).json();
  cleanup(() => page.request.put('/api/v1/net/settings', { data: { mode, lan, wlan } }));
}

async function openPro(page) {
  await open(page, '#/settings');
  await proMode(page);
  await expect(card(page).getByRole('combobox', { name: 'Interface' })).toBeVisible();
  return card(page);
}

test('the simple view shows the connection only', async ({ page }) => {
  await open(page, '#/settings');
  const c = card(page);
  await expect(stat(c, 'Active')).toHaveText('LAN');
  await expect(stat(c, 'LAN')).toHaveText('192.168.1.50 (DHCP)');
  await expect(stat(c, 'Wi-Fi')).toHaveText('off');
  await expect(c.getByText('Network settings can be changed in the Pro view.')).toBeVisible();
  await expect(c.getByRole('combobox')).toHaveCount(0);
});

test('static LAN address: the refused field is marked, then saved', async ({ page, cleanup }) => {
  await keep(page, cleanup);
  const c = await openPro(page);
  await expect(stat(c, 'LAN')).toHaveText('192.168.1.50 / 255.255.255.0 · GW 192.168.1.1 · DNS 192.168.1.1 (DHCP)');
  const lan = c.getByRole('group', { name: 'LAN' });
  const save = c.getByRole('button', { name: 'Save' });
  await expect(save).toBeDisabled();

  // switching DHCP off starts from the address in use
  await lan.getByRole('switch', { name: 'Automatic address (DHCP)' }).uncheck();
  const ip = lan.getByRole('textbox', { name: 'IP address' });
  await expect(ip).toHaveValue('192.168.1.50');
  await expect(lan.getByRole('textbox', { name: 'Subnet mask' })).toHaveValue('255.255.255.0');
  await expect(lan.getByRole('textbox', { name: 'Gateway' })).toHaveValue('192.168.1.1');
  await expect(lan.getByRole('textbox', { name: 'DNS 1' })).toHaveValue('192.168.1.1');
  await expect(lan.getByRole('textbox', { name: 'DNS 2' })).toHaveValue('');
  await expect(c.getByText('Unsaved changes')).toBeVisible();

  await ip.fill('192.168.1.300');
  await save.click();
  await expect(message(c)).toHaveText('Please check: LAN · IP address');
  await expect(ip).toBeFocused();
  await expect(ip).toHaveClass(/bad/);

  await ip.fill('192.168.1.60');
  await save.click();
  await expect(message(c)).toHaveText(
    'Saved. The device will be at http://192.168.1.60/#/settings – this page may lose the connection.');
  await expect(ip).not.toHaveClass(/bad/);
  await expect(c.getByText('Unsaved changes')).toBeHidden();
  await expect(stat(c, 'LAN')).toContainText('192.168.1.60 / 255.255.255.0');
  await expect(stat(c, 'LAN')).toContainText('(static)');
  const saved = await (await page.request.get('/api/v1/net/settings')).json();
  expect(saved.lan).toEqual({ dhcp: false, ip: '192.168.1.60', netmask: '255.255.255.0', gateway: '192.168.1.1', dns1: '192.168.1.1', dns2: '' });
});

test('interface: hints, discard and "Wi-Fi only" without a network', async ({ page, cleanup }) => {
  await keep(page, cleanup);
  const c = await openPro(page);
  const mode = c.getByRole('combobox', { name: 'Interface' });
  const ssid = c.getByRole('textbox', { name: 'Network (SSID)' });
  await expect(c.getByText(/^LAN first\. Wi-Fi starts after the LAN has been down for 30 s/)).toBeVisible();
  await mode.selectOption('lan');
  await expect(c.getByText('The Wi-Fi radio stays off.')).toBeVisible();
  await c.getByRole('button', { name: 'Discard' }).click();
  await expect(mode).toHaveValue('auto');
  await expect(c.getByText('Unsaved changes')).toBeHidden();

  await mode.selectOption('wlan');
  await expect(c.getByText('Ethernet is switched off.')).toBeVisible();
  await ssid.fill('');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(message(c)).toHaveText('"Wi-Fi only" needs a Wi-Fi network');

  await ssid.fill('HomeNet');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(message(c)).toHaveText('Saved. The settings apply in a moment.');
  await expect(stat(c, 'Active')).toHaveText('Wi-Fi');
  await expect(stat(c, 'Wi-Fi')).toContainText('HomeNet, -61 dBm');
  await expect(stat(c, 'LAN')).toHaveText('switched off');
});

test('Wi-Fi: search, pick a network, test the connection', async ({ page, cleanup }) => {
  await keep(page, cleanup);
  const c = await openPro(page);
  const wifi = c.getByRole('group', { name: 'Wi-Fi' });
  const ssid = wifi.getByRole('textbox', { name: 'Network (SSID)' });
  const password = wifi.getByLabel('Password');
  await expect(ssid).toHaveValue('HomeNet');
  await expect(password).toHaveAttribute('placeholder', 'unchanged');

  await wifi.getByRole('button', { name: 'Search' }).click();
  await expect(wifi.getByRole('status')).toHaveText('Searching …');
  const nets = wifi.locator('.nets button');
  await expect(nets).toHaveCount(4, { timeout: 10_000 });
  await expect(wifi.getByRole('status')).toHaveCount(0);
  await expect(nets.first()).toContainText('🔒 HomeNet');
  await expect(nets.last()).toHaveText(/^Cafe Open-88 dBm$/);  // open network: no lock
  await nets.filter({ hasText: 'Cafe Open' }).click();
  await expect(nets).toHaveCount(0);
  await expect(ssid).toHaveValue('Cafe Open');
  await expect(password).toBeFocused();
  await expect(password).toHaveAttribute('placeholder', '');  // another network: no password kept

  // the test connects without saving
  const testButton = wifi.getByRole('button', { name: 'Test Wi-Fi' });
  await password.fill('wrongpass1');
  await testButton.click();
  await expect(message(c)).toHaveText('Connecting to Cafe Open …');
  await expect(message(c)).toHaveText('Connection failed: wrong password', { timeout: 10_000 });
  await password.fill('rightpass1');
  await testButton.click();
  await expect(message(c)).toHaveText('Connected – address 192.168.1.77. Not saved yet.', { timeout: 10_000 });
  expect((await (await page.request.get('/api/v1/net/settings')).json()).wlan.ssid).toBe('HomeNet');

  // too short for WPA2: the device refuses it
  await password.fill('short');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(message(c)).toHaveText('Please check: Wi-Fi · Password');
  await expect(password).toBeFocused();
  await c.getByRole('button', { name: 'Discard' }).click();
  await expect(ssid).toHaveValue('HomeNet');
  await expect(message(c)).toHaveCount(0);
});
