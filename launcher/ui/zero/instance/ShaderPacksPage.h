// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Shader packs page of the instance window (docs/zero/CONTENT-PACKS.md, "Shader packs page"). Replaces Prism's
// ShaderPackPage in InstancePageProvider::getPages() (same id "shaderpacks"). The source is the instance's `shaderPackList()`.
//
// A shader pack is ON when its file has no ".disabled" suffix: the switch is Prism's own rename (the BASE
// `ResourceFolderModel::setResourceEnabled`, never the game-running modal box). A folder pack cannot be renamed that way (Prism's rule): its
// switch refuses with a toast.
//   * Nothing changes while the game runs (banner "Close the game to change shaders").
//   * The strip "<name> has no shader mod. Shaders need Iris." (ShaderModNotice, zero/content/ShaderMod) shows while `<game root>/mods` has
//     no Iris / Oculus / OptiFine; its button opens Discover > Mods for that mod.
//   * Remove moves the file to the Recycle Bin with `FS::trash` (never `deleteResources`) and drops the packwiz metadata of the files that went.
//   * The rows, the switch, the selection and the keys are the Mods page's own (ModListView / ModDelegate).
#pragma once

#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>

#include "minecraft/mod/ShaderPackFolderModel.h"
#include "ui/pages/BasePage.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/instance/ModsDialogs.h"
#include "ui/zero/instance/ModsList.h"
#include "zero/content/ModTarget.h"
#include "zero/content/ShaderMod.h"
#include "zero/content/ShaderPackIcons.h"

class MinecraftInstance;
class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace Zero {

class RemoteImageCache;
class ZeroEmptyState;
class ZeroSkeleton;
namespace Content {
class ShaderModNotice;
}
namespace ModsUi {
class RunningBanner;
}

namespace ShadersUi {

/// What the page needs from the application besides the model (tests inject fakes; the application page fills them from the instance).
struct ShadersPageEnvironment {
    std::function<Content::TargetInstance()> target;   ///< name, loaders and mods folder of the instance
    std::function<bool()> isRunning;                   ///< the game runs: no changes
    std::function<bool(const QString&)> trash;         ///< moves a file to the Recycle Bin; default FS::trash. Never deletes for good.
    ModsUi::AskFunction ask;                           ///< default: a ChoiceDialog
    std::function<void()> addShaders;                  ///< "Download shaders": Discover > Shaders for this instance; empty hides the button
    std::function<void(const QString& query)> findMod;  ///< the strip's "Find Iris": Discover > Mods for this instance; empty hides the button
    std::function<QStringList(QWidget* parent)> pickFiles;  ///< "Add file": the .zip files to add; default: a file dialog
    std::function<QString(QWidget* parent)> pickFolder;     ///< "Add folder": the folder to add; default: a folder dialog
    std::function<void(QWidget* parent)> checkUpdates;      ///< "Check for updates" (Prism's checker dialog); empty hides the entry
    std::function<void(QWidget* parent)> openClassic;       ///< the classic page in a dialog; empty hides the menu entry
    ShaderIcons::Lookup iconLookup;                         ///< project icon URLs of the platform packs; empty = every row keeps the sun
    Zero::RemoteImageCache* images = nullptr;               ///< null = RemoteImageCache::instance()
};

class ShaderPacksPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    /// The application page for `inst`.
    ShaderPacksPage(MinecraftInstance* inst, ShaderPackFolderModel* model, QWidget* parent = nullptr);
    /// With an explicit environment (tests).
    ShaderPacksPage(const ShadersPageEnvironment& env, ShaderPackFolderModel* model, QWidget* parent = nullptr);
    ~ShaderPacksPage() override;

    QString id() const override { return QStringLiteral("shaderpacks"); }
    QString displayName() const override { return tr("Shader Packs"); }
    QIcon icon() const override { return QIcon::fromTheme("shaderpacks"); }
    QString helpPage() const override { return QStringLiteral("shader-packs"); }
    void openedImpl() override;
    void closedImpl() override;

    /// What the page shows below the toolbar.
    enum class View { Loading, List, Empty, NoMatches };
    View view() const { return m_view; }
    bool isLoaded() const { return m_loaded; }
    bool isLocked() const { return m_locked; }

