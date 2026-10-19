// Client for the Citizen web API (API.md). No dependencies: global fetch only (Node 18+, every browser).
// Shapes mirror the route handlers; non-2xx responses still resolve to their JSON body ({ error }) so callers
// can branch on it. Only a network failure or a non-JSON body throws.

/** GET /api/c/<slug>/state, what the badge polls. */
export type State = {
  msg: string; // first 64 chars of the latest assistant reply, "" if none
  checkin: string; // city of the latest check-in, "" if none
  name: string; // the pet's name, "" on 404 / DB failure
  skies: string; // granted sky unlocks, comma-separated in the order kl,sg,tyo (e.g. "kl,sg"), "" if none
  creatures: string; // owned creatures, comma-separated (e.g. "tapir,hornbill"), "" if none
  updatedAt: string | null; // ISO time of the later of msg and checkin
  now: string; // server time, ISO
  error?: string; // set on 404 or a server error
};

/** POST /api/c/<slug>/chat */
export type ChatResponse = { reply: string } | { reply: null; refused: true } | { error: string };

/** GET /api/c/<slug>/telemetry: the stored F3 object plus `at`. */
export type TelemetryRow = { at: string; [k: string]: unknown };

/** GET /api/firmware/latest */
export type Firmware = { version: string; file: string; sha256: string; size: number; publishedAt: string; url: string };

export type ApiError = { error: string };

const url = (base: string, path: string) => `${base.replace(/\/$/, "")}${path}`;
const c = (slug: string) => `/api/c/${encodeURIComponent(slug)}`;

async function json<T>(res: Response): Promise<T> {
  const body = await res.json().catch(() => null);
  if (body === null) throw new Error(`citizen api ${res.status}: non-JSON body`);
  return body as T;
}

/** The badge's latest reply, check-in and name. Rate limited to 1 call per 10 s per slug from each address. */
export function getState(base: string, slug: string): Promise<State> {
  return fetch(url(base, `${c(slug)}/state`), { cache: "no-store" }).then((r) => json<State>(r));
}

/** Send one chat message (1 to 500 chars) to the pet. 20 per slug per hour. */
export function sendMessage(base: string, slug: string, text: string): Promise<ChatResponse> {
  return fetch(url(base, `${c(slug)}/chat`), {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ text }),
  }).then((r) => json<ChatResponse>(r));
}

/** Store one telemetry object (the firmware's F3 line). Needs the badge's device key (set on the badge with `token <key>`,
 * shown on the admin page). Body must be a JSON object under 2048 bytes. */
export function postTelemetry(base: string, slug: string, json_: Record<string, unknown>, deviceKey: string): Promise<{ ok: true } | ApiError> {
  return fetch(url(base, `${c(slug)}/telemetry`), {
    method: "POST",
    headers: { "content-type": "application/json", "x-badge-key": deviceKey },
    body: JSON.stringify(json_),
  }).then((r) => json<{ ok: true } | ApiError>(r));
}

/** The newest `n` telemetry rows (1..500, default 100), newest first. Telemetry is not public: pass the badge's
 * device key, or call from a session that owns the badge or has admin basic auth. */
export function getTelemetry(base: string, slug: string, n = 100, deviceKey?: string): Promise<TelemetryRow[] | ApiError> {
  return fetch(url(base, `${c(slug)}/telemetry?n=${n}`), { cache: "no-store", headers: deviceKey ? { "x-badge-key": deviceKey } : {} })
    .then((r) => json<TelemetryRow[] | ApiError>(r));
}

/** The published firmware build, or { error } when none is published. */
export function latestFirmware(base: string): Promise<Firmware | ApiError> {
  return fetch(url(base, "/api/firmware/latest"), { cache: "no-store" }).then((r) => json<Firmware | ApiError>(r));
}


// ---- the collection API (public, CORS open, no personal data): build worlds, dashboards and collections on it
export type PublicCitizen = {
  slug: string; name: string; creature: string; speaker: string; bio: string; links: { label: string; url: string }[];
  city: string; born: string; hearts: number | null; lastSeen: string | null;
  places: { slug: string; name: string; at: string }[];
  unlocks: { key: string; kind: string; name: string; art: string | null; reason: string; at: string }[];
};
export type Place = { slug: string; name: string; city: string; country: string; kind: string; unlocks: string[]; checkins: number };
export type Unlock = { key: string; kind: string; name: string; description: string; art: string | null; holders: number };

async function getJson<T>(url: string): Promise<T> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${res.status} ${url}`);
  return res.json() as Promise<T>;
}
/** Every citizen, public fields only. */
export const listCitizens = (base: string) => getJson<PublicCitizen[]>(`${base}/api/citizens`);
/** One citizen with its places and unlocks. */
export const getCitizen = (base: string, slug: string) => getJson<PublicCitizen>(`${base}/api/citizens/${encodeURIComponent(slug)}`);
/** Places a citizen can check in at and what each grants. */
export const listPlaces = (base: string) => getJson<Place[]>(`${base}/api/places`);
/** The unlock catalog. */
export const listUnlocks = (base: string) => getJson<Unlock[]>(`${base}/api/unlocks`);
