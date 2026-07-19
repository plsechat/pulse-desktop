import path from 'path';

export const APP_NAME = 'Pulse';
export const PRELOAD_PATH = path.join(__dirname, '..', 'preload.js');
export const SERVER_SELECTOR_PATH = path.join(__dirname, '..', 'server-selector.html');
export const SCREEN_PICKER_PATH = path.join(__dirname, '..', 'screen-picker.html');
export const SCREEN_PICKER_PRELOAD_PATH = path.join(__dirname, '..', 'screen-picker-preload.js');

// GitHub "owner/repo" that publishes desktop releases. The auto-update feed
// (update.electronjs.org) is derived from this, so it MUST match the real
// repository name. Single source of truth for the repo slug.
export const UPDATE_REPO = 'plsechat/pulse-desktop';
