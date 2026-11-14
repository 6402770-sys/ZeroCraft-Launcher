// SPDX-License-Identifier: GPL-3.0-only
#include "ThemeLibrary.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QWidget>

#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroToast.h"
#include "zero/theme/ThemeArchive.h"

namespace Zero {

ThemeLibrary::ThemeLibrary(Hooks hooks, QWidget* window, QObject* parent) : QObject(parent), m_hooks(std::move(hooks)), m_window(window)
{
    if (!m_hooks.previewSpec)
        m_hooks.previewSpec = [](const ThemeSpec& spec) { applySpecLive(spec); };
    if (!m_hooks.confirmDelete) {
        m_hooks.confirmDelete = [this](const QString& name) {
            QMessageBox box(QMessageBox::Question, tr("Move to Recycle Bin?"), tr("Move “%1” to the Recycle Bin?").arg(name),
                            QMessageBox::NoButton, m_window);
            box.setTextFormat(Qt::PlainText);
            box.setInformativeText(tr("You can restore it from the Recycle Bin."));
            QPushButton* confirm = box.addButton(tr("Move to Recycle Bin"), QMessageBox::AcceptRole);
            QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
            box.setDefaultButton(cancel);
            box.exec();
            return box.clickedButton() == confirm;
        };
    }
}

void ThemeLibrary::toast(const QString& text, bool error, const QString& actionText, std::function<void()> action) const
{
    if (m_window)
        ZeroToast::show(m_window, text, error ? ZeroToast::Kind::Error : ZeroToast::Kind::Success, actionText, std::move(action));
}

QString ThemeLibrary::folderOf(const QString& id) const
{
    if (!Custom::isCustomThemeId(id))
        return {};
    return QDir(m_hooks.themesRoot).absoluteFilePath(id.mid(int(qstrlen(Custom::kIdPrefix))));
}

QString ThemeLibrary::nameOf(const QString& id) const
{
    const QString folder = folderOf(id);
    if (folder.isEmpty())
        return {};
    auto def = Custom::loadDefinition(folder);
    return def ? def->name : QString();
}

bool ThemeLibrary::saveTheme(const QString& folder, const Custom::Definition& def, const QString& backgroundSource, QString* error, QString* savedId,
                            const Custom::AssetSources& assets)
{
    const bool isNew = folder.isEmpty();
    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        toast(message, true);
        return false;
    };
    auto saved = Custom::saveTheme(m_hooks.themesRoot, folder, def, backgroundSource, {}, assets);
    if (!saved)
        return fail(saved.error());
    const QString id = m_hooks.loadTheme ? m_hooks.loadTheme(saved->folder) : QString();
    if (id.isEmpty()) {
        if (isNew)
            QDir(saved->folder).removeRecursively();  // nothing registered it: do not leave a theme the app cannot show
        return fail(tr("The theme was written but could not be loaded."));
    }
    if (m_hooks.applyTheme)
        m_hooks.applyTheme(id);
    if (savedId)
        *savedId = id;
    emit themesChanged(id);
    toast(tr("Saved %1").arg(def.name.simplified()), false);
    return true;
}

bool ThemeLibrary::duplicateTheme(const QString& id)
{
    const QString folder = folderOf(id);
    if (folder.isEmpty())
        return false;
    auto copy = Custom::duplicateTheme(m_hooks.themesRoot, folder);
    if (!copy) {
        toast(copy.error(), true);
        return false;
    }
    const QString newId = m_hooks.loadTheme ? m_hooks.loadTheme(copy->folder) : QString();
    if (newId.isEmpty()) {
        QDir(copy->folder).removeRecursively();
        toast(tr("The copy could not be loaded."), true);
        return false;
    }
    emit themesChanged(newId);
    return true;
}

bool ThemeLibrary::exportTheme(const QString& id, const QString& zipPath)
{
    const QString folder = folderOf(id);
    if (folder.isEmpty())
        return false;
    if (auto r = Custom::exportTheme(folder, zipPath); !r) {
        toast(r.error(), true);
        return false;
    }
    toast(tr("Exported %1").arg(nameOf(id)), false, tr("Show in folder"),
          [zipPath]() { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(zipPath).absolutePath())); });
    return true;
}

void ThemeLibrary::exportThemeWithDialog(const QString& id)
{
    const QString name = nameOf(id);
    if (name.isEmpty())
        return;
    QString fileName = Custom::slugify(name) + QStringLiteral(".zip");
    const QString start = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).absoluteFilePath(fileName);
    QString path = QFileDialog::getSaveFileName(m_window, tr("Export theme"), start, tr("ZERO theme (*.zip)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".zip");
    exportTheme(id, path);
}

bool ThemeLibrary::importZip(const QString& zipPath)
{
    auto imported = Custom::importTheme(zipPath, m_hooks.themesRoot);
    if (!imported) {
        toast(imported.error(), true);
        return false;
    }
    const QString id = m_hooks.loadTheme ? m_hooks.loadTheme(imported->folder) : QString();
    if (id.isEmpty()) {
        QDir(imported->folder).removeRecursively();
        toast(tr("The theme was imported but could not be loaded."), true);
        return false;
    }
    emit themesChanged(id);
    toast(tr("Imported %1").arg(imported->name), false);
    return true;
}

void ThemeLibrary::importWithDialog()
{
    const QString path = QFileDialog::getOpenFileName(m_window, tr("Import theme"), QString(), tr("ZERO theme (*.zip)"));
    if (!path.isEmpty())
        importZip(path);
}

bool ThemeLibrary::deleteTheme(const QString& id)
{
    const QString folder = folderOf(id);
    if (folder.isEmpty())
        return false;
    const QString name = nameOf(id);
    if (!m_hooks.confirmDelete(name.isEmpty() ? id : name))
        return false;
    if (auto r = Custom::trashTheme(folder); !r) {
        toast(r.error(), true);  // the folder is still where it was
        return false;
    }
    if (m_hooks.removeTheme)
        m_hooks.removeTheme(id);
    emit themeDeleted(id);
    emit themesChanged(QString());
    toast(tr("Moved “%1” to the Recycle Bin").arg(name), false);
    return true;
}

}  // namespace Zero
