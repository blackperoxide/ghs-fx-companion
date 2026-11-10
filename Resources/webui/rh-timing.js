"use strict";

/* ============================================================
   Riff House timing helper. Pure logic, no DOM, unit-tested:
     node tools/test_rh_timing.js

   Watches how early or late recent hits landed and, once the
   pattern is steady, suggests a value for the Latency slider.
   It only ever suggests. riffhouse.js applies it on a click.

   dt is (note time - play position) in seconds at the moment a
   hit was judged: negative = the note was heard late.
   ============================================================ */

(function (root) {
  const MIN_HITS = 8;        // need this many usable hits before suggesting
  const MAX_HITS = 24;       // rolling window
  const EDGE_SEC = 0.025;    // hits this close to the window edge are clamped, not real timing
  const MIN_OFFSET_MS = 15;  // smaller than this is not worth changing
  const MAX_SPREAD_MS = 45;  // hits scattered wider than this are not a steady offset
  const STEP_MS = 5;         // slider step
  const SLIDER_MIN = -120, SLIDER_MAX = 120;

  function median(sorted) {
    const n = sorted.length;
    if (!n) return 0;
    return n % 2 ? sorted[(n - 1) / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
  }

  function createTracker() {
    return { samples: [] };
  }

  /** Record one judged hit. windowSec is the hit window (HIT_GOOD). Returns true if it was kept. */
  function addHit(tracker, dtSec, windowSec) {
    if (!tracker || !isFinite(dtSec) || !isFinite(windowSec)) return false;
    // A note already held/ringing when the block enters the window is judged at the edge.
    // That says nothing about latency, so leave it out.
    if (Math.abs(dtSec) >= windowSec - EDGE_SEC) return false;
    tracker.samples.push(dtSec * 1000);
    if (tracker.samples.length > MAX_HITS) tracker.samples.shift();
    return true;
  }

  function reset(tracker) {
    if (tracker) tracker.samples.length = 0;
  }

  /**
   * Returns null (nothing to suggest) or
   *   { offsetMs, direction: "late" | "early", newLatencyMs, hits }
   * offsetMs is always positive; newLatencyMs is what the slider should be set to.
   */
  function suggest(tracker, currentLatencyMs) {
    if (!tracker || tracker.samples.length < MIN_HITS) return null;
    const current = isFinite(currentLatencyMs) ? currentLatencyMs : 0;
    const sorted = tracker.samples.slice().sort((a, b) => a - b);
    const med = median(sorted);
    const spread = median(sorted.map((v) => Math.abs(v - med)).sort((a, b) => a - b));
    if (spread > MAX_SPREAD_MS) return null;
    if (Math.abs(med) < MIN_OFFSET_MS) return null;

    let next = Math.round((current + med) / STEP_MS) * STEP_MS;
    next = Math.max(SLIDER_MIN, Math.min(SLIDER_MAX, next));
    if (next === current) return null; // already at the end of the slider

    return {
      offsetMs: Math.round(Math.abs(med)),
      direction: med < 0 ? "late" : "early",
      newLatencyMs: next,
      hits: sorted.length,
    };
  }

  const api = { createTracker, addHit, reset, suggest, MIN_HITS, MAX_HITS };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.RHTiming = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
