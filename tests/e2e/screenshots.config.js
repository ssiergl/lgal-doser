// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Screenshots of the web UI for the README and the user guides (docs/images/<language>/), taken
// from the device mock in its demo state (MOCK_DEMO=1: five pumps set up, no faults). Not part of
// the tests: run it by hand after a visible change of the UI and check the pictures before
// committing them. From the repository root (setup as in playwright.config.js):
//   npx --prefix tests/e2e playwright test -c tests/e2e/screenshots.config.js
import { defineConfig, devices } from '@playwright/test';

const root = new URL('../..', import.meta.url).pathname;
const mock = (port, env = '') => ({
  command: `env MOCK_DEMO=1 ${env} python3 tools/mock/webui_server.py . ${port}`,
  cwd: root,
  url: `http://127.0.0.1:${port}/login`,
  reuseExistingServer: false,
  stdout: 'ignore',
});

export default defineConfig({
  testDir: './screenshots',
  testMatch: '*.shots.js',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  timeout: 120_000,
  reporter: 'list',
  outputDir: 'results-screenshots',
  use: { ...devices['Desktop Chrome'], reducedMotion: 'reduce' },
  // One device per language, so that the doses and runs of one language do not show in the other:
  // signed in (8781 English, 8785 German), factory state for the setup (8782, 8784); 8783 signed
  // out for the sign-in page
  webServer: [mock(8781), mock(8785), mock(8782, 'MOCK_FACTORY=1'), mock(8784, 'MOCK_FACTORY=1'),
    mock(8783, 'MOCK_STRICT_AUTH=1')],
});
