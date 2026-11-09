"use strict";

/* ============================================================
   Riff House - "Today" tab: a gentle daily routine, practice log and
   journal for a fixed-length program (default 21 days).

   Design rules (so it works on low-energy days):
   - Anything counts. One small thing = "showed up". There is no way
     to lose progress: the day tiles only ever fill in.
   - A missed day never resets anything. Rest is part of the plan.
   - Everything is saved as plain files in
       ~/Music/GHS/RiffHouse/Journal/journal.json   (+ journal.backup.json)
     and can be exported as readable Markdown.
   - Practice is tracked automatically from the Play / Note Ninja /
     Capture tabs (see window.__RH from riffhouse.js); nothing to log
     by hand unless you want to.
   ============================================================ */

(function () {
  const LS_KEY = "ghs-riffhouse-journal-v1";
  const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
  const WEEKDAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];

  const SCALE = [
    { v: 1, label: "Rough" },
    { v: 2, label: "Low" },
    { v: 3, label: "Okay" },
    { v: 4, label: "Steady" },
    { v: 5, label: "Good" },
  ];

  const QUESTS = [
    { id: "warmup", title: "Warm up", hint: "Hit 10 notes in Note Ninja (about 2 minutes).", tab: "arcade", xp: 30 },
    { id: "song", title: "Play one song", hint: "2 minutes with a chart playing. One section is plenty. Slow it down.", tab: "play", xp: 40 },
    { id: "capture", title: "Save one idea", hint: "Press Save that after anything you like, even a few notes.", tab: "capture", xp: 30 },
    { id: "journal", title: "Write a few lines", hint: "What the day was like. One sentence counts.", tab: null, xp: 30 },
  ];

  const WEEKLY_BOSS = [
    { week: 1, name: "Slow Burn", text: "Loop one section at 0.7x or slower and reach a 20 combo.",
      test: (s) => s.rate <= 0.71 && s.maxCombo >= 20 },
    { week: 2, name: "Build It", text: "Play a whole song at 0.8x or faster with 70% of notes hit.",
      test: (s) => s.rate >= 0.79 && s.judged >= 60 && s.acc >= 0.7 },
    { week: 3, name: "Boss Fight", text: "Play a whole song at full speed with 70% of notes hit, then save a take.",
      test: (s) => s.rate >= 0.97 && s.judged >= 60 && s.acc >= 0.7 },
  ];

  const PROMPTS = [
    "One thing from today I want to remember.",
    "What did my body do today, and what helped?",
    "Something someone said that stuck with me.",
    "A moment I felt even a little bit steady.",
    "What was hard, and what did I do with it?",
    "A phrase or image that could be a lyric.",
    "Something I noticed about myself that I did not expect.",
    "What I would tell someone starting this program.",
    "What I want to bring home from this.",
    "A small win I almost skipped over.",
  ];

  const TAGS = ["Group", "Body", "Win", "Hard", "Lyric", "Song idea", "Gratitude"];

  const LEVELS = ["Roadie", "Garage Band", "Opening Act", "Soundcheck", "Support Act", "Headliner", "Main Stage", "Legend"];

  /* ---------------- tiny helpers ---------------- */

  const $ = (sel, root) => (root || document).querySelector(sel);
  const $$ = (sel, root) => Array.from((root || document).querySelectorAll(sel));
  const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

  function pad(n) { return String(n).padStart(2, "0"); }
  function ymd(d) { return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()); }
  function parseYmd(s) { const [y, m, d] = s.split("-").map(Number); return new Date(y, m - 1, d); }
  function addDays(s, n) { const d = parseYmd(s); d.setDate(d.getDate() + n); return ymd(d); }
  function dayDiff(a, b) { return Math.round((parseYmd(a) - parseYmd(b)) / 86400000); }
  function prettyDate(s) { const d = parseYmd(s); return WEEKDAYS[d.getDay()] + ", " + MONTHS[d.getMonth()] + " " + d.getDate(); }
  function timeOf(ts) { const d = new Date(ts); let h = d.getHours(); const ap = h >= 12 ? "pm" : "am"; h = h % 12 || 12; return h + ":" + pad(d.getMinutes()) + ap; }
  function mins(sec) { return Math.floor(sec / 60); }
  function uid() { return Math.random().toString(36).slice(2, 9); }
  function toast(t, tone) { if (typeof showToast === "function") showToast(t, tone || "success"); }

  /* ---------------- data ---------------- */

  let S = null;       // whole journal document
  let selected = null; // YYYY-MM-DD currently shown
  let draft = { text: "", tag: "", promptIx: 0 };
  let saveTimer = null;
  let renderPending = false;
  let host = null;

  function blankDoc() {
    return { version: 1, program: null, days: {}, bossesCleared: {}, createdAt: Date.now() };
  }
  function today() { return ymd(new Date()); }
  function day(date) {
    if (!S.days[date]) {
      S.days[date] = { checkins: {}, quests: {}, practiceSec: 0, songSec: 0, ninjaHits: 0, captures: 0, showedUp: false, sessions: [], entries: [] };
    }
    return S.days[date];
  }
  function peekDay(date) { return S.days[date] || null; }

  async function load() {
    let raw = null;
    try { raw = await callNative("rhJournalLoad"); } catch (e) { raw = null; }
    if (!raw) { try { raw = localStorage.getItem(LS_KEY); } catch (e) { raw = null; } }
    try { S = raw ? JSON.parse(raw) : blankDoc(); } catch (e) { S = blankDoc(); }
    if (!S || typeof S !== "object" || !S.days) S = blankDoc();
  }

  function persistNow() {
    clearTimeout(saveTimer); saveTimer = null;
    const text = JSON.stringify(S);
    try { localStorage.setItem(LS_KEY, text); } catch (e) { /* storage may be unavailable in the plugin webview */ }
    return callNative("rhJournalSave", text);
  }
  function persistSoon() {
    if (saveTimer) return;
    saveTimer = setTimeout(persistNow, 600);
  }

  /* ---------------- derived facts ---------------- */

  function hasShownUp(date) {
    const d = peekDay(date);
    if (!d) return false;
    return !!(d.showedUp || d.practiceSec >= 30 || d.ninjaHits >= 3 || d.captures > 0 || d.entries.length > 0 ||
      Object.values(d.quests).some(Boolean));
  }
  function questDone(d, id) {
    if (d.quests[id]) return true;
    if (id === "warmup") return d.ninjaHits >= 10;
    if (id === "song") return d.songSec >= 120;
    if (id === "capture") return d.captures > 0;
    if (id === "journal") return d.entries.length > 0;
    return false;
  }
  function xpFor(date) {
    const d = peekDay(date);
    if (!d) return 0;
    let xp = 0;
    if (hasShownUp(date)) xp += 20;
    for (const q of QUESTS) if (questDone(d, q.id)) xp += q.xp;
    xp += Math.floor(d.practiceSec / 6);       // 10 xp per minute played
    xp += d.ninjaHits * 2;
    xp += d.sessions.reduce((a, s) => a + Math.floor((s.score || 0) / 200), 0);
    if (d.checkins.before && d.checkins.after) xp += 15;
    return xp;
  }
  function totalXp() {
    return Object.keys(S.days).reduce((a, k) => a + xpFor(k), 0) + Object.keys(S.bossesCleared || {}).length * 150;
  }
  function levelInfo(xp) {
    const lvl = Math.floor(Math.sqrt(xp / 150)) + 1;
    const lo = Math.pow(lvl - 1, 2) * 150, hi = Math.pow(lvl, 2) * 150;
    return { lvl, name: LEVELS[Math.min(LEVELS.length - 1, lvl - 1)], pct: Math.round(((xp - lo) / (hi - lo)) * 100), toNext: Math.ceil(hi - xp) };
  }
  // A run bridges a single skipped day: resting once is not a break.
  function currentRun(upTo) {
    const keys = Object.keys(S.days).sort();
    if (!keys.length) return 0;
    const earliest = keys[0];
    let d = hasShownUp(upTo) ? upTo : addDays(upTo, -1); // today may simply not have happened yet
    let run = 0, gap = 0;
    while (d >= earliest) {
      if (hasShownUp(d)) { run++; gap = 0; }
      else if (++gap >= 2) break;
      d = addDays(d, -1);
    }
    return run;
  }
  function totalDaysShown() { return Object.keys(S.days).filter(hasShownUp).length; }

  function programInfo() {
    const p = S.program;
    if (!p) return null;
    const n = p.days || 21;
    const idx = dayDiff(today(), p.start); // 0-based
    return { start: p.start, n, idx, name: p.name || "Program", before: idx < 0, after: idx >= n,
      dayNo: Math.min(Math.max(idx + 1, 0), n), week: Math.min(3, Math.floor(Math.max(idx, 0) / 7) + 1) };
  }
  function programDates() {
    const p = S.program; const out = [];
    for (let i = 0; i < p.days; i++) out.push(addDays(p.start, i));
    return out;
  }

  /* ---------------- bosses ---------------- */

  function evaluateBosses() {
    if (!S.program) return;
    const dates = programDates();
    for (const b of WEEKLY_BOSS) {
      if (S.bossesCleared[b.week]) continue;
      const wk = dates.slice((b.week - 1) * 7, b.week * 7);
      const hit = wk.some((dt) => { const d = peekDay(dt); return d && d.sessions.some((s) => b.test(s)); });
      if (hit) {
        S.bossesCleared[b.week] = Date.now();
        toast("Weekly goal cleared: " + b.name, "success");
      }
    }
  }

  /* ---------------- automatic practice tracking ---------------- */

  const track = { rec: null, sessionRef: null, ninjaRef: null, ninjaLast: 0, dirty: false };

  function tick() {
    const RH = window.__RH;
    if (!RH || !S) return;
    const date = today();
    const d = day(date);
    let changed = false;

    // Note Ninja hits
    if (RH.ninja) {
      if (track.ninjaRef !== RH.ninja) { track.ninjaRef = RH.ninja; track.ninjaLast = 0; }
      const delta = RH.ninja.hits - track.ninjaLast;
      if (delta > 0) { d.ninjaHits += delta; track.ninjaLast = RH.ninja.hits; changed = true; }
    } else { track.ninjaRef = null; track.ninjaLast = 0; }

    // Song play time + per-session stats
    const playing = !!(RH.live && RH.live.playing && RH.chart);
    if (playing) {
      if (!track.rec || track.sessionRef !== RH.session || track.rec.date !== date) {
        track.sessionRef = RH.session;
        track.rec = { id: uid(), date, ts: Date.now(), chart: RH.chart.title || "Untitled", instrument: RH.chart.instrument || "",
          sec: 0, hits: 0, misses: 0, judged: 0, acc: 0, maxCombo: 0, score: 0, rate: RH.settings.rate };
        d.sessions.push(track.rec);
      }
      const r = track.rec;
      r.sec += 1;
      r.rate = RH.settings.rate;
      r.hits = Math.max(r.hits, RH.session.hits);
      r.misses = Math.max(r.misses, RH.session.misses);
      r.judged = r.hits + r.misses;
      r.acc = r.judged ? r.hits / r.judged : 0;
      r.maxCombo = Math.max(r.maxCombo, RH.session.maxCombo);
      r.score = Math.max(r.score, RH.session.score);
      d.practiceSec += 1;
      d.songSec += 1;
      changed = true;
    }

    if (changed) {
      const before = QUESTS.map((q) => questDone(d, q.id)).join();
      track.dirty = true;
      evaluateBosses();
      persistSoon();
      if (QUESTS.map((q) => questDone(d, q.id)).join() !== before) requestRender();
      else liveUpdate();
    }
  }

  function liveUpdate() {
    if (!host || selected !== today()) return;
    const d = peekDay(selected);
    if (!d) return;
    const t = $("#rtMinutes", host); if (t) t.textContent = mins(d.practiceSec) + " min played today";
    const x = $("#rtXp", host); if (x) { const li = levelInfo(totalXp()); x.textContent = li.name + " - level " + li.lvl; const f = $("#rtXpFill", host); if (f) f.style.width = li.pct + "%"; const n = $("#rtXpNext", host); if (n) n.textContent = li.toNext + " XP to next level"; }
    const ni = $("#rtQuestwarmup", host); if (ni) ni.textContent = Math.min(10, d.ninjaHits) + " / 10";
    const sg = $("#rtQuestsong", host); if (sg) sg.textContent = Math.min(120, d.songSec) + "s / 120s";
  }

  /* ---------------- rendering ---------------- */

  function requestRender() {
    if (!host) return;
    const a = document.activeElement;
    if (a && host.contains(a) && (a.tagName === "TEXTAREA" || a.tagName === "INPUT")) { renderPending = true; return; }
    render();
  }

  function render() {
    renderPending = false;
    if (!host || !S) return;
    if (!S.program) { host.innerHTML = renderSetup(); bindSetup(); return; }
    const info = programInfo();
    if (!selected) selected = clampToProgram(today());
    const d = day(selected);
    const isToday = selected === today();
    const li = levelInfo(totalXp());
    const run = currentRun(today());

    host.innerHTML = `
      <div class="rt-head">
        <div>
          <div class="rt-kicker">${esc(info.name)}${info.before ? " starts " + esc(prettyDate(info.start)) : info.after ? " - complete" : " - day " + info.dayNo + " of " + info.n}</div>
          <h2 class="rt-title">${isToday ? "Today" : esc(prettyDate(selected))}</h2>
          <div class="rt-sub">Anything counts. One small thing is a full day.</div>
        </div>
        <div class="rt-rank">
          <div class="rt-rank-name" id="rtXp">${esc(li.name)} - level ${li.lvl}</div>
          <div class="rt-bar"><i id="rtXpFill" style="width:${li.pct}%"></i></div>
          <div class="rt-rank-sub" id="rtXpNext">${li.toNext} XP to next level</div>
        </div>
      </div>

      ${renderTiles(info)}

      <div class="rt-stats">
        <div><b>${totalDaysShown()}</b><span>days shown up</span></div>
        <div><b>${run}</b><span>day run (one rest day never breaks it)</span></div>
        <div><b>${mins(Object.values(S.days).reduce((a, x) => a + (x.practiceSec || 0), 0))}</b><span>minutes played in total</span></div>
        <div><b>${Object.values(S.days).reduce((a, x) => a + (x.entries ? x.entries.length : 0), 0)}</b><span>journal entries</span></div>
      </div>

      <div class="rt-grid">
        <section class="rt-card">
          <h3>Check in</h3>
          ${renderCheckin(d)}
        </section>

        <section class="rt-card">
          <h3>Today's quests <small class="quiet" id="rtMinutes">${mins(d.practiceSec)} min played${isToday ? " today" : ""}</small></h3>
          ${renderQuests(d, isToday)}
          <button class="rt-btn rt-ghost" id="rtShowed" ${hasShownUp(selected) ? "disabled" : ""}>${hasShownUp(selected) ? "You showed up today" : "I picked it up today (counts)"}</button>
        </section>

        ${renderBoss(info)}

        <section class="rt-card rt-wide">
          <h3>Journal</h3>
          ${renderJournal(d)}
        </section>

        <section class="rt-card rt-wide">
          <h3>Practice log</h3>
          ${renderSessions(d)}
        </section>
      </div>

      <div class="rt-foot">
        <button class="rt-btn rt-ghost" id="rtExport">Export program log (Markdown)</button>
        <button class="rt-btn rt-ghost" id="rtReveal">Open journal folder</button>
        <button class="rt-btn rt-ghost" id="rtSettings">Change program dates</button>
        <span class="quiet" id="rtSavedNote">Saved automatically to Music / GHS / RiffHouse / Journal</span>
      </div>
    `;
    bindMain();
    // restore draft
    const ta = $("#rtDraft", host); if (ta) ta.value = draft.text;
  }

  function clampToProgram(date) {
    const info = programInfo();
    if (!info) return date;
    if (info.idx < 0) return info.start;
    if (info.idx >= info.n) return addDays(info.start, info.n - 1);
    return date;
  }

  function renderSetup() {
    return `
      <div class="rt-setup">
        <h2 class="rt-title">Set up your program</h2>
        <p class="rt-sub">This builds a daily routine around your stay: a short check-in, four small quests, a journal, and a weekly goal. You can change the dates any time.</p>
        <label class="rt-field">Name <input id="rtSetName" type="text" value="Intensive outpatient program" /></label>
        <label class="rt-field">First day <input id="rtSetStart" type="date" value="${today()}" /></label>
        <label class="rt-field">Length in days <input id="rtSetDays" type="number" min="7" max="60" value="21" /></label>
        <button class="rt-btn rt-primary" id="rtSetGo">Start</button>
      </div>`;
  }
  function bindSetup() {
    $("#rtSetGo", host).addEventListener("click", () => {
      const start = $("#rtSetStart", host).value || today();
      const n = Math.max(7, Math.min(60, parseInt($("#rtSetDays", host).value, 10) || 21));
      S.program = { name: $("#rtSetName", host).value.trim() || "Program", start, days: n };
      selected = null;
      persistNow(); render();
    });
  }

  function renderTiles(info) {
    const dates = programDates();
    const t = today();
    let html = '<div class="rt-tiles" role="list">';
    dates.forEach((dt, i) => {
      const cls = ["rt-tile"];
      if (hasShownUp(dt)) cls.push("done");
      if (dt === t) cls.push("today");
      if (dt === selected) cls.push("sel");
      if (dt > t) cls.push("future");
      const ck = peekDay(dt) && peekDay(dt).checkins;
      let dots = "";
      if (ck && ck.before && ck.after) dots = '<em class="' + (ck.after.v >= ck.before.v ? "up" : "flat") + '"></em>';
      html += `<button class="${cls.join(" ")}" data-date="${dt}" title="${esc(prettyDate(dt))}" role="listitem"><b>${i + 1}</b><span>${WEEKDAYS[parseYmd(dt).getDay()]}</span>${dots}</button>`;
    });
    return html + "</div>";
  }

  function renderCheckin(d) {
    const b = d.checkins.before, a = d.checkins.after;
    const row = (when, cur) => `
      <div class="rt-ck-row">
        <div class="rt-ck-label">${when === "before" ? "Before I play" : "After I play"}</div>
        <div class="rt-scale">${SCALE.map((s) => `<button class="rt-pill ${cur && cur.v === s.v ? "on" : ""}" data-ck="${when}" data-v="${s.v}">${s.label}</button>`).join("")}</div>
      </div>`;
    let msg = "";
    if (b && a) {
      const diff = a.v - b.v;
      msg = diff > 0 ? "Playing moved you up " + diff + (diff === 1 ? " step." : " steps.") :
        diff === 0 ? "Steady. Staying level counts too." : "Lower than before. That is information, not failure. Be gentle with the rest of today.";
    } else if (!b) msg = "Takes ten seconds. It builds a record of what helps.";
    return row("before", b) + row("after", a) +
      `<input class="rt-input" id="rtBodyNote" type="text" placeholder="Optional: where do I feel it in my body?" value="${esc(d.checkins.body || "")}" />
       <div class="rt-msg">${esc(msg)}</div>`;
  }

  function renderQuests(d, isToday) {
    return '<ul class="rt-quests">' + QUESTS.map((q) => {
      const done = questDone(d, q.id);
      let prog = "";
      if (q.id === "warmup") prog = `<span class="rt-prog" id="rtQuestwarmup">${Math.min(10, d.ninjaHits)} / 10</span>`;
      if (q.id === "song") prog = `<span class="rt-prog" id="rtQuestsong">${Math.min(120, d.songSec)}s / 120s</span>`;
      return `<li class="${done ? "done" : ""}">
        <label class="rt-check"><input type="checkbox" data-quest="${q.id}" ${done ? "checked" : ""} /><span class="box"></span></label>
        <div class="rt-q-body"><div class="rt-q-title">${esc(q.title)} ${prog}</div><div class="rt-q-hint">${esc(q.hint)}</div></div>
        ${q.tab && isToday ? `<button class="rt-btn rt-small" data-go="${q.tab}">Go</button>` : ""}
      </li>`;
    }).join("") + "</ul>";
  }

  function renderBoss(info) {
    const wk = info.before ? 1 : info.after ? 3 : info.week;
    const b = WEEKLY_BOSS[wk - 1];
    const cleared = S.bossesCleared[b.week];
    const all = WEEKLY_BOSS.map((x) => `<span class="rt-boss-pip ${S.bossesCleared[x.week] ? "on" : ""}" title="${esc(x.name)}">${x.week}</span>`).join("");
    return `<section class="rt-card">
      <h3>Weekly goal ${all}</h3>
      <div class="rt-boss ${cleared ? "cleared" : ""}">
        <div class="rt-boss-name">Week ${b.week}: ${esc(b.name)}</div>
        <div class="rt-q-hint">${esc(b.text)}</div>
        <div class="rt-boss-state">${cleared ? "Cleared. Anything more this week is a bonus." : "Checked automatically from your play sessions."}</div>
      </div>
      <div class="rt-q-hint rt-mt">Tip: pick your own song in Songs, then use Set A / Set B and the Rate slider to work one section at a time.</div>
    </section>`;
  }

  function renderJournal(d) {
    const prompt = PROMPTS[draft.promptIx % PROMPTS.length];
    const entries = d.entries.slice().sort((x, y) => y.ts - x.ts);
    return `
      <div class="rt-prompt"><span id="rtPromptText">${esc(prompt)}</span> <button class="rt-link" id="rtNextPrompt">Another prompt</button></div>
      <textarea class="rt-textarea" id="rtDraft" rows="4" placeholder="Write, or use Mac dictation (press the dictation key) to talk it out. Messy is fine."></textarea>
      <div class="rt-tags">${TAGS.map((t) => `<button class="rt-pill ${draft.tag === t ? "on" : ""}" data-tag="${esc(t)}">${esc(t)}</button>`).join("")}</div>
      <div><button class="rt-btn rt-primary" id="rtSaveEntry">Save entry</button></div>
      <div class="rt-entries">${entries.length ? entries.map((e) => `
        <article class="rt-entry">
          <header><span>${esc(timeOf(e.ts))}${e.tag ? " - " + esc(e.tag) : ""}</span><button class="rt-link" data-del="${e.id}">Delete</button></header>
          ${e.prompt ? `<div class="rt-entry-prompt">${esc(e.prompt)}</div>` : ""}
          <p>${esc(e.text).replace(/\n/g, "<br>")}</p>
        </article>`).join("") : '<div class="quiet">No entries for this day yet.</div>'}
      </div>`;
  }

  function renderSessions(d) {
    if (!d.sessions.length) return '<div class="quiet">Play a chart on the Play tab and your sessions show up here on their own.</div>';
    return '<table class="rt-table"><thead><tr><th>Time</th><th>Song</th><th>Played</th><th>Speed</th><th>Notes hit</th><th>Best combo</th><th>Score</th></tr></thead><tbody>' +
      d.sessions.filter((s) => s.sec >= 5).map((s) => `<tr>
        <td>${esc(timeOf(s.ts))}</td><td>${esc(s.chart)}</td><td>${Math.floor(s.sec / 60)}m ${s.sec % 60}s</td>
        <td>${(s.rate || 1).toFixed(2)}x</td><td>${s.judged ? Math.round(s.acc * 100) + "%" : "-"}</td><td>${s.maxCombo || 0}</td><td>${s.score || 0}</td></tr>`).join("") +
      "</tbody></table>";
  }

  /* ---------------- events ---------------- */

  function bindMain() {
    $$(".rt-tile", host).forEach((b) => b.addEventListener("click", () => { selected = b.dataset.date; render(); }));

    $$("[data-ck]", host).forEach((b) => b.addEventListener("click", () => {
      const d = day(selected);
      d.checkins[b.dataset.ck] = { v: parseInt(b.dataset.v, 10), ts: Date.now() };
      if (d.checkins.before && d.checkins.after) toast("Check-in recorded", "success");
      persistSoon(); render();
    }));
    const bn = $("#rtBodyNote", host);
    if (bn) bn.addEventListener("input", () => { day(selected).checkins.body = bn.value; persistSoon(); });

    $$("[data-quest]", host).forEach((cb) => cb.addEventListener("change", () => {
      const d = day(selected);
      d.quests[cb.dataset.quest] = cb.checked;
      persistSoon(); render();
    }));
    $$("[data-go]", host).forEach((b) => b.addEventListener("click", () => {
      const btn = document.querySelector('.rh-subnav button[data-pane="' + b.dataset.go + '"]');
      if (btn) btn.click();
    }));
    const showed = $("#rtShowed", host);
    if (showed) showed.addEventListener("click", () => { day(selected).showedUp = true; persistSoon(); toast("Noted. That counts.", "success"); render(); });

    // journal
    const ta = $("#rtDraft", host);
    if (ta) {
      ta.addEventListener("input", () => { draft.text = ta.value; });
      ta.addEventListener("blur", () => { if (renderPending) setTimeout(render, 50); });
    }
    const next = $("#rtNextPrompt", host);
    if (next) next.addEventListener("click", () => { draft.promptIx = (draft.promptIx + 1) % PROMPTS.length; $("#rtPromptText", host).textContent = PROMPTS[draft.promptIx]; });
    $$("[data-tag]", host).forEach((b) => b.addEventListener("click", () => {
      draft.tag = draft.tag === b.dataset.tag ? "" : b.dataset.tag;
      $$("[data-tag]", host).forEach((o) => o.classList.toggle("on", o.dataset.tag === draft.tag));
    }));
    const save = $("#rtSaveEntry", host);
    if (save) save.addEventListener("click", () => {
      const text = (draft.text || "").trim();
      if (!text) { toast("Nothing to save yet", "error"); return; }
      day(selected).entries.push({ id: uid(), ts: Date.now(), date: selected, tag: draft.tag, prompt: PROMPTS[draft.promptIx % PROMPTS.length], text });
      draft.text = ""; draft.tag = ""; draft.promptIx = (draft.promptIx + 1) % PROMPTS.length;
      persistNow().then(() => toast("Entry saved", "success"));
      render();
    });
    $$("[data-del]", host).forEach((b) => b.addEventListener("click", () => {
      const d = day(selected);
      if (b.dataset.armed) { d.entries = d.entries.filter((e) => e.id !== b.dataset.del); persistNow(); render(); return; }
      b.dataset.armed = "1"; b.textContent = "Tap again to delete";
      setTimeout(() => { if (b.isConnected) { delete b.dataset.armed; b.textContent = "Delete"; } }, 3000);
    }));

    $("#rtExport", host).addEventListener("click", exportMarkdown);
    $("#rtReveal", host).addEventListener("click", () => callNative("rhJournalReveal"));
    $("#rtSettings", host).addEventListener("click", () => { S.program = null; render(); });
  }

  /* ---------------- markdown export ---------------- */

  function buildMarkdown() {
    const p = S.program;
    const L = [];
    L.push("# " + (p ? p.name : "Program") + " - practice log and journal", "");
    if (p) L.push("Program: " + prettyDate(p.start) + " for " + p.days + " days.");
    L.push("Days shown up: " + totalDaysShown() + ". Minutes played: " + mins(Object.values(S.days).reduce((a, x) => a + (x.practiceSec || 0), 0)) +
      ". Journal entries: " + Object.values(S.days).reduce((a, x) => a + (x.entries ? x.entries.length : 0), 0) + ".", "");
    const cleared = WEEKLY_BOSS.filter((b) => S.bossesCleared[b.week]).map((b) => "Week " + b.week + " " + b.name);
    if (cleared.length) L.push("Weekly goals cleared: " + cleared.join(", ") + ".", "");
    const keys = Object.keys(S.days).sort();
    for (const k of keys) {
      const d = S.days[k];
      if (!hasShownUp(k) && !d.checkins.before && !d.checkins.after) continue;
      const no = p ? dayDiff(k, p.start) + 1 : null;
      L.push("## " + (no && no > 0 ? "Day " + no + " - " : "") + prettyDate(k), "");
      const sc = (c) => (c ? SCALE.find((s) => s.v === c.v).label + " (" + c.v + "/5)" : "not recorded");
      if (d.checkins.before || d.checkins.after) {
        L.push("Check-in: before " + sc(d.checkins.before) + ", after " + sc(d.checkins.after) + ".");
        if (d.checkins.body) L.push("Body: " + d.checkins.body);
        L.push("");
      }
      const done = QUESTS.filter((q) => questDone(d, q.id)).map((q) => q.title.toLowerCase());
      L.push("Practice: " + mins(d.practiceSec) + " min played" + (done.length ? "; quests done: " + done.join(", ") : "") + ".");
      for (const s of d.sessions.filter((x) => x.sec >= 5)) {
        L.push("- " + timeOf(s.ts) + " " + s.chart + ", " + Math.floor(s.sec / 60) + "m " + (s.sec % 60) + "s at " + (s.rate || 1).toFixed(2) + "x" +
          (s.judged ? ", " + Math.round(s.acc * 100) + "% of notes hit" : "") + ", best combo " + (s.maxCombo || 0));
      }
      L.push("");
      for (const e of d.entries.slice().sort((a, b) => a.ts - b.ts)) {
        L.push("### " + timeOf(e.ts) + (e.tag ? " - " + e.tag : ""));
        if (e.prompt) L.push("> " + e.prompt);
        L.push("", e.text, "");
      }
    }
    return L.join("\n");
  }

  async function exportMarkdown() {
    await persistNow();
    const res = await callNative("rhJournalExport", "Program-Log.md", buildMarkdown());
    if (res && res.ok) toast("Exported to Music / GHS / RiffHouse / Journal / Program-Log.md", "success");
    else toast("Export needs the plugin (it only works inside the DAW window)", "error");
  }

  /* ---------------- boot ---------------- */

  async function init() {
    host = document.getElementById("rhPaneRoutine");
    if (!host) return;
    await load();
    selected = null;
    render();
    window.addEventListener("rh:capture", () => {
      const d = day(today()); d.captures += 1; persistSoon(); evaluateBosses();
      if (selected === today()) requestRender();
    });
    window.addEventListener("rh:routine-shown", () => { if (selected === today() || !selected) { selected = null; } render(); });
    setInterval(tick, 1000);
    window.addEventListener("beforeunload", () => { if (saveTimer) persistNow(); });
    document.addEventListener("visibilitychange", () => { if (document.hidden && saveTimer) persistNow(); });
    // Expose for debugging / tests.
    window.__RHRoutine = { get state() { return S; }, buildMarkdown, render };
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})();
