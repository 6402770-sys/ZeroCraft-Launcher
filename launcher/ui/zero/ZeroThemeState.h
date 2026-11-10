// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the active ThemeSpec shared by the stylesheet and all Zero components.
#pragma once

#include <QObject>
#include "ZeroTokens.h"

namespace Zero {

/// Holds the currently active ThemeSpec. Components read colors from here (never from Tokens) and
/// repaint when `changed()` fires. Defaults to Obsidian with motion enabled, so it is usable
/// without an Application object (tests, Theme Studio previews).
class Theme : public QObject {
    Q_OBJECT
   public:
    static Theme& instance();
    static const ThemeSpec& current();
    static void setCurrent(const ThemeSpec& spec);
    static bool reduceMotion() { return current().reduceMotion; }
    static void setReduceMotion(bool reduce);

   signals:
    void changed();

   private:
    Theme() = default;
    ThemeSpec m_spec = ThemeSpec::obsidian();
};

}  // namespace Zero
