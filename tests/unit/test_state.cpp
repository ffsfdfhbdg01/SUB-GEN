// SPDX-License-Identifier: AGPL-3.0-or-later
// The plug-in's state (SPEC_IO_UI §9; T-STATE-01, T-STATE-04), offline rendering (T-INV-03), the background Auto Align
// (SPEC §8.B) and the placeholder editor.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "ProcessorHarness.h"
#include "params/Parameters.h"
#include "analysis/AutoAlign.h"
#include "plugin/StateIO.h"
#include "support/AllocGuard.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::test;
using plugin::SubGenProcessor;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;

Channels same(const std::vector<double>& x, int channels) { return Channels(static_cast<size_t>(channels), x); }

bool finite(const Channels& y)
{
    for (const auto& c : y)
        for (const double v : c)
            if (!std::isfinite(v))
                return false;
    return true;
}

struct Xorshift
{
    std::uint64_t s;
    std::uint64_t operator()()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
};

float raw(SubGenProcessor& p, const char* id) { return p.parameters().getRawParameterValue(id)->load(); }

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// A fresh parameter starts at convertFrom0to1(default); a value set through JUCE comes back through convertTo0to1 and
// convertFrom0to1, which can land a float away (a log range's 160 Hz: 159.999969 fresh, 159.999954 set), so a default
// set from a state matches a fresh one to float precision.
bool sameDefault(float a, float b) { return std::abs(a - b) <= 1e-6f * std::max(1.0f, std::abs(b)); }

// every parameter away from its default, a different random value each
void randomise(SubGenProcessor& p, Xorshift& next)
{
    for (auto* param : p.getParameters())
        param->setValueNotifyingHost(static_cast<float>(next() % 100001) / 100000.0f);
}

std::vector<float> rawValues(SubGenProcessor& p)
{
    std::vector<float> v;
    for (const auto& spec : params::kParamSpecs)
        v.push_back(raw(p, spec.id));
    return v;
}

plugin::ExtraState someExtra()
{
    plugin::ExtraState e;
    e.uiScale = 1.25;
    e.uiPage = 3;
    e.abSlot = 1;
    e.theme = "light";
    e.alignPolarity = -1;
    e.alignRotationDeg = -45.0;
    e.alignConfidenceDb = 2.5;
    e.presetName = "Big Hit";
    e.presetCategory = "Cinematic Boom/Impact";
    return e;
}

bool sameExtra(const plugin::ExtraState& a, const plugin::ExtraState& b)
{
    return a.uiScale == b.uiScale && a.uiPage == b.uiPage && a.abSlot == b.abSlot && a.theme == b.theme
        && a.alignPolarity == b.alignPolarity && a.alignRotationDeg == b.alignRotationDeg
        && a.alignConfidenceDb == b.alignConfidenceDb && a.presetName == b.presetName
        && a.presetCategory == b.presetCategory;
}

// A content fault in a decodable state and what it must load as.
struct Fault
{
    bool rejected = false;             // the whole state: every parameter and the extra state at their defaults
    std::map<std::string, int> params; // per id: 1 its default, 2 its range's maximum, 3 its minimum
};

Fault damage(juce::XmlElement& xml, Xorshift& next)
{
    Fault f;
    std::vector<juce::XmlElement*> params;
    for (auto* c : xml.getChildWithTagNameIterator("PARAM"))
        params.push_back(c);
    auto* victim = params[static_cast<size_t>(next() % params.size())];
    const auto id = victim->getStringAttribute("id").toStdString();
    static const char* const garbage[] = {"abc", "", "nan", "inf", "-inf", "1e400", "1.5.2", " "};
    switch (next() % 7)
    {
    case 0:
        xml.removeChildElement(victim, true);
        f.params[id] = 1;
        break;
    case 1:
        victim->setAttribute("value", garbage[next() % std::size(garbage)]);
        f.params[id] = 1;
        break;
    case 2:
        victim->setAttribute("value", "1e6");
        f.params[id] = 2;
        break;
    case 3:
        victim->setAttribute("value", "-1e6");
        f.params[id] = 3;
        break;
    case 4:
    {
        auto* unknown = xml.createNewChildElement("PARAM");
        unknown->setAttribute("id", "no_such_param");
        unknown->setAttribute("value", "1");
        break;
    }
    case 5:
        xml.setTagName("OTHER");
        f.rejected = true;
        break;
    default:
        xml.setAttribute("stateVersion", next() % 2 == 0 ? "2" : "abc");
        f.rejected = true;
        break;
    }
    return f;
}

