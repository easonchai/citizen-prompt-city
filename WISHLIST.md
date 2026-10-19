# Wishlist

Things we would like Citizen to do next. None of it is promised or scheduled; if one of these is what you want to build, open an issue first so we can agree on the shape.

## 1. Citizen Lite: a server you can run yourself

A small open server (Node and SQLite, one process) that answers every route in [API.md](API.md), with a minimal admin to create citizens and events. Today the badge can point at any server (`url` over serial, or `CZ_DEFAULT_BASE`), but there is no open server to start from, so "run your own city" means writing one. Lite would make it `npm start`.

## 2. An organiser API

Write routes, behind an API key, so other people can build on a city instead of only reading it:

- Create and edit events (places).
- `POST /api/events/<place>/checkins { slug }`: check a citizen in. With a phone scanning the badge's QR code (`/c/<slug>`), that is a door-side **QR attendance system**: scan, checked in, and the badge hatches the event's creature on its next sync.
- Grant and revoke unlocks (creatures, skies, stamps).
- **Webhooks** for `citizen.created`, `citizen.hatched`, `checkin.created` and `unlock.granted`, so a check-in can post to a chat, award points or open a door.

## 3. Plugins, the realistic way

The badge is a 4 MB microcontroller, so code plugins on the badge itself are out of scope. Extending it means:

- **Server hooks:** the webhooks above, plus small handlers a Lite server loads from a folder.
- **Content packs:** a new creature or city as data (a sprite sheet, a palette, a skyline and a name) that the core can draw without new code.
- **A card screen:** one generic screen the server can push to a badge, with a title, a line of text and an optional QR code. Organisers could show anything (a schedule change, a quiz, a link) without new firmware.

## 4. An MCP server

A thin layer over the [SDK](sdk/README.md) that gives AI agents tools such as `list_citizens`, `talk_to_citizen`, `check_in` and `grant_unlock`: "check in everyone on this list", "which citizens have not hatched the stamp yet?". It only becomes useful once the organiser API (2) exists; on the current read-only API it could only read and chat.

## Smaller ideas

- A self-serve way to link a home-built badge to a citizen: the badge shows a short claim code, you sign in and enter it.
- Bluetooth WiFi setup through Espressif's ESP BLE Provisioning app, as an alternative to the badge's setup page.
- Badges that stay on whenever they are powered and only sleep on a battery.
- Firmware version in telemetry, so a server can see which badges need an update.
