# Citizen assets

Design assets for the Citizen badge: renders, sprite sheets, pose sprites, screen exports, the brand palette, board reference photos, the proposal and the launch film. PNGs are the originals, byte for byte. Code is MIT, art is CC BY-NC 4.0, see [Licence](#licence).

![Hero: the Citizen badge with Tenuk on screen](renders/hero.png)

## Renders

Product renders at 2K and above, used for the kit, the website and the film storyboard.

| | |
|---|---|
| ![Badge on a shirt](renders/badge-on-shirt.png) Badge on a shirt, 1792x2240 | ![Back pin](renders/back-pin.png) The pin on the back, 2048x2048 |
| ![Flat lay](renders/flat-lay.png) Flat lay of the case, front and back, 2048x1360 | ![Scan](renders/scan.png) A phone scanning the QR screen, 1792x2240 |
| ![Handover](renders/handover.png) Pinning a Harimau badge onto a speaker, 2048x1360 | ![World](renders/world.png) The pixel promenade at dusk, the plate for film shot 11, 2688x1520 |
| ![Cities](renders/cities.png) KL, SG and TYO badges, the plate for film shot 10, 2688x1520 | ![Screen spec sheet](renders/screen-spec-sheet.png) Screen spec sheet: every screen, the palette and the frame strips, 1144x2200 |
| ![Hero v1](renders/hero-v1.png) The earlier hero render, kept for reference, 2688x1520 | |

`hero.png` at the top is the current hero, 2688x1520.

## Creatures

Three animals of Kuala Lumpur. Each has a 2048x2048 sprite sheet (the painted source) and eight 64x64 pose PNGs in `sprites/`, which `tools/pack_sprites.py` packs into `core/sprites.c`.

| Tenuk, the tapir | Kenyalang, the hornbill | Harimau, the tiger cub |
|---|---|---|
| ![Tapir sheet](sheets/tapir-sheet.png) | ![Hornbill sheet](sheets/hornbill-sheet.png) | ![Tiger sheet](sheets/tiger-sheet.png) |

Poses, one file per creature and pose (`sprites/<creature>-<POSE>.png`): IDLE, BLINK, EAT, PLAY, SLEEP, SULK, TALK, CHEER.

| ![Tapir idle](sprites/tapir-IDLE.png) | ![Tapir blink](sprites/tapir-BLINK.png) | ![Tapir eat](sprites/tapir-EAT.png) | ![Tapir play](sprites/tapir-PLAY.png) | ![Tapir sleep](sprites/tapir-SLEEP.png) | ![Tapir sulk](sprites/tapir-SULK.png) | ![Tapir talk](sprites/tapir-TALK.png) | ![Tapir cheer](sprites/tapir-CHEER.png) |
|---|---|---|---|---|---|---|---|
| ![Hornbill idle](sprites/hornbill-IDLE.png) | ![Hornbill blink](sprites/hornbill-BLINK.png) | ![Hornbill eat](sprites/hornbill-EAT.png) | ![Hornbill play](sprites/hornbill-PLAY.png) | ![Hornbill sleep](sprites/hornbill-SLEEP.png) | ![Hornbill sulk](sprites/hornbill-SULK.png) | ![Hornbill talk](sprites/hornbill-TALK.png) | ![Hornbill cheer](sprites/hornbill-CHEER.png) |
| ![Tiger idle](sprites/tiger-IDLE.png) | ![Tiger blink](sprites/tiger-BLINK.png) | ![Tiger eat](sprites/tiger-EAT.png) | ![Tiger play](sprites/tiger-PLAY.png) | ![Tiger sleep](sprites/tiger-SLEEP.png) | ![Tiger sulk](sprites/tiger-SULK.png) | ![Tiger talk](sprites/tiger-TALK.png) | ![Tiger cheer](sprites/tiger-CHEER.png) |

`sprites/_preview.png` is a 192x192 contact sheet of the three idle poses, `sprites/palette.png` is the 64x1 index palette the sprites are quantised to.

## Screens

Every screen is 240x240, the badge's native resolution. `screens/<name>.png` is 1x, `screens/<name>-4x.png` is a 960x960 nearest-neighbour upscale for print. Shown here at 4x.

| ![Tapir hatch](screens/tapir-hatch-4x.png) Hatch: the prompt types out on first boot | ![Tapir home](screens/tapir-home-4x.png) Home: Tenuk idle, hearts and the three gestures | ![Tapir qr](screens/tapir-qr-4x.png) QR: hold one second, scan to chat |
|---|---|---|
| ![Tapir talk](screens/tapir-talk-4x.png) Talk: a reply from the chat page | ![Tapir sleep](screens/tapir-sleep-4x.png) Sleep: 30 s without a touch | ![Hornbill home](screens/hornbill-home-4x.png) Home: Kenyalang |
| ![Hornbill eat](screens/hornbill-eat-4x.png) Feed: tap, nasi lemak, a heart | ![Hornbill sulk](screens/hornbill-sulk-4x.png) Sulk: six hours without food | ![Tiger home](screens/tiger-home-4x.png) Home: Harimau |
| ![Tiger play](screens/tiger-play-4x.png) Play: swipe, the hop and the ball | ![Tiger cheer](screens/tiger-cheer-4x.png) Cheer: checked in at the venue | |

The design source of truth for these is `../SCREEN_DESIGN_SOURCE.html`; the running core renders the same screens into `tests/out/*.ppm` under `make test`.

## Palette

The nine brand colours from `core/gfx.c`. Swatches in `palette/` are 24x24 PNGs made with ImageMagick: `magick -size 24x24 xc:#141c3a palette/navy.png`.

| Swatch | Name | Hex | Used for |
|---|---|---|---|
| ![navy](palette/navy.png) | navy | `#141c3a` | night sky |
| ![dusk](palette/dusk.png) | dusk | `#1e2a58` | horizon |
| ![deep](palette/deep.png) | deep | `#0a0e1f` | city and ground |
| ![cream](palette/cream.png) | cream | `#f6f1e4` | text and outlines |
| ![gold](palette/gold.png) | gold | `#f0b840` | windows, events, the band |
| ![coral](palette/coral.png) | coral | `#f06a5a` | hearts |
| ![teal](palette/teal.png) | teal | `#2bb8a8` | secondary |
| ![pink](palette/pink.png) | pink | `#f4a3b5` | sprites only |
| ![sky](palette/sky.png) | sky | `#5aa9e6` | day sky |

## Board reference

The ESP32-2424S012C, the "ESP32-C3 1.28 inch round touch display with case". Vendor drawings and listing photos, for the case, the pin and the film's bezel tracking.

| ![Case front and back](ref/diymore-case-front-back.png) Case, front and back | ![Dimensions](ref/dimensions.png) Dimensions | ![Interfaces](ref/interfaces.png) Interfaces and pinout |
|---|---|---|
| ![Onboard](ref/onboard.png) Onboard parts | ![Battery](ref/battery.png) Battery header (unused, the badge runs on USB-C) | ![Product](ref/product.jpg) Listing photo |

Listing photos in `ref/ws-*.jpg`: the bare board (`ws-bare-1` to `ws-bare-3`), the case (`ws-case-1` to `ws-case-5`, `ws-case-details-5`, `ws-case-front`, `ws-case-size`).

## Proposal

[Citizen-proposal.pdf](proposal/Citizen-proposal.pdf), nine pages: the pitch to PROMPT:KL for a pixel pet badge as the speaker gift, with the three creatures, the screens, the board and the plan. `proposal/index.html` is the source it was printed from.

## Film

[citizen-launch-v1.mp4](film/citizen-launch-v1.mp4), 1920x1080, 24 fps, 54 s, 31 MB, silent (no licensed music track yet). How it is made: [../video/README.md](../video/README.md).

![Poster frame at 10 s: Meet Tenuk](film/poster.png)

Poster frame: `ffmpeg -ss 00:00:10 -i film/citizen-launch-v1.mp4 -frames:v 1 film/poster.png`.

## Licence

Code in this repository is MIT. Everything in `assets/` (sprites, sheets, renders, screens, the proposal and the film) is CC BY-NC 4.0: share and adapt for non-commercial use with attribution to the Citizen contributors. Full text in [../LICENSE](../LICENSE).
