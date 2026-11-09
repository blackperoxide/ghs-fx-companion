#include "Console1Engine.h"

namespace Console1
{
// ============================================================================
// Hardware map
// ============================================================================

const std::vector<ControlInfo>& controls()
{
    using K = ControlKind;
    // CC numbers from Ardour's Console 1 surface (libs/surfaces/console1/console1.h).
    // Labels follow the Mk I front panel. DRIVE (15) vs CHARACTER (18) naming is
    // the one to double-check on real hardware with the MIDI monitor.
    static const std::vector<ControlInfo> table {
        // Input
        { 107, "GAIN",                  "Input",         "input",  K::knob },
        { 103, "LOW_CUT",               "Low Cut",       "input",  K::knob },
        { 105, "HIGH_CUT",              "High Cut",      "input",  K::knob },
        {  61, "FILTER_TO_COMPRESSORS", "Filt to Comp",  "input",  K::button },
        { 108, "PHASE_INV",             "Phase",         "input",  K::button },
        {  17, "EXTERNAL_SIDECHAIN",    "Ext Sidechain", "input",  K::button },

        // Shape
        {  53, "SHAPE",                 "Shape On",      "shape",  K::button },
        {  59, "HARD_GATE",             "Hard Gate",     "shape",  K::button },
        {  54, "SHAPE_GATE",            "Gate",          "shape",  K::knob },
        {  56, "SHAPE_RELEASE",         "Gate Release",  "shape",  K::knob },
        {  55, "SHAPE_SUSTAIN",         "Sustain",       "shape",  K::knob },
        {  57, "SHAPE_PUNCH",           "Punch",         "shape",  K::knob },

        // EQ
        {  80, "EQ",                    "EQ On",         "eq",     K::button },
        {  82, "HIGH_GAIN",             "High Gain",     "eq",     K::knob },
        {  83, "HIGH_FREQ",             "High Freq",     "eq",     K::knob },
        {  65, "HIGH_SHAPE",            "High Shape",    "eq",     K::button },
        {  85, "HIGH_MID_GAIN",         "Hi-Mid Gain",   "eq",     K::knob },
        {  86, "HIGH_MID_FREQ",         "Hi-Mid Freq",   "eq",     K::knob },
        {  87, "HIGH_MID_SHAPE",        "Hi-Mid Q",      "eq",     K::knob },
        {  88, "LOW_MID_GAIN",          "Lo-Mid Gain",   "eq",     K::knob },
        {  89, "LOW_MID_FREQ",          "Lo-Mid Freq",   "eq",     K::knob },
        {  90, "LOW_MID_SHAPE",         "Lo-Mid Q",      "eq",     K::knob },
        {  91, "LOW_GAIN",              "Low Gain",      "eq",     K::knob },
        {  92, "LOW_FREQ",              "Low Freq",      "eq",     K::knob },
        {  93, "LOW_SHAPE",             "Low Shape",     "eq",     K::button },

        // Compressor
        {  46, "COMP",                  "Comp On",       "comp",   K::button },
        {  14, "ORDER",                 "Order",         "comp",   K::button },
        {  47, "COMP_THRESH",           "Threshold",     "comp",   K::knob },
        {  49, "COMP_RATIO",            "Ratio",         "comp",   K::knob },
        {  51, "COMP_ATTACK",           "Attack",        "comp",   K::knob },
        {  48, "COMP_RELEASE",          "Release",       "comp",   K::knob },
        {  50, "COMP_PAR",              "Parallel",      "comp",   K::knob },

        // Drive
        {  15, "DRIVE",                 "Drive",         "drive",  K::knob },
        {  18, "CHARACTER",             "Character",     "drive",  K::knob },

        // Output
        {   7, "VOLUME",                "Volume",        "output", K::knob },
        {  10, "PAN",                   "Pan",           "output", K::knob },
        {  12, "MUTE",                  "Mute",          "output", K::button },
        {  13, "SOLO",                  "Solo",          "output", K::button },

        // Navigation / utility
        {  58, "PRESET",                "Shift",         "nav",    K::button },
        { 104, "MODE",                  "Mode",          "nav",    K::button },
        { 102, "DISPLAY_ON",            "Display",       "nav",    K::button },
        {  96, "PAGE_UP",               "Page Up",       "nav",    K::button },
        {  97, "PAGE_DOWN",             "Page Down",     "nav",    K::button },
        { 120, "TRACK_COPY",            "Copy",          "nav",    K::button },
        { 123, "TRACK_GROUP",           "Group",         "nav",    K::button },

        // Track select 1-20
        { 21, "FOCUS1",  "1",  "track", K::button }, { 22, "FOCUS2",  "2",  "track", K::button },
        { 23, "FOCUS3",  "3",  "track", K::button }, { 24, "FOCUS4",  "4",  "track", K::button },
        { 25, "FOCUS5",  "5",  "track", K::button }, { 26, "FOCUS6",  "6",  "track", K::button },
        { 27, "FOCUS7",  "7",  "track", K::button }, { 28, "FOCUS8",  "8",  "track", K::button },
        { 29, "FOCUS9",  "9",  "track", K::button }, { 30, "FOCUS10", "10", "track", K::button },
        { 31, "FOCUS11", "11", "track", K::button }, { 32, "FOCUS12", "12", "track", K::button },
        { 33, "FOCUS13", "13", "track", K::button }, { 34, "FOCUS14", "14", "track", K::button },
        { 35, "FOCUS15", "15", "track", K::button }, { 36, "FOCUS16", "16", "track", K::button },
        { 37, "FOCUS17", "17", "track", K::button }, { 38, "FOCUS18", "18", "track", K::button },
        { 39, "FOCUS19", "19", "track", K::button }, { 40, "FOCUS20", "20", "track", K::button },
    };
    return table;
}

const ControlInfo* findControl(int cc)
{
    for (auto& c : controls())
        if (c.cc == cc)
            return &c;
    return nullptr;
}

namespace
{
    juce::File settingsFolder()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if JUCE_MAC
            .getChildFile("Application Support")
#endif
            .getChildFile("GHS FX Companion");
    }

    bool nameLooksLikeConsole1(const juce::String& name)
    {
        return name.containsIgnoreCase("Console 1") || name.containsIgnoreCase("Console1");
    }

    double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

