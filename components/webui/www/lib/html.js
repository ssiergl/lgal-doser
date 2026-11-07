// JSX-like templates without a build step: html`<div class=${c}>...</div>`
import { h } from 'preact';
import htm from 'htm';

export const html = htm.bind(h);
