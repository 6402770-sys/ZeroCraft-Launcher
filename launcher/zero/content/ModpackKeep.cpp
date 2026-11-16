// SPDX-License-Identifier: GPL-3.0-only
#include "ModpackKeep.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace Zero::Content {

const QStringList& keptGameFiles()
{
    static const QStringList files = {
        QStringLiteral("options.txt"),          QStringLiteral("optionsof.txt"),         QStringLiteral("optionsshaders.txt"),
        QStringLiteral("servers.dat"),          QStringLiteral("config/iris.properties"), QStringLiteral("config/oculus.properties"),
    };
    return files;
}

namespace {
/// Copies `from` to `to` (parent folders made), the target written next to its final place and renamed over it.
bool replaceFile(const QString& from, const QString& to)
{
    QFile source(from);
    if (!source.open(QIODevice::ReadOnly)) {
        return false;
    }
    if (!QDir().mkpath(QFileInfo(to).absolutePath())) {
        return false;
    }
    QSaveFile target(to);
    if (!target.open(QIODevice::WriteOnly)) {
        return false;
    }
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1 << 16);
        if (chunk.isEmpty() && !source.atEnd()) {
            target.cancelWriting();
            return false;
        }
        if (target.write(chunk) != chunk.size()) {
            target.cancelWriting();
            return false;
        }
    }
    return target.commit();
}
}  // namespace

KeptSettings::KeptSettings(const QString& tempParent)
{
    const QString base = tempParent.isEmpty() ? QDir::tempPath() : tempParent;
    m_dir = std::make_unique<QTemporaryDir>(QDir(base).filePath(QStringLiteral("zero-keep-XXXXXX")));
}

bool KeptSettings::isValid() const
{
    return m_dir && m_dir->isValid();
}

bool KeptSettings::save(const QString& gameRoot)
{
    if (!isValid()) {
        return false;
    }
    bool ok = true;
    const QDir root(gameRoot);
    for (const QString& relative : keptGameFiles()) {
        const QString source = root.filePath(relative);
        if (!QFileInfo(source).isFile()) {
            continue;
        }
        if (replaceFile(source, m_dir->filePath(relative))) {
            m_saved.append(relative);
        } else {
            ok = false;
        }
    }
    return ok;
}

bool KeptSettings::restore(const QString& gameRoot, QStringList* failed) const
{
    bool ok = true;
    if (failed) {
        failed->clear();
    }
    if (!isValid()) {
        return m_saved.isEmpty();
    }
    const QDir root(gameRoot);
    for (const QString& relative : m_saved) {
        if (!replaceFile(m_dir->filePath(relative), root.filePath(relative))) {
            ok = false;
            if (failed) {
                failed->append(relative);
            }
        }
    }
    return ok;
}

void KeptSettings::discard()
{
    if (m_dir) {
        m_dir->remove();
    }
    m_dir.reset();
    m_saved.clear();
}

}  // namespace Zero::Content
