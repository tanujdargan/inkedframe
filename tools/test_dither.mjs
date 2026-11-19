// node tools/test_dither.mjs -- checks the dither/pack code embedded in the upload page.
import { readFileSync } from 'node:fs';
import assert from 'node:assert';
const html = readFileSync(new URL('../firmware/src/index.html', import.meta.url), 'utf8');
const consts = html.match(/const W = [\s\S]*?const SATURATION[^\n]*\n/)[0];
const code = html.match(/\/\/ DITHER-BEGIN[\s\S]*?\/\/ DITHER-END/)[0];
const { dither, pack, PALETTE, W, H } = new Function(consts + code + '\nreturn { dither, pack, PALETTE, W, H };')();

// A solid field of each ink colour must come out almost entirely as that ink
// (the saturation boost leaves a little error to diffuse, so not exactly 100%).
PALETTE.forEach((rgb, p) => {
  const rgba = new Uint8ClampedArray(W * H * 4);
  for (let i = 0; i < W * H; i++) rgba.set([...rgb, 255], i * 4);
  const hit = dither(rgba).filter(v => v === p).length / (W * H);
  assert(hit > 0.97, `solid palette colour ${p} came out only ${(hit * 100).toFixed(1)}% itself`);
});
// A gradient must give exactly 192000 bytes with every nibble a valid ink.
const g = new Uint8ClampedArray(W * H * 4);
for (let i = 0; i < W * H; i++) g.set([i % W / W * 255, (i / W | 0) / H * 255, 128, 255], i * 4);
const packed = pack(dither(g));
assert.strictEqual(packed.length, 192000);
assert(packed.every(b => (b >> 4) < 6 && (b & 15) < 6));
assert(new Set(packed).size > 6, 'gradient should mix inks');
console.log('dither ok');