    bool writeJsonAtomically(const juce::File& file, const juce::var& v)
    {
        file.getParentDirectory().createDirectory();
        juce::TemporaryFile tmp(file);
        if (!tmp.getFile().replaceWithText(juce::JSON::toString(v)))
            return false;
        return tmp.overwriteTargetFileWithTemporary();
    }

    juce::DynamicObject* obj(juce::var& v) { return v.getDynamicObject(); }
}

// ============================================================================
// DeviceHub
// ============================================================================

DeviceHub::DeviceHub()
{
    loadSettings();
    tryOpen();
    startTimer(2000);
}

DeviceHub::~DeviceHub()
{
    stopTimer();
    closePorts();
}

void DeviceHub::closePorts()
{
    // Move the ports out under the lock, but stop them OUTSIDE it: MidiInput::stop()
    // waits for an in-flight callback, and that callback takes the same lock.
    std::unique_ptr<juce::MidiInput> oldIn;
    std::unique_ptr<juce::MidiOutput> oldOut;
    {
        std::lock_guard<std::mutex> g(lock);
        oldIn = std::move(input);
        oldOut = std::move(output);
    }
    if (oldIn != nullptr) oldIn->stop();
}

void DeviceHub::registerEngine(Engine* e)
{
    std::lock_guard<std::mutex> g(lock);
    engines.push_back(e);
    if (owner == nullptr)
    {
        owner = e;
        generation++;
    }
}

void DeviceHub::unregisterEngine(Engine* e)
{
    std::lock_guard<std::mutex> g(lock);
    engines.erase(std::remove(engines.begin(), engines.end(), e), engines.end());
    if (owner == e)
    {
        owner = engines.empty() ? nullptr : engines.back();
        generation++;
    }
}

void DeviceHub::claim(Engine* e)
{
    std::lock_guard<std::mutex> g(lock);
    if (owner != e)
    {
        owner = e;
        generation++;
    }
}

bool DeviceHub::isOwner(const Engine* e) const
{
    std::lock_guard<std::mutex> g(lock);
    return owner == e;
}

void DeviceHub::setPreferredDevices(const juce::String& inName, const juce::String& outName)
{
    closePorts();
    {
        std::lock_guard<std::mutex> g(lock);
        preferredIn = inName;
        preferredOut = outName;
    }
    saveSettings();
    tryOpen();
}

juce::String DeviceHub::getOpenInputName() const
{
    std::lock_guard<std::mutex> g(lock);
    return input != nullptr ? input->getName() : juce::String();
}

juce::String DeviceHub::getOpenOutputName() const
{
    std::lock_guard<std::mutex> g(lock);
    return output != nullptr ? output->getName() : juce::String();
}

bool DeviceHub::hasOutput() const
{
    std::lock_guard<std::mutex> g(lock);
    return output != nullptr;
}

bool DeviceHub::send(int channel, int cc, int value)
{
    std::lock_guard<std::mutex> g(lock);
    if (output == nullptr)
        return false;
    output->sendMessageNow(juce::MidiMessage::controllerEvent(juce::jlimit(1, 16, channel), cc, juce::jlimit(0, 127, value)));
    return true;
}

juce::StringArray DeviceHub::listInputs()
{
    juce::StringArray names;
    for (auto& d : juce::MidiInput::getAvailableDevices()) names.add(d.name);
    return names;
}

juce::StringArray DeviceHub::listOutputs()
{
    juce::StringArray names;
    for (auto& d : juce::MidiOutput::getAvailableDevices()) names.add(d.name);
    return names;
}

void DeviceHub::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m)
{
    if (!m.isController())
        return;
    std::lock_guard<std::mutex> g(lock); // MIDI thread, not the audio thread - a short lock is fine
    if (owner != nullptr)
        owner->pushDeviceEvent(m.getChannel(), m.getControllerNumber(), m.getControllerValue());
}

void DeviceHub::timerCallback()
{
    // Unplugged? An open port whose device has gone away is closed here, so the
    // reopen below picks the Console 1 up again when it comes back.
    juce::String inId, outId;
    {
        std::lock_guard<std::mutex> g(lock);
        if (input != nullptr) inId = input->getIdentifier();
        if (output != nullptr) outId = output->getIdentifier();
    }
    auto present = [](const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& id)
    {
        for (auto& d : devices) if (d.identifier == id) return true;
        return false;
    };
    const bool inGone = inId.isNotEmpty() && !present(juce::MidiInput::getAvailableDevices(), inId);
    const bool outGone = outId.isNotEmpty() && !present(juce::MidiOutput::getAvailableDevices(), outId);
    if (inGone || outGone)
    {
        std::unique_ptr<juce::MidiInput> oldIn;
        std::unique_ptr<juce::MidiOutput> oldOut;
        {
            std::lock_guard<std::mutex> g(lock);
            if (inGone) oldIn = std::move(input);
            if (outGone) oldOut = std::move(output);
            generation++;
        }
        if (oldIn != nullptr) oldIn->stop(); // outside the lock, as in closePorts()
    }

    bool missing;
    {
        std::lock_guard<std::mutex> g(lock);
        missing = input == nullptr || output == nullptr;
    }
    if (missing)
        tryOpen();
}

void DeviceHub::tryOpen()
{
    auto pick = [](const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& preferred) -> juce::MidiDeviceInfo
    {
        for (auto& d : devices)
            if (preferred.isNotEmpty() && d.name == preferred)
                return d;
        if (preferred.isEmpty())
            for (auto& d : devices)
                if (nameLooksLikeConsole1(d.name))
                    return d;
        return {};
    };

    const auto inInfo = pick(juce::MidiInput::getAvailableDevices(), preferredIn);
    const auto outInfo = pick(juce::MidiOutput::getAvailableDevices(), preferredOut);

    bool changed = false;
    std::lock_guard<std::mutex> g(lock);

    if (input == nullptr && inInfo.identifier.isNotEmpty())
    {
        input = juce::MidiInput::openDevice(inInfo.identifier, this);
        if (input != nullptr)
        {
            input->start();
            changed = true;
        }
    }
    if (output == nullptr && outInfo.identifier.isNotEmpty())
    {
        output = juce::MidiOutput::openDevice(outInfo.identifier);
        changed = changed || output != nullptr;
    }
    if (changed)
        generation++;
}

