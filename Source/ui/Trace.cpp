#include "Trace.h"

#include <climits>

namespace dendrite::ui
{
using rig::Timeline;

namespace
{
    // a level to a height, linear like a waveform: 0 dBFS fills the half row
    float levelHeight (float peak, float half) noexcept
    {
        return half * juce::jlimit (0.0f, 1.0f, peak);
    }
    juce::String semitoneText (float st)
    {
        return (st >= 0.0f ? "+" : utf8 ("\xe2\x88\x92")) + juce::String (std::abs (st), 1);
    }
    juce::String dbfs (float peak)
    {
        if (peak <= 1.0e-6f)
            return utf8 ("\xe2\x88\x92\xe2\x88\x9e dBFS");
        const float db = 20.0f * std::log10 (peak);
        return (db < 0.0f ? utf8 ("\xe2\x88\x92") : juce::String()) + juce::String (std::abs (db), 1) + " dBFS";
    }
} // namespace

Trace::Trace (DendriteProcessor& p) : processor (p)
{
    setOpaque (true);
}

Trace::Rows Trace::rows() const noexcept
{
    Rows r;
    const float x = 150.0f, w = std::max (10.0f, (float) getWidth() - x - 24.0f);
    r.sound = { x, 19.0f, w, 30.0f };
    r.fly = { x, 57.0f, w, 36.0f };
    r.echo = { x, 97.0f, w, 42.0f };
    r.pitch = { x, 143.0f, w, 22.0f };
    r.plot = { x, 17.0f, w, r.pitch.getBottom() - 17.0f };
    return r;
}

double Trace::span() const noexcept
{
    const auto& tl = processor.timeline;
    if (! tl.playing() || tl.bpm() <= 0.0f)
        return 16.0;
    return juce::jlimit (4.0, 32.0, barsShown * (double) tl.barBeats() * 60.0 / (double) tl.bpm());
}

void Trace::paint (juce::Graphics& g)
{
    g.fillAll (tone::black);
    const Hairline line (g);
    const auto& tl = processor.timeline;
    const auto R = rows();
    const auto data = fonts->at (type::data), small = fonts->at (type::small);
    const double now = tl.now(), sp = span();
    const float left = R.plot.getX(), right = R.plot.getRight(), width = R.plot.getWidth();
    auto xAt = [&] (double t) { return right - (float) ((now - t) / sp) * width; };

    g.setColour (tone::grey (tone::dim));
    line.h (0.0f, (float) getWidth(), 0.5f);

    // the rows' names
    auto label = [&] (juce::Rectangle<float> row, const char* name, const char* sub) {
        const bool two = sub != nullptr && row.getHeight() >= 30.0f;
        const float y = row.getCentreY() - (two ? 13.0f : 7.0f);
        g.setFont (data);
        g.setColour (tone::grey (tone::bright));
        g.drawText (name, juce::Rectangle<float> (14.0f, y, left - 20.0f, 14.0f), juce::Justification::centredLeft);
        if (two)
        {
            g.setFont (small);
            g.setColour (tone::grey (tone::dim));
            g.drawText (utf8 (sub), juce::Rectangle<float> (14.0f, y + 14.0f, left - 20.0f, 13.0f), juce::Justification::centredLeft);
        }
    };
    label (R.sound, "your sound", "input");
    label (R.fly, "the fly", "what it does");
    label (R.echo, "your echo", "what you hear");
    label (R.pitch, "tape pitch", nullptr);
    g.setColour (tone::grey (tone::floor));
    for (auto row : { R.sound, R.fly, R.echo })
        line.h (left, right, row.getBottom() + 2.0f);

    // one column per pixel: the loudest of its bins, whether the tape was bent in it, the tape's speed
    const int columns = juce::jmax (1, (int) std::ceil (width));
    std::vector<float> in ((size_t) columns, 0.0f), wet ((size_t) columns, 0.0f), st ((size_t) columns, 0.0f), turn ((size_t) columns, 0.0f);
    std::vector<uint8_t> bent ((size_t) columns, 0), used ((size_t) columns, 0), groomed ((size_t) columns, 0);
    int lastBar = INT_MIN;
    float lastBarLabel = 1.0e9f;
    for (int back = 0; back < tl.bins(); ++back)
    {
        const double t = tl.binTime (back);
        if (t < now - sp)
            break;
        const auto& b = tl.bin (back);
        const int c = juce::jlimit (0, columns - 1, (int) (xAt (t) - left));
        in[(size_t) c] = std::max (in[(size_t) c], b.in);
        wet[(size_t) c] = std::max (wet[(size_t) c], b.wet);
        if (std::abs (b.semitones) > std::abs (st[(size_t) c]))
            st[(size_t) c] = b.semitones;
        if (! used[(size_t) c])
            turn[(size_t) c] = b.turn;
        used[(size_t) c] = 1;
        bent[(size_t) c] |= b.jump > 0.01f ? 1 : 0;
        groomed[(size_t) c] |= b.scratch > 0.3f ? 1 : 0;

        // a bar line where the host's bar changes (newest first, so the line sits at the bar's first bin)
        if (b.playing && tl.barBeats() > 0.0f)
        {
            const int bar = (int) std::floor (b.beat / (double) tl.barBeats() + 1.0e-6);
            if (lastBar != INT_MIN && bar != lastBar)
            {
                const float x = xAt (tl.binTime (back - 1));
                g.setColour (tone::grey (tone::floor));
                line.v (x, R.plot.getY(), R.pitch.getBottom());
                if (lastBarLabel - x > 44.0f && x < right - 84.0f)
                {
                    g.setFont (small);
                    g.setColour (tone::grey (tone::dim));
                    g.drawText ("bar " + juce::String (lastBar + 1), juce::Rectangle<float> (x + 4.0f, 3.0f, 60.0f, 13.0f), juce::Justification::centredLeft);
                    lastBarLabel = x;
                }
            }
            lastBar = bar;
        }
    }
    if (! tl.playing())
    {
        // the host is stopped: seconds back from now instead of bars
        g.setFont (small);
        for (int s = 1; s < (int) sp; ++s)
        {
            const float x = right - (float) (s / sp) * width;
            g.setColour (tone::grey (tone::floor));
            line.v (x, R.plot.getY(), R.pitch.getBottom());
            if (s % 4 == 0)
            {
                g.setColour (tone::grey (tone::dim));
                g.drawText (utf8 ("\xe2\x88\x92") + juce::String (s) + " s", juce::Rectangle<float> (x + 4.0f, 3.0f, 60.0f, 13.0f), juce::Justification::centredLeft);
            }
        }
    }
    g.setColour (tone::grey (tone::mid));
    line.v (right, R.plot.getY(), R.pitch.getBottom());
    g.setFont (small);
    g.drawText ("now", juce::Rectangle<float> (right - 40.0f, 3.0f, 40.0f, 13.0f), juce::Justification::centredRight);

    // your throws, under your sound
    for (int k = 0; k < tl.throws(); ++k)
    {
        const auto& s = tl.throwAt (k);
        if (s.at + s.length < now - sp)
            break;
        const float x0 = std::max (left, xAt (s.at)), x1 = xAt (s.at + s.length);
        g.setColour (tone::grey (tone::bright));
        g.fillRect (juce::Rectangle<float> (x0, R.sound.getBottom() + 1.0f, std::max (1.0f, x1 - x0), 2.0f));
        g.setFont (small);
        if (x0 > left + 44.0f)
            g.drawText ("throw", juce::Rectangle<float> (x0 - 44.0f, R.sound.getBottom() - 4.0f, 40.0f, 12.0f), juce::Justification::centredRight);
    }

    // the envelopes and the tape
    const float soundMid = R.sound.getCentreY(), echoMid = R.echo.getCentreY();
    const float soundHalf = R.sound.getHeight() * 0.5f - 2.0f, echoHalf = R.echo.getHeight() * 0.5f - 2.0f;
    for (int c = 0; c < columns; ++c)
    {
        if (! used[(size_t) c])
            continue;
        const float x = left + (float) c;
        if (const float h = levelHeight (in[(size_t) c], soundHalf); h > 0.25f)
        {
            g.setColour (tone::grey (tone::mid));
            g.fillRect (juce::Rectangle<float> (x, soundMid - h, 1.0f, 2.0f * h));
        }
        if (const float h = levelHeight (wet[(size_t) c], echoHalf); h > 0.25f)
        {
            g.setColour (tone::grey (bent[(size_t) c] ? tone::live : tone::dim));
            g.fillRect (juce::Rectangle<float> (x, echoMid - h, 1.0f, 2.0f * h));
        }
        if (groomed[(size_t) c])
        {
            g.setColour (tone::grey (tone::mid));
            g.fillRect (juce::Rectangle<float> (x, R.fly.getBottom() - 5.0f, 1.0f, 3.0f));
        }
    }

    // steer: the fly's lean, a faint line through its row (up: left, down: right)
    {
        juce::Path p;
        bool started = false;
        for (int c = columns - 1; c >= 0; --c)
        {
            if (! used[(size_t) c])
                continue;
            const float y = R.fly.getCentreY() + 4.0f - 7.0f * juce::jlimit (-1.0f, 1.0f, turn[(size_t) c]);
            started ? p.lineTo (left + (float) c, y) : p.startNewSubPath (left + (float) c, y);
            started = true;
        }
        g.setColour (tone::grey (tone::dim));
        g.strokePath (p, juce::PathStrokeType (line.width()));
    }

    // the tape's speed: 0 st on the line, +-16 st to the row's edges
    const float pitchMid = R.pitch.getCentreY(), pitchHalf = R.pitch.getHeight() * 0.5f;
    g.setColour (tone::grey (tone::floor));
    line.h (left, right, pitchMid);
    g.setColour (tone::grey (tone::dim));
    line.v (left - 4.0f, pitchMid - pitchHalf * 0.75f, pitchMid + pitchHalf * 0.75f);
    g.setFont (small);
    g.drawText ("+12", juce::Rectangle<float> (left - 40.0f, pitchMid - pitchHalf * 0.75f - 7.0f, 32.0f, 12.0f), juce::Justification::centredRight);
    g.drawText (utf8 ("\xe2\x88\x92") + "12", juce::Rectangle<float> (left - 40.0f, pitchMid + pitchHalf * 0.75f - 5.0f, 32.0f, 12.0f), juce::Justification::centredRight);
    {
        juce::Path p;
        bool started = false;
        for (int c = 0; c < columns; ++c)
        {
            if (! used[(size_t) c])
                continue;
            const float y = pitchMid - pitchHalf * 0.75f * juce::jlimit (-16.0f, 16.0f, st[(size_t) c]) / 12.0f;
            started ? p.lineTo (left + (float) c, y) : p.startNewSubPath (left + (float) c, y);
            started = true;
        }
        g.setColour (tone::grey (tone::live));
        g.strokePath (p, juce::PathStrokeType (1.25f));
    }

    // the reflexes: one line from the fly down through your echo and the pitch, newest first so
    // the newest keeps its name when two would collide
    float limit = right;
    for (int k = 0; k < tl.reflexes(); ++k)
    {
        const auto& r = tl.reflex (k);
        if (r.at < now - sp)
            break;
        const float x = xAt (r.at);
        g.setColour (tone::grey (tone::bright));
        line.v (x, R.fly.getY() + 2.0f, R.pitch.getBottom());
        g.setColour (tone::grey (tone::live));
        g.fillRect (juce::Rectangle<float> (x - 2.5f, R.fly.getY() + 2.0f, 5.0f, 5.0f));

        const juce::String name = Timeline::moveName (r.move);
        juce::String steps;
        for (int n = 0; n < r.steps; ++n)
            steps << (n > 0 ? " " : "") << semitoneText (r.semitones[(size_t) n]);
        if (steps.isNotEmpty())
            steps << " st";
        const float nameWidth = (float) juce::GlyphArrangement::getStringWidth (data, name);
        const float stepWidth = steps.isEmpty() ? 0.0f : (float) juce::GlyphArrangement::getStringWidth (small, steps);
        // the name and its tape speeds each go right of the line if they fit before the next
        // reflex (or now); the newest, when too close to now, goes left of its line instead
        const bool onLeft = limit >= right && x + 6.0f + nameWidth >= limit - 4.0f;
        float reach = x;
        auto put = [&] (const juce::String& text, float w, float y, float h, const juce::Font& font, float level) {
            float at = -1.0f;
            if (! onLeft && x + 6.0f + w < limit - 4.0f)
                at = x + 6.0f;
            else if (onLeft && x - 6.0f - w > R.plot.getX())
                at = x - 6.0f - w;
            if (at < 0.0f)
                return;
            g.setFont (font);
            g.setColour (tone::grey (level));
            g.drawText (text, juce::Rectangle<float> (at, y, w + 2.0f, h), onLeft ? juce::Justification::centredRight : juce::Justification::centredLeft);
            reach = std::min (reach, at);
        };
        put (name, nameWidth, R.fly.getY() + 1.0f, 14.0f, data, tone::live);
        if (stepWidth > 0.0f)
            put (steps, stepWidth, R.fly.getY() + 15.0f, 13.0f, small, tone::bright);
        limit = reach;
    }

    // the newest groom gets its band written beside it
    if (tl.grooms() > 0)
    {
        const auto& s = tl.groom (0);
        if (s.at >= now - sp)
        {
            const juce::String text = "groom " + juce::String (s.hz / 1000.0f, 1) + " kHz";
            const float x = std::max (left, xAt (s.at));
            g.setFont (small);
            g.setColour (tone::grey (tone::mid));
            const float w = (float) juce::GlyphArrangement::getStringWidth (small, text) + 4.0f;
            bool clear = x + w < right;
            for (int k = 0; k < tl.reflexes() && clear; ++k)
            {
                const auto& r = tl.reflex (k);
                const float rx = xAt (r.at);
                clear = ! (rx > x - 4.0f && rx < x + w + 4.0f) && ! (rx < x && rx + 110.0f > x);
                if (r.at < now - sp)
                    break;
            }
            if (clear)
                g.drawText (text, juce::Rectangle<float> (x, R.fly.getBottom() - 17.0f, w, 12.0f), juce::Justification::centredLeft);
        }
    }

    // the fly at rest leaves the echo alone
    if (juce::roundToInt (processor.params.getRawParameterValue ("frames")->load()) == 0)
    {
        g.setFont (small);
        g.setColour (tone::grey (tone::mid));
        g.drawText (utf8 ("at rest \xc2\xb7 the fly leaves your echo alone"), R.fly.withTrimmedRight (8.0f), juce::Justification::centredRight);
    }

    // the caption: what is shown, and the scale
    g.setFont (small);
    g.setColour (tone::grey (tone::dim));
    const juce::String shown = tl.playing() ? juce::String (barsShown) + " bars" : utf8 ("16 s  \xc2\xb7  host stopped");
    g.drawText (shown + utf8 ("  \xc2\xb7  white: bent by the fly  \xc2\xb7  full row = 0 dBFS")
                , juce::Rectangle<float> (left, R.pitch.getBottom() + 4.0f, width, 13.0f), juce::Justification::centredLeft);
}

juce::String Trace::getTooltip()
{
    const auto R = rows();
    if (! getLocalBounds().toFloat().contains (hoverAt))
        return {};
    if (hoverAt.x < R.plot.getX())
    {
        if (hoverAt.y < R.sound.getBottom() + 2.0f)
            return "your input: its loudest sample every 10 ms";
        if (hoverAt.y < R.fly.getBottom() + 2.0f)
            return "the fly: each square is a reflex (its move), grey ticks are grooms (a band on the echo), the faint line is its steer";
        if (hoverAt.y < R.echo.getBottom() + 2.0f)
            return "the echo alone, without your dry: white where the fly's reflex bent the tape";
        return "the tape's speed in semitones: 0 is the echo as you set it";
    }
    const auto& tl = processor.timeline;
    const double sp = span(), now = tl.now();
    const double t = now - (double) ((R.plot.getRight() - hoverAt.x) / R.plot.getWidth()) * sp;
    const double binLength = tl.bins() > 1 ? tl.binTime (0) - tl.binTime (1) : Timeline::binSeconds;
    const int back = (int) std::floor ((tl.binTime (0) - t) / binLength);
    if (back < 0 || back >= tl.bins())
        return {};
    const auto& b = tl.bin (back);
    const auto dot = utf8 (" \xc2\xb7 ");
    juce::String text;
    if (b.playing && tl.barBeats() > 0.0f)
    {
        const double bars = b.beat / (double) tl.barBeats();
        text << "bar " << (int) std::floor (bars) + 1 << "." << (int) std::floor (std::fmod (b.beat, (double) tl.barBeats())) + 1;
    }
    else
        text << utf8 ("\xe2\x88\x92") << juce::String (now - t, 1) << " s";
    text << dot << "input " << dbfs (b.in) << dot << "echo " << dbfs (b.wet);
    if (std::abs (b.semitones) > 0.05f)
        text << dot << "tape " << semitoneText (b.semitones) << " st";
    for (int k = 0; k < tl.reflexes(); ++k)
    {
        const auto& r = tl.reflex (k);
        if (t >= r.at - 0.05 && t <= r.at + std::max (0.1, r.length))
        {
            text << dot << Timeline::moveName (r.move) << " " << juce::String (r.length, 2) << " s";
            break;
        }
        if (r.at + r.length < t - 1.0)
            break;
    }
    return text;
}
} // namespace dendrite::ui
