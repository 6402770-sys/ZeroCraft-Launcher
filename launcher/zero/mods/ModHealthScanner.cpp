// SPDX-License-Identifier: GPL-3.0-only
#include "ModHealthScanner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

namespace Zero {
namespace Mods {

Report scanModsFolder(const QString& modsDir, const InstanceContext& context)
{
    QList<ModMetadata> enabled;
    QList<ModMetadata> disabledJars;
    QStringList disabledNames;

    const QDir dir(modsDir);
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& info : files) {
        if (info.isSymLink())
            continue;
        const QString name = info.fileName();
        if (name.endsWith(QLatin1String(".disabled"), Qt::CaseInsensitive)) {
            disabledNames.append(name);
            disabledJars.append(parseJar(info.absoluteFilePath()));
        } else if (name.endsWith(QLatin1String(".jar"), Qt::CaseInsensitive)) {
            enabled.append(parseJar(info.absoluteFilePath()));
        }
    }
    Report report = analyze(enabled, context, disabledNames, disabledJars);
    for (Finding& f : report.findings)
        f.actions = actionsFor(f, enabled);
    return report;
}

ModHealthScanner::ModHealthScanner(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<Zero::Mods::Report>();
}

void ModHealthScanner::analyze(const QString& instanceId, const QString& modsDir, const QString& instanceRoot)
{
    const quint64 generation = ++m_generation;
    m_scanning = true;

    // Same pattern as StorageScanner: the pool job never touches `this`; it posts to the application object and the QPointer is
    // read on the GUI thread.
    QPointer<ModHealthScanner> self(this);
    QThreadPool::globalInstance()->start([self, instanceId, modsDir, instanceRoot, generation]() {
        const InstanceContext context = readInstanceContext(instanceRoot);
        const Report report = scanModsFolder(modsDir, context);
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, generation, instanceId, report]() {
                if (self)
                    self->onReport(generation, instanceId, report);
            },
            Qt::QueuedConnection);
    });
}

void ModHealthScanner::onReport(quint64 generation, const QString& instanceId, const Report& report)
{
    if (generation != m_generation)
        return;  // a newer analyze() superseded this one
    m_scanning = false;
    emit reportReady(instanceId, report);
}

}  // namespace Mods
}  // namespace Zero