void DeviceHub::loadSettings()
{
    const auto v = juce::JSON::parse(settingsFolder().getChildFile("Console1Settings.json"));
    preferredIn = v.getProperty("input", "").toString();
    preferredOut = v.getProperty("output", "").toString();
}

void DeviceHub::saveSettings() const
{
    juce::var v(new juce::DynamicObject());
    obj(v)->setProperty("input", preferredIn);
    obj(v)->setProperty("output", preferredOut);
    writeJsonAtomically(settingsFolder().getChildFile("Console1Settings.json"), v);
}

// ============================================================================
// Engine
// ============================================================================

Engine::Engine(Host h) : host(std::move(h))
{
    hwValue.fill(-1);
    sentValue.fill(-1);
    lastTouchMs.fill(0.0);
    lastSentMs.fill(-1.0e9);
    for (auto& b : boundMask) b.store(false);
    hub->registerEngine(this);
    startTimerHz(60);
}

Engine::~Engine()
{
    stopTimer();
    hub->unregisterEngine(this);
}

// ---------------- real-time side ----------------

void Engine::processHostMidi(juce::MidiBuffer& midi) noexcept
{
    if (source == Source::device)
        return;

    const bool consume = consumeBoundCcs.load(std::memory_order_relaxed);
    bool anyConsumed = false;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (!m.isController()) continue;
        hostFifo.push({ (juce::uint8) m.getChannel(), (juce::uint8) m.getControllerNumber(), (juce::uint8) m.getControllerValue() });
        hostEventCount.fetch_add(1, std::memory_order_relaxed);
        if (consume && boundMask[(size_t) keyOf(m.getChannel(), m.getControllerNumber())].load(std::memory_order_relaxed))
            anyConsumed = true;
    }

    if (!anyConsumed)
        return;

    // Rebuild without the bound CCs. MidiBuffer allocation on the audio thread is
    // the same trade-off JUCE's own MidiBuffer helpers make; it only happens on
    // blocks that actually carry controller moves.
    juce::MidiBuffer kept;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController() && boundMask[(size_t) keyOf(m.getChannel(), m.getControllerNumber())].load(std::memory_order_relaxed))
            continue;
        kept.addEvent(m, meta.samplePosition);
    }
    midi.swapWith(kept);
}

void Engine::pushDeviceEvent(int channel, int cc, int value) noexcept
{
    deviceFifo.push({ (juce::uint8) channel, (juce::uint8) cc, (juce::uint8) value });
    deviceEventCount.fetch_add(1, std::memory_order_relaxed);
}

// ---------------- timer: drain + feedback ----------------

void Engine::timerCallback()
{
    if (source != Source::host)
        deviceFifo.drain([this](const Event& e) { handleCc(e.channel, e.cc, e.value, false); });
    else
        deviceFifo.drain([](const Event&) {});

    if (source != Source::device)
        hostFifo.drain([this](const Event& e) { handleCc(e.channel, e.cc, e.value, false); });
    else
        hostFifo.drain([](const Event&) {});

    const auto gen = hub->getGeneration();
    const bool force = gen != seenGeneration;
    seenGeneration = gen;
    sendFeedback(force);
}

void Engine::handleCc(int channel, int cc, int value, bool fromSimulator)
{
    const int key = keyOf(channel, cc);

    // Echo guard. Some controllers send back the value they were just given. If the
    // Console 1 does, an echo of our ring update would land here as a "knob move" and
    // snap the parameter to the 128-step ring value. A real knob arriving at exactly the
    // value we sent, within 120 ms, changes nothing anyway, so dropping it is safe.
    if (!fromSimulator && value == sentValue[(size_t) key] && nowMs() - lastSentMs[(size_t) key] < 120.0)
        return;

    const int previousHw = hwValue[(size_t) key];
    hwValue[(size_t) key] = value;
    lastTouchMs[(size_t) key] = nowMs();

    // The hardware may light/clear a button's LED by itself on press or release;
    // forget what we last sent so feedback re-asserts the real state afterwards.
    if (const auto* touched = findControl(cc); touched != nullptr && touched->kind == ControlKind::button)
        sentValue[(size_t) key] = -1;

    // Learn takes priority over everything except the slot-focus buttons.
    if (learnSlot >= 0)
    {
        const auto* info = findControl(cc);
        const bool isFocusButton = trackButtonsFocusSlots && cc >= trackButtonFirstCc && cc < trackButtonFirstCc + host.numSlots;
        // A button's release (value 0) right after its press shouldn't matter: we bind on the first message.
        if (!isFocusButton)
        {
            Binding b;
            b.id = nextBindingId++;
            b.channel = channel;
            b.cc = cc;
            b.slot = learnSlot;
            b.targetBypass = learnBypass;
            b.paramIndex = learnParam;
            const bool isButton = info != nullptr ? info->kind == ControlKind::button : false;
            b.mode = (isButton || learnBypass) ? Console1Math::Mode::toggle
                                               : Console1Math::modeFromName(defaultKnobMode.toRawUTF8(), Console1Math::Mode::absolute);
            if (!learnBypass)
                if (auto* p = resolveParam(b))
                {
                    b.paramName = p->getName(64);
                    if (auto* hp = dynamic_cast<juce::HostedAudioProcessorParameter*>(p))
                        b.paramId = hp->getParameterID();
                }

            // One control -> one target per slot by default: replace an existing
            // binding of the same control on the same slot to the same target.
            bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [&](const Binding& o)
            {
                return o.slot == b.slot && o.cc == b.cc && o.channel == b.channel
                    && o.targetBypass == b.targetBypass && (b.targetBypass || o.paramIndex == b.paramIndex);
            }), bindings.end());

            bindings.push_back(b);
            lastLearnedMessage = (info != nullptr ? juce::String(info->label) : "CC " + juce::String(cc))
                                 + " -> " + targetLabel(bindings.back());
            learnedSerial++;
            learnSlot = -1;
            learnParam = -1;
            learnBypass = false;
            rebuildConsumeMask();
            return;
        }
    }

    juce::String target;
    if (handleSystemControl(cc, value))
    {
        target = "Focus slot " + juce::String(focusSlot + 1);
    }
    else
    {
        auto winners = winnersFor(channel, cc);
        for (auto* b : winners)
        {
            applyBinding(*b, value, previousHw);
            if (target.isNotEmpty()) target << " + ";
            target << targetLabel(*b);
        }
    }

    lastTouch = { channel, cc, value, ++touchSerial, target };
    recent[(size_t) recentHead] = lastTouch;
    recentHead = (recentHead + 1) % (int) recent.size();
}

