# Operator dashboard

This directory contains the full operator dashboard. `rklings serve M02` serves
the built bundle on `127.0.0.1:8080` and hosts its WebSocket API.

## Development preview

```sh
cd lib/webapp
npm ci
npm run dev
```

Open `http://127.0.0.1:5173`.

The development server proxies `/ws` and `/shell` to the operator backend.
Run `rklings serve M02` in another terminal when using the preview.
Binding an agent listener on `:443` requires root or `CAP_NET_BIND_SERVICE`.

## Build

```sh
npm ci
npm run lint
npm run build
```

Vite writes the static bundle to `dist/`.
Do not edit generated files in `dist/`; edit `src/` and rebuild.
