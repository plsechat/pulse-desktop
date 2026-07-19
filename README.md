# Pulse Desktop

Native [Electron](https://www.electronjs.org/) wrapper for the [Pulse](https://github.com/plsechat/pulse-chat) chat client. It's a thin native shell: on first launch it asks for a Pulse server URL, validates it via `GET /info`, then loads that server's web client in a `BrowserWindow`. The UI *is* the Pulse web client — this repo adds the native pieces a browser tab can't: a system tray, window-state persistence, media-permission handling, hardened external-link routing, self-update, and (on macOS) system-audio capture for screen sharing.

## Relationship to the server

This repo builds only the desktop shell. The web UI it renders is served by the Pulse server ([`plsechat/pulse-chat`](https://github.com/plsechat/pulse-chat)). The two are coupled by one runtime contract: the `window.pulseDesktop` bridge exposed from [`src/preload.ts`](src/preload.ts) and consumed by the web client. Keep that surface in sync across the two repos — there is no build-time dependency in either direction.

## Develop

```bash
bun install
bun run dev        # compile TS + launch Electron against the dev shell
```

`bun run dev` runs unpackaged, so auto-update is disabled and it never phones home.

### Quality gate

```bash
bun run check-types   # tsc --noEmit
bun run lint          # eslint (src/ only)
```

## Package

```bash
bun run make               # host platform
bun run package:mac        # darwin arm64 (builds native audio first)
bun run package:win        # win32 x64
bun run package:linux      # linux x64
```

Makers: DMG + ZIP (macOS), Squirrel installer + ZIP (Windows), deb + rpm (Linux).

### macOS native audio

`bun run build:native` builds the CoreAudio HAL plugin (`PulseAudio.driver`) and the N-API addon used to capture system audio during screen sharing. Requires CMake. macOS-only; other platforms have no system-audio-capture path.

### macOS signing / notarization

Set `APPLE_IDENTITY` (signing), and `APPLE_ID` + `APPLE_PASSWORD` + `APPLE_TEAM_ID` (notarization) to enable them in [`forge.config.ts`](forge.config.ts). Signing is **required** for macOS auto-update to apply.

## Auto-update

Packaged macOS and Windows builds self-update from this repo's GitHub Releases via the free [update.electronjs.org](https://github.com/electron/update.electronjs.org) service (see [`src/lib/updater.ts`](src/lib/updater.ts)). The repo slug lives in [`src/lib/constants.ts`](src/lib/constants.ts) as `UPDATE_REPO`.

Requirements:

- **Public repo** — this one is.
- **Published (non-draft) releases** with per-platform assets. The release workflow produces them but leaves the release as a **draft**; publish it to ship the update.
- **macOS** builds must be code-signed.
- **Windows** needs the Squirrel artifacts (`RELEASES`, `*.nupkg`, `Setup.exe`) — produced by the Squirrel maker.
- **Linux** is not covered by Squirrel; update via the distro package.

## Release

The [Release workflow](.github/workflows/release.yml) (`workflow_dispatch`) builds all three platforms, then opens a **draft** GitHub Release tagged `v<version>`. Review it, then publish to activate auto-update.
