"use strict";

/* ============================================================
   Riff House - practice/play-along game view.
   Talks to the C++ engine (Source/RiffHouseEngine.*) through the
   same callNative/onNative bridge app.js sets up. No separate
   pitch/MIDI detection here - all of that runs on the audio
   thread in C++; this file only renders the "rhLive" stream and
   turns native function calls into UI actions.
   ============================================================ */

(function () {
  const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

  function midiToName(m) {
    if (m == null || !isFinite(m)) return "--";
    const n = Math.round(m);
    return NOTE_NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
  }
  function hzToMidi(hz) { return 69 + 12 * Math.log2(hz / 440); }
  function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
  function fmtTime(s) {
    if (!isFinite(s) || s < 0) s = 0;
    const m = Math.floor(s / 60), sec = Math.floor(s % 60);
    return m + ":" + String(sec).padStart(2, "0");
  }

  /* ---------------- shared state ---------------- */

  const RH = {
    live: null,
    ccSeen: new Map(),
    chart: null,
    charts: [],
    pane: "play",
    waveform: null,
    settings: {
      latencyMs: 0,
      rate: 1,
      waitMode: false,
      memoryMode: false,
      dynamicDifficulty: false,
      hearPart: true,
    },
    loop: { a: null, b: null, active: false },
    session: null,
    waitingForNote: null,
    ninja: null,
  };

  function newSession() {
    return {
      score: 0, combo: 0, maxCombo: 0, multiplier: 1,
      rock: 50, starPower: 0, starActive: false, starUntil: 0,
      hits: 0, misses: 0, perfects: 0, judged: new Set(),
      passCount: 0, density: 1,
    };
  }

  /* ---------------- view switch (top nav) ---------------- */

  function initViewSwitch() {
    const navRack = document.getElementById("navRack");
    const navRiff = document.getElementById("navRiffHouse");
    const viewRack = document.getElementById("view-rack");
    const viewRiff = document.getElementById("view-riffhouse");
    function show(which) {
      const rack = which === "rack";
      viewRack.classList.toggle("on", rack);
      viewRiff.classList.toggle("on", !rack);
      navRack.classList.toggle("on", rack);
      navRiff.classList.toggle("on", !rack);
      if (!rack) requestAnimationFrame(resizeCanvases);
    }
    navRack.addEventListener("click", () => show("rack"));
    navRiff.addEventListener("click", () => show("riffhouse"));
  }

  /* ---------------- root DOM ---------------- */

  function buildRoot() {
    const root = document.getElementById("rhRoot");
    root.innerHTML = `
      <div class="rh-live-strip">
        <span id="rhLiveNote" class="rh-live-note quiet">--</span>
        <div class="rh-live-meter"><i id="rhLiveMeter"></i></div>
        <div id="rhLiveHeld" class="rh-live-held"></div>
        <span class="rh-spacer"></span>
        <label class="rh-mini-toggle"><input type="checkbox" id="rhMonitorExt" /> Monitor externally</label>
        <button id="rhSaveBtn" class="btn btn-ghost small rh-save-btn">Save that (last <span id="rhSaveSecLabel">12</span>s)</button>
      </div>

      <nav class="rh-subnav">
        <button data-pane="play" class="on">Play</button>
        <button data-pane="songs">Songs</button>
        <button data-pane="tuner">Tuner</button>
        <button data-pane="arcade">Note Ninja</button>
        <button data-pane="capture">Capture</button>
      </nav>

      <section class="rh-pane on" id="rhPanePlay">
        <div class="rh-controls">
          <strong id="rhChartTitle">No chart loaded</strong>
          <span class="rh-spacer"></span>
          <button id="rhPlayBtn" class="btn btn-ghost small">Play</button>
          <button id="rhSetA" class="btn btn-ghost small">Set A</button>
          <button id="rhSetB" class="btn btn-ghost small">Set B</button>
          <button id="rhLoopToggle" class="btn btn-ghost small">Loop: off</button>
          <label>Rate <input type="range" id="rhRate" min="0.4" max="1.1" step="0.05" value="1" /><span id="rhRateLabel">1.00x</span></label>
          <label>Latency <input type="range" id="rhLatency" min="-120" max="120" step="5" value="0" /><span id="rhLatencyLabel">0ms</span></label>
          <label><input type="checkbox" id="rhWaitMode" /> Wait mode</label>
          <label><input type="checkbox" id="rhMemoryMode" /> Memory mode</label>
          <label><input type="checkbox" id="rhDynDiff" /> Dynamic difficulty</label>
        </div>
        <div class="rh-hud">
          <div class="rh-hud-stat"><span class="label">Score</span><span id="rhScore" class="value">0</span></div>
          <div class="rh-hud-stat"><span class="label">Combo</span><span id="rhCombo" class="value mult-1">0x1</span></div>
          <div class="rh-meter-row"><span class="label">Rock Meter</span><div class="rh-meter-track"><i id="rhRockFill" class="rh-rock-fill" style="width:50%"></i></div></div>
          <div id="rhSpWrap" class="rh-meter-row"><span class="label">Star Power (sustain pedal)</span><div class="rh-meter-track"><i id="rhSpFill" class="rh-sp-fill" style="width:0%"></i></div></div>
        </div>
        <div class="rh-stage">
          <canvas id="rhCanvas"></canvas>
          <div id="rhBurst" class="rh-burst"></div>
        </div>
      </section>

      <section class="rh-pane" id="rhPaneSongs">
        <div class="rh-controls">
          <button id="rhImportFile" class="btn btn-ghost small">Import audio/MIDI file...</button>
          <button id="rhImportFolder" class="btn btn-ghost small">Import song folder...</button>
          <button id="rhRevealInbox" class="btn btn-ghost small">Reveal Inbox</button>
          <button id="rhRefreshCharts" class="btn btn-ghost small">Refresh</button>
        </div>
        <p class="rh-section-title">Charts</p>
        <div id="rhChartList" class="rh-chart-list"><div class="rh-empty">No charts yet. Import a song or capture an idea to get started.</div></div>
      </section>

      <section class="rh-pane" id="rhPaneTuner">
        <div class="rh-tuner">
          <div id="rhTunerNote" class="rh-tuner-note">--</div>
          <div id="rhTunerHz" class="rh-tuner-hz">-- Hz</div>
          <div class="rh-tuner-needle-track"><div id="rhTunerNeedle" class="rh-tuner-needle"></div></div>
        </div>
      </section>

      <section class="rh-pane" id="rhPaneArcade">
        <div class="rh-arcade">
          <div id="rhNinjaTarget" class="rh-arcade-target">--</div>
          <div class="rh-arcade-stats">
            <span>Streak <b id="rhNinjaStreak">0</b></span>
            <span>Best <b id="rhNinjaBest">0</b></span>
            <span>Hits <b id="rhNinjaHits">0</b></span>
          </div>
          <button id="rhNinjaStart" class="btn btn-record"><span id="rhNinjaStartLabel">Start</span></button>
        </div>
      </section>

      <section class="rh-pane" id="rhPaneCapture">
        <div class="rh-controls">
          <label>Seconds <input type="number" id="rhCapSeconds" min="2" max="60" value="12" style="width:60px" /></label>
          <label>BPM <input type="number" id="rhCapBpm" min="40" max="240" value="120" style="width:60px" /></label>
          <button id="rhCapSave" class="btn btn-ghost small">Save that</button>
          <button id="rhCapWave" class="btn btn-ghost small">Refresh waveform</button>
          <span class="rh-spacer"></span>
          <button id="rhExportLoopBtn" class="btn btn-ghost small">Export loop (bars)</button>
          <button id="rhExportSliceBtn" class="btn btn-ghost small">Export slices</button>
        </div>
        <div class="rh-wave-wrap"><canvas id="rhWave"></canvas></div>
      </section>
    `;
  }

  /* ---------------- sub nav ---------------- */

  function initSubnav() {
    const buttons = Array.from(document.querySelectorAll(".rh-subnav button"));
    const panes = {
      play: document.getElementById("rhPanePlay"),
      songs: document.getElementById("rhPaneSongs"),
      tuner: document.getElementById("rhPaneTuner"),
      arcade: document.getElementById("rhPaneArcade"),
      capture: document.getElementById("rhPaneCapture"),
    };
    buttons.forEach((b) => {
      b.addEventListener("click", () => {
        RH.pane = b.dataset.pane;
        buttons.forEach((o) => o.classList.toggle("on", o === b));
        Object.entries(panes).forEach(([k, el]) => el.classList.toggle("on", k === RH.pane));
        if (RH.pane === "play" || RH.pane === "capture") requestAnimationFrame(resizeCanvases);
        if (RH.pane === "songs") refreshCharts();
      });
    });
  }

  /* ---------------- live state ---------------- */

  function onLive(live) {
    RH.live = live;
    renderLiveStrip(live);
    if (RH.pane === "tuner") renderTuner(live);
    if (RH.pane === "arcade") judgeNinja(live);
    handleStarPowerPedal(live);
  }

  function renderLiveStrip(live) {
    const noteEl = document.getElementById("rhLiveNote");
    const meter = document.getElementById("rhLiveMeter");
    const held = document.getElementById("rhLiveHeld");
    if (live.hz && live.clarity > 0.5) {
      noteEl.textContent = midiToName(hzToMidi(live.hz));
      noteEl.classList.remove("quiet");
    } else {
      noteEl.textContent = "--";
      noteEl.classList.add("quiet");
    }
    meter.style.width = clamp((live.rms || 0) * 400, 0, 100) + "%";
    held.innerHTML = (live.held || []).map((m) => `<span>${midiToName(m)}</span>`).join(" ");
  }

  function renderTuner(live) {
    const noteEl = document.getElementById("rhTunerNote");
    const hzEl = document.getElementById("rhTunerHz");
    const needle = document.getElementById("rhTunerNeedle");
    if (!live.hz || live.clarity < 0.5) {
      noteEl.textContent = "--";
      hzEl.textContent = "-- Hz";
      needle.style.left = "50%";
      needle.classList.remove("in-tune");
      return;
    }
    const exact = hzToMidi(live.hz);
    const nearest = Math.round(exact);
    const cents = (exact - nearest) * 100;
    noteEl.textContent = midiToName(nearest);
    hzEl.textContent = live.hz.toFixed(1) + " Hz, " + (cents >= 0 ? "+" : "") + cents.toFixed(0) + " cents";
    const pct = clamp(50 + cents / 50 * 50, 2, 98);
    needle.style.left = pct + "%";
    needle.classList.toggle("in-tune", Math.abs(cents) < 6);
  }

  /* CC edge-triggering: rhLive.cc is [[num,val,serial],...]; a serial only
     changes when that controller actually moves, so compare serials rather
     than values to avoid re-triggering on every 30 Hz tick. */
  function handleStarPowerPedal(live) {
    for (const [num, val, serial] of live.cc || []) {
      if (num !== 64) continue;
      const prevSerial = RH.ccSeen.get(num);
      RH.ccSeen.set(num, serial);
      if (prevSerial === undefined || serial === prevSerial) continue;
      if (val > 0.5 && RH.session && RH.session.starPower >= 100 && !RH.session.starActive) {
        RH.session.starActive = true;
        RH.session.starUntil = performance.now() + 12000;
        document.getElementById("rhSpWrap").classList.add("rh-sp-ready");
        burst("STAR POWER!");
      }
    }
  }

  /* ---------------- canvases ---------------- */

  function resizeCanvases() {
    for (const id of ["rhCanvas", "rhWave"]) {
      const cv = document.getElementById(id);
      if (!cv || !cv.isConnected) continue;
      const rect = cv.parentElement.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      cv.width = Math.max(1, Math.round(rect.width * dpr));
      cv.height = Math.max(1, Math.round(rect.height * dpr));
    }
  }
  window.addEventListener("resize", () => requestAnimationFrame(resizeCanvases));

  /* ---------------- note highway game ---------------- */

  const LOOKAHEAD_SEC = 3.0;
  const HIT_PERFECT = 0.06, HIT_GOOD = 0.15, MISS_WINDOW = 0.22;

  function chartNoteRange() {
    if (!RH.chart || !RH.chart.notes.length) return [48, 72];
    let lo = Infinity, hi = -Infinity;
    for (const [, , m] of RH.chart.notes) { lo = Math.min(lo, m); hi = Math.max(hi, m); }
    return [lo - 2, hi + 2];
  }

  function activeNotesForDensity() {
    const notes = RH.chart ? RH.chart.notes : [];
    if (!RH.settings.dynamicDifficulty || RH.session.density >= 1) return notes;
    const keep = Math.max(1, Math.round(1 / RH.session.density));
    return notes.filter((_, i) => i % keep === 0);
  }

  function gameTick() {
    requestAnimationFrame(gameTick);
    const cv = document.getElementById("rhCanvas");
    if (!cv || !RH.live || RH.pane !== "play") return;
    const ctx = cv.getContext("2d");
    const w = cv.width, h = cv.height;
    ctx.clearRect(0, 0, w, h);

    const pos = (RH.live.pos || 0) + RH.settings.latencyMs / 1000;
    const hitY = h * 0.84;

    ctx.strokeStyle = "rgba(255,255,255,0.18)";
    ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(0, hitY); ctx.lineTo(w, hitY); ctx.stroke();

    if (!RH.chart) {
      ctx.fillStyle = "rgba(255,255,255,0.35)";
      ctx.font = "14px sans-serif";
      ctx.fillText("Load a chart from the Songs tab to play along.", 16, h / 2);
      drawLiveMarker(ctx, w, hitY);
      return;
    }

    const [lo, hi] = chartNoteRange();
    const span = Math.max(1, hi - lo);
    const notes = activeNotesForDensity();
    const memoryCutoff = RH.settings.memoryMode ? 0.6 : LOOKAHEAD_SEC;

    drawPitchAxis(ctx, w, hitY, lo, hi, span);

    let nextUp = null; // the soonest un-judged note - drives the big "Play this" readout
    for (let i = 0; i < notes.length; i++) {
      const [t, dur, midi] = notes[i];
      const dt = t - pos;
      if (dt < -MISS_WINDOW || dt > LOOKAHEAD_SEC) {
        if (dt < -MISS_WINDOW && !RH.session.judged.has(i)) judgeMiss(i);
        continue;
      }
      if (dt > memoryCutoff) continue;
      const judged = RH.session.judged.has(i);
      if (!judged && (nextUp === null || dt < nextUp.dt)) nextUp = { dt, midi };

      const y = hitY - dt / LOOKAHEAD_SEC * hitY;
      const x = ((midi - lo) / span) * (w * 0.9) + w * 0.05;
      const noteH = Math.max(18, (dur / LOOKAHEAD_SEC) * hitY);
      const blockW = Math.min(34, w * 0.9 / Math.max(1, span) + 12);
      ctx.fillStyle = judged ? "rgba(94,200,184,0.25)" : "rgba(94,200,184,0.85)";
      ctx.fillRect(x - blockW / 2, y - noteH, blockW, noteH);

      if (!judged) {
        ctx.fillStyle = "#07201c";
        ctx.font = "700 12px sans-serif";
        ctx.textAlign = "center";
        ctx.fillText(midiToName(midi), x, y - noteH / 2 + 4);
      }

      if (RH.settings.waitMode && !judged && Math.abs(dt) < 0.05) {
        maybeWaitForNote(midi);
      }
      if (!judged && Math.abs(dt) <= HIT_GOOD) {
        if (noteMatches(midi)) judgeHit(i, Math.abs(dt));
      }
    }

    drawNextNoteReadout(ctx, w, nextUp);
    drawLiveMarker(ctx, w, hitY);
    updateHud();

    if (RH.loop.active && RH.loop.b != null && pos >= RH.loop.b) {
      callNative("rhTransport", true, RH.loop.a || 0, 1.0);
    }
  }

  function drawPitchAxis(ctx, w, hitY, lo, hi, span) {
    ctx.fillStyle = "rgba(255,255,255,0.3)";
    ctx.font = "10px sans-serif";
    ctx.textAlign = "center";
    const steps = Math.min(span, 10);
    for (let s = 0; s <= steps; s++) {
      const midi = Math.round(lo + (span * s) / steps);
      const x = ((midi - lo) / span) * (w * 0.9) + w * 0.05;
      ctx.fillText(midiToName(midi), x, hitY + 16);
    }
  }

  function drawNextNoteReadout(ctx, w, nextUp) {
    ctx.textAlign = "center";
    if (!nextUp) {
      ctx.fillStyle = "rgba(255,255,255,0.4)";
      ctx.font = "600 16px sans-serif";
      ctx.fillText("...", w / 2, 36);
      return;
    }
    const urgent = nextUp.dt <= HIT_GOOD;
    ctx.fillStyle = urgent ? "#5ec8b8" : "rgba(255,255,255,0.75)";
    ctx.font = urgent ? "800 30px sans-serif" : "700 20px sans-serif";
    ctx.fillText((urgent ? "Play: " : "Next: ") + midiToName(nextUp.midi), w / 2, 40);
  }

  function drawLiveMarker(ctx, w, hitY) {
    if (!RH.live.hz || RH.live.clarity < 0.5) return;
    const [lo, hi] = chartNoteRange();
    const span = Math.max(1, hi - lo);
    const m = hzToMidi(RH.live.hz);
    const x = clamp(((m - lo) / span) * (ctx.canvas.width * 0.9) + ctx.canvas.width * 0.05, 0, ctx.canvas.width);
    ctx.fillStyle = "#e0a93a";
    ctx.beginPath(); ctx.arc(x, hitY, 6, 0, Math.PI * 2); ctx.fill();
  }

  function noteMatches(midi) {
    const live = RH.live;
    if (!live) return false;
    if (live.held && live.held.includes(midi)) return true;
    if (live.hz && live.clarity > 0.55) {
      const m = hzToMidi(live.hz);
      return Math.abs(m - midi) < 0.6;
    }
    return false;
  }

  function maybeWaitForNote(midi) {
    if (RH.waitingForNote != null) return;
    RH.waitingForNote = midi;
    callNative("rhTransport", false, RH.live.pos || 0, 1.0);
  }

  function resumeIfWaiting() {
    if (RH.waitingForNote == null) return;
    if (noteMatches(RH.waitingForNote)) {
      RH.waitingForNote = null;
      callNative("rhTransport", true, RH.live.pos || 0, 1.0);
    }
  }

  function judgeHit(i, err) {
    RH.session.judged.add(i);
    const perfect = err <= HIT_PERFECT;
    RH.session.hits++;
    if (perfect) RH.session.perfects++;
    RH.session.combo++;
    RH.session.maxCombo = Math.max(RH.session.maxCombo, RH.session.combo);
    RH.session.multiplier = RH.session.combo >= 30 ? 4 : RH.session.combo >= 20 ? 3 : RH.session.combo >= 10 ? 2 : 1;
    const starMul = RH.session.starActive ? 2 : 1;
    RH.session.score += (perfect ? 100 : 60) * RH.session.multiplier * starMul;
    RH.session.rock = clamp(RH.session.rock + (perfect ? 3 : 2), 0, 100);
    RH.session.starPower = clamp(RH.session.starPower + (perfect ? 2.2 : 1.4), 0, 100);
    burst(perfect ? "PERFECT" : "HIT");
    resumeIfWaiting();
  }

  function judgeMiss(i) {
    RH.session.judged.add(i);
    RH.session.misses++;
    RH.session.combo = 0;
    RH.session.multiplier = 1;
    RH.session.rock = clamp(RH.session.rock - 6, 0, 100);
  }

  function burst(text) {
    const el = document.getElementById("rhBurst");
    el.textContent = text;
    el.classList.remove("show");
    void el.offsetWidth;
    el.classList.add("show");
  }

  function updateHud() {
    document.getElementById("rhScore").textContent = RH.session.score;
    const comboEl = document.getElementById("rhCombo");
    comboEl.textContent = RH.session.combo + "x" + RH.session.multiplier;
    comboEl.className = "value mult-" + RH.session.multiplier;
    document.getElementById("rhRockFill").style.width = RH.session.rock + "%";
    document.getElementById("rhSpFill").style.width = RH.session.starPower + "%";
    document.getElementById("rhSpWrap").classList.toggle("rh-sp-ready", RH.session.starPower >= 100 && !RH.session.starActive);
    if (RH.session.starActive && performance.now() > RH.session.starUntil) {
      RH.session.starActive = false;
      RH.session.starPower = 0;
      document.getElementById("rhSpWrap").classList.remove("rh-sp-ready");
    }
  }

  function finishedLoopPass() {
    if (!RH.settings.dynamicDifficulty || !RH.chart) return;
    const total = RH.chart.notes.length || 1;
    const accuracy = RH.session.hits / total;
    RH.session.passCount++;
    if (accuracy > 0.85) RH.session.density = clamp(RH.session.density + 0.15, 0.25, 1);
    else if (accuracy < 0.5) RH.session.density = clamp(RH.session.density - 0.15, 0.25, 1);
    RH.session.judged.clear();
    RH.session.hits = 0;
    RH.session.misses = 0;
  }

  /* ---------------- transport / play controls ---------------- */

  function initPlayControls() {
    const playBtn = document.getElementById("rhPlayBtn");
    let playing = false;
    playBtn.addEventListener("click", () => {
      playing = !playing;
      playBtn.textContent = playing ? "Pause" : "Play";
      callNative("rhTransport", playing, RH.live ? RH.live.pos : 0, 1.0);
    });

    document.getElementById("rhSetA").addEventListener("click", () => {
      RH.loop.a = RH.live ? RH.live.pos : 0;
      showToast("Loop start set at " + fmtTime(RH.loop.a), "success");
    });
    document.getElementById("rhSetB").addEventListener("click", () => {
      RH.loop.b = RH.live ? RH.live.pos : 0;
      showToast("Loop end set at " + fmtTime(RH.loop.b), "success");
      finishedLoopPass();
    });
    const loopBtn = document.getElementById("rhLoopToggle");
    loopBtn.addEventListener("click", () => {
      RH.loop.active = !RH.loop.active;
      loopBtn.textContent = "Loop: " + (RH.loop.active ? "on" : "off");
    });

    const rate = document.getElementById("rhRate");
    const rateLabel = document.getElementById("rhRateLabel");
    rate.addEventListener("input", () => {
      RH.settings.rate = parseFloat(rate.value);
      rateLabel.textContent = RH.settings.rate.toFixed(2) + "x";
      callNative("rhSetRate", RH.settings.rate);
    });

    const latency = document.getElementById("rhLatency");
    const latencyLabel = document.getElementById("rhLatencyLabel");
    latency.addEventListener("input", () => {
      RH.settings.latencyMs = parseFloat(latency.value);
      latencyLabel.textContent = RH.settings.latencyMs + "ms";
    });

    document.getElementById("rhWaitMode").addEventListener("change", (e) => {
      RH.settings.waitMode = e.target.checked;
      RH.waitingForNote = null;
    });
    document.getElementById("rhMemoryMode").addEventListener("change", (e) => {
      RH.settings.memoryMode = e.target.checked;
    });
    document.getElementById("rhDynDiff").addEventListener("change", (e) => {
      RH.settings.dynamicDifficulty = e.target.checked;
    });

    document.getElementById("rhMonitorExt").addEventListener("change", (e) => {
      callNative("rhMonitorExternally", e.target.checked);
    });

    document.getElementById("rhSaveBtn").addEventListener("click", () => quickSaveThat());
  }

  function quickSaveThat() {
    const btn = document.getElementById("rhSaveBtn");
    btn.classList.add("busy");
    callNative("rhSaveThat", 12, 120).then((res) => {
      btn.classList.remove("busy");
      if (res && res.ok) showToast("Saved idea to Inbox", "success");
      else showToast("Could not save - try again", "error");
    });
  }

  /* ---------------- songs / charts ---------------- */

  function refreshCharts() {
    const list = document.getElementById("rhChartList");
    callNative("rhListCharts").then((charts) => {
      RH.charts = charts || [];
      if (!RH.charts.length) {
        list.innerHTML = '<div class="rh-empty">No charts yet. Import a song or capture an idea to get started.</div>';
        return;
      }
      list.innerHTML = "";
      for (const c of RH.charts) {
        const card = document.createElement("div");
        card.className = "rh-chart-card";
        card.innerHTML = `<span class="title">${escapeHtml(c.title || "Untitled")}</span>
          <span class="meta">${escapeHtml(c.instrument || "")} · ${c.bpm ? c.bpm + " bpm" : ""}</span>`;
        card.addEventListener("click", () => loadChart(c.id));
        list.appendChild(card);
      }
    });
  }

  function loadChart(id) {
    document.getElementById("rhChartTitle").textContent = "Loading...";
    callNative("rhLoadChart", id, RH.settings.hearPart, RH.settings.rate).then((chart) => {
      if (!chart) { showToast("Could not load chart", "error"); return; }
      RH.chart = chart;
      RH.session = newSession();
      document.getElementById("rhChartTitle").textContent = chart.title + " (" + (chart.instrument || "") + ")";
      document.querySelectorAll(".rh-chart-card").forEach((el) => el.classList.remove("selected"));
      showToast("Loaded " + chart.title, "success");
      selectSubnav("play");
    });
  }

  function selectSubnav(name) {
    const btn = document.querySelector(`.rh-subnav button[data-pane="${name}"]`);
    if (btn) btn.click();
  }

  function initSongsControls() {
    document.getElementById("rhImportFile").addEventListener("click", () => {
      promptModal("Instrument for this import", "e.g. guitar", (instrument) => {
        callNative("rhImport", "file", instrument || "guitar").then((res) => {
          if (res && res.ok) { showToast("Imported - analyzing...", "success"); refreshCharts(); }
          else showToast("Import cancelled or failed", "error");
        });
      });
    });
    document.getElementById("rhImportFolder").addEventListener("click", () => {
      promptModal("Instrument for this import", "e.g. guitar", (instrument) => {
        callNative("rhImport", "folder", instrument || "guitar").then((res) => {
          if (res && res.ok) { showToast("Imported song folder - analyzing...", "success"); refreshCharts(); }
          else showToast("Import cancelled or failed", "error");
        });
      });
    });
    document.getElementById("rhRevealInbox").addEventListener("click", () => callNative("rhRevealInbox"));
    document.getElementById("rhRefreshCharts").addEventListener("click", refreshCharts);
  }

  /* ---------------- Note Ninja arcade ---------------- */

  function initArcade() {
    const startBtn = document.getElementById("rhNinjaStart");
    startBtn.addEventListener("click", () => {
      if (RH.ninja) stopNinja(); else startNinja();
    });
  }

  function startNinja() {
    const [lo, hi] = RH.chart ? chartNoteRange() : [52, 76];
    RH.ninja = { lo, hi, streak: 0, best: 0, hits: 0, target: null, lastHitAt: 0 };
    document.getElementById("rhNinjaStartLabel").textContent = "Stop";
    nextNinjaTarget();
  }
  function stopNinja() {
    RH.ninja = null;
    document.getElementById("rhNinjaStartLabel").textContent = "Start";
    document.getElementById("rhNinjaTarget").textContent = "--";
  }
  function nextNinjaTarget() {
    if (!RH.ninja) return;
    const { lo, hi } = RH.ninja;
    RH.ninja.target = lo + Math.floor(Math.random() * (hi - lo + 1));
    document.getElementById("rhNinjaTarget").textContent = midiToName(RH.ninja.target);
  }
  function judgeNinja(live) {
    if (!RH.ninja || RH.ninja.target == null) return;
    const now = performance.now();
    if (now - RH.ninja.lastHitAt < 400) return;
    if (noteMatches(RH.ninja.target)) {
      RH.ninja.lastHitAt = now;
      RH.ninja.streak++;
      RH.ninja.hits++;
      RH.ninja.best = Math.max(RH.ninja.best, RH.ninja.streak);
      document.getElementById("rhNinjaStreak").textContent = RH.ninja.streak;
      document.getElementById("rhNinjaBest").textContent = RH.ninja.best;
      document.getElementById("rhNinjaHits").textContent = RH.ninja.hits;
      nextNinjaTarget();
    }
  }

  /* ---------------- capture pane ---------------- */

  function initCapture() {
    document.getElementById("rhCapSave").addEventListener("click", () => {
      const sec = parseFloat(document.getElementById("rhCapSeconds").value) || 12;
      const bpm = parseFloat(document.getElementById("rhCapBpm").value) || 120;
      callNative("rhSaveThat", sec, bpm).then((res) => {
        if (res && res.ok) { showToast("Saved to Inbox", "success"); refreshWave(); }
        else showToast("Save failed", "error");
      });
    });
    document.getElementById("rhCapWave").addEventListener("click", refreshWave);
    document.getElementById("rhExportLoopBtn").addEventListener("click", () => {
      if (RH.loop.a == null || RH.loop.b == null) { showToast("Set loop A and B on the Play tab first", "error"); return; }
      const bpm = parseFloat(document.getElementById("rhCapBpm").value) || 120;
      callNative("rhExportLoop", RH.loop.a, RH.loop.b, bpm).then((res) => {
        showToast(res && res.ok ? "Loop exported" : "Export failed", res && res.ok ? "success" : "error");
      });
    });
    document.getElementById("rhExportSliceBtn").addEventListener("click", () => {
      if (RH.loop.a == null || RH.loop.b == null) { showToast("Set loop A and B on the Play tab first", "error"); return; }
      const bpm = parseFloat(document.getElementById("rhCapBpm").value) || 120;
      callNative("rhExportSlices", RH.loop.a, RH.loop.b, 0.5, bpm).then((res) => {
        showToast(res && res.ok ? "Slices exported" : "Export failed", res && res.ok ? "success" : "error");
      });
    });
  }

  function refreshWave() {
    callNative("rhCaptureWave", 400).then((peaks) => {
      RH.waveform = peaks;
      drawWave();
    });
  }

  function drawWave() {
    const cv = document.getElementById("rhWave");
    if (!cv) return;
    const ctx = cv.getContext("2d");
    const w = cv.width, h = cv.height;
    ctx.clearRect(0, 0, w, h);
    if (!RH.waveform || !RH.waveform.length) return;
    const mid = h / 2;
    ctx.fillStyle = "rgba(94,200,184,0.7)";
    const n = RH.waveform.length;
    for (let i = 0; i < n; i++) {
      const v = clamp(RH.waveform[i], 0, 1);
      const x = (i / n) * w;
      const barW = Math.max(1, w / n);
      ctx.fillRect(x, mid - v * mid, barW, v * mid * 2);
    }
  }

  /* ---------------- boot ---------------- */

  function init() {
    initViewSwitch();
    buildRoot();
    initSubnav();
    initPlayControls();
    initSongsControls();
    initArcade();
    initCapture();
    RH.session = newSession();
    resizeCanvases();
    onNative("rhLive", onLive);
    requestAnimationFrame(gameTick);
    refreshCharts();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
