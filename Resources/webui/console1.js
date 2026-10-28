"use strict";

/* ============================================================
   Console 1 tab - turn the Console 1 Mk I into hands-on control
   of the plugins hosted in the Rack. Talks to Source/Console1Engine.*
   through the callNative / onNative bridge from app.js.

   Calm on purpose: no pop-ups, no sounds, no flashing. A touched
   control glows softly and fades. Nothing is bound without a click
   (Learn, Assign or Apply).

   User text (plugin and parameter names) is only ever set with
   textContent, never as HTML.
   ============================================================ */

(function () {
  const C1 = {
    state: null,         // last c1GetState
    params: [],          // c1ListParams(focusSlot)
    paramsSlot: -1,
    live: null,          // last c1Live
    pane: "map",
    selectedCc: -1,      // control picked on the on-screen map
    simulate: false,
    simValues: {},       // cc -> value the simulator last sent
    filter: "",
    suggestions: null,   // auto-map result waiting for Apply
    lastSerial: 0,
    lastLearnedSerial: 0,
    glow: {},            // cc -> timestamp of last touch
    visible: false,
  };
  window.C1 = C1;

  /* ---------------- tiny DOM helper ---------------- */

  function el(tag, props, ...children) {
    const n = document.createElement(tag);
    if (props) {
      for (const [k, v] of Object.entries(props)) {
        if (v === undefined || v === null || v === false) continue;
        if (k === "class") n.className = v;
        else if (k === "text") n.textContent = v;
        else if (k.startsWith("on")) n.addEventListener(k.slice(2), v);
        else if (k === "dataset") Object.assign(n.dataset, v);
        else n.setAttribute(k, v === true ? "" : v);
      }
    }
    children.flat().forEach((c) => {
      if (c === null || c === undefined || c === false) return;
      n.appendChild(typeof c === "string" ? document.createTextNode(c) : c);
    });
    return n;
  }

  function $(id) { return document.getElementById(id); }

  /* ---------------- hardware layout (front-panel order) ---------------- */

  const LAYOUT = [
    { section: "input", title: "Input", rows: [["GAIN"], ["LOW_CUT", "HIGH_CUT"], ["FILTER_TO_COMPRESSORS", "PHASE_INV", "EXTERNAL_SIDECHAIN"]] },
    { section: "shape", title: "Shape", rows: [["SHAPE", "HARD_GATE"], ["SHAPE_GATE", "SHAPE_RELEASE"], ["SHAPE_SUSTAIN", "SHAPE_PUNCH"]] },
    { section: "eq", title: "Equalizer", rows: [
      ["EQ"],
      ["HIGH_GAIN", "HIGH_FREQ", "HIGH_SHAPE"],
      ["HIGH_MID_GAIN", "HIGH_MID_FREQ", "HIGH_MID_SHAPE"],
      ["LOW_MID_GAIN", "LOW_MID_FREQ", "LOW_MID_SHAPE"],
      ["LOW_GAIN", "LOW_FREQ", "LOW_SHAPE"],
    ] },
    { section: "comp", title: "Compressor", rows: [["COMP", "ORDER"], ["COMP_THRESH", "COMP_RATIO"], ["COMP_ATTACK", "COMP_RELEASE"], ["COMP_PAR"]] },
    { section: "drive", title: "Drive", rows: [["DRIVE"], ["CHARACTER"]] },
    { section: "output", title: "Output", rows: [["VOLUME"], ["PAN"], ["MUTE", "SOLO"]] },
  ];

  function controlById(id) { return (C1.state && C1.state.controls || []).find((c) => c.id === id); }
  function controlByCc(cc) { return (C1.state && C1.state.controls || []).find((c) => c.cc === cc); }
  function controlLabel(cc) { const c = controlByCc(cc); return c ? (c.section === "track" ? "Track " + c.label : c.label) : "CC " + cc; }

  function focusSlot() { return C1.state ? C1.state.focusSlot : 0; }
  function slotName(i) {
    const s = C1.state && C1.state.slots && C1.state.slots[i];
    return s && s.name ? s.name : null;
  }

  /** Bindings that currently win each cc (focused slot first, then lowest slot) - mirrors the C++ rule. */
  function winningBindings(cc) {
    const all = (C1.state && C1.state.bindings || []).filter((b) => b.cc === cc);
    if (!all.length) return [];
    const f = focusSlot();
    const slot = all.some((b) => b.slot === f) ? f : Math.min(...all.map((b) => b.slot));
    return all.filter((b) => b.slot === slot);
  }

  /* ---------------- data ---------------- */

  async function refreshState() {
    const s = await callNative("c1GetState");
    if (!s) return;
    C1.state = s;
    if (C1.paramsSlot !== s.focusSlot) await refreshParams();
    render();
  }

  async function refreshParams() {
    const slot = focusSlot();
    const list = await callNative("c1ListParams", slot);
    C1.params = Array.isArray(list) ? list : [];
    C1.paramsSlot = slot;
  }

  async function apply(promise) {
    const s = await promise;
    if (s && s.slots) {
      C1.state = s;
      if (C1.paramsSlot !== s.focusSlot) await refreshParams();
    }
    render();
    return s;
  }

  /* ---------------- root ---------------- */

  function buildRoot() {
    const root = $("c1Root");
    root.replaceChildren(
      el("div", { class: "c1-status", id: "c1Status" }),
      el("nav", { class: "c1-subnav", id: "c1Subnav" },
        ...[["map", "Map"], ["bindings", "Bindings"], ["setup", "Setup"], ["monitor", "MIDI Monitor"]].map(([id, label]) =>
          el("button", { dataset: { pane: id }, class: id === C1.pane ? "on" : "", text: label, onclick: () => { C1.pane = id; render(); } }))),
      el("section", { class: "c1-pane", id: "c1PaneMap" }),
      el("section", { class: "c1-pane", id: "c1PaneBindings" }),
      el("section", { class: "c1-pane", id: "c1PaneSetup" }),
      el("section", { class: "c1-pane", id: "c1PaneMonitor" }),
    );
  }

  function render() {
    if (!C1.state) return;
    document.querySelectorAll("#c1Subnav button").forEach((b) => b.classList.toggle("on", b.dataset.pane === C1.pane));
    $("c1PaneMap").classList.toggle("on", C1.pane === "map");
    $("c1PaneBindings").classList.toggle("on", C1.pane === "bindings");
    $("c1PaneSetup").classList.toggle("on", C1.pane === "setup");
    $("c1PaneMonitor").classList.toggle("on", C1.pane === "monitor");
    renderStatus();
    if (C1.pane === "map") renderMap();
    if (C1.pane === "bindings") renderBindings();
    if (C1.pane === "setup") renderSetup();
    if (C1.pane === "monitor") renderMonitor();
  }

  /* ---------------- status strip ---------------- */

  function renderStatus() {
    const s = C1.state, live = C1.live || {};
    const input = live.input !== undefined ? live.input : s.device.input;
    const output = live.output !== undefined ? live.output : s.device.output;
    const owner = live.owner !== undefined ? live.owner : s.owner;

    const device = input
      ? el("span", { class: "c1-pill ok", text: "Console 1 connected" + (output ? "" : " (no feedback port)") })
      : el("span", { class: "c1-pill", text: s.source === "host" ? "Using the DAW track's MIDI" : "Console 1 not found - the simulator works without it" });

    const ownerEl = owner
      ? el("span", { class: "c1-pill ok", text: "This window has control" })
      : el("button", { class: "btn small", text: "Take control", title: "Another GHS window is using the Console 1. Use it here instead.",
          onclick: () => apply(callNative("c1TakeControl")) });

    const touch = el("div", { class: "c1-touch", id: "c1Touch" });
    fillTouch(touch);

    $("c1Status").replaceChildren(device, ownerEl, el("span", { class: "c1-spacer" }), touch);
  }

  function fillTouch(node) {
    const live = C1.live;
    if (!live || live.cc < 0 || live.serial === 0) {
      node.replaceChildren(el("span", { class: "muted", text: "Touch a control to see what it does" }));
      return;
    }
    node.replaceChildren(
      el("b", { text: controlLabel(live.cc) }),
      el("span", { class: "c1-arrow", text: "→" }),
      el("span", { text: live.target || "nothing mapped" }),
      el("span", { class: "c1-val", text: String(live.value) }),
    );
  }

  /* ---------------- MAP pane ---------------- */

  function renderMap() {
    const pane = $("c1PaneMap");
    const s = C1.state;

    const slotRow = el("div", { class: "c1-slots" },
      el("span", { class: "c1-label", text: "Controlling" }),
      ...s.slots.map((slot) => el("button", {
        class: "c1-slot" + (slot.index === s.focusSlot ? " on" : "") + (slot.name ? "" : " empty"),
        title: slot.name ? "Track button " + (slot.index + 1) + " on the Console 1 also selects this" : "Empty slot",
        onclick: () => { C1.selectedCc = -1; C1.suggestions = null; apply(callNative("c1SetFocus", slot.index)); },
      }, el("span", { class: "c1-slot-num", text: String(slot.index + 1) }), el("span", { class: "c1-slot-name", text: slot.name || "empty" }),
         slot.hasProfile ? el("span", { class: "c1-dot", title: "Has a saved Console 1 profile" }) : null)));

    const hw = el("div", { class: "c1-hw" + (C1.simulate ? " sim" : "") },
      ...LAYOUT.map((sec) => el("div", { class: "c1-sec", dataset: { section: sec.section } },
        el("div", { class: "c1-sec-title", text: sec.title }),
        ...sec.rows.map((row) => el("div", { class: "c1-row" }, ...row.map((id) => controlEl(controlById(id))).filter(Boolean))))));

    const simToggle = el("label", { class: "c1-mini-toggle" },
      el("input", { type: "checkbox", checked: C1.simulate, onchange: (e) => { C1.simulate = e.target.checked; render(); } }),
      " Simulator (drag knobs, click buttons)");

    const left = el("div", { class: "c1-left" },
      el("div", { class: "c1-hw-head" }, el("span", { class: "c1-label", text: "Console 1 Mk I" }), el("span", { class: "c1-spacer" }), simToggle),
      hw,
      el("div", { class: "c1-hint", text: C1.simulate
        ? "Simulator on: drag a knob up or down (or scroll on it). It goes through exactly the same path as the real hardware."
        : "Click a control to pick it, then press Assign next to a parameter. Or press Learn on a parameter and move a real control." }));

    pane.replaceChildren(slotRow, el("div", { class: "c1-map" }, left, assignPanel()));
  }

  function controlEl(c) {
    if (!c) return null;
    const winners = winningBindings(c.cc);
    const bound = winners.length > 0;
    const ring = C1.live && C1.live.rings && C1.live.rings[String(c.cc)];
    const value = ring !== undefined ? ring : C1.simValues[c.cc];
    const isFocusOther = winners.length && winners[0].slot !== focusSlot();
    const glowing = C1.glow[c.cc] && performance.now() - C1.glow[c.cc] < 900;

    const label = el("div", { class: "c1-ctl-label", text: c.label });
    const target = el("div", { class: "c1-ctl-target", text: bound ? shortTarget(winners) : "" });

    const node = el("div", {
      class: "c1-ctl " + c.kind + (bound ? " bound" : "") + (isFocusOther ? " other-slot" : "") + (C1.selectedCc === c.cc ? " selected" : "") + (glowing ? " glow" : ""),
      dataset: { cc: c.cc },
      title: c.label + " (CC " + c.cc + ")" + (bound ? "\n" + winners.map((w) => w.label).join("\n") : "\nNot mapped"),
    });

    if (c.kind === "knob") {
      node.append(knobSvg(value), label, target);
      wireKnob(node, c);
    } else {
      node.append(el("span", { class: "c1-led" + (value >= 64 ? " lit" : "") }), label, target);
      wireButton(node, c);
    }
    return node;
  }

  function shortTarget(winners) {
    const w = winners[0];
    const name = w.target === "bypass" ? "On/Off" : (w.paramName || "Param " + (w.paramIndex + 1));
    return (w.slot !== focusSlot() ? (w.slot + 1) + ": " : "") + name + (winners.length > 1 ? " +" + (winners.length - 1) : "");
  }

  function knobSvg(value) {
    const ns = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(ns, "svg");
    svg.setAttribute("viewBox", "0 0 40 40");
    svg.setAttribute("class", "c1-knob");
    const r = 15, cx = 20, cy = 20, start = 135, sweep = 270;
    const arc = (a0, a1) => {
      const p = (a) => [cx + r * Math.cos((a * Math.PI) / 180), cy + r * Math.sin((a * Math.PI) / 180)];
      const [x0, y0] = p(a0), [x1, y1] = p(a1);
      return `M ${x0} ${y0} A ${r} ${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1} ${y1}`;
    };
    const track = document.createElementNS(ns, "path");
    track.setAttribute("d", arc(start, start + sweep));
    track.setAttribute("class", "c1-knob-track");
    svg.appendChild(track);
    if (typeof value === "number" && value >= 0) {
      const v = Math.max(0.0001, Math.min(1, value / 127));
      const fill = document.createElementNS(ns, "path");
      fill.setAttribute("d", arc(start, start + sweep * v));
      fill.setAttribute("class", "c1-knob-fill");
      svg.appendChild(fill);
    }
    const cap = document.createElementNS(ns, "circle");
    cap.setAttribute("cx", cx); cap.setAttribute("cy", cy); cap.setAttribute("r", 9);
    cap.setAttribute("class", "c1-knob-cap");
    svg.appendChild(cap);
    return svg;
  }

  function sendSim(cc, value) {
    value = Math.max(0, Math.min(127, Math.round(value)));
    if (C1.simValues[cc] === value) return;
    C1.simValues[cc] = value;
    callNative("c1Inject", 1, cc, value);
  }

  function startValueFor(cc) {
    const ring = C1.live && C1.live.rings && C1.live.rings[String(cc)];
    if (ring !== undefined) return ring;
    return C1.simValues[cc] !== undefined ? C1.simValues[cc] : 64;
  }

  function wireKnob(node, c) {
    node.addEventListener("click", () => { if (!C1.simulate) selectControl(c.cc); });
    node.addEventListener("pointerdown", (e) => {
      if (!C1.simulate) return;
      e.preventDefault();
      node.setPointerCapture(e.pointerId);
      const y0 = e.clientY, v0 = startValueFor(c.cc);
      const move = (ev) => sendSim(c.cc, v0 + (y0 - ev.clientY) / 1.2);
      const up = () => { node.removeEventListener("pointermove", move); node.removeEventListener("pointerup", up); };
      node.addEventListener("pointermove", move);
      node.addEventListener("pointerup", up);
    });
    node.addEventListener("wheel", (e) => {
      if (!C1.simulate) return;
      e.preventDefault();
      sendSim(c.cc, startValueFor(c.cc) + (e.deltaY < 0 ? 3 : -3)); // Console 1 moves in steps of ~3
    }, { passive: false });
  }

  function wireButton(node, c) {
    node.addEventListener("click", () => {
      if (C1.simulate) {
        callNative("c1Inject", 1, c.cc, 127).then(() => callNative("c1Inject", 1, c.cc, 0));
        return;
      }
      selectControl(c.cc);
    });
  }

  function selectControl(cc) {
    C1.selectedCc = C1.selectedCc === cc ? -1 : cc;
    render();
  }

  /* ---------------- assign panel (right side of Map) ---------------- */

  function assignPanel() {
    const s = C1.state;
    const slot = s.focusSlot;
    const name = slotName(slot);
    const panel = el("div", { class: "c1-assign" });

    if (!name) {
      panel.append(
        el("h3", { text: "Slot " + (slot + 1) + " is empty" }),
        el("p", { class: "c1-hint", text: "Load a plugin into this slot on the Rack tab, then come back here to put it under the knobs." }),
        el("button", { class: "btn small", text: "Go to Rack", onclick: () => { const b = $("navRack"); if (b) b.click(); } }));
      return panel;
    }

    // header + profile actions
    panel.append(
      el("div", { class: "c1-assign-head" },
        el("h3", { text: (slot + 1) + ". " + name }),
        el("button", { class: "btn btn-ghost small", text: "Open plugin", onclick: () => callNative("c1OpenEditor", slot) })),
      el("div", { class: "c1-row-actions" },
        el("button", { class: "btn small", text: "Save as this plugin's profile", title: "Next time this plugin is loaded in any slot, these knobs come back automatically.",
          onclick: async () => { const r = await callNative("c1SaveProfile", slot); if (r) showToast(r.message, r.ok ? "success" : "error"); refreshState(); } }),
        el("button", { class: "btn btn-ghost small", text: "Load profile",
          onclick: async () => { const r = await callNative("c1LoadProfile", slot); if (r) showToast(r.message, r.ok ? "success" : "info"); refreshState(); } }),
        el("button", { class: "btn btn-ghost small", text: "Clear this slot's knobs", onclick: () => apply(callNative("c1ClearSlot", slot)) })),
    );

    // selected control banner
    if (C1.selectedCc >= 0) {
      panel.append(el("div", { class: "c1-selected" },
        el("span", { text: "Picked: " }), el("b", { text: controlLabel(C1.selectedCc) }),
        el("span", { class: "muted", text: "  - press Assign on a parameter below" }),
        el("button", { class: "btn btn-ghost small", text: "Cancel", onclick: () => { C1.selectedCc = -1; render(); } })));
    }

    // learn banner
    if (s.learn) {
      panel.append(el("div", { class: "c1-learning" },
        el("span", { text: "Move a Console 1 control now (or a simulator knob)..." }),
        el("button", { class: "btn btn-ghost small", text: "Cancel", onclick: () => apply(callNative("c1CancelLearn")) })));
    }

    // auto-map
    panel.append(autoMapBlock(slot));

    // param list
    const search = el("input", { class: "c1-search", type: "text", placeholder: "Search " + C1.params.length + " parameters...", value: C1.filter,
      oninput: (e) => { C1.filter = e.target.value; renderParamList(list, slot); } });
    const list = el("div", { class: "c1-params" });
    renderParamList(list, slot);
    panel.append(search, list);
    return panel;
  }

  function bindingsForParam(slot, index) {
    return (C1.state.bindings || []).filter((b) => b.slot === slot && b.target === "param" && b.paramIndex === index);
  }

  function renderParamList(list, slot) {
    const q = C1.filter.trim().toLowerCase();
    const rows = [];

    // GHS's own slot on/off is a target too (good fit for the EQ / Comp / Shape buttons).
    const bypassBindings = (C1.state.bindings || []).filter((b) => b.slot === slot && b.target === "bypass");
    if (!q || "on off bypass".includes(q)) rows.push(paramRow({ index: -1, name: "On / Off (slot bypass)", text: "" }, bypassBindings, slot, true));

    C1.params
      .filter((p) => !q || p.name.toLowerCase().includes(q))
      .slice(0, 400)
      .forEach((p) => rows.push(paramRow(p, bindingsForParam(slot, p.index), slot, false)));

    if (rows.length === 0) rows.push(el("div", { class: "c1-hint", text: "No parameters match." }));
    list.replaceChildren(...rows);
  }

  function paramRow(p, bound, slot, isBypass) {
    const learning = C1.state.learn && C1.state.learnSlot === slot && (isBypass ? C1.state.learnBypass : C1.state.learnParam === p.index && !C1.state.learnBypass);
    const chips = bound.map((b) => el("span", { class: "c1-chip", title: "Mode: " + b.mode }, controlLabel(b.cc),
      el("button", { class: "c1-x", title: "Remove", text: "×", onclick: () => apply(callNative("c1RemoveBinding", b.id)) })));

    const actions = [];
    if (C1.selectedCc >= 0) {
      actions.push(el("button", { class: "btn btn-primary small", text: "Assign " + controlLabel(C1.selectedCc),
        onclick: async () => {
          const binding = { channel: 1, cc: C1.selectedCc, slot, target: isBypass ? "bypass" : "param", paramIndex: p.index, paramId: p.id || "", paramName: isBypass ? "" : p.name };
          C1.selectedCc = -1;
          await apply(callNative("c1Bind", binding));
        } }));
    }
    actions.push(learning
      ? el("span", { class: "c1-learn-on", text: "Listening..." })
      : el("button", { class: "btn btn-ghost small", text: "Learn", onclick: () => apply(callNative("c1ArmLearn", slot, p.index, isBypass)) }));

    return el("div", { class: "c1-param" + (bound.length ? " bound" : "") + (learning ? " learning" : "") },
      el("span", { class: "c1-param-name", text: p.name }),
      el("span", { class: "c1-param-val", text: p.text || "" }),
      el("span", { class: "c1-param-chips" }, ...chips),
      ...actions);
  }

  /* ---------------- auto-map ---------------- */

  function autoMapBlock(slot) {
    const sel = el("select", { class: "select small" },
      ...[["all", "Whole channel strip"], ["eq", "EQ section"], ["comp", "Compressor section"], ["shape", "Shape section"], ["drive", "Drive section"], ["input", "Input section"], ["output", "Output section"]]
        .map(([v, t]) => el("option", { value: v, text: t })));
    sel.value = C1.suggestSection || "all";
    sel.addEventListener("change", () => { C1.suggestSection = sel.value; });

    const block = el("div", { class: "c1-automap" },
      el("div", { class: "c1-row-actions" },
        el("span", { class: "c1-label", text: "Suggest a layout" }), sel,
        el("button", { class: "btn small", text: "Suggest", onclick: () => {
          C1.suggestSection = sel.value;
          C1.suggestions = window.C1Automap.suggest(C1.params, sel.value, slot);
          render();
        } })));

    if (C1.suggestions) {
      const sug = C1.suggestions;
      if (!sug.suggestions.length) {
        block.append(el("div", { class: "c1-hint", text: "No confident matches from the parameter names. Use Learn instead." }));
      } else {
        const checks = [];
        const rows = sug.suggestions.map((sgg) => {
          const box = el("input", { type: "checkbox", checked: true });
          checks.push([box, sgg.binding]);
          return el("label", { class: "c1-sug" }, box, el("b", { text: controlLabel(sgg.cc) }), el("span", { class: "c1-arrow", text: "→" }), el("span", { text: sgg.paramName }));
        });
        const btnBoxes = sug.buttons.map((b) => {
          const box = el("input", { type: "checkbox", checked: true });
          checks.push([box, b]);
          return el("label", { class: "c1-sug" }, box, el("b", { text: controlLabel(b.cc) }), el("span", { class: "c1-arrow", text: "→" }), el("span", { text: "slot On / Off" }));
        });
        block.append(
          el("div", { class: "c1-sug-list" }, ...rows, ...btnBoxes),
          el("div", { class: "c1-row-actions" },
            el("button", { class: "btn btn-primary small", text: "Apply ticked", onclick: async () => {
              const chosen = checks.filter(([box]) => box.checked).map(([, b]) => b);
              C1.suggestions = null;
              await apply(callNative("c1Bind", chosen));
              showToast("Mapped " + chosen.length + " controls. Save as this plugin's profile to keep them.", "success");
            } }),
            el("button", { class: "btn btn-ghost small", text: "Dismiss", onclick: () => { C1.suggestions = null; render(); } })));
      }
    }
    return block;
  }

  /* ---------------- BINDINGS pane ---------------- */

  function renderBindings() {
    const pane = $("c1PaneBindings");
    const list = (C1.state.bindings || []).slice().sort((a, b) => a.slot - b.slot || a.cc - b.cc);
    if (!list.length) {
      pane.replaceChildren(el("div", { class: "c1-empty", text: "Nothing mapped yet. Start on the Map tab: pick a slot, then Suggest, Learn or Assign." }));
      return;
    }

    const head = el("div", { class: "c1-brow head" },
      ...["Control", "Controls", "Now", "Mode", "Range", "Invert", "Fine", ""].map((t) => el("span", { text: t })));

    const rows = list.map((b) => {
      const isButton = (controlByCc(b.cc) || {}).kind === "button" || b.target === "bypass";
      const modeSel = el("select", { class: "select small" },
        ...(isButton ? ["toggle", "momentary"] : ["absolute", "pickup", "relative"]).map((m) => el("option", { value: m, text: modeLabel(m) })));
      modeSel.value = b.mode;
      modeSel.addEventListener("change", () => apply(callNative("c1UpdateBinding", b.id, { mode: modeSel.value })));

      const pct = (v) => Math.round(v * 100);
      const minIn = el("input", { class: "c1-num", type: "number", min: 0, max: 100, value: pct(b.min), title: "Knob fully left" });
      const maxIn = el("input", { class: "c1-num", type: "number", min: 0, max: 100, value: pct(b.max), title: "Knob fully right" });
      const pushRange = () => apply(callNative("c1UpdateBinding", b.id, { min: Number(minIn.value) / 100, max: Number(maxIn.value) / 100 }));
      minIn.addEventListener("change", pushRange);
      maxIn.addEventListener("change", pushRange);

      const inv = el("input", { type: "checkbox", checked: b.invert, onchange: (e) => apply(callNative("c1UpdateBinding", b.id, { invert: e.target.checked })) });
      const fine = b.mode === "relative"
        ? el("input", { class: "c1-num", type: "number", min: 2, max: 400, step: 5, value: Math.round(b.sensitivity * 100), title: "% of the full range per full knob turn",
            onchange: (e) => apply(callNative("c1UpdateBinding", b.id, { sensitivity: Number(e.target.value) / 100 })) })
        : el("span", { class: "muted", text: "-" });

      return el("div", { class: "c1-brow" + (b.resolved ? "" : " missing") },
        el("span", { text: b.control }),
        el("span", { class: "c1-btarget", text: b.label + (b.resolved ? "" : "  (parameter not found)") }),
        el("span", { class: "c1-param-val", text: b.valueText }),
        modeSel,
        isButton ? el("span", { class: "muted", text: "-" }) : el("span", { class: "c1-range" }, minIn, "–", maxIn, "%"),
        isButton ? el("span", { class: "muted", text: "-" }) : inv,
        fine,
        el("button", { class: "c1-x", title: "Remove", text: "×", onclick: () => apply(callNative("c1RemoveBinding", b.id)) }));
    });

    pane.replaceChildren(
      el("div", { class: "c1-hint", text: "Absolute: knob = parameter. Pickup: knob only grabs once it passes the current value (use when the rings can't be updated). Relative: only the turn counts; Fine sets how much one full turn moves (lower = finer)." }),
      el("div", { class: "c1-btable" }, head, ...rows));
  }

  function modeLabel(m) {
    return { absolute: "Absolute", pickup: "Pickup", relative: "Relative (fine)", toggle: "Toggle", momentary: "Hold" }[m] || m;
  }

  /* ---------------- SETUP pane ---------------- */

  function renderSetup() {
    const s = C1.state;
    const pane = $("c1PaneSetup");

    const sourceRadios = el("div", { class: "c1-radios" },
      ...[["device", "Console 1 directly (recommended)", "GHS opens the Console 1 MIDI port itself. Works in any DAW with no routing, and lets the knob rings follow the plugin."],
          ["host", "DAW track MIDI", "Route the Console 1 to this track's MIDI input in your DAW. No ring feedback."],
          ["both", "Both", "Only if you know you need it - the same move could arrive twice."]]
        .map(([v, t, d]) => el("label", { class: "c1-radio" },
          el("input", { type: "radio", name: "c1src", value: v, checked: s.source === v, onchange: () => apply(callNative("c1SetOption", "source", v)) }),
          el("span", {}, el("b", { text: t }), el("br"), el("span", { class: "muted", text: d })))));

    const inSel = deviceSelect(s.device.inputs, s.device.preferredInput);
    const outSel = deviceSelect(s.device.outputs, s.device.preferredOutput);
    const saveDevices = () => apply(callNative("c1SetDevices", inSel.value, outSel.value));
    inSel.addEventListener("change", saveDevices);
    outSel.addEventListener("change", saveDevices);

    const toggle = (key, label, hint) => el("label", { class: "c1-check" },
      el("input", { type: "checkbox", checked: !!s[key], onchange: (e) => apply(callNative("c1SetOption", key, e.target.checked)) }),
      el("span", {}, el("b", { text: label }), hint ? el("span", { class: "muted", text: "  " + hint }) : null));

    const knobMode = el("select", { class: "select small" }, ...["absolute", "pickup", "relative"].map((m) => el("option", { value: m, text: modeLabel(m) })));
    knobMode.value = s.defaultKnobMode;
    knobMode.addEventListener("change", () => apply(callNative("c1SetOption", "defaultKnobMode", knobMode.value)));

    pane.replaceChildren(
      el("div", { class: "c1-grid" },
        el("div", { class: "c1-card" }, el("h3", { text: "Where the Console 1 comes in" }), sourceRadios),
        el("div", { class: "c1-card" }, el("h3", { text: "Ports" }),
          el("div", { class: "c1-field" }, el("span", { class: "c1-label", text: "From Console 1" }), inSel),
          el("div", { class: "c1-field" }, el("span", { class: "c1-label", text: "To Console 1" }), outSel),
          el("div", { class: "c1-hint", text: "Now open: " + (s.device.input || "nothing") + " / " + (s.device.output || "nothing") + ". Auto picks any port with \"Console 1\" in its name. GHS keeps checking every 2 seconds, so plugging in later is fine." })),
        el("div", { class: "c1-card" }, el("h3", { text: "Behaviour" }),
          toggle("feedback", "Rings and LEDs follow the plugin", "(needs the port 'To Console 1')"),
          toggle("trackButtonsFocusSlots", "Track buttons 1-8 pick the rack slot", "Page Up/Down steps through loaded slots"),
          toggle("autoLoadProfiles", "Load a plugin's saved profile automatically"),
          toggle("consumeBoundCcs", "Hide mapped CCs from hosted plugins", "(DAW-track MIDI only)"),
          el("div", { class: "c1-field" }, el("span", { class: "c1-label", text: "New knob mappings start as" }), knobMode)),
        el("div", { class: "c1-card" }, el("h3", { text: "Profiles" }),
          el("div", { class: "c1-hint", text: "Each plugin's knob layout is a small JSON file you can back up or share." }),
          el("button", { class: "btn small", text: "Show profiles folder", onclick: () => callNative("c1RevealProfiles") }))),
      el("div", { class: "c1-card c1-checklist" },
        el("h3", { text: "First test at home (10 minutes)" }),
        el("ol", {},
          ...["Open Softube's Console 1 On-Screen Display, open its menu and turn on \"Use Mk I/II as MIDI Controllers\".",
              "Open GHS in your DAW. The top of this tab should say \"Console 1 connected\". If not, pick the port under Ports.",
              "Open MIDI Monitor and turn the Comp Threshold knob. You should see CC 47 with changing values.",
              "Load any EQ in Rack slot 1. Map tab, slot 1, Suggest, EQ section, Apply.",
              "Turn Low Gain on the hardware. The plugin's band 1 gain should move.",
              "Change the gain with the mouse in the plugin. The ring on the hardware should follow.",
              "Save as this plugin's profile. Remove the plugin, load it again: the knobs should come back by themselves."]
            .map((t) => el("li", { text: t }))),
        el("div", { class: "c1-hint", text: "If a step fails, the MIDI Monitor tab and docs/CONSOLE1.md say what to check." })));
  }

  function deviceSelect(names, preferred) {
    const sel = el("select", { class: "select small" }, el("option", { value: "", text: "Auto (Console 1)" }),
      ...(names || []).map((n) => el("option", { value: n, text: n })));
    if (preferred && !(names || []).includes(preferred)) sel.append(el("option", { value: preferred, text: preferred + " (not connected)" }));
    sel.value = preferred || "";
    return sel;
  }

  /* ---------------- MONITOR pane ---------------- */

  function renderMonitor() {
    const pane = $("c1PaneMonitor");
    const live = C1.live || { recent: [] };
    const rows = (live.recent || []).map((r) => el("div", { class: "c1-mrow" },
      el("span", { class: "c1-mono", text: "ch " + r.channel }),
      el("span", { class: "c1-mono", text: "CC " + r.cc }),
      el("span", { text: controlLabel(r.cc) }),
      el("span", { class: "c1-mono", text: String(r.value) }),
      el("span", { class: "muted", text: r.target || "not mapped" })));
    pane.replaceChildren(
      el("div", { class: "c1-card" },
        el("h3", { text: "Last 12 controller messages" }),
        el("div", { class: "c1-hint", text: "Messages counted - from the Console 1 port: " + (live.deviceEvents || 0) + ", from the DAW track: " + (live.hostEvents || 0)
          + " (simulator moves aren't counted). If both stay at 0 while you turn real knobs, GHS isn't receiving anything: check Setup." }),
        rows.length ? el("div", { class: "c1-mlist" }, ...rows) : el("div", { class: "c1-empty", text: "Nothing yet. Turn a knob (or use the simulator on the Map tab)." })));
  }

  /* ---------------- live events ---------------- */

  onNative("c1Live", (live) => {
    if (!live) return;
    const prev = C1.live;
    C1.live = live;
    if (!C1.visible || !C1.state) return;

    if (live.serial !== C1.lastSerial) {
      C1.lastSerial = live.serial;
      if (live.cc >= 0) C1.glow[live.cc] = performance.now();
    }
    if (live.learnedSerial !== C1.lastLearnedSerial) {
      const first = C1.lastLearnedSerial === 0 && prev === null;
      C1.lastLearnedSerial = live.learnedSerial;
      if (!first && live.learnedMessage) showToast("Mapped: " + live.learnedMessage, "success");
      refreshState();
      return;
    }
    if (live.focusSlot !== C1.state.focusSlot || live.learn !== C1.state.learn) {
      refreshState(); // hardware track button changed the slot, or learn ended
      return;
    }
    if (prev && (prev.owner !== live.owner || prev.input !== live.input || prev.output !== live.output)) renderStatus();

    const t = $("c1Touch");
    if (t) fillTouch(t);
    if (C1.pane === "map") updateMapLive();
    if (C1.pane === "monitor") renderMonitor();
  });

  /** Cheap in-place update of rings / LEDs / glow, without rebuilding the map. */
  function updateMapLive() {
    const now = performance.now();
    document.querySelectorAll("#c1PaneMap .c1-ctl").forEach((node) => {
      const cc = Number(node.dataset.cc);
      const ring = C1.live.rings && C1.live.rings[String(cc)];
      const value = ring !== undefined ? ring : C1.simValues[cc];
      node.classList.toggle("glow", !!C1.glow[cc] && now - C1.glow[cc] < 900);
      if (node.classList.contains("knob")) {
        const old = node.querySelector("svg");
        if (old && old.dataset.v !== String(value)) {
          const svg = knobSvg(value);
          svg.dataset.v = String(value);
          old.replaceWith(svg);
        }
      } else {
        const led = node.querySelector(".c1-led");
        if (led) led.classList.toggle("lit", value >= 64);
      }
    });
  }

  // Parameter values (the "Now" text) drift when the plugin is moved with the mouse; refresh gently.
  setInterval(async () => {
    if (!C1.visible || !C1.state || C1.pane === "monitor") return;
    if (document.activeElement && /INPUT|SELECT/.test(document.activeElement.tagName)) return; // don't yank focus while typing
    const s = await callNative("c1GetState");
    if (!s) return;
    C1.state = s;
    await refreshParams();
    if (C1.pane === "map") {
      const list = document.querySelector("#c1PaneMap .c1-params");
      if (list) renderParamList(list, s.focusSlot);
    } else {
      render();
    }
  }, 1500);

  /* ---------------- show / init ---------------- */

  C1.onShow = function () {
    C1.visible = true;
    refreshState();
  };

  function watchVisibility() {
    const view = $("view-console1");
    if (!view) return;
    new MutationObserver(() => {
      C1.visible = view.classList.contains("on");
      if (C1.visible) refreshState();
    }).observe(view, { attributes: true, attributeFilter: ["class"] });
  }

  function init() {
    if (!$("c1Root")) return;
    buildRoot();
    watchVisibility();
    C1.visible = $("view-console1").classList.contains("on");
    refreshState();
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})();
