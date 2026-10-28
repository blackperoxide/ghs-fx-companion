// Integration test: the real Console1::Engine driving fake hosted plugins.
// Build (Linux, from repo root; see docs/CONSOLE1.md):
//   tools/build_console1_engine_test.sh && /tmp/c1engine/test
#include "console1_fakes.h"
#include <cstdio>

static int passed = 0, failed = 0;
#define CHECK(cond, msg) do { if (cond) { ++passed; } else { ++failed; std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); } } while (0)

static float pv(FakePlugin& p, int i) { return p.getParameters()[i]->getValue(); }
static void pump(int ms = 60) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

int main()
{
    // Keep the test away from the real settings/profiles folder.
    {
        const juce::File tmpHome("/tmp/c1engine_home");
        tmpHome.deleteRecursively(); tmpHome.createDirectory();
        setenv("HOME", tmpHome.getFullPathName().toRawUTF8(), 1);
    }
    juce::ScopedJuceInitialiser_GUI init;

    std::array<std::unique_ptr<FakePlugin>, 8> slots;
    std::array<std::unique_ptr<juce::AudioParameterBool>, 8> bypass;
    for (int i = 0; i < 8; ++i) bypass[(size_t) i] = std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "byp" + juce::String(i), 1 }, "Bypass", false);

    slots[0] = std::make_unique<FakePlugin>("Pro-C 2", std::vector<std::pair<juce::String, juce::String>> {
        { "thresh", "Threshold" }, { "ratio", "Ratio" }, { "attack", "Attack" }, { "type", "Style" } });
    slots[2] = std::make_unique<FakePlugin>("1176", std::vector<std::pair<juce::String, juce::String>> {
        { "in", "Input" }, { "out", "Output" } });

    Console1::Engine::Host host;
    host.numSlots = 8;
    host.pluginInSlot = [&](int s) -> juce::AudioPluginInstance* { return juce::isPositiveAndBelow(s, 8) ? slots[(size_t) s].get() : nullptr; };
    host.bypassForSlot = [&](int s) -> juce::AudioParameterBool* { return juce::isPositiveAndBelow(s, 8) ? bypass[(size_t) s].get() : nullptr; };

    auto mk = [](int cc, int slot, int idx, const char* mode = nullptr, bool byp = false)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("cc", cc); o->setProperty("slot", slot); o->setProperty("channel", 1);
        o->setProperty("target", byp ? "bypass" : "param"); o->setProperty("paramIndex", idx);
        if (mode) o->setProperty("mode", mode);
        return juce::var(o);
    };

    {
        Console1::Engine eng(host);
        eng.takeControl();
        CHECK(eng.isOwner(), "engine owns the hub after takeControl");

        // absolute knob
        int id = eng.addBinding(mk(47, 0, 0));
        CHECK(id > 0, "addBinding returns id");
        eng.inject(1, 47, 127);
        CHECK(std::abs(pv(*slots[0], 0) - 1.0f) < 1e-4f, "absolute 127 -> 1.0");
        eng.inject(1, 47, 0);
        CHECK(pv(*slots[0], 0) < 1e-4f, "absolute 0 -> 0.0");
        eng.inject(1, 47, 64);
        CHECK(std::abs(pv(*slots[0], 0) - 64.0f / 127.0f) < 1e-3f, "absolute 64 -> ~0.504");

        // binding resolved name/id
        auto st = eng.toVar();
        auto b0 = (*st["bindings"].getArray())[0];
        CHECK(b0["paramName"].toString() == "Threshold", "paramName filled from plugin");
        CHECK(b0["paramId"].toString() == "thresh", "paramId filled from plugin");
        CHECK(b0["mode"].toString() == "absolute", "knob defaults to absolute");

        // range + invert
        auto upd = new juce::DynamicObject(); upd->setProperty("min", 0.25); upd->setProperty("max", 0.75); upd->setProperty("invert", true);
        CHECK(eng.updateBinding(id, juce::var(upd)), "updateBinding ok");
        eng.inject(1, 47, 127);
        CHECK(std::abs(pv(*slots[0], 0) - 0.25f) < 1e-3f, "inverted range: 127 -> min 0.25");
        eng.inject(1, 47, 0);
        CHECK(std::abs(pv(*slots[0], 0) - 0.75f) < 1e-3f, "inverted range: 0 -> max 0.75");

        // stepped param snaps
        eng.addBinding(mk(49, 0, 3));
        eng.inject(1, 49, 50);
        float s = pv(*slots[0], 3);
        CHECK(std::abs(s * 3.0f - std::round(s * 3.0f)) < 1e-4f, "4-step param snapped to a step");

        // bypass toggle (section button)
        eng.addBinding(mk(46, 0, -1, nullptr, true));
        CHECK(!bypass[0]->get(), "slot 1 starts active");
        eng.inject(1, 46, 127); eng.inject(1, 46, 0);
        CHECK(bypass[0]->get(), "COMP button press toggles slot bypass on");
        eng.inject(1, 46, 127); eng.inject(1, 46, 0);
        CHECK(!bypass[0]->get(), "second press toggles it back");

        // winner rule: same CC bound in slot 1 and slot 3; focus picks
        eng.addBinding(mk(51, 0, 2));      // Attack in Pro-C
        eng.addBinding(mk(51, 2, 0));      // Input in 1176
        slots[2]->getParameters()[0]->setValue(0.5f);
        eng.inject(1, 51, 127);
        CHECK(std::abs(pv(*slots[0], 2) - 1.0f) < 1e-4f && std::abs(pv(*slots[2], 0) - 0.5f) < 1e-4f, "no focus on slot 3: lowest slot (1) wins");
        eng.inject(1, 21 + 2, 127); eng.inject(1, 21 + 2, 0); // track button 3
        CHECK(eng.getFocusSlot() == 2, "track button 3 focuses slot 3");
        eng.inject(1, 51, 0);
        CHECK(pv(*slots[2], 0) < 1e-4f && std::abs(pv(*slots[0], 2) - 1.0f) < 1e-4f, "focused slot 3 wins, slot 1 untouched");

        // page up/down skip empty slots
        eng.inject(1, 97, 127); eng.inject(1, 97, 0);
        CHECK(eng.getFocusSlot() == 0, "page down from 3 wraps to slot 1 (only 1 and 3 loaded)");
        eng.inject(1, 96, 127);
        CHECK(eng.getFocusSlot() == 2, "page up from 1 wraps to slot 3");
        eng.setFocusSlot(0);

        // macro: two bindings, same CC, same slot
        eng.addBinding(mk(83, 0, 1));
        eng.addBinding(mk(83, 0, 2));
        eng.inject(1, 83, 0);
        CHECK(pv(*slots[0], 1) < 1e-4f && pv(*slots[0], 2) < 1e-4f, "macro: both params move");

        // pickup: no jump until the knob crosses the value
        auto pk = eng.addBinding(mk(85, 0, 1, "pickup"));
        juce::ignoreUnused(pk);
        eng.removeBinding(-999);
        slots[0]->getParameters()[1]->setValue(0.5f);
        eng.slotStateRestored(0);
        // remove the macro binding on 83 that also targets param 1? it's a different CC, fine.
        eng.inject(1, 85, 10);
        CHECK(std::abs(pv(*slots[0], 1) - 0.5f) < 1e-4f, "pickup: far away -> no jump");
        eng.inject(1, 85, 40);
        CHECK(std::abs(pv(*slots[0], 1) - 0.5f) < 1e-4f, "pickup: still below -> no jump");
        eng.inject(1, 85, 70);
        CHECK(std::abs(pv(*slots[0], 1) - 70.0f / 127.0f) < 1e-3f, "pickup: crossed -> follows");
        eng.inject(1, 85, 20);
        CHECK(std::abs(pv(*slots[0], 1) - 20.0f / 127.0f) < 1e-3f, "pickup: stays picked up");

        // relative (fine)
        eng.addBinding(mk(86, 0, 0, "relative"));
        slots[0]->getParameters()[0]->setValue(0.5f);
        eng.inject(1, 86, 60);   // first message: establishes position
        float r0 = pv(*slots[0], 0);
        eng.inject(1, 86, 63);
        float r1 = pv(*slots[0], 0);
        CHECK(r1 > r0 && (r1 - r0) < 3.0f / 127.0f, "relative: +3 moves up by less than absolute would");

        // learn
        eng.armLearn(2, 1, false);
        eng.inject(1, 7, 100);
        auto st2 = eng.toVar();
        bool found = false;
        for (auto& b : *st2["bindings"].getArray())
            if ((int) b["cc"] == 7 && (int) b["slot"] == 2 && b["paramName"].toString() == "Output") found = true;
        CHECK(found, "learn binds the next CC to the armed target");
        eng.inject(1, 7, 127);
        CHECK(std::abs(pv(*slots[2], 1) - 1.0f) < 1e-4f, "learned binding works right away");

        // learn ignores focus buttons
        eng.armLearn(0, 0, false);
        eng.inject(1, 22, 127);
        CHECK(eng.getFocusSlot() == 1, "learn armed: track button still focuses");
        eng.cancelLearn();
        eng.setFocusSlot(0);

        // host MIDI path: consume bound CCs, keep the rest
        eng.setOption("source", "both");
        juce::MidiBuffer mb;
        mb.addEvent(juce::MidiMessage::controllerEvent(1, 47, 127), 0);
        mb.addEvent(juce::MidiMessage::controllerEvent(1, 1, 64), 1);    // mod wheel, unbound
        mb.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 2);
        eng.processHostMidi(mb);
        CHECK(mb.getNumEvents() == 2, "bound CC stripped from host MIDI, others kept");
        pump(80);
        CHECK(std::abs(pv(*slots[0], 0) - 0.25f) < 1e-3f, "host CC applied by the timer (inverted range 127 -> 0.25)");

        // device path via FIFO
        eng.pushDeviceEvent(1, 47, 0);
        pump(80);
        CHECK(std::abs(pv(*slots[0], 0) - 0.75f) < 1e-3f, "device CC applied by the timer");

        // source = host ignores device events
        eng.setOption("source", "host");
        eng.pushDeviceEvent(1, 47, 127);
        pump(80);
        CHECK(std::abs(pv(*slots[0], 0) - 0.75f) < 1e-3f, "source=host ignores device CCs");
        eng.setOption("source", "device");
        juce::MidiBuffer mb2; mb2.addEvent(juce::MidiMessage::controllerEvent(1, 47, 127), 0);
        eng.processHostMidi(mb2);
        CHECK(mb2.getNumEvents() == 1, "source=device leaves host MIDI alone");

        // swap slots
        eng.slotsSwapped(0, 2);
        std::swap(slots[0], slots[2]); std::swap(bypass[0], bypass[2]);
        eng.inject(1, 7, 0);
        CHECK(pv(*slots[0], 1) < 1e-4f, "after swap, 1176 binding followed it to slot 1");
        eng.slotsSwapped(0, 2);
        std::swap(slots[0], slots[2]); std::swap(bypass[0], bypass[2]);

        // profiles: save, unload, reload
        auto saved = eng.saveProfileForSlot(0);
        CHECK((bool) saved["ok"], "profile saved");
        int count = (int) saved["count"];
        CHECK(Console1::Engine::profilesFolder().getChildFile("TestCo - Pro-C 2.json").existsAsFile(), "profile file named Manufacturer - Name");
        eng.slotUnloaded(0);
        auto afterUnload = eng.toVar(); int left = 0; for (auto& b : *afterUnload["bindings"].getArray()) if ((int) b["slot"] == 0) ++left;
        CHECK(left == 0, "unload drops slot bindings");
        eng.slotLoaded(0); // same plugin comes back
        auto afterLoad = eng.toVar(); int back = 0; for (auto& b : *afterLoad["bindings"].getArray()) if ((int) b["slot"] == 0) ++back;
        CHECK(back == count, "profile auto-loads on slotLoaded");
        eng.saveProfileForSlot(0);
        CHECK(Console1::Engine::profilesFolder().getChildFile("TestCo - Pro-C 2.previous.json").existsAsFile(), "second save keeps a .previous.json backup");

        // id/name re-resolve after the plugin's params get reordered
        slots[1] = std::make_unique<FakePlugin>("Pro-C 2", std::vector<std::pair<juce::String, juce::String>> {
            { "ratio", "Ratio" }, { "thresh", "Threshold" }, { "attack", "Attack" }, { "type", "Style" } });
        eng.slotLoaded(1);
        eng.setFocusSlot(1);
        eng.inject(1, 47, 127); // inverted 0.25..0.75 binding to "thresh"
        CHECK(std::abs(slots[1]->getParameters()[1]->getValue() - 0.25f) < 1e-3f, "reordered params: binding found Threshold by id");
        eng.setFocusSlot(0);

        // session round trip (binding ids are renumbered on load by design; compare everything else)
        auto stripIds = [](juce::var v)
        {
            auto copy = juce::JSON::parse(juce::JSON::toString(v));
            for (auto& b : *copy["bindings"].getArray()) b.getDynamicObject()->removeProperty("id");
            return juce::JSON::toString(copy);
        };
        auto snap = eng.toVar();
        Console1::Engine eng2(host);
        eng2.fromVar(juce::JSON::parse(juce::JSON::toString(snap)));
        CHECK(stripIds(eng2.toVar()) == stripIds(snap), "toVar/fromVar round trip keeps every binding and option");

        // state/list functions don't crash and look sane
        auto full = eng.getState();
        CHECK(full.isObject(), "getState returns an object");
        auto live = eng.getLiveState();
        CHECK(live.isObject(), "getLiveState returns an object");
        auto params = eng.listParams(0);
        CHECK(params.isArray() || params.isObject(), "listParams returns data");
        CHECK(eng.listProfiles().size() >= 1, "listProfiles finds the saved profile");

        // UI bridge dispatcher (shared with the plugin editor)
        {
            juce::Array<juce::var> list { mk(102, 2, 0), mk(103, 2, 1) };
            auto r = eng.invoke("c1Bind", { juce::var(list) });
            int n = 0; for (auto& b : *r["bindings"].getArray()) if ((int) b["cc"] == 102 || (int) b["cc"] == 103) ++n;
            CHECK(n == 2, "invoke c1Bind with an array binds all of them");
            CHECK(r["slots"].getArray()->size() == 8, "invoke returns full state with 8 slots");
            CHECK(eng.invoke("c1SetFocus", { 2 })["focusSlot"].operator int() == 2, "invoke c1SetFocus");
            CHECK(eng.invoke("nope", {}).isVoid(), "unknown function returns nothing");
            CHECK(Console1::Engine::bridgeFunctionNames().size() == 16, "16 bridge functions");
            eng.setFocusSlot(0);
        }

        // empty slot binding is harmless
        eng.addBinding(mk(88, 5, 0));
        eng.inject(1, 88, 127);
        CHECK(true, "binding on empty slot does not crash");
    }

    std::printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
