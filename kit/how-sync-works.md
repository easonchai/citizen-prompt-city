# How sync works

The badge never waits on the web. It pulls news when it is awake and on WiFi, and the pet works the same with or without it.

```
 badge ── WiFi ──► GET  <base>/api/c/<slug>/state        (on boot, on wake, then every 60 s awake)
   ▲                    │
   │                    ▼
   │              prmpt.city database ◄── chat page <base>/c/<slug> ◄── phone scans the QR
   │                    │
   └── Talk screen ◄────┘  next poll sees a new reply
 badge ── WiFi ──► POST <base>/api/c/<slug>/telemetry    (once a minute while awake)
 badge ── WiFi ──► GET  <base>/api/firmware/latest       (after the first sync of each wake, then hourly; newer shows in Settings → Update)
```

## The steps

1. The badge stores a server base URL (default `https://citizen-eason-chai-jun-weis-projects.vercel.app` until the prmpt.city subdomain exists) and its slug. The QR on the badge encodes `<base>/c/<slug>`.
2. When WiFi credentials exist, it tries to connect for at most 8 seconds at every boot and every wake from deep sleep.
3. Right after connecting it polls `GET <base>/api/c/<slug>/state`, then again every 60 seconds while awake. Nothing happens while it sleeps.
4. The response looks like `{"msg":"...","checkin":"KL","name":"...","updatedAt":"...","now":"..."}`. `now` sets the badge clock.
5. The Talk screen fires only when `updatedAt` changes. The Check-in screen fires only when `checkin` changes. The last seen values are kept in RTC memory and Preferences, so waking up never replays an old message.
6. While awake it posts one telemetry line a minute to `POST <base>/api/c/<slug>/telemetry`.
7. After the first sync of each wake, and then at most once an hour while awake, it asks `GET <base>/api/firmware/latest`, which answers `{"version","url","sha256"}`. If that version is higher than the one running, Settings → Update shows `NEW x.y.z` (otherwise `UP TO DATE`). Nothing installs on its own: a swipe right on Update (or the serial `update` command) downloads it over HTTPS, writes it to the spare app slot, and restarts. Hearts and settings survive.
8. V1 does not verify a file signature. The download is not checked against `sha256`, and TLS certificates are not checked either. The ESP32 only rejects an image that is corrupt or not built for it. Only point a badge at a server you trust.

## What that means for a speaker

- A reply typed on the chat page shows on the badge within 60 seconds while it is awake, or on the next wake if it is asleep.
- No WiFi on the day is fine. The pet works offline and replies wait on the server.
- WiFi is set from the badge: double tap, WiFi, swipe right (or hold BOOT for 2 seconds). Join the `CITIZEN-xxxx` network on a phone and fill in the page.
