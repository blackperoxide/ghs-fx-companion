// Run: node tools/test_studio_lattice.js
const L = require("../Resources/webui/studio-lattice.js");
const assert = require("assert");
let n = 0; const t = (name, fn) => { fn(); n++; console.log("ok  " + name); };
const now = "2026-10-11T12:00:00Z";
const song = (o) => Object.assign({ id: "a", title: "Night Drive", stage: "stems", bpm: 0, key: "", stems: ["stems/x.wav"], checklist: {}, updated: "2026-10-10T00:00:00Z" }, o);

t("no songs -> empty, points at Suno", () => {
  const r = L.nextStep([], { mode: "nudge", nowIso: now });
  assert.strictEqual(r.kind, "empty"); assert.strictEqual(r.action, "open-suno");
});
t("full mix without stems -> needs-stems", () => {
  const r = L.nextStep([song({ stage: "suno", stems: [], needsStems: true, sourceFile: "source/a.mp3" })], { nowIso: now });
  assert.strictEqual(r.kind, "needs-stems");
});
t("tempo step is auto-done once BPM is set; next is listen", () => {
  const r = L.nextStep([song({ bpm: 112 })], { nowIso: now });
  assert.strictEqual(r.stepId, "listen");
});
t("first open step is tempo when BPM unset", () => {
  assert.strictEqual(L.nextStep([song()], { nowIso: now }).stepId, "tempo");
});
t("un-ticking an auto step re-opens it", () => {
  const r = L.nextStep([song({ bpm: 112, checklist: { tempo: false } })], { nowIso: now });
  assert.strictEqual(r.stepId, "tempo");
});
t("all steps ticked -> advance to the next stage", () => {
  const r = L.nextStep([song({ bpm: 100, checklist: { listen: true, mark: true } })], { nowIso: now });
  assert.strictEqual(r.kind, "advance"); assert.strictEqual(r.nextStageId, "rebuild");
});
t("session path auto-completes the session step", () => {
  const r = L.nextStep([song({ stage: "rebuild", sessionPath: "/x/y.logicx" })], { nowIso: now });
  assert.strictEqual(r.stepId, "import");
});
t("finished song says rest, no new demands", () => {
  const all = {}; L.getStage("perform").steps.forEach((s) => (all[s.id] = true));
  const r = L.nextStep([song({ stage: "perform", checklist: all })], { nowIso: now });
  assert.strictEqual(r.kind, "finished");
});
t("quiet mode: no welcome, no why, no minutes", () => {
  const r = L.nextStep([song()], { mode: "quiet", nowIso: now, lastSeenIso: "2026-09-01T00:00:00Z" });
  assert.strictEqual(r.welcome, ""); assert.strictEqual(r.why, ""); assert.strictEqual(r.minutes, 0);
});
t("nudge mode: welcome after 3+ days, never guilt", () => {
  const r = L.nextStep([song()], { mode: "nudge", nowIso: now, lastSeenIso: "2026-10-01T00:00:00Z" });
  assert.ok(/nothing to catch up on/i.test(r.welcome));
  assert.ok(!/(miss|behind|streak|lost|late)/i.test(r.welcome + r.title + r.detail));
});
t("nudge mode: no welcome when seen yesterday", () => {
  assert.strictEqual(L.nextStep([song()], { mode: "nudge", nowIso: now, lastSeenIso: "2026-10-10T12:00:00Z" }).welcome, "");
});
t("coach mode adds why and a time box", () => {
  const r = L.nextStep([song()], { mode: "coach", nowIso: now });
  assert.ok(r.why.length > 0 && r.minutes > 0);
});
t("selected song wins over most recent", () => {
  const a = song({ id: "a", updated: "2026-10-10T00:00:00Z" }), b = song({ id: "b", title: "B", updated: "2026-10-09T00:00:00Z" });
  assert.strictEqual(L.nextStep([a, b], { selectedId: "b", nowIso: now }).songId, "b");
  assert.strictEqual(L.nextStep([a, b], { nowIso: now }).songId, "a");
});
t("most recent unfinished song is chosen over a finished one", () => {
  const all = {}; L.getStage("perform").steps.forEach((s) => (all[s.id] = true));
  const done = song({ id: "done", stage: "perform", checklist: all, updated: "2026-10-11T00:00:00Z" });
  const open = song({ id: "open", updated: "2026-10-01T00:00:00Z" });
  assert.strictEqual(L.nextStep([done, open], { nowIso: now }).songId, "open");
});
t("stuck returns the small step", () => {
  assert.strictEqual(L.stuckStep([song()]), L.getStage("stems").steps[0].small);
});
t("every step has text, small, why, action and unique id within its stage", () => {
  L.STAGES.forEach((st) => { const ids = new Set(); st.steps.forEach((s) => {
    ["text", "small", "why", "action"].forEach((k) => assert.ok(s[k], st.id + "." + s.id + " missing " + k));
    assert.ok(!ids.has(s.id)); ids.add(s.id); }); });
});
t("unknown stage falls back safely", () => {
  assert.ok(L.nextStep([song({ stage: "bogus" })], { nowIso: now }).kind);
});
console.log("\n" + n + " passed");
