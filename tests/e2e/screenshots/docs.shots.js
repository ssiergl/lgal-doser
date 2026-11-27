// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Takes the screenshots of the docs, see screenshots.config.js. Every picture exists per language
// (docs/images/en/, docs/images/de/); the guides use the dark theme, the README shows the overview
// in the theme of the reader (light and dark).
import { randomBytes } from 'node:crypto';
import { mkdirSync, readFileSync, readdirSync } from 'node:fs';
import { test, expect, devices } from '@playwright/test';
import { labelPassword } from '../specs/fixtures.js';

const root = new URL('../../../', import.meta.url);
const www = new URL('components/webui/www/', root);

// Texts of the web UI: English from the views (add({...}) of each file), every other language from
// lang/<code>.json; the shots find cards and buttons by their texts in the language of the picture
const english = {};
for (const f of ['app.js', ...readdirSync(new URL('views/', www)).map(v => 'views/' + v)]) {
  for (const m of readFileSync(new URL(f, www), 'utf8').matchAll(/'([a-z0-9_.]+)': '((?:[^'\\]|\\.)*)'/g)) english[m[1]] = m[2];
}
const dicts = { en: english, de: JSON.parse(readFileSync(new URL('lang/de.json', www), 'utf8')) };
const tr = (lang, key) => {
  const v = dicts[lang][key] ?? english[key];
  if (!v) throw new Error(`no text for ${key}`);
  return v;
};
const exact = s => new RegExp('^\\s*' + s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '\\s*$');

const languageName = { en: 'English', de: 'Deutsch' };
const locales = { en: 'en-GB', de: 'de-DE' };
const signedIn = { en: 'http://127.0.0.1:8781', de: 'http://127.0.0.1:8785' };
const signedOut = 'http://127.0.0.1:8783';
const factory = { en: 'http://127.0.0.1:8782', de: 'http://127.0.0.1:8784' };
const desktop = { ...devices['Desktop Chrome'], viewport: { width: 1280, height: 860 }, deviceScaleFactor: 1.5 };
// cards are taken from a tall window, so that no card has to scroll under the sticky header
const tall = { ...desktop, viewport: { width: 1280, height: 2400 } };
const phone = { ...devices['Pixel 7'], deviceScaleFactor: 2 };

const out = lang => {
  const dir = new URL(`docs/images/${lang}/`, root).pathname;
  mkdirSync(dir, { recursive: true });
  return name => dir + name + '.png';
};

// A fresh browser with language, theme and view fixed before the first paint
async function context(browser, lang, { theme = 'dark', mode = 'beginner', device = desktop, baseURL = signedIn[lang] } = {}) {
  const ctx = await browser.newContext({ ...device, baseURL, locale: locales[lang], colorScheme: theme, reducedMotion: 'reduce' });
  await ctx.addInitScript(([l, th, m]) => {
    localStorage.setItem('lgal.lang', l);
    localStorage.setItem('lgal.theme', th);
    localStorage.setItem('lgal.mode', m);
  }, [lang, theme, mode]);
  return ctx;
}

async function settle(page, ms = 600) {
  await page.evaluate(() => document.fonts.ready);
  await page.waitForTimeout(ms);
}

// Opens a page of the app and waits until the live data is there and the page stands still
async function open(page, hash) {
  await page.goto('/' + hash);
  await expect(page.locator('header .live.on')).toHaveCount(1);
  await expect(page.locator('main h1').first()).toBeVisible();
  await settle(page);
}

// The whole page with the window as tall as the page: a full-page shot would leave the reef (fixed
// to the bottom of the window) in the middle of the picture
async function whole(page, path) {
  const height = await page.evaluate(() => document.documentElement.scrollHeight);
  await page.setViewportSize({ width: page.viewportSize().width, height });
  await settle(page, 400);
  await page.screenshot({ path });
}

const card = (page, lang, key) =>
  page.locator('main :is(section, form).card').filter({ has: page.locator('h2', { hasText: exact(tr(lang, key)) }) }).first();
const button = (scope, lang, key) => scope.getByRole('button', { name: exact(tr(lang, key)) });

// A collage of a few highlights for the README: the tops of some cards, three by two, captioned
// with the titles of the cards; all cards at the same scale (wider ones fit the tile)
async function collage(browser, path, tiles) {
  const ctx = await browser.newContext({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1.2 });
  const page = await ctx.newPage();
  const cssWidth = png => png.readUInt32BE(16) / desktop.deviceScaleFactor;  // width from the PNG header
  const cells = tiles.map(([title, png]) => `<figure><div class="pic"><img style="width:${Math.round(cssWidth(png) * 0.85)}px"
      src="data:image/png;base64,${png.toString('base64')}"></div>
    <figcaption>${title}</figcaption></figure>`).join('');
  await page.setContent(`<!doctype html><style>
    body { margin: 0; background: #08131c; font: 15px/1.45 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
    .grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 28px; padding: 36px; }
    figure { margin: 0; }
    .pic { height: 300px; overflow: hidden; display: flex; justify-content: center; align-items: flex-start;
      -webkit-mask-image: linear-gradient(#000 78%, transparent); mask-image: linear-gradient(#000 78%, transparent); }
    .pic img { max-width: 100%; border-radius: 14px; }
    figcaption { margin-top: 6px; color: #e2edf4; font-size: 19px; font-weight: 650; text-align: center; }
  </style><div class="grid">${cells}</div>`);
  await page.evaluate(() => document.fonts.ready);
  await page.locator('.grid').screenshot({ path });
  await ctx.close();
}