bool Engine::handleSystemControl(int cc, int value)
{
    if (!trackButtonsFocusSlots)
        return false;

    if (cc >= trackButtonFirstCc && cc < trackButtonFirstCc + host.numSlots)
    {
        if (Console1Math::isPress(value))
            setFocusSlot(cc - trackButtonFirstCc);
        return true;
    }

    if (cc == pageUpCc || cc == pageDownCc)
    {
        if (!Console1Math::isPress(value))
            return true;
        const int dir = cc == pageUpCc ? -1 : 1;
        // Next slot that actually has a plugin; stay put if none.
        for (int step = 1; step <= host.numSlots; ++step)
        {
            const int s = ((focusSlot + dir * step) % host.numSlots + host.numSlots) % host.numSlots;
            if (host.pluginInSlot(s) != nullptr)
            {
                setFocusSlot(s);
                break;
            }
        }
        return true;
    }
    return false;
}

std::vector<Binding*> Engine::winnersFor(int channel, int cc)
{
    std::vector<Binding*> candidates;
    for (auto& b : bindings)
        if (b.cc == cc && (b.channel == 0 || b.channel == channel))
            candidates.push_back(&b);

    if (candidates.empty())
        return candidates;

    // Focused slot wins; otherwise the lowest-numbered slot that binds this control.
    int winningSlot = -1;
    for (auto* b : candidates)
        if (b->slot == focusSlot) { winningSlot = focusSlot; break; }
    if (winningSlot < 0)
    {
        winningSlot = candidates.front()->slot;
        for (auto* b : candidates) winningSlot = juce::jmin(winningSlot, b->slot);
    }

    std::vector<Binding*> winners;
    for (auto* b : candidates)
        if (b->slot == winningSlot)
            winners.push_back(b);
    return winners;
}

juce::AudioProcessorParameter* Engine::resolveParam(Binding& b)
{
    if (b.targetBypass) return nullptr;
    auto* plugin = host.pluginInSlot(b.slot);
    if (plugin == nullptr) return nullptr;
    const auto& params = plugin->getParameters();

    auto idOf = [](juce::AudioProcessorParameter* p) -> juce::String
    {
        if (auto* hp = dynamic_cast<juce::HostedAudioProcessorParameter*>(p)) return hp->getParameterID();
        return {};
    };

    // Fast path: index still points at the same parameter.
    if (juce::isPositiveAndBelow(b.paramIndex, params.size()))
    {
        auto* p = params[b.paramIndex];
        if (b.paramId.isEmpty() || idOf(p) == b.paramId)
            return p;
    }
    // Re-resolve by stable id, then by name (plugin updated / AU vs VST3 of the same plugin).
    for (int i = 0; i < params.size(); ++i)
        if (b.paramId.isNotEmpty() && idOf(params[i]) == b.paramId) { b.paramIndex = i; return params[i]; }
    for (int i = 0; i < params.size(); ++i)
        if (b.paramName.isNotEmpty() && params[i]->getName(64) == b.paramName) { b.paramIndex = i; return params[i]; }
    return nullptr;
}

float Engine::currentNorm(Binding& b)
{
    if (b.targetBypass)
    {
        auto* bp = host.bypassForSlot(b.slot);
        // LED/state convention matches Console 1's section buttons: lit = active (not bypassed).
        return bp != nullptr && !bp->get() ? 1.0f : 0.0f;
    }
    if (auto* p = resolveParam(b)) return p->getValue();
    return 0.0f;
}

void Engine::applyBinding(Binding& b, int value, int previousHw)
{
    using namespace Console1Math;

    if (b.targetBypass)
    {
        auto* bp = host.bypassForSlot(b.slot);
        if (bp == nullptr) return;
        if (b.mode == Mode::momentary)
            *bp = !isPress(value); // held = active
        else if (isPress(value))
            *bp = !bp->get();
        return;
    }

    auto* p = resolveParam(b);
    if (p == nullptr) return;

    const float now = p->getValue();
    std::optional<float> next;
    switch (b.mode)
    {
        case Mode::absolute:  next = ccToNorm(value, b.range); break;
        case Mode::pickup:    next = pickup(value, previousHw, now, b.range, b.pickedUp); break;
        case Mode::relative:  next = relative(value, previousHw, now, b.range, b.sensitivity); break;
        case Mode::toggle:    next = toggle(value, now, b.range); break;
        case Mode::momentary: next = momentary(value, b.range); break;
    }
    if (!next.has_value()) return;

    float v = *next;
    const int steps = p->getNumSteps();
    if (p->isDiscrete() || steps < 128)
        v = snapToSteps(v, steps);

    if (std::abs(v - now) > 1.0e-6f)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(v);
        p->endChangeGesture();
    }

    // Relative mode: put the ring (and the hardware's internal counter) back on the
    // parameter immediately, so the next turn is measured from where the parameter is.
    if (b.mode == Mode::relative && feedbackEnabled && hub->isOwner(this))
    {
        const int key = keyOf(b.channel == 0 ? 1 : b.channel, b.cc);
        const int ring = normToCc(v, b.range);
        if (hub->send(b.channel == 0 ? 1 : b.channel, b.cc, ring))
        {
            sentValue[(size_t) key] = ring;
            lastSentMs[(size_t) key] = nowMs();
            hwValue[(size_t) key] = ring;
        }
    }
}

