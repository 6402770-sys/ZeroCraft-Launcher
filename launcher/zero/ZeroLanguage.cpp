// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroLanguage.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTranslator>

static void zeroInitI18nResources()
{
    Q_INIT_RESOURCE(zero_i18n);
}

namespace Zero::Language {

QList<Choice> choices()
{
    // Each language is named in itself: the picker must stay readable for someone who landed in the wrong language.
    return { { QString(kEnglishCode), QStringLiteral("English") }, { QString(kSpanishCode), QStringLiteral("Español") } };
}

bool isSpanish(const QString& code)
{
    const QString c = code.trimmed();
    return c.compare(kSpanishCode, Qt::CaseInsensitive) == 0 || c.startsWith(QLatin1String("es_"), Qt::CaseInsensitive) ||
           c.startsWith(QLatin1String("es-"), Qt::CaseInsensitive);
}

QString initialCode(const QString& saved, const QLocale& system)
{
    if (saved.trimmed().isEmpty())
        return system.language() == QLocale::Spanish ? QString(kSpanishCode) : QString(kEnglishCode);
    return isSpanish(saved) ? QString(kSpanishCode) : QString(kEnglishCode);
}

QLocale localeFor(const QString& code, const QLocale& system)
{
    if (!isSpanish(code))
        return QLocale(QString(kEnglishCode));
    if (system.language() == QLocale::Spanish)
        return system;
    return QLocale(QString(kSpanishCode));
}

QString prismKey(const QString& code, const QLocale& system, const QStringList& available)
{
    if (!isSpanish(code))
        return {};
    QStringList spanish;
    for (const QString& key : available) {
        if (isSpanish(key))
            spanish.append(key);
    }
    if (spanish.isEmpty())
        return {};
    if (system.language() == QLocale::Spanish) {
        if (spanish.contains(system.name()))
            return system.name();
        if (system.territory() == QLocale::Spain && spanish.contains(QLatin1String("es_ES")))
            return QStringLiteral("es_ES");
    }
    for (const QLatin1String preferred : { QLatin1String("es_UY"), QLatin1String("es_ES") }) {
        if (spanish.contains(preferred))
            return QString(preferred);
    }
    spanish.sort();
    return spanish.first();
}

std::unique_ptr<QTranslator> loadZeroTranslator(const QString& code)
{
    if (!isSpanish(code))
        return nullptr;
    static bool initialised = false;
    if (!initialised) {
        zeroInitI18nResources();
        initialised = true;
    }
    const QString path = QStringLiteral(":/zero/i18n/zero_%1.qm").arg(kSpanishCode);
    if (!QFile::exists(path)) {
        qDebug() << "ZERO has no translation file for" << code;
        return nullptr;
    }
    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(path)) {
        qWarning() << "ZERO's translation file for" << code << "could not be loaded";
        return nullptr;
    }
    return translator;
}

}  // namespace Zero::Language
