// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroCustomTheme.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace Zero {

std::unique_ptr<ZeroCustomTheme> ZeroCustomTheme::load(const QString& folder)
{
    auto def = Custom::loadDefinition(folder);
    if (!def) {
        qWarning() << "[Theme] Skipping custom theme" << folder << ":" << def.error();
        return nullptr;
    }
    return std::unique_ptr<ZeroCustomTheme>(new ZeroCustomTheme(QDir(folder).absolutePath(), *def));
}

ZeroCustomTheme::ZeroCustomTheme(QString folder, Custom::Definition def)
    : m_folder(std::move(folder)), m_def(std::move(def)), m_id(Custom::idForFolderName(QFileInfo(m_folder).fileName()))
{}

ThemeSpec ZeroCustomTheme::baseSpec()
{
    return Custom::deriveSpec(m_def, m_folder, m_id);
}

QString ZeroCustomTheme::id()
{
    return m_id;
}

QString ZeroCustomTheme::name()
{
    return m_def.name;
}

QString ZeroCustomTheme::tooltip()
{
    return QObject::tr("Your theme, based on %1.").arg(m_def.base == Custom::Base::Minecraft ? QObject::tr("Minecraft") : QObject::tr("Obsidian"));
}

}  // namespace Zero
