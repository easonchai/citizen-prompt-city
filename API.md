# Citizen web API

What the badge firmware and the [SDK](sdk/README.md) talk to. The reference server runs at `https://connect.prmpt.city`; to run your own city, answer these routes and point the badge at your server (`CZ_DEFAULT_BASE` in `firmware/platformio.ini`, or `url https://your.server` over serial).

All responses are JSON. Errors are `{ "error": "..." }` with a 4xx or 5xx status. A citizen is identified by its **slug** (for example `tenuk-d2e8`), which is public: it is printed in the badge's QR code.

## Routes the badge uses

### `GET /api/c/<slug>/state`

The badge polls this after every wake, after every WiFi (re)connect, from Settings > Sync, and every 60 s while awake.

```json
{ "msg": "", "checkin": "KL", "name": "Tenuk", "skies": "kl", "creatures": "tapir,hornbill,tiger",
  "updatedAt": "2026-10-16T10:03:56.208Z", "now": "2026-10-19T14:10:31.478Z" }
```

| Field | Meaning on the badge |
|---|---|
| `name` | The pet's name (10 characters shown). |
| `msg` | The latest chat reply, up to 64 characters; shown as a speech bubble when it changes. |
| `checkin` | City code of the latest check-in; a new one shows the Check-in screen. |
| `skies` | Skies the citizen owns, comma-separated from `kl`, `sg`, `tyo`. Settings > Sky steps through these. |
| `creatures` | Creatures the citizen owns, from `tapir`, `hornbill`, `tiger`. A newly owned one hatches from an egg; Settings > Citizen steps through these. Unknown names are ignored, so a server can grant creatures before the firmware draws them. |
| `updatedAt`, `now` | ISO times; `now` also sets the badge clock. |

Unknown slug: `404`. The response is public and can be cached briefly (the reference server caches it at the CDN for 30 s).

### `POST /api/c/<slug>/telemetry`

Every 10 minutes and on every sync. Header `X-Badge-Key: <device key>` (a per-badge secret stored on the badge with the `token` serial command). Body: the badge's status line, for example

```json
{ "t": 179404, "screen": 1, "hearts": 4, "wifi": "ok", "rssi": -62, "creature": "hornbill", "sky": "kl", "heap": 147188 }
```

`200 { "ok": true }`, `401` without a valid key. The server may record which creature and sky the badge is showing (only ones it owns).

### `GET /api/firmware/latest`

```json
{ "version": "1.3.0", "file": "firmware-1.3.0.bin", "sha256": "…", "size": 1239744, "publishedAt": "…",
  "url": "https://connect.prmpt.city/firmware/firmware-1.3.0.bin" }
```

The badge checks hourly and installs a strictly newer version when it has sat idle on Home for 10 s (Settings > Update to do it by hand).

## Public read routes

CORS is open (`*`) on these, for dashboards and other sites. They are cached for 15 s.

| Route | Returns |
|---|---|
| `GET /api/citizens` | Every citizen: `slug`, `name`, `creature`, `speaker`, `bio`, `links`, `city`, `born`, `hearts`, `lastSeen`, `places`, `unlocks`. |
| `GET /api/citizens/<slug>` | One citizen, same shape. |
| `GET /api/places` | Places (`kind` is `event` or `city`) with their unlock keys and check-in counts. |
| `GET /api/unlocks` | The unlock catalog: `key` (`creature.tapir`, `sky.kl`, `stamp.prompt-kl-2026`…), `kind`, `name`, `description`, `art`, `holders`. |
| `GET /api/world` | Every citizen's `slug`, `creature`, `name` and latest `msg`, for a city map. |

## Talking to a citizen

### `POST /api/c/<slug>/chat`

Body `{ "text": "where can I find you?" }` as JSON. Returns `{ "reply": "..." }`. The citizen answers with its owner's own model key; `409` when the owner has not connected one. Browsers on other sites are refused (`403`); scripts and servers are fine. Limits on the reference server: 10 messages an hour per IP, 20 an hour per citizen.

## Collection model

A citizen owns **unlocks**. A **place** (an event or a city) grants unlocks: some when a person signs up, some when they attend (on the reference server, attending is an imported ticket list matched by email). The badge only needs the result: the owned `skies` and `creatures` in `/state`. How a server decides who owns what (QR check-in, ticket import, quests, anything) is up to that server.
