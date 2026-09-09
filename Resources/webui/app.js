"use strict";

/* ============================================================
   Native bridge - hand-rolled low-level JUCE protocol (no build
   step / ES module bundler needed). Mirrors what JUCE's own
   frontend helper (native/javascript/index.js) does internally:
   calling a native function emits "__juce__invoke" with a result
   id, and the backend replies via a "__juce__complete" event.
   ============================================================ */

let nextResultId = 0;
const pendingCalls = new Map();

function ensureBridgeReady() {
  if (typeof window.__JUCE__ === "undefined" || !window.__JUCE__.backend) {
    console.warn("window.__JUCE__.backend is not available - running outside the plugin?");
    return false;
  }
  return true;
}

if (ensureBridgeReady()) {
  window.__JUCE__.backend.addEventListener("__juce__complete", ({ promiseId, result }) => {
    const resolve = pendingCalls.get(promiseId);
    if (resolve) {
      pendingCalls.delete(promiseId);
      resolve(result);
    }
  });
}

function callNative(name, ...params) {
  return new Promise((resolve) => {
    if (!ensureBridgeReady()) {
      resolve(null);
      return;
    }
    const resultId = nextResultId++;
    pendingCalls.set(resultId, resolve);
    window.__JUCE__.backend.emitEvent("__juce__invoke", { name, params, resultId });
  });
}

function onNative(eventId, handler) {
  if (!ensureBridgeReady()) return;
  window.__JUCE__.backend.addEventListener(eventId, handler);
}

/* ============================================================
   Toasts
   ============================================================ */

function showToast(text, tone) {
  const root = document.getElementById("toasts");
  const el = document.createElement("div");
  el.className = "toast";
  if (tone === "error") el.style.borderColor = "rgba(226,86,79,0.5)";
  if (tone === "success") el.style.borderColor = "rgba(94,200,184,0.5)";
  el.textContent = text;
  root.appendChild(el);
  setTimeout(() => {
    el.classList.add("leaving");
    setTimeout(() => el.remove(), 220);
  }, 3600);
}

onNative("toast", (payload) => showToast(payload.text, payload.tone));

/* ============================================================
   Modal helper (preset naming, confirmations)
   ============================================================ */

function openModal({ title, bodyEl, actions }) {
  const backdrop = document.createElement("div");
  backdrop.className = "modal-backdrop";

  const modal = document.createElement("div");
  modal.className = "modal";

  const head = document.createElement("div");
  head.className = "modal-head";
  const h3 = document.createElement("h3");
  h3.textContent = title;
  head.appendChild(h3);

  const body = document.createElement("div");
  body.className = "modal-body";
  body.appendChild(bodyEl);

  const foot = document.createElement("div");
  foot.className = "modal-foot";

  const close = () => backdrop.remove();

  for (const action of actions) {
    const btn = document.createElement("button");
    btn.className = "btn " + (action.className || "btn-ghost");
    btn.textContent = action.label;
    btn.onclick = () => {
      if (action.onClick) action.onClick(close);
      else close();
    };
    foot.appendChild(btn);
  }

  modal.appendChild(head);
  modal.appendChild(body);
  modal.appendChild(foot);
  backdrop.appendChild(modal);
  backdrop.addEventListener("click", (e) => { if (e.target === backdrop) close(); });

  document.getElementById("modalRoot").appendChild(backdrop);
  return { close, modal };
}

function promptModal(title, placeholder, onSubmit) {
  const input = document.createElement("input");
  input.className = "modal-input";
  input.type = "text";
  input.placeholder = placeholder || "";

  const { close } = openModal({
    title,
    bodyEl: input,
    actions: [
      { label: "Cancel" },
      {
        label: "Save",
        className: "btn-primary",
        onClick: (closeFn) => {
          const value = input.value.trim();
          if (value) onSubmit(value);
          closeFn();
        },
      },
    ],
  });

  setTimeout(() => input.focus(), 30);
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      const value = input.value.trim();
      if (value) onSubmit(value);
      close();
    } else if (e.key === "Escape") {
      close();
    }
  });
}

/* ============================================================
   App state
   ============================================================ */

const state = {
  slots: [],
  presets: [],
  scanCount: 0,
  scanAgo: "",
  capturing: false,
  captureSeconds: 0,
  searchResults: [],
  justLoadedSlot: -1,
};

async function refreshState() {
  const s = await callNative("getState");
  if (!s) return;
  Object.assign(state, s);
  renderSlots();
  renderPresets();
  renderScanStatus();
}

/* ============================================================
   Plugin browser
   ============================================================ */

const searchBox = document.getElementById("searchBox");
const pluginListEl = document.getElementById("pluginList");

let searchDebounce = null;
searchBox.addEventListener("input", () => {
  clearTimeout(searchDebounce);
  searchDebounce = setTimeout(runSearch, 120);
});

