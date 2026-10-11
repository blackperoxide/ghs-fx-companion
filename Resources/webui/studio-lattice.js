"use strict";

/* ============================================================
   Studio lattice: the song pipeline as data, plus the rules that pick
   ONE next step. No AI, no network, no randomness: the same song gives
   the same answer, it works offline, and it can be unit-tested
   (tools/test_studio_lattice.js).

   Rows are stages, columns are the small steps inside each stage.
   Every step has:
     text     what to do
     small    the 2-minute version, used by "I'm stuck"
     why      one plain sentence, only shown in Coach mode
     minutes  rough time box
     auto     optional: the app can tell it is already done from the song's data
     action   what button the card offers: open-suno | drop | session | stems | rack | riffhouse | none

   The same shape is where the later "mix engineer" layer plugs in:
   it will only add measured facts (loudness, clashes) to a step, never
   change a stage on its own.
   ============================================================ */

(function (root) {
  const STAGES = [
    {
      id: "suno", name: "Suno", blurb: "Get the take you love out of Suno.",
      steps: [
        { id: "pick", text: "Pick the take you love, not the perfect one.", small: "Play two takes. Keep the one that made you move.", why: "Choosing fast keeps the energy. You can always make another.", minutes: 5, action: "open-suno" },
        { id: "export", text: "Export Multitrack (WAV) from Suno Studio.", small: "Just press export. Nothing else has to happen yet.", why: "Separate stems are what let you rebuild and mix it properly.", minutes: 3, action: "open-suno" },
        { id: "drop", text: "Drop the zip into the Drop folder.", small: "Drag the downloaded zip onto the Drop folder.", why: "The app does the unzipping and setup for you.", minutes: 1, action: "drop",
          auto: (s) => (s.stems && s.stems.length > 0) || !!s.sourceFile },
      ],
    },
    {
      id: "stems", name: "Stems", blurb: "Stems are in. Get oriented.",
      steps: [
        { id: "tempo", text: "Set the tempo and key.", small: "Type the BPM Suno shows. Skip the key if unsure.", why: "Stems only land on the grid when the session tempo matches.", minutes: 2, action: "none",
          auto: (s) => Number(s.bpm) > 0 },
        { id: "listen", text: "Listen once with everything up.", small: "Press play and close your eyes for one chorus.", why: "One calm listen tells you what to keep before you start fixing.", minutes: 4, action: "stems" },
        { id: "mark", text: "Note what you will replace or play yourself.", small: "Name one part you want to play yourself.", why: "Rebuilding one part is more satisfying than fixing ten.", minutes: 3, action: "none" },
      ],
    },
    {
      id: "rebuild", name: "Rebuild", blurb: "Put your sound and your playing in.",
      steps: [
        { id: "session", text: "Start a session from your template.", small: "Press Open template and let the app copy it.", why: "A copy keeps your template clean for next time.", minutes: 2, action: "session",
          auto: (s) => !!s.sessionPath },
        { id: "import", text: "Set the tempo, then drag the stems in.", small: "Set the tempo. Drag in just the drums.", why: "Tempo first means nothing needs warping later.", minutes: 5, action: "stems" },
        { id: "replace", text: "Replace or layer one part with your instrument.", small: "Arm one track and play the first four bars.", why: "Your playing is what makes it yours.", minutes: 20, action: "none" },
        { id: "record", text: "Record one full take of that part.", small: "Record a single pass. Keep it even if it is rough.", why: "A rough take that exists beats a perfect one that does not.", minutes: 15, action: "none" },
      ],
    },
    {
      id: "mix", name: "Mix", blurb: "Balance first, effects second.",
      steps: [
        { id: "balance", text: "Set levels with no plugins.", small: "Pull every fader down, then bring up drums, bass, vocal.", why: "Most of a mix is balance. Plugins come after.", minutes: 15, action: "none" },
        { id: "clash", text: "Fix the single biggest clash.", small: "Solo the two parts that fight and cut one of them a little.", why: "One clear fix is worth more than ten small ones.", minutes: 10, action: "none" },
        { id: "chain", text: "Build a chain on the part that matters most.", small: "Open the Rack and use Record & Suggest on that part.", why: "Spend effort where the listener's ear goes first.", minutes: 15, action: "rack" },
        { id: "check", text: "Check it on a phone or a second speaker.", small: "Play the chorus on your phone, quietly.", why: "If it works small and quiet it will work big.", minutes: 5, action: "none" },
      ],
    },
    {
      id: "master", name: "Master", blurb: "Print it and finish it.",
      steps: [
        { id: "print", text: "Bounce the mix.", small: "Bounce it once. Do not listen yet.", why: "A printed file is a finished step you cannot lose.", minutes: 5, action: "none" },
        { id: "master", text: "Master it (LANDR or your own chain).", small: "Upload or load the bounce and take the first suggestion.", why: "Mastering is the last polish, not a second mix.", minutes: 10, action: "none" },
        { id: "compare", text: "Compare to a song you love at the same loudness.", small: "Switch between them once at equal volume.", why: "Matching loudness is what makes the comparison honest.", minutes: 5, action: "none" },
      ],
    },
    {
      id: "perform", name: "Perform", blurb: "Play it. It is yours now.",
      steps: [
        { id: "chart", text: "Make a practice chart in Riff House.", small: "Import the stem or MIDI of the part you play.", why: "Playing along is the fastest way to know it by heart.", minutes: 5, action: "riffhouse" },
        { id: "playthrough", text: "Play it through once.", small: "Play just the chorus at a slow speed.", why: "One pass is enough to start.", minutes: 5, action: "riffhouse" },
        { id: "perf-take", text: "Record a performance take.", small: "Press record and play the first verse.", why: "This is the take people will hear.", minutes: 10, action: "none" },
      ],
    },
  ];

  const STAGE_IDS = STAGES.map((s) => s.id);

  function stageIndex(id) { const i = STAGE_IDS.indexOf(id); return i < 0 ? 0 : i; }
  function getStage(id) { return STAGES[stageIndex(id)]; }

  /** A step is done if you ticked it, or the app can see it is already true. */
  function isDone(song, step) {
    if (song && song.checklist && song.checklist[step.id] === true) return true;
    if (song && song.checklist && song.checklist[step.id] === false) return false; // you un-ticked it
    return typeof step.auto === "function" ? !!step.auto(song || {}) : false;
  }

  function stageProgress(song, stageId) {
    const st = getStage(stageId);
    const done = st.steps.filter((s) => isDone(song, s)).length;
    return { done, total: st.steps.length, complete: done === st.steps.length };
  }

  function firstOpenStep(song, stageId) {
    return getStage(stageId).steps.find((s) => !isDone(song, s)) || null;
  }

  /** The song to focus on: the one you chose, otherwise the one touched most recently that is not finished. */
  function pickSong(songs, selectedId) {
    if (!songs || !songs.length) return null;
    const chosen = selectedId && songs.find((s) => s.id === selectedId);
    if (chosen) return chosen;
    const open = songs.filter((s) => !isFinished(s));
    const pool = open.length ? open : songs;
    return pool.slice().sort((a, b) => String(b.updated || "").localeCompare(String(a.updated || "")))[0];
  }

  function isFinished(song) {
    return song.stage === "perform" && stageProgress(song, "perform").complete;
  }

  function daysBetween(aIso, bIso) {
    if (!aIso || !bIso) return 0;
    const a = Date.parse(aIso), b = Date.parse(bIso);
    if (!isFinite(a) || !isFinite(b)) return 0;
    return Math.floor((b - a) / 86400000);
  }

  /**
   * Picks ONE next step. Pure function.
   * ctx: { mode: "quiet"|"nudge"|"coach", nowIso, lastSeenIso, selectedId }
   * Returns { kind, title, detail, why, minutes, action, stageId, stepId, songId, welcome }.
   * Kinds: empty | needs-stems | advance | step | finished
   * Never returns a guilt message; "welcome back" only says there is nothing to catch up on.
   */
  function nextStep(songs, ctx) {
    ctx = ctx || {};
    const mode = ctx.mode || "nudge";
    const song = pickSong(songs, ctx.selectedId);

    let welcome = "";
    if (mode !== "quiet" && ctx.lastSeenIso && daysBetween(ctx.lastSeenIso, ctx.nowIso) >= 3) {
      welcome = "Welcome back. There is nothing to catch up on.";
    }

    if (!song) {
      return {
        kind: "empty", songId: null, welcome, action: "open-suno",
        title: "Nothing in progress yet.",
        detail: "Export a Multitrack from Suno Studio and drop the zip in the Drop folder, or start a blank song.",
        why: mode === "coach" ? "Starting with a real song gives every other step something to work on." : "",
        minutes: 3,
      };
    }

    const base = { songId: song.id, welcome, stageId: song.stage };

    if (song.needsStems && !(song.stems && song.stems.length)) {
      return Object.assign({}, base, {
        kind: "needs-stems", action: "open-suno", stepId: null,
        title: song.title + " is a full mix, not stems.",
        detail: "Export Multitrack from Suno Studio and drop that zip in, or split it with the Riff House importer.",
        why: mode === "coach" ? "Stems are what let you replace parts and mix properly." : "",
        minutes: 5,
      });
    }

    if (isFinished(song)) {
      return Object.assign({}, base, {
        kind: "finished", action: "none", stepId: null,
        title: song.title + " is finished.",
        detail: "Rest, or pick the next song.",
        why: "", minutes: 0,
      });
    }

    const open = firstOpenStep(song, song.stage);
    if (!open) {
      const next = STAGES[Math.min(stageIndex(song.stage) + 1, STAGES.length - 1)];
      return Object.assign({}, base, {
        kind: "advance", action: "advance", stepId: null, nextStageId: next.id,
        title: getStage(song.stage).name + " is done for " + song.title + ".",
        detail: "Move on to " + next.name + " when you are ready.",
        why: mode === "coach" ? next.blurb : "", minutes: 0,
      });
    }

    return Object.assign({}, base, {
      kind: "step", action: open.action || "none", stepId: open.id,
      title: open.text,
      detail: song.title + " · " + getStage(song.stage).name,
      why: mode === "coach" ? open.why : "",
      minutes: mode === "coach" ? open.minutes : 0,
    });
  }

  /** The smallest possible move for the current step. */
  function stuckStep(songs, ctx) {
    const song = pickSong(songs, ctx && ctx.selectedId);
    if (!song) return "Open Suno Studio and press play on anything. That is the whole step.";
    const open = firstOpenStep(song, song.stage);
    if (open) return open.small;
    return "Nothing is blocking you here. Rest, or move to the next stage.";
  }

  const api = { STAGES, STAGE_IDS, stageIndex, getStage, isDone, stageProgress, firstOpenStep, pickSong, isFinished, nextStep, stuckStep, daysBetween };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.StudioLattice = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