    QString searchText() const;
    void setSearchText(const QString& text);

    /// The game started or stopped (also re-read from the environment).
    void refreshRunning();
    /// Reads the model into the list now (normally coalesced to the next event-loop turn).
    void rebuildNow();

    // The actions behind the switch, the keys and the row menu. `rows` are rows of the list.
    void requestToggle(const QList<int>& rows);
    void requestRemove(const QList<int>& rows);
    /// "Add file" / "Add folder": copies the picked paths into the folder (asks before it replaces a pack of the same name).
    void addPaths(const QStringList& paths);
    QList<int> selectedRows() const;
    void selectRow(int row);

    // Parts, for the tests and the dev aids.
    ModsUi::ModListView* listView() const { return m_list; }
    ModsUi::ModListModel* listModel() const { return m_listModel; }
    ZeroEmptyState* emptyState() const { return m_empty; }
    ZeroSkeleton* skeleton() const { return m_skeleton; }
    QLineEdit* searchField() const { return m_search; }
    QLabel* captionLabel() const { return m_caption; }
    QLabel* subtitleLabel() const { return m_subtitle; }
    QPushButton* downloadButton() const { return m_downloadButton; }
    QPushButton* addFileButton() const { return m_addFileButton; }
    QToolButton* overflowButton() const { return m_overflow; }
    Content::ShaderModNotice* shaderModNotice() const { return m_notice; }
    QWidget* banner() const;
    /// Whether the "no shader mod" strip is on screen.
    bool noticeShown() const;
    /// Fills `menu` with the `...` entries of `row` (selecting it); onRowMenu() shows it.
    void fillRowMenu(QMenu* menu, int row);

   signals:
    void addShadersRequested();

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

   private:
    void build();
    void connectModel();
    void scheduleRebuild();
    void rebuild();
    void applyView();
    void updateChrome();
    /// Asks (once per session and project) for the icon URLs of the packs with platform metadata.
    void requestIcons(const QList<ShaderIcons::Ref>& refs);
    void setView(View view);
    /// A row of the list and the Prism pack behind it, checked: `pack` is null when the Prism model moved on since the row was read.
    struct Picked {
        int listRow = -1;
        int sourceRow = -1;
        ShaderPack* pack = nullptr;
    };
    Picked pick(int row) const;
    bool stillThere(const Picked& picked) const;
    void onRowMenu(int row, const QPoint& globalPos);
    void onEmptyAction();
    void openShadersFolder();
    void toast(const QString& text, ZeroToast::Kind kind);
    bool blocked() const { return m_locked; }

    ShadersPageEnvironment m_env;
    QPointer<ShaderPackFolderModel> m_model;

    // chrome
    ModsUi::RunningBanner* m_banner = nullptr;
    Content::ShaderModNotice* m_notice = nullptr;
    QWidget* m_toolbar = nullptr;
    QLineEdit* m_search = nullptr;
    QPushButton* m_downloadButton = nullptr;
    QPushButton* m_addFileButton = nullptr;
    QToolButton* m_overflow = nullptr;
    QWidget* m_statusRow = nullptr;
    QLabel* m_caption = nullptr;
    QLabel* m_subtitle = nullptr;
    QStackedWidget* m_stack = nullptr;
    ZeroSkeleton* m_skeleton = nullptr;
    ModsUi::ModListView* m_list = nullptr;
    ModsUi::ModListModel* m_listModel = nullptr;
    ZeroEmptyState* m_empty = nullptr;

    // state
    QList<Mods::Installed::Row> m_rows;  ///< every pack, in the Prism model's order
    QList<quintptr> m_tokens;            ///< the Prism packs of m_rows (compared, never dereferenced)
    QList<int> m_sourceRows;
    QHash<QString, QString> m_iconKeys;  ///< row id -> ShaderIcons key of the pack's platform project
    Content::ShaderModFacts m_shaderMod;
    QString m_query;
    View m_view = View::Loading;
    QTimer m_rebuildTimer;
    bool m_loaded = false;
    bool m_dirty = true;
    bool m_locked = false;
    bool m_startedWatching = false;
    bool m_building = false;
};

}  // namespace ShadersUi
}  // namespace Zero
