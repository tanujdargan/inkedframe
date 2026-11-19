// node tools/test_dither.mjs -- checks the dither/pack code embedded in the upload page.
import { readFileSync } from 'node:fs';
import assert from 'node:assert';
const html = readFileSync(new URL('../firmware/src/index.html', import.meta.url), 'utf8');
const consts = html.match(/const W = [\s\S]*?const SATURATION[^\n]*\n/)[0];
const code = html.match(/\/\/ DITHER-BEGIN[\s\S]*?\/\/ DITHER-END/)[0];
const { dither, pack, dominantInk, stamp, PALETTE, W, H } = new Function(consts + code + '\nreturn { dither, pack, dominantInk, stamp, PALETTE, W, H };')();

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

// Treatments. Duotone uses only black, white and the chosen ink; poster carries less error, so a
// smooth gradient breaks into fewer, larger patches (fewer ink changes along each row).
for (const ink of [2, 3, 4, 5]) {
  const used = new Set(dither(g, 'duotone', ink));
  assert.deepStrictEqual([...used].sort(), [0, 1, ink].sort(), `duotone ${ink} used ${[...used]}`);
}
const changes = idx => idx.reduce((n, v, i) => n + (i % W && v !== idx[i - 1]), 0);
assert(changes(dither(g, 'poster')) < 0.8 * changes(dither(g)), 'poster should be flatter than natural');
PALETTE.slice(2).forEach((rgb, k) => {  // a field of a slightly lighter shade of each ink picks that ink
  const rgba = new Uint8ClampedArray(W * H * 4);
  for (let i = 0; i < W * H; i++) rgba.set([...rgb.map(c => Math.min(255, c + 20)), 255], i * 4);
  assert.strictEqual(dominantInk(rgba), k + 2, `dominant ink of ${rgb}`);
});
const text = new Uint8ClampedArray(W * H * 4), idx = new Uint8Array(W * H).fill(4);
text[3] = 255; text[7] = 127;  // first pixel solid, second faint
stamp(idx, text, 3);
assert.deepStrictEqual([...idx.subarray(0, 3)], [3, 4, 4], 'stamp sets solid text pixels only');
console.log('dither ok');