// 80 Hz with a quiet 40 Hz in phase `deg`: DIVIDE's Sub -1 at 40 Hz (sub_level 0 dB) beside the dry's 80 Hz, so the
// sub's rotation moves the low end's peak (Headroom has a clear optimum; the 80 Hz holds most of the energy, so Energy
// barely moves)
Channels twoTones(double seconds, double deg)
{
    const auto n = static_cast<size_t>(seconds * kFs);
    std::vector<double> x(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / kFs;
        x[i] = 0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * 80.0 * t)
             + 0.02 * std::sin(2.0 * juce::MathConstants<double>::pi * 40.0 * t + deg * juce::MathConstants<double>::pi / 180.0);
    }
    return same(x, 2);
}

// a rotation psi (degrees) in the canonical form the parameters hold: |theta| <= 90 and a polarity
void setRotation(SubGenProcessor& p, double psiDeg)
{
    double psi = std::remainder(psiDeg, 360.0);
    const bool invert = std::abs(psi) > 90.0;
    if (invert)
        psi = std::remainder(psi + 180.0, 360.0);
    setParam(p, "sub_pol", invert ? 1.0f : 0.0f);
    setParam(p, "sub_rot", static_cast<float>(psi));
}
} // namespace

TEST_CASE("T-STATE-01 all 141 parameters round-trip bit-identically", "[state]")
{
    REQUIRE(params::kNumParams == 141);
    auto p = makeProcessor({});
    Xorshift next{0x2545F4914F6CDD1Dull};
    randomise(*p, next);
    p->setExtraState(someExtra());
    juce::MemoryBlock blob;
    p->getStateInformation(blob);

    auto q = makeProcessor({});
    q->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    for (size_t i = 0; i < params::kParamSpecs.size(); ++i)
    {
        const char* id = params::kParamSpecs[i].id;
        INFO(id);
        // the values the processor reads (the APVTS's); a parameter's own normalised value, which hosts show, can sit a
        // float away from them (JUCE keeps both) and a bool keeps whatever normalised value it was given
        CHECK(sameBits(raw(*q, id), raw(*p, id)));
    }
    CHECK(sameExtra(q->extraState(), someExtra()));
    // and once more: a loaded state saves as it loaded
    juce::MemoryBlock again;
    q->getStateInformation(again);
    auto r = makeProcessor({});
    r->setStateInformation(again.getData(), static_cast<int>(again.getSize()));
    CHECK(rawValues(*r) == rawValues(*p));
}

