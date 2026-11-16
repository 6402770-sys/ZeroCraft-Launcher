// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Keep my game settings" of a modpack update (docs/zero/CONTENT.md, "Modpacks" > "Update").
//
// Prism's update removes the old pack's override files and copies the new pack's overrides over the instance, so the pack's own
// options.txt, servers.dat ... overwrite the player's. `KeptSettings` copies the player's versions of a fixed list of files into a
// temporary folder before the update and, after a successful update of the same instance, puts them back one by one (written next to
// the target and renamed over it, never half-written). Files the instance did not have are not created; a file that cannot be put
// back leaves what the update wrote.
//
// No widgets, no APPLICATION.
#pragma once

#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

namespace Zero::Content {

/// The files kept, relative to the instance's game root (forward slashes): the options, keybinds and resource pack choices, the server
/// list and the shader choice.
const QStringList& keptGameFiles();

class KeptSettings {
   public:
    /// `tempParent`: where the temporary folder is made (empty = the system temp folder).
    explicit KeptSettings(const QString& tempParent = {});

    /// False when the temporary folder could not be made.
    bool isValid() const;

    /// Copies each of `keptGameFiles()` that exists under `gameRoot` into the temporary folder. False when a copy failed (the files
    /// copied so far stay recorded, `restore()` still works); true when all that exist were copied (maybe none).
    bool save(const QString& gameRoot);

    /// The files `save()` copied (relative paths).
    QStringList savedFiles() const { return m_saved; }

    /// Puts the saved files back under `gameRoot`, each replacing what is there. False when one could not be put back (`failed`, when
    /// given, lists them); the others are restored anyway.
    bool restore(const QString& gameRoot, QStringList* failed = nullptr) const;

    /// Removes the temporary copy now (also done when the object goes away).
    void discard();

   private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QStringList m_saved;
};

}  // namespace Zero::Content