void Engine::sendFeedback(bool force)
{
    if (!feedbackEnabled || !hub->hasOutput() || !hub->isOwner(this))
        return;

    if (force)
        sentValue.fill(-1), lastFocusLed = -1;

    const double t = nowMs();

    // Track buttons: light the focused slot.
    if (trackButtonsFocusSlots && lastFocusLed != focusSlot)
    {
        for (int s = 0; s < host.numSlots; ++s)
        {
            const int v = s == focusSlot ? 127 : 0;
            if (hub->send(1, trackButtonFirstCc + s, v))
            {
                sentValue[(size_t) keyOf(1, trackButtonFirstCc + s)] = v;
                lastSentMs[(size_t) keyOf(1, trackButtonFirstCc + s)] = t;
            }
        }
        lastFocusLed = focusSlot;
    }

    // One value per physical control: whichever binding currently wins it.
    std::array<bool, 16 * 128> done {};
    for (auto& b : bindings)
    {
        const int ch = b.channel == 0 ? 1 : b.channel;
        const int key = keyOf(ch, b.cc);
        if (done[(size_t) key]) continue;
        auto winners = winnersFor(ch, b.cc);
        if (winners.empty()) continue;
        done[(size_t) key] = true;

        auto& w = *winners.front();
        if (w.mode == Console1Math::Mode::pickup) continue; // pickup exists precisely for "no feedback"

        int want;
        if (Console1Math::isButtonMode(w.mode))
            want = Console1Math::ledOn(currentNorm(w), w.range) ? 127 : 0;
        else
        {
            // Don't fight a hand that's on the knob right now.
            if (t - lastTouchMs[(size_t) key] < 250.0) continue;
            want = Console1Math::normToCc(currentNorm(w), w.range);
        }

        if (Console1Math::isButtonMode(w.mode))
        {
            if (sentValue[(size_t) key] == want) continue;
        }
        else
        {
            if (sentValue[(size_t) key] == want && hwValue[(size_t) key] == want) continue;
            // Hardware already reports this position (it was just turned there) - nothing to send.
            if (hwValue[(size_t) key] == want) { sentValue[(size_t) key] = want; continue; }
        }
        if (hub->send(ch, b.cc, want))
        {
            sentValue[(size_t) key] = want;
            lastSentMs[(size_t) key] = t;
            if (!Console1Math::isButtonMode(w.mode)) hwValue[(size_t) key] = want;
        }
    }
}

void Engine::rebuildConsumeMask()
{
    for (auto& m : boundMask) m.store(false, std::memory_order_relaxed);
    for (auto& b : bindings)
    {
        if (b.channel == 0)
            for (int ch = 1; ch <= 16; ++ch) boundMask[(size_t) keyOf(ch, b.cc)].store(true, std::memory_order_relaxed);
        else
            boundMask[(size_t) keyOf(b.channel, b.cc)].store(true, std::memory_order_relaxed);
    }
}

void Engine::resetPickupForSlot(int slot)
{
    for (auto& b : bindings)
        if (slot < 0 || b.slot == slot)
            b.pickedUp = false;
}

// ---------------- message-thread API ----------------

juce::String Engine::targetLabel(const Binding& b)
{
    juce::String plugin = "Slot " + juce::String(b.slot + 1);
    if (auto* p = host.pluginInSlot(b.slot)) plugin = p->getName();
    if (b.targetBypass) return plugin + " / On-Off";
    return plugin + " / " + (b.paramName.isNotEmpty() ? b.paramName : "Param " + juce::String(b.paramIndex + 1));
}

juce::var Engine::bindingToVar(const Binding& b) const
{
    juce::var v(new juce::DynamicObject());
    auto* o = obj(v);
    o->setProperty("id", b.id);
    o->setProperty("channel", b.channel);
    o->setProperty("cc", b.cc);
    o->setProperty("slot", b.slot);
    o->setProperty("target", b.targetBypass ? "bypass" : "param");
    o->setProperty("paramIndex", b.paramIndex);
    o->setProperty("paramId", b.paramId);
    o->setProperty("paramName", b.paramName);
    o->setProperty("mode", Console1Math::modeName(b.mode));
    o->setProperty("min", b.range.min);
    o->setProperty("max", b.range.max);
    o->setProperty("invert", b.range.invert);
    o->setProperty("sensitivity", b.sensitivity);
    return v;
}

bool Engine::bindingFromVar(Binding& b, const juce::var& v)
{
    if (!v.isObject()) return false;
    if (v.hasProperty("channel"))     b.channel = juce::jlimit(0, 16, (int) v["channel"]);
    if (v.hasProperty("cc"))          b.cc = juce::jlimit(0, 127, (int) v["cc"]);
    if (v.hasProperty("slot"))        b.slot = juce::jlimit(0, host.numSlots - 1, (int) v["slot"]);
    if (v.hasProperty("target"))      b.targetBypass = v["target"].toString() == "bypass";
    if (v.hasProperty("paramIndex"))  b.paramIndex = (int) v["paramIndex"];
    if (v.hasProperty("paramId"))     b.paramId = v["paramId"].toString();
    if (v.hasProperty("paramName"))   b.paramName = v["paramName"].toString();
    if (v.hasProperty("mode"))        b.mode = Console1Math::modeFromName(v["mode"].toString().toRawUTF8(), b.mode);
    if (v.hasProperty("min"))         b.range.min = Console1Math::clamp01((float) (double) v["min"]);
    if (v.hasProperty("max"))         b.range.max = Console1Math::clamp01((float) (double) v["max"]);
    if (v.hasProperty("invert"))      b.range.invert = (bool) v["invert"];
    if (v.hasProperty("sensitivity")) b.sensitivity = juce::jlimit(0.02f, 4.0f, (float) (double) v["sensitivity"]);
    b.pickedUp = false;
    return b.cc >= 0 && b.slot >= 0;
}