TEST_CASE("the state XML holds stateVersion 1, every parameter, UI, AB, ALIGN and PRESET; the host gets it compressed",
          "[state]")
{
    auto p = makeProcessor({});
    p->setExtraState(someExtra());
    juce::MemoryBlock blob;
    p->getStateInformation(blob);
    const auto xml = plugin::decompressState(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE(xml != nullptr);
    CHECK(xml->hasTagName("SUBGEN"));
    CHECK(xml->getIntAttribute("stateVersion") == 1);
    int params = 0;
    for (auto* c : xml->getChildWithTagNameIterator("PARAM"))
        params += c->hasAttribute("id") && c->hasAttribute("value") ? 1 : 0;
    CHECK(params == 141);
    for (const char* tag : {"UI", "AB", "ALIGN", "PRESET"})
    {
        INFO(tag);
        CHECK(xml->getChildByName(tag) != nullptr);
    }
    CHECK(xml->getChildByName("UI")->getDoubleAttribute("scale") == 1.25);
    CHECK(xml->getChildByName("ALIGN")->getIntAttribute("pol") == -1);
    CHECK(xml->getChildByName("PRESET")->getStringAttribute("name") == "Big Hit");
    CHECK(blob.getSize() < xml->toString().getNumBytesAsUTF8()); // compressed
}

TEST_CASE("migrate takes a v1 SUB-GEN state and refuses other roots and versions", "[state]")
{
    auto state = [](const char* tag, const char* version) {
        auto x = std::make_unique<juce::XmlElement>(tag);
        if (version != nullptr)
            x->setAttribute("stateVersion", version);
        return x;
    };
    CHECK(plugin::migrate(state("SUBGEN", "1")) != nullptr);
    CHECK(plugin::migrate(state("SUBGEN", "2")) == nullptr); // a newer plug-in's
    CHECK(plugin::migrate(state("SUBGEN", "0")) == nullptr);
    CHECK(plugin::migrate(state("SUBGEN", nullptr)) == nullptr);
    CHECK(plugin::migrate(state("SUBGEN", "1x")) == nullptr);
    CHECK(plugin::migrate(state("OTHER", "1")) == nullptr);
    CHECK(plugin::migrate(nullptr) == nullptr);
}

TEST_CASE("a state a float away from the current values loads exactly", "[state]")
{
    // the APVTS ignores a change within a float epsilon of its value: loading steps past that (at 99 % Drive one step
    // of the normalised value is less than that)
    auto p = makeProcessor({});
    p->parameters().getParameter("drive")->setValueNotifyingHost(0.99f);
    const float before = raw(*p, "drive");
    auto q = makeProcessor({});
    q->parameters().getParameter("drive")->setValueNotifyingHost(std::nextafter(0.99f, 1.0f));
    const float next = raw(*q, "drive");
    REQUIRE_FALSE(sameBits(next, before));
    REQUIRE(juce::approximatelyEqual(next, before));
    juce::MemoryBlock blob;
    q->getStateInformation(blob);
    p->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    CHECK(sameBits(raw(*p, "drive"), next));
}

TEST_CASE("deeply nested or oversized XML is refused before parsing", "[state]")
{
    auto zipped = [](const juce::String& text) {
        juce::MemoryBlock block;
        {
            juce::MemoryOutputStream out(block, false);
            juce::GZIPCompressorOutputStream zip(out, 9);
            zip.write(text.toRawUTF8(), text.getNumBytesAsUTF8());
        }
        return block;
    };
    // JUCE's parser recurses once per level: 100000 levels (700 KB) would overrun the stack
    const auto deep = zipped("<SUBGEN stateVersion=\"1\">" + juce::String::repeatedString("<a>", 100000)
                             + juce::String::repeatedString("</a>", 100000) + "</SUBGEN>");
    CHECK(plugin::decompressState(deep.getData(), static_cast<int>(deep.getSize())) == nullptr);
    // a v1 state is about 8 KB: over 1 MB unpacked is not one
    const auto big = zipped("<SUBGEN stateVersion=\"1\"><PRESET name=\"" + juce::String::repeatedString("x", 2 << 20)
                            + "\"/></SUBGEN>");
    CHECK(plugin::decompressState(big.getData(), static_cast<int>(big.getSize())) == nullptr);
    auto p = makeProcessor({});
    p->setStateInformation(deep.getData(), static_cast<int>(deep.getSize()));
    p->setStateInformation(big.getData(), static_cast<int>(big.getSize()));
    CHECK(sameExtra(p->extraState(), plugin::ExtraState{}));
}

TEST_CASE("XML built to slip past the nesting check is refused: comments, CDATA, a quote in a closing tag", "[state]")
{
    // JUCE's parser reads a closing tag to the first '>', CDATA to "]]>" and skips comments and processing
    // instructions anywhere; a pre-scan that reads any of them differently can be steered to undercount the nesting,
    // and 90000 levels (under the 1 MB cap) overrun the stack. The scan accepts only what a state holds.
    auto zipped = [](const juce::String& text) {
        juce::MemoryBlock block;
        {
            juce::MemoryOutputStream out(block, false);
            juce::GZIPCompressorOutputStream zip(out, 9);
            zip.write(text.toRawUTF8(), text.getNumBytesAsUTF8());
        }
        return block;
    };
    auto refused = [&](const juce::String& text) {
        const auto blob = zipped(text);
        return plugin::decompressState(blob.getData(), static_cast<int>(blob.getSize())) == nullptr;
    };
    constexpr int n = 90000;
    const auto open = juce::String::repeatedString("<a>", n), close = juce::String::repeatedString("</a>", n);
    const juce::String root = "<SUBGEN stateVersion=\"1\">";
    CHECK(refused(root + "<![CDATA[" + close + "]]>" + open + close + "</SUBGEN>")); // closings hidden as text
    CHECK(refused(root + "<b></x '>" + open + close + "'></SUBGEN>"));              // a quote JUCE does not see
    CHECK(refused(root + "<!-- " + close + " -->" + open + close + "</SUBGEN>"));
    CHECK(refused(root + "<?pi " + close + " ?>" + open + close + "</SUBGEN>"));
    CHECK(refused(close + root + open + close + "</SUBGEN>")); // closings first, to start below zero
    // and nothing a state never holds: a DOCTYPE, a comment, a header not at the start, a bare '<' in a value
    CHECK(refused("<!DOCTYPE x [<!ENTITY a \"b\">]>" + root + "</SUBGEN>"));
    CHECK(refused(root + "<!-- a comment --></SUBGEN>"));
    CHECK(refused(root + "<?xml version=\"1.0\"?></SUBGEN>"));
    CHECK(refused(root + "<PRESET name=\"a<b\"/></SUBGEN>"));
    // what JUCE writes still loads, header and all
    CHECK_FALSE(refused("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" + root
                        + "<PARAM id=\"drive\" value=\"33.0\"/>\n  <UI scale=\"1.0\" theme=\"a &amp; b\"/><AB/></SUBGEN>"));
}

TEST_CASE("T-STATE-04 corrupt and unknown states (1000 fuzzed blobs) fall back to defaults without crashing", "[state]")
{
    const auto defaults = rawValues(*makeProcessor({}));
    auto source = makeProcessor({});
    Xorshift next{0x9E3779B97F4A7C15ull};
    randomise(*source, next);
    source->setExtraState(someExtra());
    const auto kept = rawValues(*source);
    juce::MemoryBlock valid;
    source->getStateInformation(valid);
    const auto validXml = plugin::decompressState(valid.getData(), static_cast<int>(valid.getSize()));
    REQUIRE(validXml != nullptr);

    auto p = makeProcessor({});
    int decoded = 0, checkedFaults = 0;
    bool ok = true, inRange = true;
    for (int k = 0; k < 1000; ++k)
    {
        juce::MemoryBlock blob;
        Fault fault;
        bool known = false; // a content fault: the result is fully known
        switch (k % 5)
        {
        case 0: // random bytes
            blob.setSize(static_cast<size_t>(next() % 2048));
            for (size_t i = 0; i < blob.getSize(); ++i)
                blob[i] = static_cast<char>(next());
            break;
        case 1: // the valid blob with bytes flipped
            blob = valid;
            for (int f = 0, n = 1 + static_cast<int>(next() % 8); f < n; ++f)
                blob[static_cast<size_t>(next() % blob.getSize())] ^= static_cast<char>(1 + next() % 255);
            break;
        case 2: // the valid blob cut short
            blob = valid;
            blob.setSize(static_cast<size_t>(next() % valid.getSize()));
            break;
        case 3: // decodable, with a fault in its content
        {
            juce::XmlElement xml(*validXml);
            fault = damage(xml, next);
            blob = plugin::compressState(xml);
            known = true;
            break;
        }
        default: // XML as text, not compressed, and JUCE's own binary XML
        {
            if (next() % 2 == 0)
            {
                const auto text = validXml->toString();
                blob.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
            }
            else
                juce::AudioProcessor::copyXmlToBinary(*validXml, blob);
            break;
        }
        }
        p->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
        const auto xml = plugin::migrate(plugin::decompressState(blob.getData(), static_cast<int>(blob.getSize())));
        decoded += xml != nullptr ? 1 : 0;
        for (size_t i = 0; i < params::kParamSpecs.size(); ++i)
        {
            const auto& spec = params::kParamSpecs[i];
            const float v = raw(*p, spec.id);
            inRange = inRange && std::isfinite(v) && v >= spec.min - 1e-4 * std::abs(spec.min) - 1e-6
                   && v <= spec.max + 1e-4 * std::abs(spec.max) + 1e-6;
            if (xml == nullptr)
                ok = ok && sameDefault(v, defaults[i]);
            else if (known)
            {
                const auto it = fault.params.find(spec.id);
                const int kind = it == fault.params.end() ? 0 : it->second;
                if (kind == 0)
                    ok = ok && sameBits(v, kept[i]);
                else if (kind == 1)
                    ok = ok && sameDefault(v, defaults[i]);
                else
                {
                    const double edge = kind == 2 ? spec.max : spec.min;
                    ok = ok && std::abs(v - edge) <= 1e-5 * std::max(1.0, std::abs(edge));
                }
            }
        }
        if (known && !fault.rejected)
        {
            ++checkedFaults;
            ok = ok && sameExtra(p->extraState(), someExtra());
        }
        if (xml == nullptr)
            ok = ok && sameExtra(p->extraState(), plugin::ExtraState{});
        if (!ok)
        {
            INFO("blob " << k << ", kind " << k % 5);
            CHECK(ok);
            break;
        }
    }
    CHECK(ok);
    CHECK(inRange);
    CHECK(decoded > 100); // the content faults decode: parameter by parameter fallback was exercised
    CHECK(checkedFaults > 100);
    const auto y = render(*p, same(sig::bass808(kFs), 2));
    CHECK(finite(y));
}

TEST_CASE("T-INV-03 setNonRealtime(true) renders identically to real time", "[state]")
{
    const auto x = same(sig::bass808(kFs), 2);
    for (const bool linear : {false, true}) // Mix, and Print with Linear Phase (offline waits for each kernel)
    {
        INFO("Linear Phase " << linear);
        auto setup = [linear](SubGenProcessor& s) {
            setParam(s, "drive", 40.0f);
            if (linear)
            {
                setParam(s, "quality", 2.0f);
                setParam(s, "filt_phase", 1.0f);
            }
        };
        auto live = makeProcessor({}, kFs, 512, setup);
        REQUIRE(live->waitForLinearPhase(20000));
        const auto a = render(*live, x);
        auto offline = makeProcessor({}, kFs, 512, [&](SubGenProcessor& s) {
            setup(s);
            s.setNonRealtime(true);
        });
        const auto b = render(*offline, x);
        CHECK(a == b);
        CHECK(finite(a));
    }
}

TEST_CASE("requestAutoAlign sets polarity and rotation unless align_lock", "[state]")
{
    const auto x = twoTones(3.0, 120.0);
    for (const bool locked : {false, true})
    {
        INFO("align_lock " << locked);
        auto p = makeProcessor({}, kFs, 512, [locked](SubGenProcessor& s) {
            setParam(s, "align_lock", locked ? 1.0f : 0.0f);
            setParam(s, "align_obj", 1.0f); // Headroom
            setParam(s, "sub_level", 0.0f);
        });
        render(*p, x);
        p->requestAutoAlign();
        REQUIRE(p->waitForAutoAlign(60000));
        p->applyPendingConfiguration(); // the message thread's half
        const auto r = p->lastAutoAlign();
        INFO("polarity " << r.polarity << ", rotation " << r.rotationDeg << ", confidence " << r.confidenceDb << " dB");
        REQUIRE(r.clearOptimum);
        CHECK_FALSE((r.polarity == 1 && r.rotationDeg == 0.0)); // not the identity: the parameters must move
        const auto e = p->extraState(); // <ALIGN/> holds the result either way
        CHECK(e.alignPolarity == r.polarity);
        CHECK(e.alignRotationDeg == r.rotationDeg);
        CHECK(e.alignConfidenceDb == r.confidenceDb);
        if (locked)
        {
            CHECK(getParam(*p, "sub_pol") == 0.0f);
            CHECK(getParam(*p, "sub_rot") == 0.0f);
        }
        else
        {
            CHECK(getParam(*p, "sub_pol") == (r.polarity < 0 ? 1.0f : 0.0f));
            CHECK(std::abs(getParam(*p, "sub_rot") - r.rotationDeg) < 1e-3);
        }
    }
}

TEST_CASE("Auto Align without a clear optimum leaves sub_pol and sub_rot as they are", "[state]")
{
    // 80 Hz alone: DIVIDE's 40 Hz sub never meets the dry, so every rotation gives the low end the same energy; the
    // result (the identity, by the tie rule) is kept in <ALIGN/> but must not undo the user's rotation
    auto p = makeProcessor({}, kFs, 512, [](SubGenProcessor& s) { setParam(s, "sub_rot", 45.0f); });
    render(*p, same(sig::sine(80.0, kFs, 2.0, -12.0), 2));
    p->requestAutoAlign();
    REQUIRE(p->waitForAutoAlign(60000));
    p->applyPendingConfiguration();
    const auto r = p->lastAutoAlign();
    INFO("polarity " << r.polarity << ", rotation " << r.rotationDeg << ", confidence " << r.confidenceDb << " dB");
    CHECK_FALSE(r.clearOptimum);
    CHECK(p->extraState().alignConfidenceDb == r.confidenceDb);
    CHECK(std::abs(getParam(*p, "sub_rot") - 45.0f) < 1e-3f);
    CHECK(getParam(*p, "sub_pol") == 0.0f);
}

TEST_CASE("Auto Align finds the output's own optimum: its rotation beats 30 degrees to either side", "[state]")
{
    // the capture must pair the dry and the sub as they meet at the output (the delay; the band without a phase
    // shift): otherwise the analysed optimum misses the output's. Headroom: the low end's peak, read the same way.
    auto setup = [](SubGenProcessor& s) {
        setParam(s, "align_obj", 1.0f);
        setParam(s, "sub_level", 0.0f);
    };
    for (const double deg : {120.0, -60.0})
    {
        INFO("40 Hz at " << deg << " degrees");
        const auto x = twoTones(3.0, deg);
        auto p = makeProcessor({}, kFs, 512, setup);
        render(*p, x);
        p->requestAutoAlign();
        REQUIRE(p->waitForAutoAlign(60000));
        p->applyPendingConfiguration();
        const auto r = p->lastAutoAlign();
        REQUIRE(r.clearOptimum);
        const double psi = r.rotationDeg + (r.polarity < 0 ? 180.0 : 0.0);
        auto peak = [&](double at) {
            auto q = makeProcessor({}, kFs, 512, [&](SubGenProcessor& s) {
                setup(s);
                setRotation(s, at);
            });
            const auto band = analysis::zeroPhaseBand(render(*q, x)[0], kFs, 15.0, 120.0); // [range_lo/2, range_hi]
            double m = 0.0;
            for (size_t i = 72000; i < 120000; ++i) // 1.5-2.5 s
                m = std::max(m, std::abs(band[i]));
            return 20.0 * std::log10(m);
        };
        const double best = peak(psi), up = peak(psi + 30.0), down = peak(psi - 30.0);
        INFO("psi " << psi << ": peak " << best << " dB, +30: " << up << " dB, -30: " << down << " dB");
        CHECK(best < up);
        CHECK(best < down);
    }
}

TEST_CASE("the editor opens and closes 100 times without leaks", "[state]")
{
    // A GUI session (JUCE up, a processor, `editors` editors opened and closed, JUCE down) and what it leaves allocated
    // on this thread. Without a message loop the messages an editor posts wait in the queue until JUCE shuts down, so
    // whole sessions are compared: 100 editors must leave what 10 leave.
    auto session = [](int editors) {
        bool ok = true;
        const AllocGuard guard;
        {
            const juce::ScopedJuceInitialiser_GUI gui;
            auto p = makeProcessor({});
            for (int i = 0; i < editors; ++i)
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditorAndMakeActive());
                ok = ok && editor != nullptr && p->getActiveEditor() == editor.get();
                p->editorBeingDeleted(editor.get()); // as a host's wrapper does before it deletes the editor
                editor.reset();
                ok = ok && p->getActiveEditor() == nullptr;
            }
        }
        return std::pair{ok, guard.live()};
    };
    session(10); // JUCE's lasting caches (fonts, the look and feel, the string pool)
    const auto [okFew, few] = session(10);
    const auto [okMany, many] = session(100);
    INFO("left after 10 editors: " << few << ", after 100: " << many);
    CHECK(okFew);
    CHECK(okMany);
    CHECK(std::abs(many - few) < 90); // under one per editor: an editor's component tree is thousands
}
