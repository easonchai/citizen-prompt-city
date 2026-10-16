# Citizen speaker kit

A thank-you from PROMPT:KL, 31 October 2026.

## What is in the box

- The Citizen badge: a round touch screen with your pixel citizen inside
- A card with your citizen's name, its system prompt and a QR code to chat with it
- A lanyard
- Tape, to stick the badge on a laptop or a wall
- A loose pin, to wear it on a shirt or bag

## What it is

A small virtual pet on a round smart badge. Your citizen is one of three animals of Kuala Lumpur (Tenuk the tapir, Kenyalang the hornbill, Harimau the tiger cub). It hatched from a one-line system prompt we wrote about your talk. That line is printed on your card, typed out on the badge the first time it boots, and is the citizen's personality when people chat with it.

## What it can do

- Tap to feed it, swipe to play, and it keeps up to four hearts.
- It gets hungry over time and sulks if you forget it for six hours.
- It is always moving: breathing, blinking, hopping, eating.
- The sky follows the real clock, from dawn to blue day to dusk to a night with lit windows.
- Hold the screen for a second to show a QR code. Anyone who scans it can chat with your citizen.
- Replies from that chat appear on the badge within a minute when it is on WiFi.
- Double tap for Settings: brightness, clock, timezone, citizen, WiFi, update, about.
- It updates itself over WiFi from Settings → Update. USB flashing still works as a fallback.
- It sleeps after 30 seconds without a touch. A tap wakes it.
- It runs from any USB-C power source.

## What is in this folder

| Path | What |
|---|---|
| `booklet.pdf` | The 8-page A5 guide. Print it or send it |
| `thank-you/<slug>.pdf` | One A6 card per speaker, front and back |
| `how-sync-works.md` | How the badge and the website talk to each other |
| `videos/` | The teaser and the launch film |
| `renders/` | Hero, flat lay, badge on a shirt, and the other launch images |

## Getting help

Start with the last page of the booklet. If that does not fix it, message the PROMPT:KL team with what you see on the screen, and a photo if you can.

## Rebuilding this kit

```sh
kit/build.sh
```

Needs Google Chrome, Node, ImageMagick and `web/node_modules` installed. It prints `booklet.pdf` and every card from `web/seed/badges.json`, and copies the renders and videos.
