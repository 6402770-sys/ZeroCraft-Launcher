// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroThemeState.h"

namespace Zero {

Theme& Theme::instance()
{
    static Theme theme;
    return theme;
}

const ThemeSpec& Theme::current()
{
    return instance().m_spec;
}

void Theme::setCurrent(const ThemeSpec& spec)
{
    instance().m_spec = spec;
    emit instance().changed();
}

void Theme::setReduceMotion(bool reduce)
{
    if (instance().m_spec.reduceMotion == reduce)
        return;
    instance().m_spec.reduceMotion = reduce;
    emit instance().changed();
}

}  // namespace Zero
