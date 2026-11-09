"use strict";

/* ============================================================
   Studio - the launchpad. One screen to: open a DAW template and get to
   work, bring a Suno export in, and see the ONE next step for a song.

   Files live in ~/Music/GHS/Studio (see Source/StudioEngine.h). The
   "next step" rules are in studio-lattice.js and are plain data: no AI,
   no network. This file only draws things and calls the native bridge
   (callNative / showToast come from app.js).

   Calm by design: nothing pops up, nothing makes a sound, nothing resets.
   The assistant is quiet until you look, and you choose how chatty it is.
   ============================================================ */

(function () {
  const L = window.StudioLattice;
  const SUNO_URL = "https://suno.com/";
  const MODES = [
    { id: "quiet", label: "Quiet", hint: "Just the next step." },
    { id: "nudge", label: "Nudge", hint: "Next step, and a gentle hello after time away." },
    { id: "coach", label: "Coach", hint: "Adds why it matters and a rough time box." },
  ];
  const DAWS = {
    logicx: "Logic Pro", als: "Ableton Live", luna: "Luna", rpp: "Reaper", ptx: "Pro Tools",
    cpr: "Cubase", flp: "FL Studio", song: "Studio One", bwproject: "Bitwig", band: "GarageBand",
  };

  const ST = {
    cfg: { templates: [], settings: {} },
    songs: [],
    selectedId: null,
    stuckText: "",
    prevSeen: null,
    dropPath: "",
    waiting: [],
    busy: false,
  };

  /* ---------------- tiny DOM helper (no innerHTML with user text) ---------------- */

  function h(tag, attrs, ...kids) {
    const e = document.createElement(tag);
    if (attrs) for (const k of Object.keys(attrs)) {
      const v = attrs[k];
      if (v === false || v == null) continue;
      if (k === "class") e.className = v;
      else if (k.startsWith("on") && typeof v === "function") e.addEventListener(k.slice(2), v);
      else if (k === "value") e.value = v;
      else e.setAttribute(k, v === true ? "" : v);
    }
    kids.flat().forEach((c) => { if (c != null && c !== false) e.append(c.nodeType ? c : document.createTextNode(String(c))); });
    return e;
  }

  const native = (...a) => (typeof callNative === "function" ? callNative(...a) : Promise.resolve(null));
  const toast = (t, tone) => { if (typeof showToast === "function") showToast(t, tone); };

  function dawFor(path) {
    const ext = String(path || "").split(".").pop().toLowerCase();
    return DAWS[ext] || "DAW project";
  }
  function baseName(path) {
    const leaf = String(path || "").replace(/[\\/]+$/, "").split(/[\\/]/).pop() || "";
    return leaf.replace(/\.[^.]+$/, "");
  }
  const nowIso = () => new Date().toISOString();
  const mode = () => (ST.cfg.settings && ST.cfg.settings.assistMode) || "nudge";
  const selectedSong = () => L.pickSong(ST.songs, ST.selectedId);

  /* ---------------- persistence ---------------- */

  async function saveCfg() {
    const { templates, settings } = ST.cfg;
    const r = await native("stSaveConfig", JSON.stringify({ templates, settings }));
    if (r && r.ok === false) toast(r.error || "Could not save Studio settings.", "error");
  }

  async function loadAll() {
    const cfg = await native("stLoadConfig");
    if (cfg && typeof cfg === "object") {
      ST.cfg.templates = Array.isArray(cfg.templates) ? cfg.templates : [];
      ST.cfg.settings = cfg.settings && typeof cfg.settings === "object" ? cfg.settings : {};
      ST.dropPath = cfg.dropPath || "";
    }
    await reloadSongs();
    ST.selectedId = ST.cfg.settings.selectedSong || null;
    ST.prevSeen = ST.cfg.settings.lastSeen || null;
    ST.cfg.settings.lastSeen = nowIso();
    saveCfg();
  }

  async function reloadSongs() {
    const songs = await native("stListSongs");
    ST.songs = Array.isArray(songs) ? songs : [];
  }

  async function patchSong(id, patch) {
    const r = await native("stUpdateSong", id, JSON.stringify(patch));
    if (r && r.ok === false) { toast(r.error || "Could not save the song.", "error"); return null; }
    await reloadSongs();
    return r;
  }

  /* ---------------- actions ---------------- */

  function go(navId) { const b = document.getElementById(navId); if (b) b.click(); }

  async function runAction(action, step) {
    const song = selectedSong();
    switch (action) {
      case "open-suno": await native("stOpenUrl", SUNO_URL); toast("Opened Suno in your browser. Studio is under Create.", "success"); break;
      case "drop": await native("stReveal", ST.dropPath); break;
      case "stems": if (song && song.stemsFolder) await native("stReveal", song.stemsFolder); break;
      case "rack": go("navRack"); break;
      case "riffhouse": go("navRiffHouse"); break;
      case "session": startFromTemplateButton(); break;
      case "advance": await advance(song); break;
      default: break;
    }
  }

  async function advance(song) {
    if (!song) return;
    const next = L.STAGES[Math.min(L.stageIndex(song.stage) + 1, L.STAGES.length - 1)];
    await patchSong(song.id, { stage: next.id });
    render();
  }

  async function tick(song, stepId, value) {
    const checklist = Object.assign({}, song.checklist || {}, { [stepId]: value });
    await patchSong(song.id, { checklist });
    ST.stuckText = "";
    render();
  }

  function startFromTemplateButton() {
    const t = ST.cfg.templates;
    if (!t.length) { toast("Add a template first, then press Open.", "error"); const b = document.getElementById("stAddFile"); if (b) b.focus(); return; }
    if (t.length === 1) { openTemplate(t[0]); return; }
    const card = document.getElementById("stTemplates");
    if (card) card.scrollIntoView({ behavior: "smooth", block: "center" });
  }

  async function openTemplate(t) {
    if (ST.busy) return;
    const nameInput = document.getElementById("stProjectName");
    const song = selectedSong();
    const name = (nameInput && nameInput.value.trim()) || (song && song.title) || "Untitled";
    ST.busy = true; render();
    const r = await native("stStartSession", t.path, name, song ? song.id : "");
    ST.busy = false;
    if (!r || r.ok === false) { toast((r && r.error) || "Could not start the session.", "error"); render(); return; }
    await native("stOpen", r.openPath);
    toast("Session copied and opening in " + dawFor(t.path) + ".", "success");
    await reloadSongs();
    render();
  }

  async function addTemplate(kind) {
    const path = await native("stPickTemplate", kind);
    if (!path) return;
    if (ST.cfg.templates.some((t) => t.path === path)) { toast("That template is already in the list.", "error"); return; }
    ST.cfg.templates.push({ id: "t" + Date.now(), name: baseName(path), path, daw: dawFor(path) });
    await saveCfg();
    render();
  }

  async function removeTemplate(id) {
    ST.cfg.templates = ST.cfg.templates.filter((t) => t.id !== id);
    await saveCfg();
    render();
  }

  async function setMode(id) {
    ST.cfg.settings.assistMode = id;
    ST.stuckText = "";
    await saveCfg();
    render();
  }

  async function newSong(title) {
    const r = await native("stCreateSong", title || "", 0, "");
    if (!r || r.ok === false) { toast((r && r.error) || "Could not create the song.", "error"); return; }
    await reloadSongs();
    selectSong(r.id);
  }

  function selectSong(id) {
    ST.selectedId = id;
    ST.stuckText = "";
    ST.cfg.settings.selectedSong = id;
    saveCfg();
    render();
  }

  async function checkDrop(silent) {
    const r = await native("stScanDrop");
    if (!r || typeof r !== "object") return;
    ST.dropPath = r.dropPath || ST.dropPath;
    ST.waiting = Array.isArray(r.waiting) ? r.waiting : [];
    const imported = Array.isArray(r.imported) ? r.imported : [];
    if (imported.length) {
      await reloadSongs();
      ST.selectedId = imported[0].id;
      ST.cfg.settings.selectedSong = imported[0].id;
      saveCfg();
      toast("Brought in " + imported.map((s) => s.title).join(", ") + ".", "success");
      silent ? renderSafe() : render();
    } else if (!silent) {
      toast(ST.waiting.length ? "Something is still downloading or needs a look." : "Nothing new in the Drop folder.", ST.waiting.length ? "error" : undefined);
      render();
    } else if (ST.waiting.length) {
      renderSafe();
    }
  }

  /* ---------------- rendering ---------------- */

  const ACTION_LABEL = {
    "open-suno": "Open Suno", drop: "Show Drop folder", session: "Open template", stems: "Show stems",
    rack: "Open Rack", riffhouse: "Open Riff House", advance: "Move on",
  };

  function nextStepCard() {
    const m = mode();
    const step = L.nextStep(ST.songs, { mode: m, nowIso: nowIso(), lastSeenIso: ST.prevSeen, selectedId: ST.selectedId });
    const song = selectedSong();
    const card = h("section", { class: "st-card st-next " + "st-" + m + " st-kind-" + step.kind, "aria-live": "polite" });

    card.append(h("div", { class: "st-next-top" },
      h("span", { class: "st-kicker" }, "Next step"),
      h("div", { class: "st-modes", role: "group", "aria-label": "How much help" },
        MODES.map((o) => h("button", { class: "st-mode" + (o.id === m ? " on" : ""), title: o.hint, onclick: () => setMode(o.id) }, o.label)))));

    if (step.welcome) card.append(h("div", { class: "st-welcome" }, step.welcome));
    card.append(h("h2", { class: "st-next-title" }, step.title));
    if (m !== "quiet" && step.detail) card.append(h("div", { class: "st-next-detail" }, step.detail));
    if (step.why) card.append(h("div", { class: "st-next-why" }, step.why));

    const row = h("div", { class: "st-next-actions" });
    if (step.action && step.action !== "none") {
      const label = step.action === "advance" ? "Move to " + L.getStage(step.nextStageId).name : ACTION_LABEL[step.action];
      row.append(h("button", { class: "btn btn-primary", onclick: () => runAction(step.action, step) }, label));
    }
    if (step.kind === "step" && song) row.append(h("button", { class: "btn", onclick: () => tick(song, step.stepId, true) }, "Mark done"));
    if (step.kind !== "finished") row.append(h("button", { class: "btn btn-ghost", onclick: () => { ST.stuckText = L.stuckStep(ST.songs, { selectedId: ST.selectedId }); render(); } }, "I'm stuck"));
    if (step.minutes) row.append(h("span", { class: "st-minutes" }, "about " + step.minutes + " min"));
    card.append(row);

    if (ST.stuckText) {
      card.append(h("div", { class: "st-stuck" },
        h("b", null, "Smallest step: "), ST.stuckText,
        h("button", { class: "btn btn-ghost small", onclick: () => { ST.stuckText = ""; render(); } }, "Got it")));
    }
    return card;
  }

  function templatesCard() {
    const song = selectedSong();
    const card = h("section", { class: "st-card", id: "stTemplates" }, h("h3", null, "Start a session"));
    card.append(h("div", { class: "st-row st-name-row" },
      h("label", { for: "stProjectName", class: "st-label" }, "Project name"),
      h("input", { id: "stProjectName", class: "st-input", type: "text", placeholder: song ? song.title : "Untitled", value: "" })));

    const list = h("div", { class: "st-templates" });
    if (!ST.cfg.templates.length) {
      list.append(h("div", { class: "st-empty" }, "No templates yet. Add the Logic, Ableton or Luna template you start from. The app copies it each time, so your original is never touched."));
    }
    ST.cfg.templates.forEach((t) => {
      const missing = false;
      list.append(h("div", { class: "st-tpl" + (missing ? " missing" : "") },
        h("span", { class: "st-chip" }, t.daw || dawFor(t.path)),
        h("span", { class: "st-tpl-name", title: t.path }, t.name),
        h("button", { class: "btn btn-primary small", disabled: ST.busy, onclick: () => openTemplate(t) }, "Open"),
        h("button", { class: "btn btn-ghost small", title: "Open the original template to edit it", onclick: () => native("stOpen", t.path) }, "Edit"),
        h("button", { class: "st-x", title: "Remove from list (does not delete the file)", "aria-label": "Remove " + t.name, onclick: () => removeTemplate(t.id) }, "×")));
    });
    card.append(list);
    card.append(h("div", { class: "st-row" },
      h("button", { id: "stAddFile", class: "btn btn-ghost small", onclick: () => addTemplate("file") }, "Add template file…"),
      h("button", { class: "btn btn-ghost small", onclick: () => addTemplate("folder") }, "Add template folder…")));
    card.append(h("div", { class: "st-hint" }, "Each Open makes a dated copy in Studio/Sessions with a notes file (tempo, key, stems location), then opens it."));
    return card;
  }

  function stagePills(song) {
    return h("div", { class: "st-stages", role: "group", "aria-label": "Stage" },
      L.STAGES.map((s) => {
        const p = L.stageProgress(song, s.id);
        return h("button", {
          class: "st-stage" + (s.id === song.stage ? " on" : "") + (p.complete ? " done" : ""),
          title: s.blurb, onclick: async () => { await patchSong(song.id, { stage: s.id }); render(); },
        }, s.name, h("small", null, p.done + "/" + p.total));
      }));
  }

  function songDetail(song) {
    const box = h("div", { class: "st-song-detail" });
    box.append(stagePills(song));

    const bpm = h("input", { class: "st-input narrow", type: "number", min: "30", max: "300", step: "0.1", value: song.bpm > 0 ? String(song.bpm) : "", placeholder: "BPM", "aria-label": "Tempo in BPM" });
    const key = h("input", { class: "st-input narrow", type: "text", maxlength: "12", value: song.key || "", placeholder: "Key", "aria-label": "Key" });
    const save = async () => { await patchSong(song.id, { bpm: Number(bpm.value) || 0, key: key.value.trim() }); render(); };
    bpm.addEventListener("change", save); key.addEventListener("change", save);
    box.append(h("div", { class: "st-row" }, bpm, key,
      song.stemsFolder && song.stems && song.stems.length ? h("button", { class: "btn btn-ghost small", onclick: () => native("stReveal", song.stemsFolder) }, song.stems.length + " stems · Show") : h("span", { class: "quiet" }, "no stems yet"),
      song.sessionPath ? h("button", { class: "btn btn-ghost small", onclick: () => native("stOpen", song.sessionPath) }, "Open its session") : null));

    const stage = L.getStage(song.stage);
    const list = h("ul", { class: "st-checks" });
    stage.steps.forEach((s) => {
      const done = L.isDone(song, s);
      const auto = done && !(song.checklist && song.checklist[s.id] === true);
      const cb = h("input", { type: "checkbox", id: "chk-" + s.id });
      cb.checked = done;
      cb.addEventListener("change", () => tick(song, s.id, cb.checked));
      list.append(h("li", { class: done ? "done" : "" }, cb, h("label", { for: "chk-" + s.id }, s.text), auto ? h("small", { class: "quiet" }, "noticed") : null));
    });
    box.append(list);
    return box;
  }

  function songsCard() {
    const sel = selectedSong();
    const card = h("section", { class: "st-card st-songs" }, h("h3", null, "Songs"));
    if (!ST.songs.length) card.append(h("div", { class: "st-empty" }, "No songs yet. Drop a Suno export in the Drop folder, or add one below."));
    const list = h("div", { class: "st-song-list" });
    ST.songs.forEach((s) => {
      const p = L.stageProgress(s, s.stage);
      list.append(h("button", { class: "st-song" + (sel && s.id === sel.id ? " on" : ""), onclick: () => selectSong(s.id) },
        h("span", { class: "st-song-title" }, s.title),
        h("span", { class: "st-song-meta" }, L.getStage(s.stage).name + " · " + p.done + "/" + p.total)));
    });
    card.append(list);

    const input = h("input", { class: "st-input", type: "text", placeholder: "New song title", "aria-label": "New song title" });
    const add = () => { const v = input.value.trim(); input.value = ""; newSong(v || "Untitled song"); };
    input.addEventListener("keydown", (e) => { if (e.key === "Enter") add(); });
    card.append(h("div", { class: "st-row" }, input, h("button", { class: "btn btn-ghost small", onclick: add }, "Add song")));

    if (sel) card.append(songDetail(sel));
    return card;
  }

  function sunoCard() {
    const card = h("section", { class: "st-card" }, h("h3", null, "Suno → DAW"));
    card.append(h("div", { class: "st-row" },
      h("button", { class: "btn btn-primary small", onclick: () => runAction("open-suno") }, "Open Suno"),
      h("button", { class: "btn btn-ghost small", onclick: () => native("stReveal", ST.dropPath) }, "Open Drop folder"),
      h("button", { class: "btn btn-ghost small", onclick: () => checkDrop(false) }, "Check now")));
    card.append(h("div", { class: "st-hint" }, "Export a Multitrack zip from Suno Studio into the Drop folder. It is unzipped, turned into a song, and the original is kept in Drop/Done. Watching while this tab is open."));
    if (ST.dropPath) card.append(h("code", { class: "st-path" }, ST.dropPath));
    ST.waiting.forEach((w) => {
      card.append(h("div", { class: "st-wait" }, typeof w === "string" ? w + " - waiting for the download to finish." : w.file + " - " + w.problem));
    });

    const det = h("details", { class: "st-details" },
      h("summary", null, "Hear Suno inside your DAW with WAX (10-minute test)"),
      h("ol", null,
        h("li", null, "Put WAX on a track as an Instrument."),
        h("li", null, "Open Suno in it and try to sign in. Suno recommends Chrome, so a sign-in or playback problem here is possible."),
        h("li", null, "Press play in Suno and watch the track meter. Check your session is 48 kHz."),
        h("li", null, "Record the track for 30 seconds and listen back for dropouts.")),
      h("p", { class: "quiet" }, "Good for auditioning. For real work, export the Multitrack and use the Drop folder."));
    card.append(det);
    return card;
  }

  function render() {
    const root = document.getElementById("stRoot");
    if (!root) return;
    const keep = document.getElementById("stProjectName");
    const typed = keep ? keep.value : "";
    const scroller = root.scrollTop;
    root.replaceChildren(
      h("div", { class: "st-head" },
        h("div", null, h("div", { class: "st-kicker" }, "Studio"), h("h1", { class: "st-title" }, "Pick up where you left off"),
          h("div", { class: "st-sub" }, "Open a template, bring Suno in, and take one step at a time."))),
      nextStepCard(),
      h("div", { class: "st-grid" }, templatesCard(), songsCard(), sunoCard()));
    const nameInput = document.getElementById("stProjectName");
    if (nameInput && typed) nameInput.value = typed;
    root.scrollTop = scroller;
  }

  /** Used by the background check: never redraw while you are typing in a field. */
  function renderSafe() {
    const root = document.getElementById("stRoot");
    const a = document.activeElement;
    if (root && a && root.contains(a) && (a.tagName === "INPUT" || a.tagName === "SELECT" || a.tagName === "TEXTAREA")) return;
    render();
  }

  /* ---------------- boot ---------------- */

  let pollTimer = null;
  function startPolling() {
    if (pollTimer) return;
    pollTimer = setInterval(() => {
      const view = document.getElementById("view-studio");
      if (view && view.classList.contains("on") && !document.hidden && !ST.busy) checkDrop(true);
    }, 4000);
  }

  async function init() {
    if (!L || !document.getElementById("stRoot")) return;
    await loadAll();
    render();
    await checkDrop(true);
    startPolling();
    const nav = document.getElementById("navStudio");
    if (nav) nav.addEventListener("click", async () => { await reloadSongs(); checkDrop(true); render(); });
  }

  window.ST = { render, reload: async () => { await reloadSongs(); render(); }, state: ST };
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init); else init();
})();
