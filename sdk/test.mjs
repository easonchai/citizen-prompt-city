// Self-check against a local server: `cd web && pnpm start -p 3110`, then `node sdk/test.mjs [base] [slug]`.
// Node 22.6+ strips the types in citizen.ts; older Node needs --experimental-strip-types.
import assert from "node:assert/strict";
import { getState, getTelemetry, latestFirmware, postTelemetry, sendMessage } from "./citizen.ts";

const base = process.argv[2] ?? "http://localhost:3110";
const slug = process.argv[3] ?? "tenuk-d2e8";

const state = await getState(base, slug);
assert.deepEqual(Object.keys(state).sort(), ["checkin", "msg", "name", "now", "updatedAt"], "state keys");
assert.equal(typeof state.msg, "string");
assert.ok(state.msg.length <= 64, "msg is at most 64 chars");
assert.equal(typeof state.checkin, "string");
assert.equal(typeof state.name, "string");
assert.ok(state.updatedAt === null || !Number.isNaN(Date.parse(state.updatedAt)), "updatedAt is null or ISO");
assert.ok(!Number.isNaN(Date.parse(state.now)), "now is ISO");
console.log("state ok", state);

const again = await getState(base, slug); // second poll inside 10 s is rate limited
assert.equal(again.error, "1 poll per 10 s");
assert.equal(again.msg, "");
console.log("state 429 ok");

const missing = await getState(base, "no-such-badge-" + Date.now());
assert.equal(missing.name, "");
console.log("state 404 ok");

const bad = await sendMessage(base, slug, "");
assert.equal(bad.error, "text must be 1-500 chars");
console.log("chat 400 ok");

// telemetry is not public any more: without the device key both reads and writes are refused
const rows = await getTelemetry(base, slug);
assert.ok("error" in rows, "telemetry GET without a key is refused");
console.log("telemetry 401 ok");
const post = await postTelemetry(base, slug, { hearts: 3 }, "not-the-key");
assert.ok("error" in post, "telemetry POST with a wrong key is refused");
console.log("telemetry post 401 ok");

const fw = await latestFirmware(base);
assert.ok("error" in fw || (typeof fw.version === "string" && fw.url.endsWith(`/firmware/${fw.file}`)));
console.log("firmware ok", fw);

console.log("sdk ok");
