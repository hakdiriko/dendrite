#include "Glossary.h"
#include "../rig/Identities.h"
#include "../rig/Recording.h"
#include "dsp/FlyData.h"
#include <algorithm>
#include <map>

namespace dendrite::ui::glossary
{
namespace
{
    using P = rig::Recording;
    using M = rig::Recording::Method;

    Entry make (const char* key, const char* head, const char* kind, const char* body, const char* here, juce::StringArray see,
                juce::StringArray words = {}, int population = -1, int method = -1, const char* typeName = "")
    {
        Entry e;
        e.key = juce::String (juce::CharPointer_UTF8 (key)).toLowerCase();
        e.head = juce::String (juce::CharPointer_UTF8 (head));
        e.kind = juce::String (juce::CharPointer_UTF8 (kind));
        e.body = juce::String (juce::CharPointer_UTF8 (body));
        e.inDendrite = juce::String (juce::CharPointer_UTF8 (here));
        e.see = see;
        e.words = words;
        e.population = population;
        e.method = method;
        e.typeName = typeName;
        return e;
    }

    std::vector<Entry> build()
    {
        std::vector<Entry> t;
        // the specimen's parts
        t.push_back (make ("dnp01", "DNp01 \xc2\xb7 giant fiber", "descending neuron, one each side",
                           "The largest descending neuron in the fly. It carries a looming shadow from the eyes, and a loud sound from the ears, down to the legs and wings in a single step: the fly jumps before it has decided anything.",
                           "Past its threshold the tape leaps, dives, ducks or sinks, and the moment is thrown into the echo. The atlas's jumpy sets the threshold; lurch sets how readily a jump becomes a big one.",
                           { "lplc2", "jo", "jumpy", "lurch", "escape", "reflex" }, { "reflex", "throw" }, P::giantFiber, -1, "DNp01"));
        t.push_back (make ("lplc2", "LPLC2 \xc2\xb7 looming", "visual projection neurons, both eyes",
                           "Lobula plate and lobula columnar cells tuned to something growing fast in view: an object on a collision course. They are the giant fiber's main visual input.",
                           "Hits in your music, and the kick on the sidechain, loom here. They tire with use (the atlas's eyes tire) and recover (eyes back); the instruments read how fresh they are.",
                           { "dnp01", "eyes tire", "eyes back", "sidechain" }, { "kick" }, P::eyesLeft, -1, "LPLC2"));
        t.push_back (make ("jo", "JO \xc2\xb7 Johnston's organ", "the ear, in the second segment of each antenna",
                           "Hundreds of stretch-sensitive neurons that hear the antenna move: near-field sound, wind, gravity. In the fly they carry courtship song.",
                           "Your track is what the antennae hear, left and right. Low vibration, sound and its loudness reach different groups of JO neurons.",
                           { "dnp01", "stimulus", "dng11" }, {}, P::earsLeft, -1, "JO"));
        t.push_back (make ("kc", "KC \xc2\xb7 Kenyon cells", "the mushroom body's intrinsic neurons",
                           "About two thousand neurons that each listen to a few smell channels at random, so every odour lights a sparse, unique set. The fly's associative memory lives in their output synapses.",
                           "The groove's smell reaches them. Their synapses onto the MBONs learn when the fly is hurt (a jump), which is the memory reading.",
                           { "mbon", "pam", "ppl1", "memory", "learn", "forget" }, {}, P::kenyon, -1, "KC"));
        t.push_back (make ("mbon", "MBON \xc2\xb7 mushroom body output", "output neurons of the mushroom body",
                           "A few dozen neurons that read the Kenyon cells and tell the fly to approach or avoid.",
                           "Their balance is the fly's feeling: keep away or come closer. It colours where the grooming scratches and how readily it moves.",
                           { "kc", "fear", "memory" }, {}, P::mbon, -1, "MBON"));
        t.push_back (make ("ppl1", "PPL1", "dopamine neurons, punishment",
                           "Dopaminergic neurons that teach the mushroom body what hurt.",
                           "They fire when the fly is startled; the synapses they reach weaken, so the fly learns to fear what it smelled then.",
                           { "pam", "kc", "fear" }, {}, P::ppl1, -1, "PPL1"));
        t.push_back (make ("pam", "PAM", "dopamine neurons, reward",
                           "Dopaminergic neurons that teach the mushroom body what was good.",
                           "Part of the fly's learning, with PPL1.",
                           { "ppl1", "kc", "memory" }, {}, P::pam, -1, "PAM"));
        t.push_back (make ("dng11", "DNg11 \xc2\xb7 grooming", "descending neurons",
                           "Descending neurons that drive the front legs to clean the antennae.",
                           "When they fire the fly grooms: a narrow band of the echo is scratched, at the frequency where your music leaves room. The atlas's scratch sets how hard.",
                           { "scratch", "groom", "jo" }, { "groom" }, P::groom, -1, "DNg11"));
        t.push_back (make ("steering", "DNa01, DNa02 \xc2\xb7 steering", "descending neurons, each side",
                           "Descending neurons that turn the fly in flight and on foot, toward one side.",
                           "Their imbalance leans the echo's space left or right. The atlas's lean sets how far; the instruments' steer reads the lean the echo gets.",
                           { "lean", "steer" }, {}, P::steerLeft, -1, "DNa01"));
        t.push_back (make ("smell", "olfactory projection neurons", "the antennal lobe to the mushroom body",
                           "The neurons that carry each smell channel from the antennal lobe up to the Kenyon cells and the lateral horn.",
                           "The groove's shape (its density and brightness) is turned into a smell, so the mushroom body can remember it.",
                           { "kc", "fear" }, {}, P::smell));

        // the stimulus and the instruments
        t.push_back (make ("stimulus", "stimulus", "what reaches the specimen",
                           "Everything the preparation is given: your track's level, the plug-in's output, and the host's tempo.",
                           "The stimulus column on the right reports it; it is not a control. The fly hears it through Johnston's organ and sees its hits as looming.",
                           { "jo", "lplc2", "sidechain", "input", "tempo" }));
        t.push_back (make ("input", "input", "a reading, dBFS", "The track's level as it arrives (rms over 300 ms).", "What the fly hears.", { "stimulus", "output" }));
        t.push_back (make ("output", "output", "a reading, dBFS", "What leaves the plug-in (rms over 300 ms), after the safety limiter.", "Set by the output trim along the bottom.", { "stimulus", "dry" }));
        t.push_back (make ("tempo", "tempo", "a reading, bpm", "The host's tempo, and whether it plays.", "The echo's heads, the itch's bars and the fly's grid all follow it.", { "itch selector", "heads" }));
        t.push_back (make ("light", "the light", "590 nm, on CsChrimson",
                           "In optogenetics a neuron carries a light-gated channel; light on it makes it fire. CsChrimson answers to orange-red light.",
                           "A spot of light sits on the brain. Its size is space, its power is feed. Press anywhere else on the plate for a flash: the throw, and the neurons under the light fire for as long as you hold.",
                           { "feed", "space", "throw", "flash" }));
        t.push_back (make ("feed", "feed", "a control: the light's power",
                           "How much of the echo goes round again.",
                           "Drag up or down inside the light, or the wheel on it. Low: a few repeats. High: the loop sings and can run away, held by the saturation in the loop and the limiter at the output.",
                           { "light", "space", "throw" }));
        t.push_back (make ("space", "space", "a control: the light's size",
                           "How wide the echo grows around your head: each pass is pushed to the sides and smeared.",
                           "Drag the light's edge. From one neuron (8 \xc2\xb5m) to 150 \xc2\xb5m across; the lean of the fly's steering adds to it.",
                           { "light", "feed", "lean" }));
        t.push_back (make ("throw", "throw", "a gesture",
                           "The dub engineer's move: a moment sent into the echo and left there.",
                           "Press and hold anywhere on the plate off the light. The fly throws too, when the giant fiber fires.",
                           { "light", "dnp01", "flash" }, { "throw" }));
        t.push_back (make ("flash", "flash", "the light, pressed",
                           "A pulse of the stimulating light.",
                           "While you hold a throw, the lit neurons fire at 20 Hz (no power) to 100 Hz (full). Light the giant fiber and it leaps; the grooming neurons and it grooms.",
                           { "light", "throw" }));
        t.push_back (make ("itch selector", "itch selector", "a setting, along the bottom",
                           "How often the itch can fire, from off to every 8 bars; also how awake the fly is kept.",
                           "Off: the fly rests and its brain does not step. 1/4 bar: wide awake.",
                           { "heads", "awake" }));
        t.push_back (make ("heads", "heads", "a setting: dotted, triplet, half",
                           "The two tape heads' spacing against the beat.",
                           "Dotted is the dub classic; triplet and half change the echo's walk.",
                           { "head a", "tempo" }));
        t.push_back (make ("dry", "dry", "a setting", "Your track, unechoed, in the output.", "Turn it off when dendrite sits on a send.", { "output" }));
        t.push_back (make ("head a", "head a, head b", "readings: ms, dB", "The two tape heads' delay times and gains now.", "They follow the tempo and the heads setting; the fly's lurch bends their speed.", { "heads", "tape pitch" }));
        t.push_back (make ("return", "return", "a reading, dB", "The echo coming back into the output now.", "With its ducking and its breath.", { "feed", "sidechain" }));
        t.push_back (make ("correlation", "correlation", "a reading", "The output's left/right correlation over 300 ms: +1 mono, 0 wide, below 0 out of phase.", "Space and the fly's lean move it.", { "space", "lean" }));
        t.push_back (make ("afterglow", "afterglow", "a reading", "How much of the last itch is still sounding.", "An itch dissolves over about nine bars.", { "itch selector" }));
        t.push_back (make ("sidechain", "sidechain", "an optional input bus",
                           "A second signal routed in from the host.",
                           "Feed it a kick: each hit looms in the fly's low eyes and ducks the echo; a steady kick is learned on a 16th grid and stops startling it.",
                           { "lplc2", "return" }, { "kick" }));

        // its state
        t.push_back (make ("awake", "awake", "a reading, 0 to 1", "How awake the itch selector keeps the fly.", "1/4 bar wide awake, 8 bars half asleep.", { "itch selector", "arousal" }));
        t.push_back (make ("arousal", "arousal", "a mood, 0 to 1", "How wound up the fly is.", "Each escape winds it; it calms over about 40 s. It turns its senses up.", { "fatigue", "fear", "escape" }));
        t.push_back (make ("fatigue", "fatigue", "a mood, 0 to 1", "How tired the fly is: how busy its brain has been over about two minutes.", "A tired fly rests longer between moves and its senses dull.", { "arousal" }));
        t.push_back (make ("fear", "fear", "a mood, 0 to 1", "How much of what it smells now once hurt it.", "Learned in the mushroom body; fear makes its jumps dive.", { "kc", "ppl1", "memory" }));
        t.push_back (make ("memory", "memory", "a reading", "Its 28,761 learning synapses (Kenyon cell to MBON) against their strength at birth.", "Saved with your set: the same fly remembers your session next time.", { "kc", "mbon", "learn", "forget" }));
        t.push_back (make ("escape", "escape", "a reflex", "The giant fiber fired: the tape lurches.", "Shown with its move and the tape's speed in semitones.", { "dnp01", "lurch" }, { "reflex" }));
        t.push_back (make ("groom", "groom", "a reflex", "Grooming: the band it scratches.", "Logged with the band's frequency and how long.", { "dng11", "scratch" }, { "groom" }));
        t.push_back (make ("steer", "steer", "a reading", "How far the fly leans the echo's space now, and which way.", "The steering neurons' imbalance times the atlas's lean.", { "steering", "lean" }));
        t.push_back (make ("eyes", "eyes", "a reading, % fresh", "How rested the fly's looming eyes are.", "Every hit tires them; they recover over seconds. A blind fly ignores the groove.", { "lplc2", "eyes tire", "eyes back" }));
        t.push_back (make ("reflex", "reflex", "a log line", "A move the fly made: hop, leap, duck, dive, rush, hold or sink.", "Click it in the log or the trace to light the path that made it.", { "dnp01", "escape" }, { "reflex" }));

        // the atlas
        t.push_back (make ("jumpy", "jumpy", "the atlas: giant fiber", "How easily the giant fiber fires.", "Divides its threshold: at 0.2 it almost never jumps, at 2.5 it jumps half again as often.", { "dnp01", "lurch" }));
        t.push_back (make ("lurch", "lurch", "the atlas: giant fiber", "How big a jump becomes.", "Octaves of tape at a full jump; above about 0.3 most jumps are big and wait for a bar line.", { "dnp01", "jumpy" }));
        t.push_back (make ("scratch", "scratch", "the atlas: grooming", "How hard the fly scratches.", "0: it never grooms. 2: it grooms most of the time.", { "dng11" }));
        t.push_back (make ("lean", "lean", "the atlas: steering", "How far the fly's steering leans the space.", "0: it never leans.", { "steering", "space" }));
        t.push_back (make ("eyes tire", "eyes tire", "the atlas: eyes", "How fast the looming eyes tire of hits.", "High: after a few bars of groove the fly goes blind to it and quiet.", { "lplc2", "eyes back" }));
        t.push_back (make ("eyes back", "eyes back", "the atlas: eyes", "How many seconds the eyes need to recover.", "Short: it wakes to every change. Long: one busy passage leaves it blind for bars.", { "lplc2", "eyes tire" }));
        t.push_back (make ("learn", "learn", "the atlas: mushroom body", "How fast its synapses learn when hurt.", "", { "kc", "memory", "forget" }));
        t.push_back (make ("forget", "forget", "the atlas: mushroom body", "How many seconds its synapses take to drift back.", "", { "kc", "memory", "learn" }));
        t.push_back (make ("atlas", "the atlas", "magnified 2.5\xc3\x97 or more",
                           "A brain atlas names its regions and their neurons.",
                           "Magnify the brain and its regions open small cards with the fly's own numbers: eight parameters, automatable, centred on the fly's own values.",
                           { "jumpy", "lurch", "scratch", "lean", "eyes tire", "eyes back", "learn", "forget" }));

        // the imaging methods
        t.push_back (make ("trace", "trace", "imaging method",
                           "Each neuron's firing over the last 300 ms, over this session's exposure.",
                           "The default. The only method that also shows the exposure and the scars.",
                           { "exposure", "scars", "two-photon" }, {}, -1, M::traceMethod));
        t.push_back (make ("two-photon", "two-photon", "imaging method, GCaMP6s, \xce\x94""F/F",
                           "Calcium imaging: a fluorescent indicator brightens with the calcium each spike lets in. It is read as \xce\x94""F/F, the change against the cell's own baseline.",
                           "Each neuron against its own last 20 s, so what fires steadily goes dark and only a change lights: what the last bar woke up.",
                           { "trace", "voltage" }, {}, -1, M::twoPhoton));
        t.push_back (make ("voltage", "voltage", "imaging method, ASAP2f",
                           "A voltage indicator dims when the membrane fires and forgets in milliseconds.",
                           "Every neuron glows at rest; a spike in this frame darkens it.",
                           { "two-photon" }, {}, -1, M::voltage));
        t.push_back (make ("photons", "photon counting", "imaging method",
                           "One photon a spike, counted.",
                           "Each spike of the last second is a grain within 3 \xc2\xb5m of its neuron.",
                           { "trace" }, {}, -1, M::photonCounting));
        t.push_back (make ("tracts", "tractography", "imaging method",
                           "Diffusion MRI traces fibre bundles by where water moves along axons. In a connectome the routes are known exactly.",
                           "The busiest connections now (synapses times the sender's spikes in the last second) as curves, over a faint skeleton of the strongest wiring. The curves are drawn; the links are real.",
                           { "trace" }, {}, -1, M::tracts));
        t.push_back (make ("2-dg", "2-DG \xc2\xb7 2-deoxyglucose", "imaging method, autoradiograph",
                           "A sugar a neuron takes up but cannot burn, so it piles up where the work was done. One film per session, never two.",
                           "This session only, never saved. Grey where the music worked a neuron at all; bright where it worked it more than twice what ordinary music does, so the film is what your session did to this fly. Press the small brain to watch it build up.",
                           { "trace", "exposure" }, {}, -1, M::deoxyglucose));
        t.push_back (make ("exposure", "exposure", "the trace's floor",
                           "Every spike of the session leaves its neuron a little brighter for good.",
                           "Saved with the set, so a reopened project shows where its music drove the brain.",
                           { "trace", "scars" }));
        t.push_back (make ("scars", "scars", "marks on the trace",
                           "At each reflex the 64 neurons firing hardest are burned in.",
                           "Never removed; saved with the set.",
                           { "trace", "reflex" }));
        t.push_back (make ("keep", "keep", "the last minute, on tape",
                           "The echo's last minute on two tapes, with a diary of marks.",
                           "Lines in the log that keep still holds are handles: drag one into Live to get that take, bar to bar.",
                           { "reflex" }));
        t.push_back (make ("tape pitch", "tape pitch", "the trace's last row",
                           "The tape's speed in semitones.",
                           "A leap bends it up, a dive down; the echo carries the bend.",
                           { "dnp01", "lurch" }));
        return t;
    }