juce::var Engine::getState()
{
    juce::var v(new juce::DynamicObject());
    auto* o = obj(v);

    juce::var device(new juce::DynamicObject());
    obj(device)->setProperty("input", hub->getOpenInputName());
    obj(device)->setProperty("output", hub->getOpenOutputName());
    obj(device)->setProperty("preferredInput", hub->getPreferredInput());
    obj(device)->setProperty("preferredOutput", hub->getPreferredOutput());
    juce::Array<juce::var> ins, outs;
    for (auto& n : DeviceHub::listInputs()) ins.add(n);
    for (auto& n : DeviceHub::listOutputs()) outs.add(n);
    obj(device)->setProperty("inputs", ins);
    obj(device)->setProperty("outputs", outs);
    o->setProperty("device", device);

    o->setProperty("owner", isOwner());
    o->setProperty("source", source == Source::device ? "device" : source == Source::host ? "host" : "both");
    o->setProperty("feedback", feedbackEnabled);
    o->setProperty("autoLoadProfiles", autoLoadProfiles);
    o->setProperty("trackButtonsFocusSlots", trackButtonsFocusSlots);
    o->setProperty("consumeBoundCcs", consumeBoundCcs.load());
    o->setProperty("defaultKnobMode", defaultKnobMode);
    o->setProperty("focusSlot", focusSlot);

    juce::Array<juce::var> slots;
    for (int s = 0; s < host.numSlots; ++s)
    {
        juce::var slot(new juce::DynamicObject());
        auto* plugin = host.pluginInSlot(s);
        obj(slot)->setProperty("index", s);
        obj(slot)->setProperty("name", plugin != nullptr ? juce::var(plugin->getName()) : juce::var());
        obj(slot)->setProperty("hasProfile", plugin != nullptr
            && profilesFolder().getChildFile(profileKeyFor(plugin->getPluginDescription()) + ".json").existsAsFile());
        slots.add(slot);
    }
    o->setProperty("slots", slots);

    juce::Array<juce::var> list;
    for (auto& b : bindings)
    {
        auto bv = bindingToVar(b);
        obj(bv)->setProperty("label", targetLabel(b));
        const auto* info = findControl(b.cc);
        obj(bv)->setProperty("control", info != nullptr ? juce::var(info->label) : juce::var("CC " + juce::String(b.cc)));
        obj(bv)->setProperty("valueText", [&]
        {
            if (b.targetBypass) return juce::String(currentNorm(b) > 0.5f ? "On" : "Off");
            if (auto* p = resolveParam(b)) return p->getCurrentValueAsText();
            return juce::String("(missing)");
        }());
        obj(bv)->setProperty("resolved", b.targetBypass || resolveParam(b) != nullptr);
        list.add(bv);
    }
    o->setProperty("bindings", list);

    juce::Array<juce::var> table;
    for (auto& c : controls())
    {
        juce::var cv(new juce::DynamicObject());
        obj(cv)->setProperty("cc", c.cc);
        obj(cv)->setProperty("id", c.id);
        obj(cv)->setProperty("label", c.label);
        obj(cv)->setProperty("section", c.section);
        obj(cv)->setProperty("kind", c.kind == ControlKind::knob ? "knob" : "button");
        table.add(cv);
    }
    o->setProperty("controls", table);

    o->setProperty("learn", learnSlot >= 0);
    o->setProperty("learnSlot", learnSlot);
    o->setProperty("learnParam", learnParam);
    o->setProperty("learnBypass", learnBypass);
    return v;
}

juce::var Engine::getLiveState()
{
    juce::var v(new juce::DynamicObject());
    auto* o = obj(v);
    o->setProperty("serial", (int) touchSerial);
    o->setProperty("cc", lastTouch.cc);
    o->setProperty("channel", lastTouch.channel);
    o->setProperty("value", lastTouch.value);
    o->setProperty("target", lastTouch.target);
    o->setProperty("learn", learnSlot >= 0);
    o->setProperty("learnedSerial", (int) learnedSerial);
    o->setProperty("learnedMessage", lastLearnedMessage);
    o->setProperty("owner", isOwner());
    o->setProperty("input", hub->getOpenInputName());
    o->setProperty("output", hub->getOpenOutputName());
    o->setProperty("focusSlot", focusSlot);
    o->setProperty("hostEvents", (int) hostEventCount.load());
    o->setProperty("deviceEvents", (int) deviceEventCount.load());

    // Ring positions (what each bound control "should" show) so the on-screen map can draw them.
    juce::var rings(new juce::DynamicObject());
    std::array<bool, 128> done {};
    for (auto& b : bindings)
    {
        if (done[(size_t) b.cc]) continue;
        auto winners = winnersFor(b.channel == 0 ? 1 : b.channel, b.cc);
        if (winners.empty()) continue;
        done[(size_t) b.cc] = true;
        auto& w = *winners.front();
        const int val = Console1Math::isButtonMode(w.mode)
                            ? (Console1Math::ledOn(currentNorm(w), w.range) ? 127 : 0)
                            : Console1Math::normToCc(currentNorm(w), w.range);
        obj(rings)->setProperty(juce::String(b.cc), val);
    }
    o->setProperty("rings", rings);

    juce::Array<juce::var> rec;
    for (int i = 0; i < (int) recent.size(); ++i)
    {
        const auto& t = recent[(size_t) ((recentHead - 1 - i + (int) recent.size() * 2) % (int) recent.size())];
        if (t.cc < 0) continue;
        juce::var tv(new juce::DynamicObject());
        obj(tv)->setProperty("cc", t.cc);
        obj(tv)->setProperty("channel", t.channel);
        obj(tv)->setProperty("value", t.value);
        obj(tv)->setProperty("serial", (int) t.serial);
        obj(tv)->setProperty("target", t.target);
        rec.add(tv);
    }
    o->setProperty("recent", rec);
    return v;
}

juce::var Engine::listParams(int slot)
{
    juce::Array<juce::var> list;
    if (!juce::isPositiveAndBelow(slot, host.numSlots)) return list;
    auto* plugin = host.pluginInSlot(slot);
    if (plugin == nullptr) return list;

    const auto& params = plugin->getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        juce::var pv(new juce::DynamicObject());
        obj(pv)->setProperty("index", i);
        if (auto* hp = dynamic_cast<juce::HostedAudioProcessorParameter*>(p))
            obj(pv)->setProperty("id", hp->getParameterID());
        obj(pv)->setProperty("name", p->getName(64));
        obj(pv)->setProperty("label", p->getLabel());
        obj(pv)->setProperty("steps", p->getNumSteps());
        obj(pv)->setProperty("discrete", p->isDiscrete() || p->isBoolean());
        obj(pv)->setProperty("value", p->getValue());
        obj(pv)->setProperty("text", p->getCurrentValueAsText());
        obj(pv)->setProperty("automatable", p->isAutomatable());
        list.add(pv);
    }
    return list;
}

void Engine::setOption(const juce::String& name, const juce::var& value)
{
    if (name == "source")
    {
        const auto s = value.toString();
        source = s == "host" ? Source::host : s == "both" ? Source::both : Source::device;
    }
    else if (name == "feedback")               { feedbackEnabled = (bool) value; sentValue.fill(-1); lastFocusLed = -1; }
    else if (name == "autoLoadProfiles")       autoLoadProfiles = (bool) value;
    else if (name == "trackButtonsFocusSlots") trackButtonsFocusSlots = (bool) value;
    else if (name == "consumeBoundCcs")        consumeBoundCcs = (bool) value;
    else if (name == "defaultKnobMode")        defaultKnobMode = value.toString();
}

