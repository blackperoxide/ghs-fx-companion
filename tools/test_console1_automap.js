// node tools/test_console1_automap.js
// Parameter names below are typical of each plugin family; exact names on
// Shawn's installed versions still need checking against c1ListParams output.
"use strict";
const { suggest, CC } = require("../Resources/webui/console1-automap.js");

let pass = 0, fail = 0;
function check(cond, label) {
  if (cond) pass++;
  else { fail++; console.log("FAIL:", label); }
}
const P = (names) => names.map((name, index) => ({ index, id: "p" + index, name, discrete: false, steps: 0x7fffffff }));
const mapOf = (r) => Object.fromEntries(r.suggestions.map((s) => [s.control, s.paramName]));

// FabFilter Pro-Q style: many numbered bands
{
  const names = ["Output Gain", "Output Pan"];
  for (let b = 1; b <= 6; b++) names.push(`Band ${b} Used`, `Band ${b} Enabled`, `Band ${b} Frequency`, `Band ${b} Gain`, `Band ${b} Q`, `Band ${b} Shape`);
  const m = mapOf(suggest(P(names), "eq", 0));
  check(m.LOW_FREQ === "Band 1 Frequency", "proq low freq");
  check(m.LOW_GAIN === "Band 1 Gain", "proq low gain");
  check(m.LOW_MID_SHAPE === "Band 2 Q", "proq lowmid q");
  check(m.HIGH_MID_FREQ === "Band 3 Frequency", "proq highmid freq");
  check(m.HIGH_GAIN === "Band 4 Gain", "proq high gain");
  check(!Object.values(m).some((n) => /Band [56]/.test(n)), "proq ignores bands 5+");
  const all = mapOf(suggest(P(names), "all", 0));
  check(all.VOLUME === "Output Gain", "proq output -> volume");
  check(all.PAN === "Output Pan", "proq pan");
}

// FabFilter Pro-C style compressor
{
  const r = suggest(P(["Style", "Threshold", "Ratio", "Knee", "Range", "Attack", "Release", "Auto Release", "Lookahead", "Hold", "Output Level", "Dry Gain", "Wet Gain", "Mix", "MIDI CC 0|0"]), "comp", 1);
  const m = mapOf(r);
  check(m.COMP_THRESH === "Threshold", "proc thresh");
  check(m.COMP_RATIO === "Ratio", "proc ratio");
  check(m.COMP_ATTACK === "Attack", "proc attack");
  check(m.COMP_RELEASE === "Release", "proc release");
  check(m.COMP_PAR === "Mix", "proc mix -> parallel");
  check(r.buttons.length === 1 && r.buttons[0].cc === CC.COMP && r.buttons[0].target === "bypass", "comp on button -> bypass");
  check(r.suggestions.every((s) => s.binding.slot === 1), "slot carried into bindings");
}

// UAD 1176 style: no threshold, input drives it
{
  const comp = mapOf(suggest(P(["Input", "Output", "Attack", "Release", "Ratio", "Meter", "Mix"]), "comp", 0));
  check(comp.COMP_THRESH === "Input", "1176 input -> threshold in comp section");
  check(comp.COMP_RATIO === "Ratio", "1176 ratio");
  const all = mapOf(suggest(P(["Input", "Output", "Attack", "Release", "Ratio", "Meter", "Mix"]), "all", 0));
  check(all.GAIN === "Input" && all.VOLUME === "Output", "1176 all: input/output section");
}

// SSL-style channel strip with named bands, filters, gate and comp
{
  const names = ["HF Gain", "HF Freq", "HMF Gain", "HMF Freq", "HMF Q", "LMF Gain", "LMF Freq", "LMF Q", "LF Gain", "LF Freq",
    "HP Freq", "LP Freq", "Comp Ratio", "Comp Threshold", "Comp Release", "Comp Attack", "Gate Threshold", "Gate Range", "Gate Release", "Output", "Input"];
  const m = mapOf(suggest(P(names), "all", 2));
  check(m.HIGH_GAIN === "HF Gain" && m.HIGH_FREQ === "HF Freq", "ssl hf");
  check(m.HIGH_MID_SHAPE === "HMF Q", "ssl hmf q");
  check(m.LOW_MID_FREQ === "LMF Freq", "ssl lmf freq");
  check(m.LOW_GAIN === "LF Gain", "ssl lf gain");
  check(m.LOW_CUT === "HP Freq", "ssl hp -> low cut");
  check(m.HIGH_CUT === "LP Freq", "ssl lp -> high cut");
  check(m.COMP_THRESH === "Comp Threshold", "ssl comp thresh not gate");
  check(m.SHAPE_GATE === "Gate Threshold", "ssl gate thresh");
  check(m.SHAPE_RELEASE === "Gate Release", "ssl gate release");
  check(m.COMP_RELEASE === "Comp Release", "ssl comp release");
  check(m.GAIN === "Input" && m.VOLUME === "Output", "ssl in/out");
}

// Saturation (Decapitator-ish)
{
  const m = mapOf(suggest(P(["Drive", "Style", "Tone", "Low Cut", "High Cut", "Mix", "Output", "Punish"]), "all", 0));
  check(m.DRIVE === "Drive", "sat drive");
  check(m.CHARACTER === "Style", "sat style -> character");
  check(m.LOW_CUT === "Low Cut" && m.HIGH_CUT === "High Cut", "sat filters");
}

// Each control and each param used at most once
{
  const r = suggest(P(["Threshold", "Threshold 2", "Ratio", "Attack", "Release"]), "all", 0);
  const ccs = r.suggestions.map((s) => s.cc), idx = r.suggestions.map((s) => s.paramIndex);
  check(new Set(ccs).size === ccs.length && new Set(idx).size === idx.length, "no duplicates");
}

// Empty / junk input
check(suggest([], "all", 0).suggestions.length === 0, "empty");
check(suggest(P(["MIDI CC 0|1", "Bypass", "Program"]), "all", 0).suggestions.length === 0, "junk ignored");

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