async function runSearch() {
  const query = searchBox.value.trim();
  const results = await callNative("searchPlugins", query);
  state.searchResults = results || [];
  renderPluginList();
}

function renderPluginList() {
  pluginListEl.innerHTML = "";

  if (state.searchResults.length === 0) {
    const hint = document.createElement("div");
    hint.className = "empty-hint";
    hint.textContent = state.scanCount === 0
      ? "No plugin list yet. Run the GHS FX Companion Scanner outside your DAW once, then click “Refresh Scan”."
      : "No plugins match that search.";
    pluginListEl.appendChild(hint);
    return;
  }

  for (const p of state.searchResults) {
    const card = document.createElement("div");
    card.className = "plugin-card";
    card.draggable = true;
    card.dataset.identifier = p.identifier;

    const name = document.createElement("div");
    name.className = "name";
    name.textContent = p.name;

    const meta = document.createElement("div");
    meta.className = "meta";

    const fmt = document.createElement("span");
    fmt.className = "pill";
    fmt.textContent = p.format;
    meta.appendChild(fmt);

    if (p.manufacturer) {
      const mf = document.createElement("span");
      mf.className = "pill";
      mf.textContent = p.manufacturer;
      meta.appendChild(mf);
    }

    if (p.category) {
      const cat = document.createElement("span");
      cat.className = "pill pill-accent";
      cat.textContent = p.category;
      meta.appendChild(cat);
    }

    card.appendChild(name);
    card.appendChild(meta);

    card.addEventListener("dragstart", (e) => {
      card.classList.add("dragging");
      e.dataTransfer.setData("application/x-ghs-plugin", p.identifier);
      e.dataTransfer.effectAllowed = "copy";
    });
    card.addEventListener("dragend", () => card.classList.remove("dragging"));

    // Double-click as a no-drag-required path into the first empty slot.
    card.addEventListener("dblclick", () => loadIntoFirstEmptySlot(p.identifier));

    pluginListEl.appendChild(card);
  }
}

async function loadIntoFirstEmptySlot(identifier) {
  const target = state.slots.find((s) => !s.name);
  if (!target) {
    showToast("Every slot is full - remove one first.", "error");
    return;
  }
  await loadPluginIntoSlot(target.index, identifier);
}

/* ============================================================
   Rack
   ============================================================ */

const slotListEl = document.getElementById("slotList");
let dragSlotIndex = -1;

function renderSlots() {
  slotListEl.innerHTML = "";

  for (const slot of state.slots) {
    const card = document.createElement("div");
    card.className = "slot-card" + (slot.name ? " filled" : "") + (slot.bypassed ? " bypassed" : "");
    if (slot.index === state.justLoadedSlot) card.classList.add("just-loaded");
    card.dataset.index = String(slot.index);

    const handle = document.createElement("span");
    handle.className = "drag-handle";
    handle.textContent = "≡";
    handle.draggable = true;

    const idx = document.createElement("div");
    idx.className = "slot-index";
    idx.textContent = String(slot.index + 1);

    const body = document.createElement("div");
    body.className = "slot-body";
    const name = document.createElement("div");
    name.className = "slot-name" + (slot.name ? "" : " empty");
    name.textContent = slot.name || "Empty - drag a plugin here";
    body.appendChild(name);
    if (slot.name) {
      const fmt = document.createElement("div");
      fmt.className = "slot-format";
      fmt.textContent = slot.format || "";
      body.appendChild(fmt);
    }

    const actions = document.createElement("div");
    actions.className = "slot-actions";

    const switchLabel = document.createElement("label");
    switchLabel.className = "switch";
    switchLabel.title = "Bypass this slot";
    const switchInput = document.createElement("input");
    switchInput.type = "checkbox";
    switchInput.checked = !!slot.bypassed;
    switchInput.disabled = !slot.name;
    switchInput.addEventListener("change", () => toggleBypass(slot.index));
    const switchTrack = document.createElement("span");
    switchTrack.className = "switch-track";
    switchLabel.appendChild(switchInput);
    switchLabel.appendChild(switchTrack);

    const editBtn = iconButton("✎", "Open plugin editor", !slot.name, () => callNative("openHostedEditor", slot.index));
    const removeBtn = iconButton("✕", "Remove", !slot.name, () => unloadSlot(slot.index), true);

    actions.appendChild(switchLabel);
    actions.appendChild(editBtn);
    actions.appendChild(removeBtn);

    card.appendChild(handle);
    card.appendChild(idx);
    card.appendChild(body);
    card.appendChild(actions);

    // Drop target: accepts either a plugin from the browser (copy) or another slot (reorder).
    card.addEventListener("dragover", (e) => {
      e.preventDefault();
      card.classList.add("drag-over");
    });
    card.addEventListener("dragleave", () => card.classList.remove("drag-over"));
    card.addEventListener("drop", async (e) => {
      e.preventDefault();
      card.classList.remove("drag-over");

      const pluginId = e.dataTransfer.getData("application/x-ghs-plugin");
      if (pluginId) {
        await loadPluginIntoSlot(slot.index, pluginId);
        return;
      }
      if (dragSlotIndex >= 0 && dragSlotIndex !== slot.index) {
        await moveSlot(dragSlotIndex, slot.index);
      }
    });

    handle.addEventListener("dragstart", (e) => {
      dragSlotIndex = slot.index;
      e.dataTransfer.effectAllowed = "move";
      e.dataTransfer.setData("application/x-ghs-slot", String(slot.index));
    });
    handle.addEventListener("dragend", () => { dragSlotIndex = -1; });

    slotListEl.appendChild(card);
  }

  if (state.justLoadedSlot >= 0) {
    const flashedSlot = state.justLoadedSlot;
    state.justLoadedSlot = -1;
    setTimeout(() => {
      // no-op timer just to let the flash animation play once per load
    }, 900);
    void flashedSlot;
  }
}

