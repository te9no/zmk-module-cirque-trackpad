# DYA Cirque Trackpad Web UI

Web UI for the `dya__trackpad` custom Studio subsystem.

## Runtime Behavior

- The UI searches for `dya__trackpad`, `dya_trackpad`, `zmk__trackpad`, then
  `trackpad`.
- RPC operations are serialized so repeated clicks do not start overlapping
  writes.
- RPC calls use a bounded timeout. If the subsystem is missing or not
  responding, the UI returns to an operable state and shows the error.
- The current UI can list devices, refresh the selected device, toggle sleep,
  and reset device settings.

## Usage

```sh
npm ci
npm run dev -- --host 127.0.0.1 --port 5173
```

## Build

```sh
npm run build
```

## Deployment

GitHub Pages deployment is handled by `.github/workflows/web-ui-pages.yml`.
The workflow builds `web/` with Node.js 24 and publishes `web/dist`.
