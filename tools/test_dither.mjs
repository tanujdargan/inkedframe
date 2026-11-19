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
// A gradient must give exactly 128000 bytes, each three valid inks (a*36 + b*6 + c, at most 215).
const g = new Uint8ClampedArray(W * H * 4);
for (let i = 0; i < W * H; i++) g.set([i % W / W * 255, (i / W | 0) / H * 255, 128, 255], i * 4);
const packed = pack(dither(g));
assert.strictEqual(packed.length, 128000);
assert(packed.every(b => b < 216));
assert(new Set(packed).size > 6, 'gradient should mix inks');
assert.deepStrictEqual([...pack([5, 4, 3, 0, 1, 2])], [5*36 + 4*6 + 3, 0*36 + 1*6 + 2]);
console.log('dither ok');
