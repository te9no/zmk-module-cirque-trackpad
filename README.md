# zmk-module-cirque-trackpad

ZMK external module for Cirque/Pinnacle trackpad support with a DYA Studio
custom subsystem.

## DYA Studio Subsystem

- Subsystem identifier: `dya__trackpad`
- Compatibility candidates used by the Web UI: `dya_trackpad`,
  `zmk__trackpad`, `trackpad`
- Firmware option: `CONFIG_INPUT_PINNACLE_STUDIO_RPC=y`
- UI URL in firmware: `https://te9no.github.io/zmk-module-cirque-trackpad/`

The Web UI can list trackpad devices, refresh a selected device, put the
trackpad to sleep or wake it, and reset runtime settings.

RPC operations are serialized and use a timeout so an unavailable subsystem does
not leave the page permanently busy.

## Web UI

```sh
cd web
npm ci
npm run dev -- --host 127.0.0.1 --port 5173
```

Production build:

```sh
cd web
npm run build
```

GitHub Pages deployment is handled by `.github/workflows/web-ui-pages.yml`.
