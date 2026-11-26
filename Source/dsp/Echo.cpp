#include "Echo.h"

namespace dendrite
{
namespace
{
    // feed: off -> whisper -> dub -> drown -> endless. Each hidden setting turns at its
    // own place, so the regimes blur into each other instead of stepping.
    const Path<6> sendPath { { { { 0.0f, 0.0f }, { 0.08f, 0.2f }, { 0.25f, 0.42f }, { 0.5f, 0.62f }, { 0.75f, 0.78f }, { 1.0f, 0.88f } } } };
    // dub (0.5) sums to just under 1: a single hit's echo now fades over many bars instead
    // of growing until the ceiling holds it (1.0 summed to 1.04). drown and endless still
    // regenerate, as they should.
    const Path<6> dottedPath { { { { 0.0f, 0.3f }, { 0.25f, 0.47f }, { 0.5f, 0.6f }, { 0.72f, 0.72f }, { 0.88f, 0.8f }, { 1.0f, 0.86f } } } };
    const Path<6> quarterPath { { { { 0.0f, 0.18f }, { 0.25f, 0.31f }, { 0.5f, 0.38f }, { 0.72f, 0.5f }, { 0.88f, 0.57f }, { 1.0f, 0.62f } } } };
    // How far filling the silence may take the loop (dotted + quarter): at whisper it
    // lingers, it doesn't regenerate.
    const Path<5> loopCapPath { { { { 0.0f, 0.7f }, { 0.25f, 0.88f }, { 0.5f, 1.2f }, { 0.75f, 1.4f }, { 1.0f, 1.6f } } } };
    const Path<6> returnPath { { { { 0.0f, 0.0f }, { 0.06f, 0.3f }, { 0.25f, 0.95f }, { 0.5f, 1.6f }, { 0.75f, 1.9f }, { 1.0f, 2.0f } } } };
    // whisper ducks less than it used to (0.65): on a busy part it was easy to miss
    const Path<4> duckPath { { { { 0.0f, 0.4f }, { 0.5f, 0.55f }, { 0.75f, 0.45f }, { 1.0f, 0.3f } } } };
    const Path<4> ceilingPath { { { { 0.0f, 0.45f }, { 0.5f, 0.5f }, { 0.8f, 0.62f }, { 1.0f, 0.8f } } } };
    const Path<4> drivePath { { { { 0.0f, 1.1f }, { 0.5f, 1.16f }, { 0.85f, 1.4f }, { 1.0f, 1.9f } } } };

    // space: inside -> around -> envelop -> beyond.
    const Path<3> inWidthPath { { { { 0.0f, 0.3f }, { 0.25f, 1.0f }, { 1.0f, 1.0f } } } };
    const Path<5> panPath { { { { 0.0f, 0.0f }, { 0.1f, 0.13f }, { 0.3f, 0.21f }, { 0.6f, 0.24f }, { 1.0f, 0.26f } } } };
    const Path<5> spreadPath { { { { 0.0f, 0.0f }, { 0.1f, 0.1f }, { 0.3f, 0.2f }, { 0.6f, 0.27f }, { 1.0f, 0.34f } } } };
    const Path<5> diffusePath { { { { 0.0f, 0.0f }, { 0.35f, 0.0f }, { 0.6f, 0.45f }, { 0.8f, 0.62f }, { 1.0f, 0.7f } } } };
    const Path<3> diffuseMixPath { { { { 0.0f, 0.0f }, { 0.3f, 0.0f }, { 0.7f, 1.0f } } } };
    const Path<4> shiftPath { { { { 0.0f, 0.0f }, { 0.74f, 0.0f }, { 0.86f, 0.09f }, { 1.0f, 0.35f } } } };
    const Path<5> sideOutPath { { { { 0.0f, 0.35f }, { 0.25f, 1.05f }, { 0.45f, 1.5f }, { 0.7f, 1.8f }, { 1.0f, 1.85f } } } };
    // How the quarter head treats the Side: -1 crosses it (the 0.5 ping-pong), toward +1 it
    // keeps it, so the Side loop sustains like the Mid and every pass adds to how far out
    // the repeat sits.
    const Path<5> crossPath { { { { 0.0f, -1.0f }, { 0.1f, -0.5f }, { 0.3f, 0.35f }, { 0.6f, 0.8f }, { 1.0f, 0.92f } } } };
    const Path<5> trimPath { { { { 0.0f, 1.0f }, { 0.3f, 0.97f }, { 0.6f, 0.9f }, { 0.8f, 0.78f }, { 1.0f, 0.66f } } } };

