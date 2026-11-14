// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - what a target instance has installed, as Discover > Mods needs it (docs/zero/CONTENT-MODS.md, "Discover > Mods").
//
//  - `InstalledMod`: one mod with packwiz metadata (provider + project id + file id); the key has the shape of `managedKey()`, so
//    a card finds its mod with `installedKey(item)`.
//  - `InstalledModsSource`: the small interface the section uses (tests derive fakes). It loads the target's mods on demand and
//    says when they changed.
//  - `InstanceModsSource`: the production one over `MinecraftInstance::loaderModList()` (the same ModFolderModel the instance's mods
//    page uses). It starts the model's file watching only while the section is visible and stops it again only when it was the one
//    who started it (ResourceFolderModel::startWatching() is a toggle, not a counter).
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class QDir;
class ResourceFolderModel;

namespace Zero::Content {

struct InstalledMod {
    QString key;     ///< managedKey("modrinth" | "flame", project id)
    QString fileId;  ///< the provider file / version id of the installed file (compared with the Versions tab's ids)
    QString name;    ///< the mod's name from its metadata
    QString version; ///< the installed version number, when the metadata has it
};

class InstalledModsSource : public QObject {
    Q_OBJECT
   public:
    using QObject::QObject;

    /// The instance whose mods are read; empty = none. Loading starts when the source is active.
    virtual void setInstance(const QString& instanceId) = 0;
    /// Active = the instance's mods folder is watched (the section is visible). Becoming active loads the mods again.
    virtual void setActive(bool active) = 0;
    /// The installed mods that have provider metadata (a jar without metadata cannot be matched to a project).
    virtual QList<InstalledMod> mods() const = 0;

   signals:
    /// The set of installed mods may have changed (the folder finished loading, a mod was added or removed).
    void changed();
};

/// The installed entries of a packwiz `.index` directory (`*.pw.toml`) whose file is still in `filesDir` (a ".disabled" copy counts as
/// there). Entries without provider metadata are not listed. Pure: no instance, no model (Discover > Resource Packs reads
/// `resourcepacks/.index` with it; tests write a temp folder).
QList<InstalledMod> installedFromIndex(const QDir& indexDir, const QDir& filesDir);

/// The instance folder a source reads.
enum class InstalledFolder { Mods, ResourcePacks, ShaderPacks };

/// Production source. `InstalledFolder::Mods` reads the metadata of the instance's `loaderModList()`; `ResourcePacks` follows the
/// instance's `resourcePackList()` (the same model the instance's packs page uses) and reads its `.index` with installedFromIndex();
/// `ShaderPacks` follows `shaderPackList()`, whose packwiz metadata sits in the shaderpacks folder itself (its `indexDir()` is that
/// folder, so installedFromIndex() gets the same folder twice).
class InstanceModsSource : public InstalledModsSource {
    Q_OBJECT
   public:
    explicit InstanceModsSource(QObject* parent = nullptr, InstalledFolder folder = InstalledFolder::Mods);
    ~InstanceModsSource() override;

    void setInstance(const QString& instanceId) override;
    void setActive(bool active) override;
    QList<InstalledMod> mods() const override;

   private:
    void attach();
    void detach();

    InstalledFolder m_folder;
    QString m_instanceId;
    QPointer<ResourceFolderModel> m_model;
    bool m_active = false;
    bool m_startedWatching = false;  ///< this source turned the model's watching on, so it turns it off
    QTimer m_changedTimer;
};

}  // namespace Zero::Content
