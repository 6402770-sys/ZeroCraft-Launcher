// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroCustomFont.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QSet>

namespace Zero::CustomFont {

namespace {

struct State {
    QString path;
    qint64 stamp = 0;
    int id = -1;  // QFontDatabase application font id, -1 = nothing registered
    QString family;
    QSet<QString> everFamilies;
};

State& state()
{
    static State s;
    return s;
}

void unregister(State& s)
{
    if (s.id >= 0)
        QFontDatabase::removeApplicationFont(s.id);
    s.id = -1;
    s.family.clear();
}

}  // namespace

QString load(const QString& path)
{
    State& s = state();
    if (path.isEmpty()) {
        unregister(s);
        s.path.clear();
        s.stamp = 0;
        return {};
    }
    const QFileInfo info(path);
    const qint64 stamp = info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
    if (path == s.path && stamp == s.stamp)
        return s.family;  // same file, same answer (also when it failed: no new warning)

    unregister(s);
    s.path = path;
    s.stamp = stamp;
    if (!info.isFile()) {
        qWarning().noquote() << "ZeroCustomFont: cannot use" << path << "- the file is missing (using the default font)";
        return {};
    }
    // From memory, not from the path: no handle on the file stays behind, so the theme folder can still replace, delete or trash it.
    QFile file(path);
    const QByteArray data = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    file.close();
    const int id = data.isEmpty() ? -1 : QFontDatabase::addApplicationFontFromData(data);
    const QStringList families = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
    if (id < 0 || families.isEmpty()) {
        if (id >= 0)
            QFontDatabase::removeApplicationFont(id);
        qWarning().noquote() << "ZeroCustomFont: cannot use" << path << "- Qt could not load it as a font (using the default font)";
        return {};
    }
    s.id = id;
    s.family = families.first();
    for (const QString& family : families)
        s.everFamilies.insert(family);
    return s.family;
}

QString currentFamily()
{
    return state().family;
}

bool isThemeFontFamily(const QString& family)
{
    return !family.isEmpty() && state().everFamilies.contains(family);
}

}  // namespace Zero::CustomFont
