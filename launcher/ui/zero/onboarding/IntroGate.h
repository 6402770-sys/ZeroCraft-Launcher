// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - when the first-run intro plays (pure gating, no Qt). See IntroOverlay.h and docs/zero/SHELL.md "First-run intro".
#pragma once

namespace Zero {
namespace Onboarding {

/// What startup knows when it decides about the intro.
struct IntroGate {
    bool seen = false;             ///< the ZeroIntroSeen setting
    bool forced = false;           ///< ZERO_FORCE_INTRO=1 (developers)
    bool screenshotRun = false;    ///< --zero-screenshot
    bool launchesInstance = false; ///< started to launch an instance / show an instance window directly
    bool existingData = false;     ///< the data dir already has instances or accounts and was not just imported from Prism
};

struct IntroDecision {
    bool show = false;      ///< play the intro now
    bool markSeen = false;  ///< set ZeroIntroSeen silently (an older ZERO data dir: those users never see the intro)
};

/// Pure gating. Screenshot and direct-launch runs never show it and never touch the setting; `forced` shows it regardless of the
/// setting and of existing data; seen: nothing; existing data: silently seen; otherwise (a new data dir) show.
inline IntroDecision decideIntro(const IntroGate& gate)
{
    if (gate.screenshotRun || gate.launchesInstance)
        return {};
    if (gate.forced)
        return { true, false };
    if (gate.seen)
        return {};
    if (gate.existingData)
        return { false, true };
    return { true, false };
}

}  // namespace Onboarding
}  // namespace Zero