void Engine::setDevices(const juce::String& inputName, const juce::String& outputName)
{
    hub->setPreferredDevices(inputName, outputName);
}

void Engine::takeControl() { hub->claim(this); }
bool Engine::isOwner() const { return hub->isOwner(this); }

void Engine::setFocusSlot(int slot)
{
    if (!juce::isPositiveAndBelow(slot, host.numSlots)) return;
    if (slot != focusSlot)
    {
        focusSlot = slot;
        resetPickupForSlot(-1);
        // Different plugin may now own the shared knobs: let rings jump to it.
        std::fill(lastTouchMs.begin(), lastTouchMs.end(), 0.0);
    }
}

void Engine::armLearn(int slot, int paramIndex, bool bypass)
{
    if (!juce::isPositiveAndBelow(slot, host.numSlots)) return;
    learnSlot = slot;
    learnParam = paramIndex;
    learnBypass = bypass;
}

void Engine::cancelLearn()
{
    learnSlot = -1;
    learnParam = -1;
    learnBypass = false;
}

int Engine::addBinding(const juce::var& json)
{
    Binding b;
    if (!bindingFromVar(b, json)) return -1;
    if (!json.hasProperty("mode"))
    {
        const auto* info = findControl(b.cc);
        const bool button = (info != nullptr && info->kind == ControlKind::button) || b.targetBypass;
        b.mode = button ? Console1Math::Mode::toggle
                        : Console1Math::modeFromName(defaultKnobMode.toRawUTF8(), Console1Math::Mode::absolute);
    }
    if (!b.targetBypass)
        if (auto* p = resolveParam(b))
        {
            if (b.paramName.isEmpty()) b.paramName = p->getName(64);
            if (b.paramId.isEmpty())
                if (auto* hp = dynamic_cast<juce::HostedAudioProcessorParameter*>(p)) b.paramId = hp->getParameterID();
        }
    b.id = nextBindingId++;
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [&](const Binding& o)
    {
        return o.slot == b.slot && o.cc == b.cc && o.channel == b.channel
            && o.targetBypass == b.targetBypass && (b.targetBypass || o.paramIndex == b.paramIndex);
    }), bindings.end());
    bindings.push_back(b);
    rebuildConsumeMask();
    return b.id;
}

bool Engine::updateBinding(int id, const juce::var& json)
{
    for (auto& b : bindings)
        if (b.id == id)
        {
            Binding copy = b;
            if (!bindingFromVar(copy, json)) return false;
            b = copy;
            sentValue.fill(-1);
            rebuildConsumeMask();
            return true;
        }
    return false;
}

bool Engine::removeBinding(int id)
{
    const auto before = bindings.size();
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [id](const Binding& b) { return b.id == id; }), bindings.end());
    rebuildConsumeMask();
    return bindings.size() != before;
}

void Engine::clearSlot(int slot)
{
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [slot](const Binding& b) { return b.slot == slot; }), bindings.end());
    rebuildConsumeMask();
}

void Engine::inject(int channel, int cc, int value)
{
    handleCc(juce::jlimit(1, 16, channel), juce::jlimit(0, 127, cc), juce::jlimit(0, 127, value), true);
}

void Engine::slotLoaded(int slot)
{
    clearSlot(slot);
    if (autoLoadProfiles)
        loadProfileForSlot(slot);
    sentValue.fill(-1);
}

void Engine::slotUnloaded(int slot)
{
    clearSlot(slot);
    sentValue.fill(-1);
}

void Engine::slotsSwapped(int a, int b)
{
    for (auto& binding : bindings)
    {
        if (binding.slot == a) binding.slot = b;
        else if (binding.slot == b) binding.slot = a;
    }
    sentValue.fill(-1);
}

void Engine::slotStateRestored(int slot)
{
    resetPickupForSlot(slot);
    sentValue.fill(-1);
}

// ---------------- profiles ----------------

juce::File Engine::profilesFolder()
{
    return settingsFolder().getChildFile("Console1Profiles");
}

juce::String Engine::profileKeyFor(const juce::PluginDescription& d)
{
    // Format-agnostic on purpose: the AU and VST3 of a plugin share one profile when both
    // report the same maker + name (bindings re-resolve by parameter name if ids differ).
    auto key = (d.manufacturerName.isNotEmpty() ? d.manufacturerName + " - " : juce::String()) + d.name;
    return juce::File::createLegalFileName(key).replaceCharacter('/', '-');
}

juce::var Engine::saveProfileForSlot(int slot)
{
    juce::var result(new juce::DynamicObject());
    auto* plugin = juce::isPositiveAndBelow(slot, host.numSlots) ? host.pluginInSlot(slot) : nullptr;
    if (plugin == nullptr)
    {
        obj(result)->setProperty("ok", false);
        obj(result)->setProperty("message", "No plugin in that slot.");
        return result;
    }

    const auto& desc = plugin->getPluginDescription();
    juce::var profile(new juce::DynamicObject());
    obj(profile)->setProperty("version", 1);
    obj(profile)->setProperty("plugin", desc.name);
    obj(profile)->setProperty("manufacturer", desc.manufacturerName);
    obj(profile)->setProperty("savedWithFormat", desc.pluginFormatName);
    obj(profile)->setProperty("parameterCount", plugin->getParameters().size());
    juce::Array<juce::var> list;
    for (auto& b : bindings)
        if (b.slot == slot)
        {
            auto bv = bindingToVar(b);
            obj(bv)->removeProperty("id");
            obj(bv)->removeProperty("slot");
            list.add(bv);
        }
    obj(profile)->setProperty("bindings", list);

    const auto file = profilesFolder().getChildFile(profileKeyFor(desc) + ".json");
    if (file.existsAsFile()) // keep one previous version, never silently lose a mapping
        file.copyFileTo(file.withFileExtension(".previous.json"));
    const bool ok = writeJsonAtomically(file, profile);
    obj(result)->setProperty("ok", ok);
    obj(result)->setProperty("count", list.size());
    obj(result)->setProperty("message", ok ? "Saved " + juce::String(list.size()) + " controls for " + desc.name
                                           : juce::String("Couldn't write the profile file."));
    return result;
}

