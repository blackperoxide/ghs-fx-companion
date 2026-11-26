"use strict";

/* ============================================================
   Voice - real-time voice-to-MIDI. Talks to the C++ engine
   (Source/VoiceEngine.*) through the same callNative/onNative
   bridge app.js sets up, same pattern as visuals.js/console1.js.
   Melody mode only in v1 - sing/hum a pitch, it drives whichever
   Rack slot's instrument you pick, no external MIDI routing.
   ============================================================ */

(function () {
  const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  function midiToName(m) {
    if (m == null || m < 0 || !isFinite(m)) return "--";
    const n = Math.round(m);
    return NOTE_NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
  }

  const VC = {
    live: null,
    params: { enabled: false, sensitivity: 0.5, targetSlot: -1, midiChannel: 1, slots: [] },
  };
  window.VC = VC;

  /* ---------------- root DOM ---------------- */

  function buildRoot() {
    const root = document.getElementById("vcRoot");
    root.innerHTML = `
      <div class="vc-controls">
        <label class="vc-toggle">
          <input type="checkbox" id="vcEnabled" />
          <span>Voice On</span>
        </label>
        <label>Target slot
          <select id="vcTargetSlot"></select>
        </label>
        <label>Sensitivity
          <input type="range" id="vcSensitivity" min="0" max="1" step="0.01" />
        </label>
        <label>MIDI channel
          <input type="number" id="vcMidiChannel" min="1" max="16" step="1" />
        </label>
      </div>
      <div class="vc-readout">
        <div class="vc-note" id="vcNote">--</div>
        <div class="vc-meters">
          <label>Clarity<div class="vc-bar"><div class="vc-bar-fill" id="vcClarityFill"></div></div></label>
          <label>Level<div class="vc-bar"><div class="vc-bar-fill" id="vcRmsFill"></div></div></label>
        </div>
      </div>
      <p class="vc-note-help">
        Melody mode: sing or hum - the pitch drives whichever Rack slot you pick above.
        Typical onset-to-note latency is roughly 100-150&nbsp;ms, same order as the Riff
        House pitch display. Vocal-percussion / rhythm mode is a separate, later feature.
      </p>
    `;
  }

  function fillTargetSlotOptions() {
    const sel = document.getElementById("vcTargetSlot");
    const prev = sel.value;
    sel.innerHTML = "";
    const none = document.createElement("option");
    none.value = "-1";
    none.textContent = "None";
    sel.appendChild(none);
    (VC.params.slots || []).forEach((s) => {
      const opt = document.createElement("option");
      opt.value = String(s.index);
      opt.textContent = "Slot " + (s.index + 1) + (s.name ? ": " + s.name : " (empty)");
      sel.appendChild(opt);
    });
    sel.value = prev || String(VC.params.targetSlot);
  }

  /* ---------------- params / wiring ---------------- */

  async function refreshParams() {
    const p = await callNative("vcGetParams");
    if (!p) return;
    VC.params = p;
    document.getElementById("vcEnabled").checked = !!p.enabled;
    document.getElementById("vcSensitivity").value = p.sensitivity;
    document.getElementById("vcMidiChannel").value = p.midiChannel;
    fillTargetSlotOptions();
  }

  function wireControls() {
    document.getElementById("vcEnabled").addEventListener("change", (e) => {
      callNative("vcSetParam", "enabled", e.target.checked);
    });
    document.getElementById("vcSensitivity").addEventListener("input", (e) => {
      callNative("vcSetParam", "sensitivity", parseFloat(e.target.value));
    });
    document.getElementById("vcMidiChannel").addEventListener("change", (e) => {
      callNative("vcSetParam", "midiChannel", parseInt(e.target.value, 10) || 1);
    });
    document.getElementById("vcTargetSlot").addEventListener("change", (e) => {
      callNative("vcSetTargetSlot", parseInt(e.target.value, 10));
    });
  }

  /* ---------------- live readout ---------------- */

  function onLive(live) {
    VC.live = live;
    document.getElementById("vcNote").textContent = midiToName(live.note);
    document.getElementById("vcClarityFill").style.width = Math.round((live.clarity || 0) * 100) + "%";
    document.getElementById("vcRmsFill").style.width = Math.round(Math.min(1, (live.rms || 0) / 0.35) * 100) + "%";
  }

  /* ---------------- show / init ---------------- */

  VC.onShow = function () {
    refreshParams();
  };

  function init() {
    buildRoot();
    wireControls();
    refreshParams();
    onNative("voiceLive", onLive);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