    struct Types
    {
        int count = 0;
        std::vector<std::vector<uint16_t>> members;
        std::map<juce::String, int> byName; // lower case
        Types()
        {
            namespace id = rig::identities;
            for (int n = 0; n < id::count; ++n)
                count = std::max (count, (int) id::type[n] + 1);
            members.resize ((size_t) count);
            for (int n = 0; n < id::count; ++n)
                members[(size_t) id::type[n]].push_back ((uint16_t) n);
            for (int t = 0; t < count; ++t)
                if (juce::String name (id::typeName[t]); name.isNotEmpty() && ! members[(size_t) t].empty())
                    byName[name.toLowerCase()] = t;
        }
    };
    const Types& types()
    {
        static const Types t;
        return t;
    }
} // namespace

const std::vector<Entry>& terms()
{
    static const std::vector<Entry> all = build();
    return all;
}

const Entry* term (const juce::String& key)
{
    const auto k = key.trim().toLowerCase();
    for (const auto& e : terms())
        if (e.key == k || e.head.toLowerCase() == k)
            return &e;
    return nullptr;
}

int typeCount() { return types().count; }
int typeOf (int neuron) { return (int) rig::identities::type[neuron]; }
juce::String typeNameOf (int t) { return t >= 0 && t < typeCount() ? juce::String (rig::identities::typeName[t]) : juce::String(); }
int typeIndex (const juce::String& name)
{
    const auto& m = types().byName;
    const auto it = m.find (name.trim().toLowerCase());
    return it == m.end() ? -1 : it->second;
}
const std::vector<uint16_t>& neuronsOfType (int t)
{
    static const std::vector<uint16_t> none;
    return t >= 0 && t < typeCount() ? types().members[(size_t) t] : none;
}

juce::StringArray partners (int t, bool inputs, int count) { return partners (neuronsOfType (t), inputs, count); }

juce::StringArray partners (const std::vector<uint16_t>& mine, bool inputs, int count)
{
    // synapses summed by partner type, from the fly's own wiring (inputs: every row; outputs: its rows)
    std::map<int, long> sum;
    if (mine.empty())
        return {};
    std::vector<uint8_t> is ((size_t) flydata::neurons, 0);
    for (auto n : mine)
        is[n] = 1;
    if (inputs)
    {
        for (int n = 0; n < flydata::neurons; ++n)
            for (uint32_t k = flydata::rowStart[n]; k < flydata::rowStart[n + 1]; ++k)
                if (is[flydata::target[k]] && ! is[(size_t) n])
                    sum[typeOf (n)] += std::abs ((int) flydata::weight[k]);
    }
    else
    {
        for (auto n : mine)
            for (uint32_t k = flydata::rowStart[n]; k < flydata::rowStart[n + 1]; ++k)
                if (! is[flydata::target[k]])
                    sum[typeOf (flydata::target[k])] += std::abs ((int) flydata::weight[k]);
    }
    std::vector<std::pair<long, int>> ranked;
    for (const auto& [type, s] : sum)
        if (typeNameOf (type).isNotEmpty())
            ranked.push_back ({ s, type });
    std::sort (ranked.begin(), ranked.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    juce::StringArray out;
    for (int k = 0; k < std::min (count, (int) ranked.size()); ++k)
        out.add (typeNameOf (ranked[(size_t) k].second) + " " + juce::String (ranked[(size_t) k].first));
    return out;
}

std::vector<Hit> search (const juce::String& query, int most)
{
    std::vector<Hit> hits;
    const auto q = query.trim().toLowerCase();
    if (q.isEmpty())
        return hits;
    auto score = [&] (const juce::String& s) {
        const auto l = s.toLowerCase();
        if (l == q) return 100;
        if (l.startsWith (q)) return 80 - std::min (30, l.length() - q.length());
        if (l.contains (" " + q) || l.contains ("-" + q) || l.contains ("_" + q)) return 50;
        if (l.contains (q)) return 30;
        return 0;
    };
    juce::StringArray written; // types a matching word already stands for: one line each, not two
    for (const auto& e : terms())
    {
        const int s = std::max ({ score (e.key), score (e.head), score (e.kind) / 2, e.body.toLowerCase().contains (q) ? 10 : 0 });
        if (s > 0)
        {
            hits.push_back ({ e.head, e.kind, e.key, -1, s + 5 });
            if (const int t = typeIndex (e.typeName); t >= 0 && e.population < 0)
            {
                written.add (typeNameOf (t));
                const auto n = (int) neuronsOfType (t).size();
                hits.back().detail << ", " << n << (n == 1 ? " neuron" : " neurons");
            }
        }
    }
    namespace id = rig::identities;
    for (const auto& [name, t] : types().byName)
    {
        const int s = score (name);
        if (s > 0 && ! written.contains (typeNameOf (t)))
        {
            const auto& m = neuronsOfType (t);
            juce::String detail = juce::String ((int) m.size()) + (m.size() == 1 ? " neuron" : " neurons");
            if (const juce::String nt (id::transmitterName[id::transmitter[m.front()]]); nt.isNotEmpty())
                detail << ", " << nt;
            if (const juce::String c (id::className[id::superClass[m.front()]]); c.isNotEmpty())
                detail << ", " << c.replaceCharacter ('_', ' ');
            hits.push_back ({ typeNameOf (t), detail, {}, t, s });
        }
    }
    std::stable_sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.score > b.score; });
    if ((int) hits.size() > most)
        hits.resize ((size_t) most);
    return hits;
}
} // namespace dendrite::ui::glossary