juce::var Engine::loadProfileForSlot(int slot)
{
    juce::var result(new juce::DynamicObject());
    auto* plugin = juce::isPositiveAndBelow(slot, host.numSlots) ? host.pluginInSlot(slot) : nullptr;
    const auto file = plugin != nullptr ? profilesFolder().getChildFile(profileKeyFor(plugin->getPluginDescription()) + ".json") : juce::File();
    if (plugin == nullptr || !file.existsAsFile())
    {
        obj(result)->setProperty("ok", false);
        obj(result)->setProperty("message", "No saved Console 1 profile for this plugin yet.");
        return result;
    }

    const auto profile = juce::JSON::parse(file);
    clearSlot(slot);
    int loaded = 0, missing = 0;
    if (auto* arr = profile["bindings"].getArray())
        for (auto& bv : *arr)
        {
            Binding b;
            b.slot = slot;
            if (!bindingFromVar(b, bv)) continue;
            b.slot = slot;
            b.id = nextBindingId++;
            if (!b.targetBypass && resolveParam(b) == nullptr) ++missing;
            bindings.push_back(b);
            ++loaded;
        }
    rebuildConsumeMask();
    sentValue.fill(-1);

    obj(result)->setProperty("ok", true);
    obj(result)->setProperty("count", loaded);
    obj(result)->setProperty("missing", missing);
    obj(result)->setProperty("message", "Loaded " + juce::String(loaded) + " controls for " + plugin->getName()
                                        + (missing > 0 ? " (" + juce::String(missing) + " parameters not found)" : juce::String()));
    return result;
}

juce::var Engine::listProfiles()
{
    juce::Array<juce::var> list;
    for (auto& f : profilesFolder().findChildFiles(juce::File::findFiles, false, "*.json"))
        if (!f.getFileName().endsWith(".previous.json"))
            list.add(f.getFileNameWithoutExtension());
    return list;
}

// ---------------- UI bridge ----------------

const juce::StringArray& Engine::bridgeFunctionNames()
{
    static const juce::StringArray names { "c1GetState", "c1ListParams", "c1SetOption", "c1SetDevices", "c1TakeControl",
                                           "c1SetFocus", "c1ArmLearn", "c1CancelLearn", "c1Bind", "c1UpdateBinding",
                                           "c1RemoveBinding", "c1ClearSlot", "c1Inject", "c1SaveProfile", "c1LoadProfile",
                                           "c1ListProfiles" };
    return names;
}

juce::var Engine::invoke(const juce::String& name, const juce::Array<juce::var>& a)
{
    auto arg = [&a](int i, juce::var fallback) { return i < a.size() ? a[i] : fallback; };

    if (name == "c1GetState")      return getState();
    if (name == "c1ListParams")    return listParams((int) arg(0, 0));
    if (name == "c1SetOption")     { setOption(arg(0, "").toString(), arg(1, juce::var())); return getState(); }
    if (name == "c1SetDevices")    { setDevices(arg(0, "").toString(), arg(1, "").toString()); return getState(); }
    if (name == "c1TakeControl")   { takeControl(); return getState(); }
    if (name == "c1SetFocus")      { setFocusSlot((int) arg(0, 0)); return getState(); }
    if (name == "c1ArmLearn")      { armLearn((int) arg(0, 0), (int) arg(1, -1), (bool) arg(2, false)); return getState(); }
    if (name == "c1CancelLearn")   { cancelLearn(); return getState(); }
    if (name == "c1Bind")
    {
        // One binding object, or an array of them (auto-map "Apply").
        const auto v = arg(0, juce::var());
        if (auto* list = v.getArray()) for (auto& b : *list) addBinding(b);
        else addBinding(v);
        return getState();
    }
    if (name == "c1UpdateBinding") { updateBinding((int) arg(0, -1), arg(1, juce::var())); return getState(); }
    if (name == "c1RemoveBinding") { removeBinding((int) arg(0, -1)); return getState(); }
    if (name == "c1ClearSlot")     { clearSlot((int) arg(0, -1)); return getState(); }
    if (name == "c1Inject")        { inject((int) arg(0, 1), (int) arg(1, 0), (int) arg(2, 0)); return {}; }
    if (name == "c1SaveProfile")   return saveProfileForSlot((int) arg(0, -1));
    if (name == "c1LoadProfile")   return loadProfileForSlot((int) arg(0, -1));
    if (name == "c1ListProfiles")  return listProfiles();
    return {};
}

// ---------------- session persistence ----------------

juce::var Engine::toVar() const
{
    juce::var v(new juce::DynamicObject());
    auto* o = obj(v);
    o->setProperty("version", 1);
    o->setProperty("source", source == Source::device ? "device" : source == Source::host ? "host" : "both");
    o->setProperty("feedback", feedbackEnabled);
    o->setProperty("autoLoadProfiles", autoLoadProfiles);
    o->setProperty("trackButtonsFocusSlots", trackButtonsFocusSlots);
    o->setProperty("consumeBoundCcs", consumeBoundCcs.load());
    o->setProperty("defaultKnobMode", defaultKnobMode);
    o->setProperty("focusSlot", focusSlot);
    juce::Array<juce::var> list;
    for (auto& b : bindings) list.add(bindingToVar(b));
    o->setProperty("bindings", list);
    return v;
}

void Engine::fromVar(const juce::var& v)
{
    if (!v.isObject()) return;
    setOption("source", v.getProperty("source", "device"));
    setOption("feedback", v.getProperty("feedback", true));
    setOption("autoLoadProfiles", v.getProperty("autoLoadProfiles", true));
    setOption("trackButtonsFocusSlots", v.getProperty("trackButtonsFocusSlots", true));
    setOption("consumeBoundCcs", v.getProperty("consumeBoundCcs", true));
    setOption("defaultKnobMode", v.getProperty("defaultKnobMode", "absolute"));
    focusSlot = juce::jlimit(0, host.numSlots - 1, (int) v.getProperty("focusSlot", 0));

    bindings.clear();
    if (auto* arr = v["bindings"].getArray())
        for (auto& bv : *arr)
        {
            Binding b;
            if (!bindingFromVar(b, bv)) continue;
            b.id = nextBindingId++;
            bindings.push_back(b);
        }
    rebuildConsumeMask();
    sentValue.fill(-1);
}
}