function iconButton(glyph, title, disabled, onClick, danger) {
  const btn = document.createElement("button");
  btn.className = "icon-btn" + (danger ? " danger" : "");
  btn.textContent = glyph;
  btn.title = title;
  btn.disabled = disabled;
  btn.onclick = onClick;
  return btn;
}

async function loadPluginIntoSlot(slotIndex, identifier) {
  const result = await callNative("loadPluginIntoSlot", slotIndex, identifier);
  if (result && result.success) {
    state.justLoadedSlot = slotIndex;
    showToast("Loaded into slot " + (slotIndex + 1) + ".", "success");
  } else {
    showToast((result && result.error) || "Failed to load plugin.", "error");
  }
  await refreshState();
}

async function unloadSlot(slotIndex) {
  await callNative("unloadSlot", slotIndex);
  await refreshState();
}

async function moveSlot(from, to) {
  await callNative("moveSlot", from, to);
  await refreshState();
}

async function toggleBypass(slotIndex) {
  await callNative("toggleBypass", slotIndex);
  await refreshState();
}

/* ============================================================
   Presets
   ============================================================ */

const presetSelect = document.getElementById("presetSelect");

function renderPresets() {
  const previousValue = presetSelect.value;
  presetSelect.innerHTML = "";

  const placeholder = document.createElement("option");
  placeholder.value = "";
  placeholder.textContent = "Load preset...";
  presetSelect.appendChild(placeholder);

  for (const name of state.presets) {
    const opt = document.createElement("option");
    opt.value = name;
    opt.textContent = name;
    presetSelect.appendChild(opt);
  }

  if (state.presets.includes(previousValue)) presetSelect.value = previousValue;
}

presetSelect.addEventListener("change", async () => {
  const name = presetSelect.value;
  if (!name) return;
  showToast("Loading preset “" + name + "”...", "info");
  await callNative("loadPreset", name);
  await refreshState();
  showToast("Loaded preset “" + name + "”.", "success");
});

document.getElementById("savePresetBtn").addEventListener("click", () => {
  promptModal("Save Preset", "Preset name", async (name) => {
    const result = await callNative("savePreset", name);
    if (result && result.success) {
      showToast("Saved preset “" + name + "”.", "success");
      await refreshState();
    } else {
      showToast("Failed to save preset.", "error");
    }
  });
});

document.getElementById("deletePresetBtn").addEventListener("click", async () => {
  const name = presetSelect.value;
  if (!name) {
    showToast("Select a preset from the dropdown first.", "error");
    return;
  }
  const result = await callNative("deletePreset", name);
  if (result && result.success) {
    showToast("Deleted preset “" + name + "”.", "success");
    await refreshState();
  } else {
    showToast("Failed to delete preset.", "error");
  }
});

document.getElementById("importRecipeBtn").addEventListener("click", () => {
  callNative("importRecipe");
});

onNative("recipeImported", async ({ matchedCount, unmatchedNames }) => {
  await refreshState();
  if (matchedCount > 0) {
    let msg = "Recipe imported - " + matchedCount + " plugin(s) loaded.";
    if (unmatchedNames && unmatchedNames.length > 0)
      msg += " Not in your library: " + unmatchedNames.join(", ") + ".";
    showToast(msg, "success");
  } else {
    showToast("None of that recipe's plugins were found in your library.", "error");
  }
});

/* ============================================================
   Scan status
   ============================================================ */

function renderScanStatus() {
  const el = document.getElementById("scanStatus");
  el.textContent = state.scanCount > 0
    ? state.scanCount + " plugins" + (state.scanAgo ? " • scanned " + state.scanAgo + " ago" : "")
    : "no scan yet";
}

