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
