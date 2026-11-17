// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// ESLint for the web UI. The UI has no build step, so a misspelt name or a missing import only
// shows in the browser; this finds it before. From the repository root:
//   npm ci --prefix tests/lint
//   tests/lint/node_modules/.bin/eslint -c tests/lint/eslint.config.js components/webui/www
import js from '@eslint/js';
import globals from 'globals';

export default [
  { ignores: ['components/webui/www/vendor/**'] },
  {
    files: ['components/webui/www/**/*.js'],
    languageOptions: {
      ecmaVersion: 2023,
      sourceType: 'module',
      globals: { ...globals.browser },
    },
    rules: {
      ...js.configs.recommended.rules,
      'no-unused-vars': ['error', { args: 'none', caughtErrors: 'none' }],
    },
  },
];
