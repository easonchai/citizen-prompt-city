// Writes one filled HTML card per badge into thank-you/build/<slug>.html (build.sh prints them to PDF).
import { createRequire } from 'node:module';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..', '..');
const QRCode = createRequire(join(repo, 'web', 'package.json'))('qrcode');
// Same base as the firmware's CZ_DEFAULT_BASE (firmware/platformio.ini); change both when the prmpt.city subdomain exists.
const SITE = 'https://citizen-eason-chai-jun-weis-projects.vercel.app';

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const tpl = readFileSync(join(here, 'index.html'), 'utf8');
const badges = JSON.parse(readFileSync(join(repo, 'web', 'seed', 'badges.json'), 'utf8'));
mkdirSync(join(here, 'build'), { recursive: true });

for (const b of badges) {
  const url = `${SITE}/c/${b.slug}`;
  const qr = await QRCode.toString(url, { type: 'svg', margin: 2, errorCorrectionLevel: 'M', color: { dark: '#141c3a', light: '#ffffff' } });
  const fill = { speaker: esc(b.speaker), name: esc(b.name), NAME: esc(b.name.toUpperCase()), prompt: esc(b.prompt), url: esc(url), sprite: `../../img/sprites/${b.creature}-IDLE.png`, qr };
  const html = tpl.replace(/\{\{(\w+)\}\}/g, (_, k) => {
    if (!(k in fill)) throw new Error(`unknown placeholder ${k}`);
    return fill[k];
  });
  writeFileSync(join(here, 'build', `${b.slug}.html`), html);
  console.log(`${b.slug} -> ${url}`);
}
