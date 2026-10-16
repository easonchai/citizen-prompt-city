// Client for the Citizen web API (web/app/api/**). No dependencies: global fetch only (Node 18+, every browser).
// Shapes mirror the route handlers; non-2xx responses still resolve to their JSON body ({ error }) so callers
// can branch on it. Only a network failure or a non-JSON body throws.

/** GET /api/c/<slug>/state, what the badge polls. */
export type State = {
  msg: string; // first 64 chars of the latest assistant reply, "" if none
  checkin: string; // city of the latest check-in, "" if none
  name: string; // the pet's name, "" on 404 / DB failure
  updatedAt: string | null; // ISO time of the later of msg and checkin
  now: string; // server time, ISO
  error?: string; // "1 poll per 10 s" on 429
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

/** The badge's latest reply, check-in and name. Rate limited to 1 call per 10 s per slug. */
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

/** Store one telemetry object (the firmware's F3 line). Body must be a JSON object under 2048 bytes. */
export function postTelemetry(base: string, slug: string, json_: Record<string, unknown>): Promise<{ ok: true } | ApiError> {
  return fetch(url(base, `${c(slug)}/telemetry`), {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(json_),
  }).then((r) => json<{ ok: true } | ApiError>(r));
}

/** The newest `n` telemetry rows (1..500, default 100), newest first. */
export function getTelemetry(base: string, slug: string, n = 100): Promise<TelemetryRow[] | ApiError> {
  return fetch(url(base, `${c(slug)}/telemetry?n=${n}`), { cache: "no-store" }).then((r) => json<TelemetryRow[] | ApiError>(r));
}

/** The published firmware build, or { error } when none is published. */
export function latestFirmware(base: string): Promise<Firmware | ApiError> {
  return fetch(url(base, "/api/firmware/latest"), { cache: "no-store" }).then((r) => json<Firmware | ApiError>(r));
}