for (const lang of ['en', 'de']) {
  test(`screenshots ${lang}`, async ({ browser, request }) => {
    test.setTimeout(240_000);
    const file = out(lang);
    let chart, retract;  // the chart of the history and the retraction, only for the collage
    // the device language of the picture (the demo mock names the pumps in it)
    for (const base of [signedIn[lang], signedOut, factory[lang]]) {
      expect((await request.post(`${base}/mock/language?lang=${lang}`)).ok()).toBeTruthy();
    }

    // overview: README (light and dark, phone) and guide
    for (const theme of ['dark', 'light']) {
      const ctx = await context(browser, lang, { theme });
      const page = await ctx.newPage();
      await open(page, '#/');
      await whole(page, file('overview-' + theme));
      await ctx.close();
    }
    {
      const ctx = await context(browser, lang, { device: phone });
      const page = await ctx.newPage();
      await open(page, '#/');
      await page.screenshot({ path: file('overview-phone') });
      await ctx.close();
    }

    // event list (pro view), before the pump page below adds manual doses: the top of the list
    {
      const ctx = await context(browser, lang, { mode: 'pro', device: tall });
      const page = await ctx.newPage();
      await open(page, '#/history');
      const box = await card(page, lang, 'hi.events').boundingBox();
      await page.screenshot({ path: file('history-events'), clip: { ...box, height: Math.min(box.height, 620) } });
      await ctx.close();
    }

    // pump page (simple view): the top of the page
    {
      const ctx = await context(browser, lang);
      const page = await ctx.newPage();
      await open(page, '#/pump/1');
      await page.screenshot({ path: file('pump') });
      await ctx.close();
    }

    // single cards of the pump page (simple view)
    {
      const ctx = await context(browser, lang, { device: tall });
      const page = await ctx.newPage();
      await open(page, '#/pump/1');
      await card(page, lang, 'sched.title').screenshot({ path: file('schedule') });
      // container: change size and content
      const hero = page.locator('main section.card.hero');
      await button(hero, lang, 'ct.edit').click();
      await settle(page, 300);
      await hero.screenshot({ path: file('container-edit') });
      await button(hero, lang, 'ct.cancel').click();
      // boost: amount and days entered, the preview names the extra amount per day
      const boost = card(page, lang, 'bo.title');
      await boost.getByLabel(tr(lang, 'bo.ml')).fill('20');
      await boost.getByLabel(tr(lang, 'bo.days')).fill('5');
      await settle(page, 300);
      await boost.screenshot({ path: file('boost') });
      // dose now: a dose of 5 ml running
      const dose = card(page, lang, 'dose.title');
      await dose.getByLabel(tr(lang, 'dose.amount')).fill('5');
      await button(dose, lang, 'dose.btn').click();
      await expect(dose.getByRole('button', { name: tr(lang, 'dose.stop') })).toBeVisible();  // "■ Stop"
      await page.waitForTimeout(1200);
      await dose.screenshot({ path: file('dose-running') });
      await expect(button(dose, lang, 'dose.btn')).toBeVisible({ timeout: 30_000 });
      // calibration: the wizard, the run, then entering the measured amount (discarded afterwards)
      await button(card(page, lang, 'cal.title'), lang, 'cal.start').click();
      const dlg = page.locator('section.card.dialog');
      await expect(dlg).toBeVisible();
      await settle(page, 400);
      await dlg.screenshot({ path: file('calibration') });
      await button(dlg, lang, 'cal.dlg.ok').click();
      const cal = card(page, lang, 'cal.title');
      await expect(cal.getByLabel(tr(lang, 'cal.measured'))).toBeVisible({ timeout: 60_000 });
      await cal.getByLabel(tr(lang, 'cal.measured')).fill('9.8');
      await settle(page, 300);
      await cal.screenshot({ path: file('calibration-measure') });
      await button(cal, lang, 'cal.discard').click();
      // container with its warning (calcium)
      await open(page, '#/pump/2');
      await hero.screenshot({ path: file('container-warning') });
      await ctx.close();
    }

    // pump page (pro view): schedule with single times and weekdays, fine-tuning, consumption
    {
      const ctx = await context(browser, lang, { mode: 'pro', device: tall });
      const page = await ctx.newPage();
      await open(page, '#/pump/3');
      await card(page, lang, 'sched.title').screenshot({ path: file('schedule-pro') });
      await open(page, '#/pump/1');
      await card(page, lang, 'pset.title').screenshot({ path: file('pump-settings-pro') });
      await card(page, lang, 'use.title').screenshot({ path: file('consumption') });
      // for the collage: the fine-tuning down to the anti-drip retraction
      const fine = card(page, lang, 'pset.title').locator('fieldset.pro');
      const a = await fine.boundingBox();
      const b = await fine.locator('.field').filter({ hasText: tr(lang, 'pset.retract.hint') }).boundingBox();
      retract = await page.screenshot({ clip: { x: a.x, y: a.y, width: a.width, height: b.y + b.height - a.y + 12 } });
      await ctx.close();
    }

    // history and settings pages (simple view)
    {
      const ctx = await context(browser, lang);
      const page = await ctx.newPage();
      await open(page, '#/history');
      await page.screenshot({ path: file('history') });
      await open(page, '#/settings');
      await page.screenshot({ path: file('settings') });
      await ctx.close();
    }

    // single cards: sign-in, status LED, updates (simple view)
    {
      const ctx = await context(browser, lang, { device: tall });
      const page = await ctx.newPage();
      await open(page, '#/settings');
      await card(page, lang, 'ac.title').screenshot({ path: file('account') });
      await card(page, lang, 'led.title').screenshot({ path: file('led') });
      await open(page, '#/maintenance');
      await card(page, lang, 'mt.rel').screenshot({ path: file('updates') });
      await ctx.close();
    }

    // single cards in the pro view: lock times, MQTT
    {
      const ctx = await context(browser, lang, { mode: 'pro', device: tall });
      const page = await ctx.newPage();
      await open(page, '#/settings');
      await card(page, lang, 'lk.title').screenshot({ path: file('locks') });
      await card(page, lang, 'mq.title').screenshot({ path: file('mqtt') });
      await open(page, '#/history');
      chart = await card(page, lang, 'hi.daily').screenshot();
      await ctx.close();
    }

    // sign-in page of a signed-out browser, on a phone
    {
      const ctx = await context(browser, lang, { device: phone, baseURL: signedOut });
      const page = await ctx.newPage();
      await page.goto('/login');
      await expect(page.locator('input[type=password]').first()).toBeVisible();
      await settle(page, 800);
      await page.screenshot({ path: file('sign-in') });
      await ctx.close();
    }

    // the setup from the factory state, step by step on a phone: the window of its browser, as the
    // other pictures of the phone; the admin password is a random one (only dots in the picture)
    {
      const ctx = await context(browser, lang, { device: phone, baseURL: factory[lang] });
      const page = await ctx.newPage();
      const step = async (n, name) => {
        await expect(page.locator('#stepno')).toContainText(String(n));
        await settle(page);
        await page.screenshot({ path: file(name) });
      };
      await page.goto('/setup');
      await step(1, 'setup-1-language');
      await page.locator('#langs').getByRole('button', { name: languageName[lang] }).click();
      const pw = 'demo-' + randomBytes(8).toString('hex');
      await page.locator('#pw1').fill(pw);
      await page.locator('#pw2').fill(pw);
      await step(2, 'setup-2-password');  // the device password field is plain text: empty here
      await page.locator('#pw0').fill(labelPassword);
      await page.locator('#f_pw button').click();
      await expect(page).toHaveURL(/\/login/);
      await page.locator('#l_pw').fill(pw);
      await page.locator('#f_login button').click();
      await expect(page).toHaveURL(/\/setup/);
      await expect(page.locator('#next')).toBeEnabled();
      await step(3, 'setup-3-network');
      await page.locator('input[name=via][value=wlan]').check();
      await page.locator('#scan').click();
      await expect(page.locator('#nets button').first()).toBeVisible({ timeout: 10_000 });
      await step(3, 'setup-3-wifi');
      await page.locator('input[name=via][value=lan]').check();
      await page.locator('#next').click();
      await expect(page.locator('#timest')).not.toBeEmpty();
      await step(4, 'setup-4-time');
      await page.locator('#next').click();
      await step(5, 'setup-5-finish');
      await ctx.close();
    }

    // highlights for the README
    const png = name => readFileSync(file(name));
    await collage(browser, file('highlights'), [
      [tr(lang, 'sched.title'), png('schedule-pro')],
      [tr(lang, 'lk.title'), png('locks')],
      [tr(lang, 'hi.daily'), chart],
      [tr(lang, 'pset.retract'), retract],
      [tr(lang, 'bo.title'), png('boost')],
      [tr(lang, 'mq.title'), png('mqtt')],
    ]);
  });
}
