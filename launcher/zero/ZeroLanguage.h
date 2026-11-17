// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the languages of the launcher: English (the source language) and Español.
//
// The saved "Language" setting is Prism's own (it stays the store). ZERO only understands two values: "en_US" and "es". Anything else a
// settings file may hold from Prism ("es_ES", "de_DE", ...) is read as the nearest one of the two: a Spanish code is Spanish, the rest is
// English. How the strings get translated is in docs/zero/ARCHITECTURE.md "Languages".
#pragma once

#include <QList>
#include <QLatin1String>
#include <QLocale>
#include <QString>
#include <QStringList>

#include <memory>

class QTranslator;

namespace Zero::Language {

inline constexpr QLatin1String kEnglishCode("en_US");
inline constexpr QLatin1String kSpanishCode("es");

/// One entry of the language picker: the code that is saved and the name in its own language (never translated).
struct Choice {
    QString code;
    QString name;
};

/// English first, then Español.
QList<Choice> choices();

/// True for "es", "es_ES", "es-MX", ... (any Spanish code).
bool isSpanish(const QString& code);

/// The code to run with: a saved value wins (a Spanish code is Spanish, anything else English); with nothing saved the launcher
/// follows Windows: Spanish when the system language is any Spanish, English otherwise.
QString initialCode(const QString& saved, const QLocale& system);

/// The locale for dates and numbers of a ZERO code: the system locale when it is already Spanish (keeps its region), plain "es" otherwise;
/// en_US for English.
QLocale localeFor(const QString& code, const QLocale& system);

/// The key of Prism's own translation (its index and mmc_<key>.qm / <key>.po) that fits a ZERO code, chosen among the keys Prism offers
/// ("es_ES", "es_UY", ...); empty for English or when none fits. Spanish: the system locale itself when Prism has it, then Spain's for
/// a Spanish system, otherwise Prism's Latin American Spanish, then any other Spanish.
QString prismKey(const QString& code, const QLocale& system, const QStringList& available);

/// ZERO's own translator for a code, loaded from the qrc (":/zero/i18n/zero_<code>.qm"); null for English or when the file is missing.
std::unique_ptr<QTranslator> loadZeroTranslator(const QString& code);

}  // namespace Zero::Language
