# @prmpt/citizen-sdk

A small TypeScript client for the Citizen badge web API (`web/app/api/**`). No dependencies: it uses the global `fetch`, so it runs in Node 18+ and in every browser. Response types match the route handlers; see `web/README.md` for the full route table.

The badge firmware (`firmware/src/main.cpp`) is the reference client. It speaks the same three routes in C with `HTTPClient`: `/state` after every wake and every 60 s while awake, `/telemetry` once a minute, `/api/firmware/latest` hourly. This package exists for scripts, dashboards and tests, not for the badge.

## Functions

| Function | Route | Resolves to |
|---|---|---|
| `getState(base, slug)` | `GET /api/c/<slug>/state` | `State`: `{ msg, checkin, name, updatedAt, now, error? }` |
| `sendMessage(base, slug, text)` | `POST /api/c/<slug>/chat` | `{ reply }`, `{ reply: null, refused: true }` or `{ error }` |
| `postTelemetry(base, slug, json)` | `POST /api/c/<slug>/telemetry` | `{ ok: true }` or `{ error }` |
| `getTelemetry(base, slug, n = 100)` | `GET /api/c/<slug>/telemetry?n=` | `TelemetryRow[]` (newest first) or `{ error }` |
| `latestFirmware(base)` | `GET /api/firmware/latest` | `Firmware` or `{ error }` |

`base` is the origin, for example `https://connect.prmpt.city` or `http://localhost:3110`. Error responses resolve (not reject) with the server's `{ error }` body so you can branch on them; only a network failure or a non-JSON body throws. Server rate limits apply: `/state` once per 10 s per slug, `/chat` 20 per hour per slug.

## Node

Node 22.6 and later strip TypeScript types, so the `.ts` file imports directly (Node 22.6 to 22.x needs `--experimental-strip-types`). Otherwise point `tsx` or your bundler at it.

```ts
import { getState, sendMessage } from "./sdk/citizen.ts";

const base = "https://connect.prmpt.city";
const state = await getState(base, "tenuk-d2e8");
console.log(state.name, state.msg, state.checkin);

const res = await sendMessage(base, "tenuk-d2e8", "where can I find you?");
if ("reply" in res && res.reply) console.log(res.reply);
else console.log("no reply:", "error" in res ? res.error : "refused");
```

## Browser

Serve or bundle `citizen.ts` with any TypeScript-aware bundler, or copy the file into your app. The web app's own pages call the same routes with plain `fetch`.

```ts
import { getState } from "@prmpt/citizen-sdk";

setInterval(async () => {
  const s = await getState(location.origin, "tenuk-d2e8");
  document.querySelector("#msg").textContent = s.msg;
}, 10_000); // not faster: the server allows one poll per 10 s per slug
```

Cross-origin calls from another site work for the GET routes and the JSON POSTs only if the deployment adds CORS headers; it does not today, so call from the same origin or from a server.

## Self-check

`test.mjs` runs against a local production build and asserts the `/state` shape, the 10 s rate limit, a 404, a chat `400`, a telemetry read, a telemetry `413` and the firmware manifest. It writes nothing.

```bash
cd web && set -a; . ./.env.local; set +a; pnpm build && pnpm start -p 3110 &
node sdk/test.mjs                      # defaults: http://localhost:3110 tenuk-d2e8
node sdk/test.mjs http://localhost:3110 kenyalang-b7c2
```


## Build on Citizen

The collection API is public, CORS is open, and it never returns personal data: no emails, no keys, no device telemetry, no private notes. It is meant for other people's worlds, dashboards and collections.

```ts
import { listCitizens, getCitizen, listPlaces, listUnlocks } from "./citizen";
const base = "https://connect.prmpt.city";
const everyone = await listCitizens(base);        // slug, name, creature, speaker, bio, links, born, hearts, places, unlocks
const tenuk = await getCitizen(base, "tenuk-d2e8");
const places = await listPlaces(base);             // where a citizen can check in and what it earns there
const catalog = await listUnlocks(base);           // every background, creature and stamp, with holder counts
```

How a citizen earns things: it is born with its creature and the Kuala Lumpur sky; checking in at a place (the organiser does this from the admin page) grants that place's unlocks, so a citizen that travels collects skies and stamps. Unlocks marked with no art are catalogued but not drawn yet; the badge firmware does not render them until the art exists.
