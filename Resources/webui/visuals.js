"use strict";

/* ============================================================
   Visuals - audio-reactive performance display. Talks to the C++
   engine (Source/VisualsEngine.*) through the same callNative/
   onNative bridge app.js sets up, same pattern as riffhouse.js.
   ============================================================ */

(function () {
  const SCENE_NAMES = ["Bars", "Radial", "Scope"]; // must match the AudioParameterChoice order in C++
  const PALETTES = [
    { name: "Mono", colors: ["#5ec8b8", "#7fe0d1"] },
    { name: "Neon", colors: ["#ff2fb0", "#2feeff"] },
    { name: "Sunset", colors: ["#ff7a45", "#ffd04d"] },
    { name: "Ice", colors: ["#8fd3ff", "#eaf7ff"] },
    { name: "Fire", colors: ["#ff3b1f", "#ffb020"] },
  ]; // must match the AudioParameterChoice order in C++

  const VZ = {
    live: null,
    params: { intensity: 0.8, palette: 0, scene: 0 },
    pane: "stage",
    lastBeatSerial: 0,
    beatFlash: 0,
    fullscreen: false,
    learnArmed: -1,
  };
  window.VZ = VZ;

  function lerpColor(a, b, t) {
    const pa = parseInt(a.slice(1), 16), pb = parseInt(b.slice(1), 16);
    const ar = (pa >> 16) & 255, ag = (pa >> 8) & 255, ab = pa & 255;
    const br = (pb >> 16) & 255, bg = (pb >> 8) & 255, bb = pb & 255;
    const r = Math.round(ar + (br - ar) * t), g = Math.round(ag + (bg - ag) * t), b2 = Math.round(ab + (bb - ab) * t);
    return "rgb(" + r + "," + g + "," + b2 + ")";
  }

  /* ---------------- root DOM ---------------- */

  function buildRoot() {
    const root = document.getElementById("vzRoot");
    root.innerHTML = `
      <nav class="vz-subnav">
        <button data-pane="stage" class="on">Stage</button>
        <button data-pane="midi">MIDI</button>
      </nav>

      <section class="vz-pane on" id="vzPaneStage">
        <div class="vz-controls">
          <label>Intensity <input type="range" id="vzIntensity" min="0" max="1" step="0.01" /></label>
          <label>Palette
            <select id="vzPalette"></select>
          </label>
          <span class="vz-spacer"></span>
          <div class="vz-scene-group" id="vzSceneGroup"></div>
          <button id="vzFullscreenBtn" class="btn btn-ghost small">Fullscreen</button>
        </div>
        <div class="vz-stage" id="vzStage">
          <canvas id="vzCanvas"></canvas>
          <button class="vz-exit-fullscreen" id="vzExitFullscreenBtn">Exit Fullscreen &#10005;</button>
        </div>
      </section>

      <section class="vz-pane" id="vzPaneMidi">
        <p class="rh-section-title">MIDI scene triggers - click Learn, then move a knob or hit a note/pad.</p>
        <div class="vz-learn-list" id="vzLearnList"></div>
      </section>
    `;
  }

  /* ---------------- sub nav ---------------- */

  function initSubnav() {
    const buttons = Array.from(document.querySelectorAll(".vz-subnav button"));
    const panes = { stage: document.getElementById("vzPaneStage"), midi: document.getElementById("vzPaneMidi") };
    buttons.forEach((b) => {
      b.addEventListener("click", () => {
        VZ.pane = b.dataset.pane;
        buttons.forEach((o) => o.classList.toggle("on", o === b));
        Object.entries(panes).forEach(([k, el]) => el.classList.toggle("on", k === VZ.pane));
      });
    });
  }

  /* ---------------- scenes ---------------- */

  function drawBars(ctx, w, h, live, palette, intensity) {
    const bands = (live && live.bands) || [];
    const n = bands.length || 1;
    const gap = 3;
    const barW = (w - gap * (n - 1)) / n;
    for (let i = 0; i < n; i++) {
      const v = clamp01(bands[i] * intensity);
      const barH = v * h * 0.95;
      const x = i * (barW + gap);
      const grad = ctx.createLinearGradient(0, h, 0, h - barH);
      grad.addColorStop(0, palette.colors[0]);
      grad.addColorStop(1, palette.colors[1]);
      ctx.fillStyle = grad;
      ctx.fillRect(x, h - barH, barW, barH);
    }
  }

  function drawRadial(ctx, w, h, live, palette, intensity) {
    const bands = (live && live.bands) || [];
    const cx = w / 2, cy = h / 2;
    const baseR = Math.min(w, h) * 0.18;
    const avg = bands.length ? bands.reduce((a, b) => a + b, 0) / bands.length : 0;

    ctx.save();
    ctx.translate(cx, cy);
    const flash = VZ.beatFlash;
    const r = baseR * (1 + avg * intensity * 1.2 + flash * 0.5);
    ctx.beginPath();
    ctx.arc(0, 0, r, 0, Math.PI * 2);
    ctx.strokeStyle = lerpColor(palette.colors[0], palette.colors[1], avg);
    ctx.lineWidth = 3 + flash * 10;
    ctx.globalAlpha = 0.6 + flash * 0.4;
    ctx.stroke();

    const n = bands.length;
    for (let i = 0; i < n; i++) {
      const a = (i / n) * Math.PI * 2;
      const v = clamp01(bands[i] * intensity);
      const r1 = baseR * 1.1, r2 = r1 + v * Math.min(w, h) * 0.3;
      ctx.beginPath();
      ctx.moveTo(Math.cos(a) * r1, Math.sin(a) * r1);
      ctx.lineTo(Math.cos(a) * r2, Math.sin(a) * r2);
      ctx.strokeStyle = palette.colors[i % 2];
      ctx.lineWidth = 2;
      ctx.globalAlpha = 0.85;
      ctx.stroke();
    }
    ctx.restore();
  }

  function drawScope(ctx, w, h, live, palette, intensity) {
    const scope = (live && live.scope) || [];
    const n = scope.length;
    if (!n) return;
    ctx.beginPath();
    for (let i = 0; i < n; i++) {
      const x = (i / (n - 1)) * w;
      const y = h / 2 - scope[i] * intensity * h * 0.45;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = palette.colors[0];
    ctx.lineWidth = 2;
    ctx.shadowColor = palette.colors[1];
    ctx.shadowBlur = 8;
    ctx.stroke();
  }

  const SCENES = [drawBars, drawRadial, drawScope];

  function clamp01(v) { return Math.max(0, Math.min(1, v || 0)); }

  /* ---------------- render loop ---------------- */

  function render() {
    requestAnimationFrame(render);
    const canvas = document.getElementById("vzCanvas");
    if (!canvas || !canvas.isConnected) return;
    const ctx = canvas.getContext("2d");
    const w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    if (VZ.beatFlash > 0) VZ.beatFlash = Math.max(0, VZ.beatFlash - 0.06);

    const sceneIndex = (VZ.live && typeof VZ.live.scene === "number") ? VZ.live.scene : VZ.params.scene;
    const palette = PALETTES[VZ.params.palette] || PALETTES[0];
    const draw = SCENES[sceneIndex] || SCENES[0];
    draw(ctx, w, h, VZ.live, palette, VZ.params.intensity);
  }

  function onLive(live) {
    VZ.live = live;
    if (live.beatSerial !== VZ.lastBeatSerial) {
      VZ.lastBeatSerial = live.beatSerial;
      VZ.beatFlash = 1.0;
    }
    VZ.learnArmed = typeof live.learnArmed === "number" ? live.learnArmed : -1;
    renderLearnList(live.bindings);
  }

  /* ---------------- macros ---------------- */

  async function refreshParams() {
    const p = await callNative("vzGetParams");
    if (!p) return;
    VZ.params.intensity = p.intensity;
    VZ.params.palette = p.palette;
    VZ.params.scene = p.scene;

    document.getElementById("vzIntensity").value = p.intensity;

    const select = document.getElementById("vzPalette");
    select.innerHTML = (p.paletteNames || PALETTES.map((x) => x.name))
      .map((name, i) => `<option value="${i}"${i === p.palette ? " selected" : ""}>${escapeHtml(name)}</option>`)
      .join("");

    const sceneGroup = document.getElementById("vzSceneGroup");
    const names = p.sceneNames || SCENE_NAMES;
    sceneGroup.innerHTML = names
      .map((name, i) => `<button class="vz-scene-btn${i === p.scene ? " on" : ""}" data-scene="${i}">${escapeHtml(name)}</button>`)
      .join("");
    Array.from(sceneGroup.querySelectorAll("button")).forEach((btn) => {
      btn.addEventListener("click", () => {
        callNative("vzSetParam", "scene", parseInt(btn.dataset.scene, 10));
      });
    });
  }

  function wireMacros() {
    document.getElementById("vzIntensity").addEventListener("input", (e) => {
      VZ.params.intensity = parseFloat(e.target.value);
      callNative("vzSetParam", "intensity", VZ.params.intensity);
    });
    document.getElementById("vzPalette").addEventListener("change", (e) => {
      VZ.params.palette = parseInt(e.target.value, 10);
      callNative("vzSetParam", "palette", VZ.params.palette);
    });
  }

  // Scene buttons' highlighted state follows the live param (so host
  // automation visibly changes which button looks selected), not a local click.
  function syncSceneButtons() {
    if (!VZ.live) return;
    const group = document.getElementById("vzSceneGroup");
    if (!group) return;
    Array.from(group.querySelectorAll("button")).forEach((btn) => {
      btn.classList.toggle("on", parseInt(btn.dataset.scene, 10) === VZ.live.scene);
    });
  }
  setInterval(syncSceneButtons, 200);

  /* ---------------- MIDI learn ---------------- */

  function renderLearnList(bindings) {
    if (VZ.pane !== "midi") return;
    const list = document.getElementById("vzLearnList");
    if (!list) return;
    const cc = (bindings && bindings.cc) || [];
    const note = (bindings && bindings.note) || [];

    list.innerHTML = SCENE_NAMES.map((name, i) => {
      const ccHit = cc.find((p) => p[1] === i);
      const noteHit = note.find((p) => p[1] === i);
      const label = ccHit ? "CC " + ccHit[0] : noteHit ? "Note " + noteHit[0] : "Unbound";
      const armed = VZ.learnArmed === i;
      return `<div class="vz-learn-row${armed ? " armed" : ""}">
        <span class="name">${escapeHtml(name)}</span>
        <span class="binding">${armed ? "Listening..." : escapeHtml(label)}</span>
        <button class="btn btn-ghost small" data-learn="${i}">${armed ? "Cancel" : "Learn"}</button>
        <button class="btn btn-ghost small" data-clear="${i}">Clear</button>
      </div>`;
    }).join("");

    Array.from(list.querySelectorAll("[data-learn]")).forEach((btn) => {
      btn.addEventListener("click", () => {
        const i = parseInt(btn.dataset.learn, 10);
        callNative("vzMidiLearn", VZ.learnArmed === i ? -1 : i);
      });
    });
    Array.from(list.querySelectorAll("[data-clear]")).forEach((btn) => {
      btn.addEventListener("click", () => callNative("vzClearBinding", parseInt(btn.dataset.clear, 10)));
    });
  }

  /* ---------------- fullscreen ---------------- */

  function wireFullscreen() {
    document.getElementById("vzFullscreenBtn").addEventListener("click", () => {
      VZ.fullscreen = true;
      document.body.classList.add("vz-fullscreen");
      requestAnimationFrame(VZ.resize);
      callNative("vzEnterFullscreen");
    });
    document.getElementById("vzExitFullscreenBtn").addEventListener("click", () => {
      VZ.fullscreen = false;
      document.body.classList.remove("vz-fullscreen");
      requestAnimationFrame(VZ.resize);
      callNative("vzExitFullscreen");
    });
  }

  /* ---------------- canvas sizing ---------------- */

  VZ.resize = function () {
    const canvas = document.getElementById("vzCanvas");
    if (!canvas || !canvas.isConnected) return;
    const rect = canvas.parentElement.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.max(1, Math.round(rect.width * dpr));
    canvas.height = Math.max(1, Math.round(rect.height * dpr));
  };
  window.addEventListener("resize", () => requestAnimationFrame(VZ.resize));

  /* ---------------- boot ---------------- */

  function init() {
    buildRoot();
    initSubnav();
    wireMacros();
    wireFullscreen();
    refreshParams();
    onNative("vzLive", onLive);
    requestAnimationFrame(VZ.resize);
    requestAnimationFrame(render);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