document.getElementById("scanBtn").addEventListener("click", async () => {
  const result = await callNative("refreshPluginScan");
  showToast((result && result.count) ? "Found " + result.count + " plugin(s)." : "No plugins found - run the scanner first.",
             (result && result.count) ? "success" : "error");
  await refreshState();
  await runSearch();
});

/* ============================================================
   Record & Suggest
   ============================================================ */

const recordBtn = document.getElementById("recordBtn");
const recordBtnLabel = document.getElementById("recordBtnLabel");

recordBtn.addEventListener("click", async () => {
  if (recordBtn.classList.contains("busy")) return;

  if (state.capturing) {
    recordBtn.classList.add("busy");
    recordBtn.classList.remove("recording");
    recordBtnLabel.textContent = "Analyzing...";
    const result = await callNative("stopToneCaptureAndAnalyze");
    recordBtn.classList.remove("busy");
    recordBtnLabel.textContent = "Record & Suggest";
    if (result && result.stages) showSuggestions(result);
  } else {
    await callNative("startToneCapture");
    state.capturing = true;
    recordBtn.classList.add("recording");
    recordBtnLabel.textContent = "Stop & Suggest (0.0s)";
  }
});

onNative("toneCaptureTick", ({ seconds }) => {
  if (!state.capturing) return;
  recordBtnLabel.textContent = "Stop & Suggest (" + seconds.toFixed(1) + "s)";
});

function showSuggestions(payload) {
  const wrap = document.createElement("div");

  const header = document.createElement("div");
  header.className = "suggest-header";
  header.innerHTML = "Closest of your 5 original vibes: <span class=\"vibe\">" + escapeHtml(payload.nearestVibe || "-") + "</span>";
  wrap.appendChild(header);

  if (!payload.stages || payload.stages.length === 0) {
    const hint = document.createElement("div");
    hint.className = "empty-hint";
    hint.textContent = "Couldn't get a usable read from that take - try a longer, louder phrase.";
    wrap.appendChild(hint);
  } else {
    const list = document.createElement("div");
    list.className = "suggest-list";

    for (const stage of payload.stages) {
      const card = document.createElement("div");
      const topOwned = stage.options.find((o) => o.owned);
      card.className = "suggest-card" + (topOwned ? " owned" : "");

      const nameEl = document.createElement("div");
      nameEl.className = "stage-name";
      nameEl.textContent = stage.name;
      card.appendChild(nameEl);

      const roleEl = document.createElement("div");
      roleEl.className = "stage-role";
      roleEl.textContent = stage.role;
      card.appendChild(roleEl);

      const pick = topOwned || stage.options[0];
      if (pick) {
        const pickRow = document.createElement("div");
        pickRow.className = "stage-pick";

        const pluginPick = document.createElement("div");
        pluginPick.className = "plugin-pick";
        pluginPick.innerHTML = "<span class=\"brand\">" + escapeHtml(pick.brand) + "</span> " + escapeHtml(pick.plugin)
          + (topOwned ? " <span class=\"pill pill-accent\">owned</span>" : " <span class=\"pill\">not in your library</span>");
        pickRow.appendChild(pluginPick);

        const loadBtn = document.createElement("button");
        loadBtn.className = "btn small " + (topOwned ? "btn-primary" : "btn-ghost");
        loadBtn.textContent = "Load";
        loadBtn.disabled = !topOwned || stage.targetSlotIndex === null || stage.targetSlotIndex === undefined;
        loadBtn.onclick = async () => {
          loadBtn.disabled = true;
          await loadPluginIntoSlot(stage.targetSlotIndex, topOwned.identifier);
        };
        pickRow.appendChild(loadBtn);

        card.appendChild(pickRow);

        if (pick.tip) {
          const tipEl = document.createElement("div");
          tipEl.className = "tip";
          tipEl.textContent = pick.tip;
          card.appendChild(tipEl);
        }
      }

      list.appendChild(card);
    }

    wrap.appendChild(list);
  }

  const hasAnyOwned = (payload.stages || []).some((s) => s.options.some((o) => o.owned));

  openModal({
    title: "Suggested Chain",
    bodyEl: wrap,
    actions: [
      { label: "Close" },
      {
        label: "Load All",
        className: "btn-primary",
        onClick: async (close) => {
          for (const stage of payload.stages || []) {
            const owned = stage.options.find((o) => o.owned);
            if (owned && stage.targetSlotIndex !== null && stage.targetSlotIndex !== undefined)
              await loadPluginIntoSlot(stage.targetSlotIndex, owned.identifier);
          }
          close();
        },
      },
    ].slice(hasAnyOwned ? 0 : 1),
  });
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;" }[c]));
}

/* ============================================================
   Boot
   ============================================================ */

(async function boot() {
  await refreshState();
  await runSearch();
})();
