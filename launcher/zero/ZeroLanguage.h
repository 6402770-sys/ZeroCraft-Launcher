// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QLatin1String>

namespace Zero::Language {

// ZERO's own strings exist only in English for now. Loading Prism's translations (and Qt's) for another language, or
// formatting dates with another locale, gives a half-translated UI ("Cuentas" next to "Accounts", Spanish dates inside
// English sentences). Until ZERO ships its own translations the whole launcher runs in English: no translator is
// installed, the default locale is en_US, the translation index is not downloaded and the language settings are hidden.
// The user's saved "Language" setting is left untouched so it applies again once translations exist.
inline constexpr bool kEnglishOnly = true;
inline constexpr QLatin1String kEnglishCode("en_US");

}  // namespace Zero::Language
