// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// JSX-like templates without a build step: html`<div class=${c}>...</div>`
import { h } from 'preact';
import htm from 'htm';

export const html = htm.bind(h);