    // The tape's warmth, as chosen by ear on the 0.5 sketches: the highs give gently,
    // mild saturation, a little drifting diffusion. Since 1.1 every pass also thins the low
    // mids a little, so a regenerating tail never settles at 220-320 Hz (the rumble Hakan
    // heard), and the diffusers are short, so each repeat keeps its edge for a few passes
    // before it dissolves.
    constexpr float lp1Hz = 5450.0f, lp2Hz = 8500.0f, emphasisHz = 2500.0f, emphasisAmount = 0.24f;
    constexpr float hpHz = 150.0f, hpQ = 0.6f, mudHz = 280.0f, mudQ = 0.9f, mudDb = -1.0f;
    constexpr float tapeDiffusion = 0.38f;
    constexpr float wowSeconds = 0.0007f, flutterSeconds = 0.00007f;
} // namespace

//==============================================================================
void Echo::TapePass::prepare (float newSampleRate, int lane)
{
    sr = newSampleRate;
    lp1.setCutoff (lp1Hz, sr);
    lp2.setCutoff (lp2Hz, sr);
    emphasis.setCutoff (emphasisHz, sr);
    hp.highpass (hpHz, hpQ, sr);
    mud.peak (mudHz, mudQ, mudDb, sr);
    dc.prepare (sr);
    // irregular lengths, different for each lane
    constexpr float ms[2][4] = { { 1.9f, 3.1f, 4.4f, 6.6f }, { 2.1f, 3.4f, 4.8f, 7.1f } };
    bulk = 0.0f;
    for (size_t k = 0; k < diffusers.size(); ++k)
    {
        diffusers[k].prepare (sr, ms[lane][k]);
        bulk += ms[lane][k] * 0.001f * sr;
    }
    drift.prepare (sr, 6.5f + 0.7f * (float) lane, 0x7a9e + (uint64_t) lane);
}

void Echo::TapePass::clear()
{
    for (auto* p : { &lp1, &lp2, &emphasis })
        p->clear();
    hp.clear();
    mud.clear();
    dc.clear();
    for (auto& d : diffusers)
        d.clear();
    drift.reset();
}

float Echo::TapePass::process (float x, float drive, float diffusion) noexcept
{
    // pre-emphasis, soft saturation, de-emphasis: the highs saturate a little more
    const float hi = x - emphasis.lowpass (x);
    float y = std::tanh (drive * (x + emphasisAmount * hi) + 0.05f) - std::tanh (0.05f);
    y = y / drive - emphasisAmount / 1.4f * hi;

    y = lp2.lowpass (lp1.lowpass (mud.process (hp.process (y))));

    // every pass smears the repeat a little more
    const float mod = drift.next() * 0.00016f * sr;
    for (size_t k = 0; k < diffusers.size(); ++k)
        y = diffusers[k].process (y, diffusion, mod * 0.5f * (float) (k + 1));

    return dc.process (y);
}

//==============================================================================
void Echo::prepare (float newSampleRate)
{
    sampleRate = newSampleRate;
    setTempo (120.0);

    // half time's long head (a half note) at 30 BPM is 4 s; leave room for lean and wow.
    loopM.prepare ((int) (sampleRate * 4.4f));
    loopS.prepare ((int) (sampleRate * 4.4f));
    tapeM.prepare (sampleRate, 0);
    tapeS.prepare (sampleRate, 0); // the same tape: a Side that is a share of the Mid stays that share
    glowM.prepare ((int) (sampleRate * 4.4f));
    glowS.prepare ((int) (sampleRate * 4.4f));
    glowTapeM.prepare (sampleRate, 1); // the afterglow's tape: other diffusers, so it blurs apart from the echo
    glowTapeS.prepare (sampleRate, 1);

    constexpr float sideMs[5] = { 1.13f, 2.31f, 3.67f, 5.53f, 8.29f };
    for (size_t k = 0; k < sideDiffusers.size(); ++k)
        sideDiffusers[k].prepare (sampleRate, sideMs[k]);

    wowA.prepare (sampleRate, 2.6f, 0x51);
    wowB.prepare (sampleRate, 3.3f, 0x52);
    flutter.prepare (sampleRate, 0.17f, 0x53);
    breathWander.prepare (sampleRate, 11.0f, 0x54);
    thetaWander.prepare (sampleRate, 13.0f, 0x55);
    sideDrift.prepare (sampleRate, 7.0f, 0x56);

    cFast = approach (0.001f, sampleRate);
    rFast = approach (0.03f, sampleRate);
    cSlow = approach (0.25f, sampleRate);
    aDuck = approach (0.008f, sampleRate);
    rDuck = approach (0.7f, sampleRate);
    cShort = approach (0.4f, sampleRate);
    cLong = approach (4.0f, sampleRate);
    cPeak = approach (6.0f, sampleRate);
    cLean = approach (0.9f, sampleRate);
    cFillUp = approach (0.6f, sampleRate);
    cFillDown = approach (0.12f, sampleRate);
    cControl = approach (0.04f, sampleRate);
    cHeldUp = approach (0.005f, sampleRate);
    cHeldDown = approach (0.04f, sampleRate);
    cHead = approach (0.25f, sampleRate);
    aLoop = approach (0.01f, sampleRate);
    rLoop = approach (0.3f, sampleRate);
    aOut = approach (0.001f, sampleRate);
    rOut = approach (0.25f, sampleRate);
    cLaunch = approach (0.08f, sampleRate);
    cTurn = approach (0.6f, sampleRate);
    cTurnHeal = approach (45.0f, sampleRate);
    cRunUp = approach (1.6f, sampleRate);
    cGateUp = approach (0.003f, sampleRate);
    cGateDown = approach (0.012f, sampleRate);
    cGlowDir = approach (0.05f, sampleRate);

    reset();
}

void Echo::reset()
{
    loopM.clear();
    bandM.clear();
    bandS.clear();
    touchNow = Touch {};
    loopS.clear();
    tapeM.clear();
    tapeS.clear();
    for (auto& d : sideDiffusers)
        d.clear();
    pairM.clear();
    pairS.clear();
    tiltM.clear();
    tiltS.clear();
    for (auto* w : { &wowA, &wowB, &flutter, &breathWander, &thetaWander, &sideDrift })
        w->reset();
    dice.reseed (0xd3d217e);

    feed = feedTarget;
    space = spaceTarget;
    updatePaths();
    wasHeld = held;
    dottedSm = quarterSm = 0.0f;
    eFast = eSlow = eDuck = rmsShort = 0.0f;
    // The followers start empty and learn the track from its first sound, so a quiet
    // track and a loud one sound the same from the first second.
    rmsLong = peakLong = 0.0f;
    heard = 0;
    hitPeak = 0.0f;
    outEnvM = outEnvS = 0.0f;
    lean = fillAmount = heldSend = throwEnv = throwTail = 0.0f;
    envM = envS = 0.0f;
    easeNow = 1.0f;
    launch = launchTarget = turn = turnTarget = rotatePhase = runaway = 0.0f;
    breathNow = levelNow = thetaNow = 0.0f;
    onsetPending = 0.0f;
    onsetArmed = 1.0f;
    onsetThrown = false;
    silentFor = count = 0;

    glowM.clear();
    glowS.clear();
    glowTapeM.clear();
    glowTapeS.clear();
    glowPair.clear();
    glowSpinM.clear();
    glowSpinS.clear();
    for (auto* p : { &glowDarkM, &glowDarkS, &glowToneM, &glowToneS })
        p->clear();
    glowGate = glowEnvM = glowEnvS = glowDir = glowMu = glowAlive = glowYouth = glowSpin = glowSpinSide = glowPhaseM = glowPhaseS = 0.0f;
    glowFallback = 0.7f;
    glowAge = 1.0e9f;
    glowDiffusion = tapeDiffusion;
    wasThrowing = false;
}

void Echo::updatePaths() noexcept
{
    const float feedUse = clamp01 (feed + touchNow.feed), spaceUse = clamp01 (space + touchNow.space);
    pSend = sendPath (feedUse);
    pDotted = dottedPath (feedUse);
    pQuarter = quarterPath (feedUse);
    pReturn = returnPath (feedUse);
    pDuck = duckPath (feedUse);
    pCeiling = ceilingPath (feedUse);
    pDrive = drivePath (feedUse);

    pInWidth = inWidthPath (spaceUse);
    pPan = panPath (spaceUse);
    pSpread = spreadPath (spaceUse);
    pDiffuse = diffusePath (spaceUse);
    pDiffuseMix = diffuseMixPath (spaceUse);
    pShift = shiftPath (spaceUse);
    pSideOut = sideOutPath (spaceUse);
    pTrim = trimPath (spaceUse);
    pCross = crossPath (spaceUse);
    pLoopCap = loopCapPath (feedUse);

    // after a throw, the feedback ride falls back over about two bars
    cThrowDown = approach (std::max (0.3f, 32.0f * sixteenth / sampleRate) * 0.5f, sampleRate);
    cFillFade = approach (std::max (0.5f, 16.0f * sixteenth / sampleRate), sampleRate);
    cThrowTail = approach (std::max (0.6f, 32.0f * sixteenth / sampleRate), sampleRate); // a closed send's throw rings ~2 bars
}

Echo::View Echo::view() const noexcept
{
    View v;
    v.dottedSeconds = dottedSm / sampleRate;
    v.quarterSeconds = quarterSm / sampleRate;
    v.gainDotted = gainDottedNow * easeNow;
    v.gainQuarter = gainQuarterNow * easeNow;
    v.pan = pPan;
    v.spread = pSpread;
    v.diffuse = pDiffuse;
    v.rotate = pShift;
    v.theta = thetaNow;
    v.returnLevel = levelNow;
    v.fill = fillAmount;
    v.throwing = std::max ({ heldSend, throwEnv, tail > 0 ? glowYouth * glowAlive : 0.0f });
    v.ease = easeNow;
    v.breath = breathNow;
    v.runaway = runaway;
    v.glow = glowAlive;
    // relative to the track's own recent peaks, so a quiet track and a loud one look alike
    v.body = peakLong > 1.0e-9f ? clamp01 (1.41f * std::sqrt (rmsShort) / peakLong) : 0.0f;
    v.echoBody = peakLong > 1.0e-9f ? clamp01 (std::max (outEnvM, outEnvS) / (0.85f * peakLong)) : 0.0f;
    return v;
}

void Echo::process (float xm, float xs, float im, float is, float& wm, float& ws) noexcept
{
    // the itch's throw rides the echo's own loop only in 2.5's tail; since 2.6 it goes to the
    // afterglow (processGlow), and the echo carries on as it was
    const bool itchThrow = touchNow.throwing;
    const bool holding = held || (tail == 0 && itchThrow);
    // controls glide; the paths follow every 16 samples
    feed += cControl * (feedTarget - feed);
    space += cControl * (spaceTarget - space);
    if ((count & 15) == 0)
        updatePaths();

    // the heads glide like tape when the tempo changes
    if (dottedSm <= 0.0f)
    {
        dottedSm = (timing == 1 ? 8.0f / 3.0f : timing == 2 ? 6.0f : 3.0f) * sixteenth;
        quarterSm = (timing == 2 ? 8.0f : 4.0f) * sixteenth;
    }
    // the two heads' places in sixteenths: dotted 1/8 + 1/4, 1/4 triplet + 1/4, or half
    // time (dotted 1/4 + 1/2). Changing it glides the heads like tape.
    static constexpr float shortHead[3] = { 3.0f, 8.0f / 3.0f, 6.0f }, longHead[3] = { 4.0f, 4.0f, 8.0f };
    dottedSm += cHead * (shortHead[timing] * sixteenth - dottedSm);
    quarterSm += cHead * (longHead[timing] * sixteenth - quarterSm);

    // listening: max(|L|, |R|) = |M| + |S|
    const float m = std::abs (xm) + std::abs (xs);
    eFast += (m > eFast ? cFast : rFast) * (m - eFast);
    eSlow += cSlow * (eFast - eSlow);
    eDuck += (m > eDuck ? aDuck : rDuck) * (m - eDuck);
    rmsShort += cShort * (m * m - rmsShort);
    if (m > 1.0e-6f || heard > 0)
        ++heard;
    // a running mean until four seconds have been heard, then the four-second follower
    rmsLong += std::max (cLong, 1.0f / (float) std::max<int64_t> (1, heard)) * (m * m - rmsLong);
    // while a throw runs away, the loop's measure of "what was played" holds still, so the
    // howl can climb on its own in a silence
    peakLong = std::max (eFast, runaway > 0.05f ? peakLong : peakLong * (1.0f - cPeak));
    silentFor = eFast < peakLong * 0.02f ? silentFor + 1 : 0; // 34 dB under recent peaks

    // a new tree for the window: an onset that stands out from the recent level
    hitPeak = std::max (hitPeak, peakLong > 1.0e-9f ? eFast / peakLong : 0.0f);
    if (onsetArmed > 0.0f && eFast > 1.9f * eSlow + 0.01f * peakLong + 1.0e-7f && eFast > 0.12f * peakLong)
    {
        onsetPending = std::max (onsetPending, clamp01 (eFast / peakLong));
        onsetArmed = 0.0f;
    }
    else if (eFast < 1.25f * eSlow)
    {
        onsetArmed = 1.0f;
    }

    // the throw: the send opens at once; the feedback ride and the launch linger
    heldSend += (holding ? cHeldUp : cHeldDown) * ((holding ? 1.0f : 0.0f) - heldSend);
    if (holding)
    {
        throwEnv += cHeldUp * (1.0f - throwEnv);
        throwTail += cHeldUp * (1.0f - throwTail);
    }
    else
    {
        throwEnv -= cThrowDown * throwEnv;
        throwTail -= cThrowTail * throwTail;
    }
    if ((holding && ! wasHeld) || (itchThrow && ! wasThrowing && ! holding))
    {
        launchTarget = dice.sign() * dice.range (0.7f, 1.5f);
        onsetPending = 1.0f;
        onsetThrown = true;
    }
    wasHeld = holding;
    wasThrowing = itchThrow;
    // the runaway: a throw held at endless may grow past what was played, into a howl the
    // tape itself holds; it falls back like any throw when you let go
    const float runTarget = held ? clamp01 ((feed - 0.87f) / 0.1f) : 0.0f;
    runaway += (runTarget > runaway ? cRunUp : cThrowDown) * (runTarget - runaway);
    launch += cLaunch * (launchTarget - launch);
    launchTarget -= cTurnHeal * 4.0f * launchTarget; // heals over ~10 s
    turn += cTurn * (turnTarget - turn);
    turnTarget -= cTurnHeal * turnTarget;            // heals over ~a minute
    const float send = std::max (pSend, 1.15f * heldSend) * (1.0f - touchNow.freeze);

    // breathing: lean late when the part is busy, and swell on its own
    const float busy = rmsLong > 1.0e-14f ? std::clamp (std::sqrt (rmsShort / rmsLong) - 0.7f, 0.0f, 1.0f) : 0.0f;
    lean += cLean * (busy - lean);
    const float leanSamples = lean * 0.008f * sampleRate;
    const float wobbleA = (wowA.next() * wowSeconds + flutter.next() * flutterSeconds) * sampleRate;
    // Since 2.6 the tape's wow moves both heads together, as a real tape's speed does (the quarter
    // head by its longer distance), with only a fifth of a wow of its own. In 2.5 each head wobbled
    // on its own: in a long, regenerating tail the two heads drifted against each other until their
    // repeats cancelled, and the tail fell 15 dB in one beat (after a throw, or four bars into a break).
    const float ownWow = wowB.next() * wowSeconds * sampleRate;
    const float wobbleB = tail > 0 ? quarterSm / std::max (1.0f, dottedSm) * wobbleA + 0.2f * ownWow : ownWow;
    breathNow = std::clamp (breathWander.next() * 1.4f, -1.0f, 1.0f);

    // filling the silence: an eighth in, four bars at most
    const bool silent = silentFor > (int64_t) (2.0f * sixteenth) && silentFor < (int64_t) (64.0f * sixteenth);
    // When the silence outlasts the four bars, the fill lets go over about two bars (since 2.6):
    // until then it let go in a tenth of a second, and the tail fell 16 dB in one beat. When the
    // music comes back it still lets go at once.
    const bool longSilence = tail > 0 && silentFor >= (int64_t) (64.0f * sixteenth);
    fillAmount += (silent ? cFillUp : longSilence ? cFillFade : cFillDown) * ((silent ? 1.0f : 0.0f) - fillAmount);

    // filling raises the feedback, never past the feed's cap; a throw rides it up, and with
    // a low feed it lends the loop a dub's worth of it
    float gd = pDotted + 0.14f * fillAmount, gq = pQuarter + 0.12f * fillAmount;
    if (gd + gq > pLoopCap)
    {
        const float s = pLoopCap / (gd + gq);
        gd *= s;
        gq *= s;
    }
    // a break that goes on: past the fill's four bars the loop lets go a little more over the next
    // four, so the tail dissolves with no corner instead of hanging at dub's near-unity for half a minute
    if (longSilence)
    {
        const float over = (float) (silentFor - (int64_t) (64.0f * sixteenth)) / (64.0f * sixteenth);
        const float letGo = 1.0f - 0.12f * smooth01 (over);
        gd *= letGo;
        gq *= letGo;
    }
    gainDottedNow = std::min (0.9f + 0.12f * runaway, std::max (gd + (0.12f + 0.2f * runaway) * throwEnv, 0.72f * throwTail));
    gainQuarterNow = std::min (0.66f + 0.1f * runaway, std::max (gq + (0.1f + 0.15f * runaway) * throwEnv, 0.5f * throwTail));
    // the hand's freeze: the loop holds what it has (the ceiling below keeps it from growing)
    gainDottedNow = lerp (gainDottedNow, 0.62f, touchNow.freeze);
    gainQuarterNow = lerp (gainQuarterNow, 0.42f, touchNow.freeze);

    // the heads
    const float onBeat = tapeM.latency() - 2.0f;
    // the hand's tape speed: faster tape, closer heads (the repeats rise); never past the loop
    const float maxDelay = 4.3f * sampleRate;
    const float speed = std::clamp (touchNow.speed, 0.4f, 2.5f);
    const float dDelay = std::min (dottedSm / speed - onBeat + leanSamples + wobbleA, maxDelay);
    const float qDelay = std::min (quarterSm / speed - onBeat + 1.3f * leanSamples + wobbleB, maxDelay);
    const float dM = loopM.read (dDelay), qM = loopM.read (qDelay);
    const float dS = loopS.read (dDelay), qS = loopS.read (qDelay);

    // dotted keeps its side, quarter crosses: in M/S, keep S and flip S. With more
    // space the quarter keeps the Side too, so the Side grows with every pass.
    float fbM = gainDottedNow * dM + gainQuarterNow * qM;
    float fbS = gainDottedNow * dS + pCross * gainQuarterNow * qS;

    // the hand's band: the loop passes through a narrow band, so the repeats thin into it
    if ((count & 15) == 0)
    {
        bandM.bandpass (std::min (touchNow.bandHz, 0.4f * sampleRate), 5.0f, sampleRate);
        bandS.bandpass (std::min (touchNow.bandHz, 0.4f * sampleRate), 5.0f, sampleRate);
    }
    {
        const float bm = bandM.process (fbM), bs = bandS.process (fbS);
        const float amount = clamp01 (touchNow.band);
        fbM = lerp (fbM, 1.4f * bm, amount);
        fbS = lerp (fbS, 1.4f * bs, amount);
    }

    // The loop may never grow louder than what was played into it: past a share of the
    // recent input peaks the feedback eases off (Mid and Side each on their own, so the
    // Side never moves the Mid).
    const float ceiling = (pCeiling + 0.25f * throwEnv) * peakLong * (1.0f + 7.0f * runaway * runaway);
    envM += (std::abs (fbM) > envM ? aLoop : rLoop) * (std::abs (fbM) - envM);
    envS += (std::abs (fbS) > envS ? aLoop : rLoop) * (std::abs (fbS) - envS);
    easeNow = envM > ceiling ? ceiling / envM : 1.0f;
    fbM *= easeNow;
    fbS *= envS > 1.3f * ceiling ? 1.3f * ceiling / envS : 1.0f;

    // the Side's own loop: each pass dissolves it further, and beyond, it turns
    const float sideMod = sideDrift.next() * 0.0003f * sampleRate;
    {
        // always running, faded in: a diffuser that switches on would jump the Side in time
        float diffused = fbS;
        for (size_t k = 0; k < sideDiffusers.size(); ++k)
            diffused = sideDiffusers[k].process (diffused, pDiffuse, sideMod * (0.4f + 0.3f * (float) k));
        fbS = lerp (fbS, diffused, pDiffuseMix);
    }
    {
        float re = 0.0f, imag = 0.0f;
        pairS.process (fbS, re, imag);
        if (pShift > 0.0005f)
        {
            rotatePhase = wrapAngle (rotatePhase + tau * pShift / sampleRate);
            const float blend = smooth01 (pShift / 0.04f);
            fbS = lerp (fbS, re * std::cos (rotatePhase) - imag * std::sin (rotatePhase), blend);
        }
    }

    // write the Mid
    const float drive = pDrive * (1.0f + 1.6f * runaway);
    const float writeM = tapeM.process (send * xm + fbM + im, drive, tapeDiffusion);

    // every pass writes a little of the Mid into the Side again: the repeats step outward
    thetaNow = 1.6f * thetaWander.next() + launch + turn;
    float reM = 0.0f, imM = 0.0f;
    pairM.process (writeM, reM, imM);
    const float breathSpace = 1.0f + 0.15f * breathNow;
    const float pan = (pPan + 0.12f * throwEnv) * breathSpace;
    const float inject = pan * std::sin (thetaNow) * writeM + pSpread * breathSpace * std::cos (thetaNow) * imM;
    const float writeS = tapeS.process (send * pInWidth * xs + fbS + is, drive, tapeDiffusion) + inject;

    loopM.push (writeM);
    loopS.push (writeS);

    // ducks under what's playing, blooms in the gaps, swells on its own (-3..+3 dB)
    const float duck = pDuck * (1.0f - 0.8f * throwEnv);
    const float under = std::clamp ((toDb (eDuck) - toDb (peakLong) + 26.0f) / 18.0f, 0.0f, 1.0f); // how loud the music is now
    float level = 1.0f - duck * under;
    const float breathGain = std::pow (10.0f, 0.15f * breathNow);
    level *= breathGain;
    // a throw always comes back, even with feed all the way down: the dub engineer's closed send
    const float returnNow = std::max (pReturn, 1.2f * std::sqrt (throwTail));
    level *= returnNow * (1.0f + 0.2f * fillAmount) * (1.0f + 0.35f * throwEnv);
    level *= touchNow.lift; // the hand rides the return
    levelNow = level;
    if ((count & 15) == 0)
    {
        const float cutoff = 6500.0f * std::pow (2.0f, 0.9f * breathNow); // brighter on the in-breath
        tiltM.setCutoff (cutoff, sampleRate);
        tiltS.setCutoff (cutoff, sampleRate);
    }

    wm = level * tiltM.lowpass (0.8f * dM + 0.55f * qM);
    ws = level * pSideOut * tiltS.lowpass (0.8f * dS + 0.55f * pCross * qS);

    if (tail > 0)
    {
        // the afterglow ducks half as deep as the echo: it breathes with the music but stays
        const float glowLevel = (1.0f - 0.5f * pDuck * under) * breathGain * touchNow.lift;
        float gm = 0.0f, gs = 0.0f;
        processGlow (xm, xs, dDelay, qDelay, glowLevel, gm, gs);
        probeGlowM = gm;
        probeGlowS = gs;
        wm += gm;
        ws += gs;
    }

    // The echo may be as loud as what was played, not louder: a ceiling under the safety
    // limiter, for the Mid and the Side each on its own (so space never moves the Mid).
    const float wetCeiling = 0.85f * peakLong * (1.0f + 7.0f * runaway * runaway);
    outEnvM += (std::abs (wm) > outEnvM ? aOut : rOut) * (std::abs (wm) - outEnvM);
    outEnvS += (std::abs (ws) > outEnvS ? aOut : rOut) * (std::abs (ws) - outEnvS);
    if (outEnvM > wetCeiling)
        wm *= wetCeiling / outEnvM;
    if (outEnvS > wetCeiling)
        ws *= wetCeiling / outEnvS;

    // 3.6: the fly steering: the echo comes from the side it turns to (constant power, at most
    // about 9 dB between the sides). Left = M + S, right = M - S, so a balance moves Mid into Side.
    if (touchNow.turn != 0.0f)
    {
        const float a = 0.25f * 3.14159265f * (1.0f + 0.55f * std::clamp (touchNow.turn, -1.0f, 1.0f));
        const float gl = 1.41421356f * std::sin (a), gr = 1.41421356f * std::cos (a);
        const float sum = 0.5f * (gl + gr), diff = 0.5f * (gl - gr);
        const float m = wm, sd = ws;
        wm = sum * m + diff * sd;
        ws = diff * m + sum * sd;
    }

    // space's loudness trim comes last, so the ceiling above never sees it and the Mid
    // changes with space by level only
    wm *= pTrim;
    ws *= pTrim;
    ++count;
}
} // namespace dendrite

