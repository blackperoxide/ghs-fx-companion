// Run: node tools/test_rh_timing.js
const T = require("../Resources/webui/rh-timing.js");
const assert = require("assert");
let n = 0; const t = (name, fn) => { fn(); n++; console.log("ok  " + name); };
const W = 0.15; // HIT_GOOD in riffhouse.js
const fill = (dts) => { const k = T.createTracker(); dts.forEach((d) => T.addHit(k, d, W)); return k; };
const rep = (v, count) => Array.from({ length: count }, () => v);

t("too few hits -> no suggestion", () => {
  assert.strictEqual(T.suggest(fill(rep(-0.04, T.MIN_HITS - 1)), 0), null);
});
t("steadily 40 ms late -> slider goes to -40", () => {
  const s = T.suggest(fill(rep(-0.04, 10)), 0);
  assert.strictEqual(s.direction, "late"); assert.strictEqual(s.offsetMs, 40); assert.strictEqual(s.newLatencyMs, -40);
});
t("steadily 30 ms early -> slider goes up", () => {
  const s = T.suggest(fill(rep(0.03, 10)), 0);
  assert.strictEqual(s.direction, "early"); assert.strictEqual(s.newLatencyMs, 30);
});
t("adds to the current slider value", () => {
  assert.strictEqual(T.suggest(fill(rep(-0.03, 10)), -20).newLatencyMs, -50);
});
t("rounds to the 5 ms slider step", () => {
  assert.strictEqual(T.suggest(fill(rep(-0.037, 10)), 0).newLatencyMs, -35);
});
t("already tight -> no suggestion", () => {
  assert.strictEqual(T.suggest(fill(rep(-0.008, 12)), 0), null);
});
t("scattered timing -> no suggestion", () => {
  assert.strictEqual(T.suggest(fill([-0.12, 0.1, -0.09, 0.11, -0.1, 0.12, -0.11, 0.09, -0.12, 0.1]), 0), null);
});
t("a few outliers do not move the median", () => {
  const s = T.suggest(fill(rep(-0.04, 9).concat([0.1, 0.11])), 0);
  assert.strictEqual(s.newLatencyMs, -40);
});
t("hits clamped at the window edge are ignored", () => {
  const k = T.createTracker();
  assert.strictEqual(T.addHit(k, 0.15, W), false);
  assert.strictEqual(T.addHit(k, 0.13, W), false);
  assert.strictEqual(T.addHit(k, -0.1, W), true);
  assert.strictEqual(k.samples.length, 1);
});
t("bad input is ignored", () => {
  const k = T.createTracker();
  assert.strictEqual(T.addHit(k, NaN, W), false);
  assert.strictEqual(T.addHit(null, 0.01, W), false);
  assert.strictEqual(T.suggest(null, 0), null);
});
t("rolling window keeps only the newest hits", () => {
  const k = fill(rep(0.05, T.MAX_HITS).concat(rep(-0.04, T.MAX_HITS)));
  assert.strictEqual(k.samples.length, T.MAX_HITS);
  assert.strictEqual(T.suggest(k, 0).direction, "late");
});
t("suggestion is clamped to the slider range", () => {
  assert.strictEqual(T.suggest(fill(rep(-0.1, 10)), -100).newLatencyMs, -120);
  assert.strictEqual(T.suggest(fill(rep(-0.1, 10)), -120), null);
});
t("reset clears the tracker", () => {
  const k = fill(rep(-0.04, 10)); T.reset(k);
  assert.strictEqual(T.suggest(k, 0), null);
});
console.log("\n" + n + " passed");
