// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - what the Appearance page does with the user's custom themes on disk: save, duplicate, export, import, delete
// (Recycle Bin only). It reports to the user with toasts and tells ThemeManager through hooks, so tests can run it without an
// Application. The pure file work is in zero/theme/ThemeFiles.h and ThemeArchive.h.
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

#include "ui/zero/ZeroTokens.h"
#include "zero/theme/ThemeFiles.h"

class QWidget;

namespace Zero {

class ThemeLibrary : public QObject {
    Q_OBJECT
   public:
    /// How the library reaches the application. Everything but the dialogs' defaults must be set.
    struct Hooks {
        QString themesRoot;                                    ///< absolute folder holding the theme folders
        std::function<QString(const QString&)> loadTheme;      ///< (re)register the theme folder; returns its id, empty on failure
        std::function<void(const QString&)> removeTheme;       ///< unregister a theme id (falls back when it was the active one)
        std::function<void(const QString&)> applyTheme;        ///< apply a registered theme and persist it as the application theme
        std::function<void(const QString&)> reapplyTheme;      ///< apply theme `id` again even when it is the current one (editor Cancel)
        std::function<QString()> activeThemeId;                ///< the persisted application theme id ("" = the ZERO default)
        std::function<void(const ThemeSpec&)> previewSpec;     ///< live preview; default Zero::applySpecLive
        std::function<bool(const QString&)> confirmDelete;     ///< "Move <name> to the Recycle Bin?"; default is a QMessageBox
    };

    /// `window` hosts the toasts and the dialogs.
    ThemeLibrary(Hooks hooks, QWidget* window, QObject* parent = nullptr);

    const Hooks& hooks() const { return m_hooks; }
    QString themesRoot() const { return m_hooks.themesRoot; }
    /// themesRoot/<slug> for a custom theme id; empty for any other id.
    QString folderOf(const QString& id) const;
    /// Display name stored in the theme's definition (empty when unreadable).
    QString nameOf(const QString& id) const;

    /// Writes the theme (`folder` empty = new), registers it, applies it and persists it, then toasts "Saved <name>". On failure
    /// nothing is applied, `error` (when given) is set and an error toast shows. `savedId` receives the theme id.
    bool saveTheme(const QString& folder, const Custom::Definition& def, const QString& backgroundSource, QString* error = nullptr,
                   QString* savedId = nullptr, const Custom::AssetSources& assets = {});
    /// Copies the theme and selects the copy (themesChanged); nothing is applied.
    bool duplicateTheme(const QString& id);
    /// Writes the theme zip to `zipPath` and toasts "Exported <name>" with a "Show in folder" action.
    bool exportTheme(const QString& id, const QString& zipPath);
    /// Asks for a file name (default "<name>.zip") and exports.
    void exportThemeWithDialog(const QString& id);
    /// Imports the theme zip into a new folder, registers and selects it (nothing is applied). Errors show verbatim in a toast.
    bool importZip(const QString& zipPath);
    /// Asks for a *.zip and imports it.
    void importWithDialog();
    /// Asks for confirmation, moves the folder to the Recycle Bin and unregisters the theme. A failure (or a refusal) keeps
    /// the folder and every registration as they were. Never deletes permanently.
    bool deleteTheme(const QString& id);

   signals:
    /// The set of custom themes changed; the page rebuilds its cards and selects `selectId` (nothing is applied). May be empty.
    void themesChanged(const QString& selectId);
    /// A theme was moved to the Recycle Bin and unregistered.
    void themeDeleted(const QString& id);

   private:
    void toast(const QString& text, bool error, const QString& actionText = {}, std::function<void()> action = {}) const;

    Hooks m_hooks;
    QPointer<QWidget> m_window;
};

}  // namespace Zero
