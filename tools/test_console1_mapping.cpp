// Unit tests for Source/Console1Mapping.h (no JUCE needed).
//   g++ -std=c++17 -I Source tools/test_console1_mapping.cpp -o /tmp/t && /tmp/t
#include "Console1Mapping.h"
#include <cstdio>
#include <cmath>

using namespace Console1Math;

static int failures = 0, passes = 0;
#define CHECK(cond) do { if (cond) ++passes; else { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

int main()
{
    Range full;                    // 0..1
    Range inv; inv.invert = true;
    Range half; half.min = 0.25f; half.max = 0.75f;

    // absolute
    CHECK(near(ccToNorm(0, full), 0.0f));
    CHECK(near(ccToNorm(127, full), 1.0f));
    CHECK(near(ccToNorm(64, full), 64.0f / 127.0f));
    CHECK(near(ccToNorm(0, inv), 1.0f));
    CHECK(near(ccToNorm(127, half), 0.75f));
    CHECK(near(ccToNorm(-5, full), 0.0f));     // clamps
    CHECK(near(ccToNorm(200, full), 1.0f));

    // round trip cc -> norm -> cc
    bool roundTrip = true;
    for (int cc = 0; cc <= 127; ++cc)
        for (const Range* r : { &full, &inv, &half })
            if (normToCc(ccToNorm(cc, *r), *r) != cc) roundTrip = false;
    CHECK(roundTrip);
    CHECK(normToCc(0.9f, half) == 127); // outside range clamps to the end

    // snapping
    CHECK(near(snapToSteps(0.49f, 2), 0.0f));
    CHECK(near(snapToSteps(0.51f, 2), 1.0f));
    CHECK(near(snapToSteps(0.4f, 5), 0.5f));
    CHECK(near(snapToSteps(0.3333f, 0x7fffffff), 0.3333f)); // continuous untouched

    // pickup: param at 0.5 (cc ~64). Knob at 10 shouldn't move it.
    bool picked = false;
    CHECK(!pickup(10, -1, 0.5f, full, picked).has_value());
    CHECK(!pickup(13, 10, 0.5f, full, picked).has_value());
    CHECK(!picked);
    // jumps across 64 between messages -> catches
    auto p = pickup(70, 60, 0.5f, full, picked);
    CHECK(p.has_value() && picked && near(*p, 70.0f / 127.0f));
    // once caught, follows freely
    p = pickup(20, 70, 0.55f, full, picked);
    CHECK(p.has_value() && near(*p, 20.0f / 127.0f));
    // within tolerance catches immediately
    bool picked2 = false;
    CHECK(pickup(62, -1, 0.5f, full, picked2).has_value());

    // relative
    CHECK(!relative(64, -1, 0.5f, full, 1.0f).has_value());     // first touch only records
    CHECK(!relative(64, 64, 0.5f, full, 1.0f).has_value());     // no movement
    auto r = relative(67, 64, 0.5f, full, 1.0f);
    CHECK(r.has_value() && near(*r, 0.5f + 3.0f / 127.0f));
    r = relative(67, 64, 0.5f, full, 0.25f);                   // fine: quarter the step
    CHECK(r.has_value() && near(*r, 0.5f + 0.75f / 127.0f));
    r = relative(67, 64, 0.999f, full, 1.0f);                  // clamps at top
    CHECK(r.has_value() && near(*r, 1.0f));
    r = relative(67, 64, 0.5f, inv, 1.0f);                     // inverted goes down
    CHECK(r.has_value() && *r < 0.5f);
    r = relative(67, 64, 0.74f, half, 1.0f);                   // range-limited
    CHECK(r.has_value() && *r <= 0.75f + 1e-6f);
    // fine mode keeps working when the ring is resynced to the param each time:
    // the hardware keeps reporting +3 from the same resynced position.
    float v = 0.5f;
    for (int i = 0; i < 40; ++i)
    {
        const int ring = normToCc(v, full);
        v = *relative(ring + 3, ring, v, full, 0.25f);
    }
    CHECK(v > 0.5f + 0.2f); // actually moved a meaningful distance

    // toggle / momentary
    CHECK(!toggle(0, 0.0f, full).has_value());
    CHECK(near(*toggle(127, 0.0f, full), 1.0f));
    CHECK(near(*toggle(127, 1.0f, full), 0.0f));
    CHECK(near(momentary(127, full), 1.0f));
    CHECK(near(momentary(0, full), 0.0f));
    CHECK(ledOn(1.0f, full) && !ledOn(0.0f, full));

    // mode names round trip
    for (auto m : { Mode::absolute, Mode::pickup, Mode::relative, Mode::toggle, Mode::momentary })
        CHECK(modeFromName(modeName(m), Mode::absolute) == m);
    CHECK(modeFromName("nonsense", Mode::pickup) == Mode::pickup);

    std::printf("%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
