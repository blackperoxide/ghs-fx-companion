// Headless UI test server for the Console 1 tab.
// Runs the REAL Console1::Engine with fake plugins (FabFilter / UAD style
// parameter names) and serves Resources/webui over HTTP. A tiny bridge
// script stands in for JUCE's WebView bridge: native calls become POST
// /invoke (handled by Engine::invoke, the same dispatcher the plugin editor
// uses) and the editor's 30 Hz "c1Live" event becomes polling of /live.
//
//   tools/build_console1_engine_test.sh   # also builds this
//   HOME=/tmp/c1ui_home /tmp/c1engine/ui_server 8765 &
//   open http://localhost:8765/console1-test.html
#include "console1_fakes.h"
#include <thread>

using Param = std::pair<juce::String, juce::String>;

static std::vector<Param> proQ()
{
    std::vector<Param> ps { { "out", "Output Gain" }, { "pan", "Output Pan" } };
    for (int b = 1; b <= 6; ++b)
        for (auto* what : { "Used", "Enabled", "Frequency", "Gain", "Q", "Shape" })
            ps.push_back({ "b" + juce::String(b) + what, "Band " + juce::String(b) + " " + what });
    return ps;
}

struct UiServer : juce::Thread
{
    UiServer(Console1::Engine& e, juce::File root, int p) : juce::Thread("c1ui"), engine(e), webRoot(root), port(p) {}
    Console1::Engine& engine;
    juce::File webRoot;
    int port;
    juce::StreamingSocket listener;

    void run() override
    {
        if (!listener.createListener(port, "127.0.0.1")) { std::printf("cannot listen on %d\n", port); return; }
        std::printf("listening on http://127.0.0.1:%d/console1-test.html\n", port);
        std::fflush(stdout);
        while (!threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> c(listener.waitForNextConnection());
            if (c != nullptr) handle(*c);
        }
    }

    static juce::String readRequest(juce::StreamingSocket& c)
    {
        juce::MemoryBlock buf;
        char tmp[8192];
        for (;;)
        {
            if (c.waitUntilReady(true, 2000) != 1) break;
            const int n = c.read(tmp, sizeof(tmp), false);
            if (n <= 0) break;
            buf.append(tmp, (size_t) n);
            const auto s = buf.toString();
            const int headerEnd = s.indexOf("\r\n\r\n");
            if (headerEnd >= 0)
            {
                const int len = s.fromFirstOccurrenceOf("Content-Length:", false, true).getIntValue();
                if ((int) buf.getSize() >= headerEnd + 4 + len) break;
            }
        }
        return buf.toString();
    }

    static void reply(juce::StreamingSocket& c, int code, const juce::String& type, const juce::MemoryBlock& body)
    {
        juce::String h;
        h << "HTTP/1.1 " << code << (code == 200 ? " OK" : " Not Found") << "\r\nContent-Type: " << type
          << "\r\nContent-Length: " << (int) body.getSize() << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        c.write(h.toRawUTF8(), (int) h.getNumBytesAsUTF8());
        if (body.getSize() > 0) c.write(body.getData(), (int) body.getSize());
    }

    template <typename Fn> juce::var onMessageThread(Fn&& fn)
    {
        juce::var result;
        juce::WaitableEvent done;
        juce::MessageManager::callAsync([&] { result = fn(); done.signal(); });
        done.wait(5000);
        return result;
    }

    void handle(juce::StreamingSocket& c)
    {
        const auto req = readRequest(c);
        const auto line = req.upToFirstOccurrenceOf("\r\n", false, false);
        const auto method = line.upToFirstOccurrenceOf(" ", false, false);
        auto path = line.fromFirstOccurrenceOf(" ", false, false).upToFirstOccurrenceOf(" ", false, false).upToFirstOccurrenceOf("?", false, false);
        const auto body = req.fromFirstOccurrenceOf("\r\n\r\n", false, false);
        auto json = [&](const juce::var& v) { const auto s = juce::JSON::toString(v, true); reply(c, 200, "application/json", juce::MemoryBlock(s.toRawUTF8(), s.getNumBytesAsUTF8())); };

        if (method == "POST" && path == "/invoke")
        {
            const auto v = juce::JSON::parse(body);
            const auto name = v["name"].toString();
            juce::Array<juce::var> args;
            if (auto* a = v["params"].getArray()) args = *a;
            json(onMessageThread([&] { return engine.invoke(name, args); }));
            return;
        }
        if (path == "/live") { json(onMessageThread([&] { return engine.getLiveState(); })); return; }

        if (path == "/") path = "/console1-test.html";
        auto f = webRoot.getChildFile(path.substring(1));
        if (path == "/console1-test.html") f = juce::File::getCurrentWorkingDirectory().getChildFile("tools/console1-test.html");
        if (!f.existsAsFile() || !f.isAChildOf(webRoot.getParentDirectory().getParentDirectory()))
        { reply(c, 404, "text/plain", juce::MemoryBlock("not found", 9)); return; }
        juce::MemoryBlock data; f.loadFileAsData(data);
        const auto ext = f.getFileExtension();
        reply(c, 200, ext == ".html" ? "text/html" : ext == ".css" ? "text/css" : ext == ".js" ? "text/javascript" : "application/octet-stream", data);
    }
};

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const int port = argc > 1 ? juce::String(argv[1]).getIntValue() : 8765;

    std::array<std::unique_ptr<FakePlugin>, 8> slots;
    std::array<std::unique_ptr<juce::AudioParameterBool>, 8> bypass;
    for (int i = 0; i < 8; ++i)
        bypass[(size_t) i] = std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "byp" + juce::String(i), 1 }, "Bypass", false);

    slots[0] = std::make_unique<FakePlugin>("Pro-Q 3", proQ(), "FabFilter");
    slots[1] = std::make_unique<FakePlugin>("Pro-C 2", std::vector<Param> {
        { "style", "Style" }, { "thresh", "Threshold" }, { "ratio", "Ratio" }, { "knee", "Knee" }, { "range", "Range" },
        { "attack", "Attack" }, { "release", "Release" }, { "out", "Output Level" }, { "mix", "Mix" } }, "FabFilter");
    slots[3] = std::make_unique<FakePlugin>("UAD 1176 Rev A", std::vector<Param> {
        { "in", "Input" }, { "out", "Output" }, { "attack", "Attack" }, { "release", "Release" }, { "ratio", "Ratio" }, { "mix", "Mix" } }, "Universal Audio");

    Console1::Engine::Host host;
    host.numSlots = 8;
    host.pluginInSlot = [&](int s) -> juce::AudioPluginInstance* { return juce::isPositiveAndBelow(s, 8) ? slots[(size_t) s].get() : nullptr; };
    host.bypassForSlot = [&](int s) -> juce::AudioParameterBool* { return juce::isPositiveAndBelow(s, 8) ? bypass[(size_t) s].get() : nullptr; };

    Console1::Engine engine(host);
    engine.takeControl();

    UiServer server(engine, juce::File::getCurrentWorkingDirectory().getChildFile("Resources/webui"), port);
    server.startThread();
    juce::MessageManager::getInstance()->runDispatchLoop();
    return 0;
}
