"use strict";

/* ============================================================
   Console 1 auto-map: suggests which Console 1 control should
   drive which parameter of a hosted plugin, from parameter names
   alone. Pure functions, no DOM, no bridge - unit tested with
   `node tools/test_console1_automap.js`.

   It only ever SUGGESTS. The UI shows the list; nothing is bound
   until Shawn presses Apply.
   ============================================================ */

(function (root) {
  // CC numbers - must match Source/Console1Engine.cpp controls().
  const CC = {
    GAIN: 107, LOW_CUT: 103, HIGH_CUT: 105,
    SHAPE: 53, SHAPE_GATE: 54, SHAPE_RELEASE: 56, SHAPE_SUSTAIN: 55, SHAPE_PUNCH: 57,
    EQ: 80,
    LOW_GAIN: 91, LOW_FREQ: 92,
    LOW_MID_GAIN: 88, LOW_MID_FREQ: 89, LOW_MID_SHAPE: 90,
    HIGH_MID_GAIN: 85, HIGH_MID_FREQ: 86, HIGH_MID_SHAPE: 87,
    HIGH_GAIN: 82, HIGH_FREQ: 83,
    COMP: 46, COMP_THRESH: 47, COMP_RATIO: 49, COMP_ATTACK: 51, COMP_RELEASE: 48, COMP_PAR: 50,
    DRIVE: 15, CHARACTER: 18,
    VOLUME: 7, PAN: 10,
  };

  const SECTION_OF = {
    GAIN: "input", LOW_CUT: "input", HIGH_CUT: "input",
    SHAPE_GATE: "shape", SHAPE_RELEASE: "shape", SHAPE_SUSTAIN: "shape", SHAPE_PUNCH: "shape",
    LOW_GAIN: "eq", LOW_FREQ: "eq", LOW_MID_GAIN: "eq", LOW_MID_FREQ: "eq", LOW_MID_SHAPE: "eq",
    HIGH_MID_GAIN: "eq", HIGH_MID_FREQ: "eq", HIGH_MID_SHAPE: "eq", HIGH_GAIN: "eq", HIGH_FREQ: "eq",
    COMP_THRESH: "comp", COMP_RATIO: "comp", COMP_ATTACK: "comp", COMP_RELEASE: "comp", COMP_PAR: "comp",
    DRIVE: "drive", CHARACTER: "drive",
    VOLUME: "output", PAN: "output",
  };

  // Section on/off buttons -> GHS slot bypass (lit = slot active).
  const SECTION_BUTTON = { shape: "SHAPE", eq: "EQ", comp: "COMP" };

  // Params that are never worth a knob.
  const IGNORE = /^(midi cc|program|bypass|preset|oversampl|latency|license|ui |gui |zoom|undo|redo|a\/b|copy|paste)|midi cc \d|\bmidi cc\b/i;

  // Simple name rules: [control, regex, score]. Higher score wins conflicts.
  const RULES = [
    ["GAIN", /^(input( gain| level| trim)?|in( gain)?|trim|pre ?gain)$/i, 9],
    ["GAIN", /\binput\b/i, 5],
    ["LOW_CUT", /(low ?cut|hpf|high ?pass|lo ?cut|hp freq)/i, 9],
    ["HIGH_CUT", /(high ?cut|lpf|low ?pass|hi ?cut|lp freq)/i, 9],

    ["SHAPE_GATE", /(gate.*thresh|thresh.*gate)/i, 10],
    ["SHAPE_RELEASE", /(gate.*release|release.*gate)/i, 11],
    ["SHAPE_RELEASE", /(\bhold\b|\bdecay\b)/i, 6],
    ["SHAPE_SUSTAIN", /sustain/i, 9],
    ["SHAPE_PUNCH", /(punch|transient|\bsnap\b)/i, 9],

    ["COMP_THRESH", /^(?!.*gate).*thresh/i, 10],
    ["COMP_THRESH", /\b(peak reduction|compress(ion)?|squeeze)\b/i, 8],
    ["COMP_THRESH", /\binput\b/i, 4],          // 1176-style: input drives into a fixed threshold
    ["COMP_RATIO", /ratio/i, 10],
    ["COMP_ATTACK", /attack/i, 10],
    ["COMP_RELEASE", /^(?!.*gate).*(release|recovery)/i, 10],
    ["COMP_PAR", /(\bmix\b|dry.?wet|parallel|blend)/i, 10],
    ["COMP_PAR", /\bwet\b/i, 6],

    ["DRIVE", /(drive|saturat|\bsat\b|heat|\bamount\b|crunch|distort)/i, 9],
    ["CHARACTER", /(character|\btone\b|\bstyle\b|flavou?r|harmonic|colou?r)/i, 7],

    ["VOLUME", /(make.?up|output( gain| level)?$|out gain|\bvolume\b|\boutput\b)/i, 8],
    ["PAN", /(\bpan\b|balance)/i, 9],
  ];

  // ---------------- EQ bands ----------------

  const ATTR = {
    freq: /(\bfreq|frequency|\bhz\b|\bf\b)/i,
    gain: /(\bgain\b|\bboost\b|\bg\b)/i,
    q: /(\bq\b|\bbw\b|bandwidth|\bwidth\b|resonance)/i,
  };

  const NAMED_BAND = [
    ["LOW_MID", /(low[\s-]?mid|\blmf\b|\blm\b|lo[\s-]?mid)/i],
    ["HIGH_MID", /(high[\s-]?mid|hi[\s-]?mid|\bhmf\b|\bhm\b)/i],
    ["LOW", /(\blow\b|\blf\b|\bbass\b|\blo\b)/i],
    ["HIGH", /(\bhigh\b|\bhf\b|\btreble\b|\bhi\b|\bair\b)/i],
  ];

  // "High Pass Freq" / "Low Cut" are filters (Input section), not EQ bands.
  const FILTER_WORDS = /(pass|\bcut\b|\bhpf\b|\blpf\b|\bhp\b|\blp\b)/i;

  function attrOf(name) {
    if (ATTR.q.test(name)) return "q";
    if (ATTR.freq.test(name)) return "freq";
    if (ATTR.gain.test(name)) return "gain";
    return null;
  }

  function eqControl(band, attr) {
    if (attr === "q") return band === "LOW_MID" ? "LOW_MID_SHAPE" : band === "HIGH_MID" ? "HIGH_MID_SHAPE" : null;
    return band + "_" + (attr === "freq" ? "FREQ" : "GAIN");
  }

  /** Returns [{control, param, score}] for EQ-looking params. */
  function eqCandidates(params) {
    const out = [];

    // 1) Numbered bands (FabFilter Pro-Q "Band 3 Frequency", many others).
    const numbered = new Map(); // band number -> [{param, attr}]
    params.forEach((p) => {
      const m = /\bband\s*(\d+)\b/i.exec(p.name);
      if (!m) return;
      const attr = attrOf(p.name.replace(m[0], ""));
      if (!attr) return;
      const n = parseInt(m[1], 10);
      if (!numbered.has(n)) numbered.set(n, []);
      numbered.get(n).push({ param: p, attr });
    });
    const bandNums = [...numbered.keys()].sort((a, b) => a - b);
    const order = bandNums.length >= 4 ? ["LOW", "LOW_MID", "HIGH_MID", "HIGH"]
      : bandNums.length === 3 ? ["LOW", "LOW_MID", "HIGH"]
      : bandNums.length === 2 ? ["LOW", "HIGH"]
      : ["LOW_MID"];
    bandNums.slice(0, order.length).forEach((n, i) => {
      numbered.get(n).forEach(({ param, attr }) => {
        const control = eqControl(order[i], attr);
        if (control) out.push({ control, param, score: 9 });
      });
    });

    // 2) Named bands (SSL / Neve / API style "LMF Gain", "HF Freq", "Low Mid Q").
    params.forEach((p) => {
      if (/\bband\s*\d+\b/i.test(p.name) || FILTER_WORDS.test(p.name)) return;
      const attr = attrOf(p.name);
      if (!attr) return;
      for (const [band, re] of NAMED_BAND) {
        if (re.test(p.name)) {
          const control = eqControl(band, attr);
          if (control) out.push({ control, param: p, score: 10 });
          break; // mid patterns are checked before low/high on purpose
        }
      }
    });
    return out;
  }

  // ---------------- main entry ----------------

  /**
   * params: [{index, id, name, discrete, steps}] (what c1ListParams returns)
   * section: "all" | "input" | "shape" | "eq" | "comp" | "drive" | "output"
   * slot: rack slot index the bindings are for
   * Returns { suggestions: [{cc, control, controlLabel, paramIndex, paramId, paramName, score, binding}],
   *           buttons: [binding] }   (section on/off button -> slot bypass)
   */
  function suggest(params, section, slot) {
    section = section || "all";
    const usable = (params || []).filter((p) => p && p.name && !IGNORE.test(p.name.trim()));

    let candidates = [];
    usable.forEach((p) => {
      RULES.forEach(([control, re, score]) => {
        if (re.test(p.name)) candidates.push({ control, param: p, score });
      });
    });
    candidates = candidates.concat(eqCandidates(usable));

    // Don't let generic rules steal EQ band params ("Band 2 Gain" isn't Output Volume).
    candidates = candidates.filter((c) => {
      const isBandParam = /\bband\s*\d+\b/i.test(c.param.name)
        || (!FILTER_WORDS.test(c.param.name) && NAMED_BAND.some(([, re]) => re.test(c.param.name)) && attrOf(c.param.name));
      return !isBandParam || SECTION_OF[c.control] === "eq";
    });

    if (section !== "all") candidates = candidates.filter((c) => SECTION_OF[c.control] === section);

    // Stepped params only make sense on knobs if they have a handful of steps or more.
    candidates = candidates.filter((c) => !(c.param.discrete && c.param.steps > 0 && c.param.steps < 3));

    // Greedy: best score first, each control and each param used once.
    candidates.sort((a, b) => b.score - a.score || a.param.index - b.param.index);
    const usedControl = new Set(), usedParam = new Set(), suggestions = [];
    candidates.forEach((c) => {
      if (usedControl.has(c.control) || usedParam.has(c.param.index)) return;
      usedControl.add(c.control);
      usedParam.add(c.param.index);
      suggestions.push({
        cc: CC[c.control],
        control: c.control,
        paramIndex: c.param.index,
        paramId: c.param.id || "",
        paramName: c.param.name,
        score: c.score,
        binding: { channel: 1, cc: CC[c.control], slot, target: "param", paramIndex: c.param.index, paramId: c.param.id || "", paramName: c.param.name },
      });
    });
    suggestions.sort((a, b) => a.cc - b.cc);

    const buttons = [];
    const sectionsUsed = new Set(suggestions.map((s) => SECTION_OF[s.control]));
    Object.keys(SECTION_BUTTON).forEach((sec) => {
      if ((section === "all" || section === sec) && sectionsUsed.has(sec)) {
        buttons.push({ channel: 1, cc: CC[SECTION_BUTTON[sec]], slot, target: "bypass", mode: "toggle" });
      }
    });
    return { suggestions, buttons };
  }

  const api = { suggest, CC, SECTION_OF };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  root.C1Automap = api;
})(typeof window !== "undefined" ? window : globalThis);