namespace dendrite
{
//==============================================================================
// The afterglow. Every 16 samples: how much each pass keeps, how dark and blurred it gets, how fast
// it spirals. The level falls along a half parabola in dB (-60 dB at the end of its life): it
// hardly drops for the first bar, then dissolves faster and faster, so there is no corner where
// it stops. passSeconds: one pass of the loop, weighted like its two heads.
void Echo::updateGlow (float passSeconds) noexcept
{
    const float barSeconds = 16.0f * sixteenth / sampleRate;
    // whisper 7 bars, dub 9, endless 13 (to -60 dB)
    glowLife = (5.0f + 8.0f * clamp01 (feed + touchNow.feed)) * barSeconds;
    const float x = glowAge / glowLife;
    glowAlive = glowAge > 0.0f ? clamp01 (1.0f - x * x) : 1.0f;
    glowYouth = std::exp (-glowAge / (0.75f * barSeconds));
    const float dbPerSecond = -120.0f * x / glowLife - 0.6f;
    // the tape loses a little on every pass (its filters, about half a dB on keys); 1.05 gives it
    // back, so for the first bar it keeps feeding itself, and the saturation and the ceiling hold
    // anything more
    // (the spiral loses a little more: what it shifts leaves the dotted head's comb)
    const float giveBack = tail == 2 ? 1.11f : 1.05f;
    glowMu = x > 1.6f ? 0.0f : std::min (giveBack + 0.02f, giveBack * fromDb (dbPerSecond * passSeconds));
    // darker and more blurred as it goes
    const float dark = 11000.0f * std::pow (2.0f, -2.2f * std::min (x, 1.3f));
    glowDarkM.setCutoff (dark, sampleRate);
    glowDarkS.setCutoff (dark, sampleRate);
    glowDiffusion = tapeDiffusion + 0.3f * smooth01 (x);
    // the spiral: still at first, then each pass is shifted a few Hz, up after a rising move,
    // down after a falling one
    // (a move that leaves the tape alone, a hold, spirals the way the dice say)
    const float dir = std::abs (glowDir) > 0.15f ? glowDir : glowFallback;
    glowSpin = tail == 2 ? 6.0f * dir * smooth01 (x / 0.15f) * (1.0f + x) : 0.0f;
    // the Side turns a little faster on every pass, so the image whirls faster as it fades,
    // like water finding the drain
    glowSpinSide = glowSpin + (tail == 2 ? 0.3f * std::abs (dir) : 0.0f);
}

void Echo::processGlow (float xm, float xs, float dDelay, float qDelay, float glowLevel, float& gm, float& gs) noexcept
{
    const bool writing = touchNow.throwing;
    glowGate += (writing ? cGateUp : cGateDown) * ((writing ? 1.0f : 0.0f) - glowGate);
    if (writing)
    {
        if (glowAge > 0.0f)
        {
            glowDir = 0.0f; // a new throw: listen again for where the tape goes
            glowFallback = 0.7f * dice.sign();
        }
        glowAge = 0.0f;
    }
    else
    {
        glowAge = std::min (glowAge + 1.0f / sampleRate, 1.0e9f);
    }
    // which way the move bends the tape while the afterglow is young
    if (glowAge < 0.6f)
    {
        const float bend = std::clamp (4.0f * (touchNow.speed - 1.0f), -1.0f, 1.0f);
        if (std::abs (bend) > std::abs (glowDir))
            glowDir += cGlowDir * (bend - glowDir);
    }
    if ((count & 15) == 0)
        updateGlow ((0.6f * dDelay + 0.4f * qDelay) / sampleRate);

    const float dM = glowM.read (dDelay), qM = glowM.read (qDelay);
    const float dS = glowS.read (dDelay), qS = glowS.read (qDelay);
    // it feeds itself through the dotted head alone: two heads that wow apart cancel each other a
    // little more on every pass, and the afterglow would die young. The quarter head is heard, not fed.
    float fbM = glowMu * dM;
    float fbS = glowMu * dS;
    fbM = glowDarkM.lowpass (fbM);
    fbS = glowDarkS.lowpass (fbS);

    if (tail == 2)
    {
        // a single-sideband shift inside the loop: every pass moves every partial by the same few
        // Hz, so harmonics stop being harmonic and the tail climbs (or sinks) like a barber pole.
        // The Side's own loop turns a little faster than the Mid's, so the image circles the head.
        float re = 0.0f, im = 0.0f;
        glowSpinM.process (fbM, re, im);
        glowPhaseM = wrapAngle (glowPhaseM + tau * glowSpin / sampleRate);
        fbM = re * std::cos (glowPhaseM) + im * std::sin (glowPhaseM); // (this pair's im leads: + shifts up)
        glowSpinS.process (fbS, re, im);
        glowPhaseS = wrapAngle (glowPhaseS + tau * glowSpinSide / sampleRate);
        fbS = re * std::cos (glowPhaseS) + im * std::sin (glowPhaseS);
    }

    // never louder than what was played into the track: the Mid and the Side each on its own
    const float ceiling = 0.4f * peakLong;
    glowEnvM += (std::abs (fbM) > glowEnvM ? aLoop : rLoop) * (std::abs (fbM) - glowEnvM);
    glowEnvS += (std::abs (fbS) > glowEnvS ? aLoop : rLoop) * (std::abs (fbS) - glowEnvS);
    if (glowEnvM > ceiling)
        fbM *= ceiling / glowEnvM;
    if (glowEnvS > 1.3f * ceiling)
        fbS *= 1.3f * ceiling / glowEnvS;

    const float writeM = glowTapeM.process (glowGate * xm + fbM, pDrive, glowDiffusion);
    float reM = 0.0f, imM = 0.0f;
    glowPair.process (writeM, reM, imM);
    // it drifts further out from the head as it dissolves
    const float out = 1.0f - glowAlive;
    const float pan = pPan + 0.12f + 0.1f * out, spread = pSpread + 0.08f + 0.15f * out;
    const float inject = pan * std::sin (thetaNow + 1.3f) * writeM + spread * std::cos (thetaNow + 1.3f) * imM;
    const float writeS = glowTapeS.process (glowGate * pInWidth * xs + fbS, pDrive, glowDiffusion) + inject;
    glowM.push (writeM);
    glowS.push (writeS);

    if ((count & 15) == 0)
    {
        const float cutoff = 6500.0f * std::pow (2.0f, 0.9f * breathNow);
        glowToneM.setCutoff (cutoff, sampleRate);
        glowToneS.setCutoff (cutoff, sampleRate);
    }
    // its first repeats land as hard as a throw (+8 dB), and that settles over a bar or two into the
    // long dissolve
    const float level = 0.55f * glowLevel * (1.0f + 1.6f * glowYouth);
    gm = level * glowToneM.lowpass (0.8f * dM + 0.55f * qM);
    gs = level * pSideOut * glowToneS.lowpass (0.8f * dS + 0.55f * pCross * qS);
}
} // namespace dendrite
