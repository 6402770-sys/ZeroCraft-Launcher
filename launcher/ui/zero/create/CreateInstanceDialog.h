// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Create Instance (docs/zero/CONTENT.md, "Create Instance"). Replaces NewInstanceDialog for "New instance"
// and "Import": header, four type cards (Vanilla, Modded, Modpack, Import), body [configuration | live preview], footer
// (icon, name, group, Advanced: location + template, Cancel, Create). extractTask() builds exactly what
// NewInstanceDialog::extractTask() builds; MainWindow runs the task while this dialog is still alive (import tasks parent
// their dialogs to it). Modpack sources, "More sources" and curseforge:// links hand over to the classic dialog through
// signals.
#pragma once

#include <QDialog>
#include <QIcon>
#include <QList>
#include <QPair>
#include <QVariant>
#include <functional>
#include <optional>

#include "BaseVersion.h"
#include "zero/content/CreateInstanceModel.h"

class InstanceTask;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace Zero {
namespace Create {

class ImportPanel;
class InstancePreviewPanel;
class MetaSource;
class ModpackSourcesPanel;
class TypeCardRow;
class VersionPanel;

/// Everything the dialog needs from the application, so tests can build it without APPLICATION or network.
struct CreateInstanceEnvironment {
    QStringList groups;                                                 ///< existing instance groups
    std::function<QList<QPair<QString, QString>>()> instanceDirs;       ///< (label, canonical path) of usable instance folders
    QString lastUsedDir;                                                ///< preselected folder (canonical path)
    bool templateAvailable = false;                                     ///< the instance template folder exists
    QString templateToolTip;                                            ///< e.g. the symlink note on Windows
    bool curseForge = false;                                            ///< the build can talk to CurseForge
    std::function<QVariant(const QString&)> readSetting;                ///< null QVariant when unset
    std::function<void(const QString&, const QVariant&)> writeSetting;
    std::function<QIcon(const QString&)> instanceIcon;                  ///< icon of an instance icon key
    std::function<QString(QWidget*, const QString&)> pickIcon;          ///< icon picker; returns the new key, empty if cancelled
    std::function<QString(QWidget*)> chooseImportFile;                  ///< file picker for Import; empty if cancelled
    std::function<MetaSource*(QObject*)> makeMetaSource;                ///< the version lists (parented to the argument)

    /// The real thing: settings, icons, groups, instance folders and Prism's meta lists.
    static CreateInstanceEnvironment fromApplication();
};

class CreateInstanceDialog : public QDialog {
    Q_OBJECT
   public:
    enum class Kind { Vanilla, Modded, Modpack, Import };
    static constexpr int kDefaultWidth = 1000;
    static constexpr int kDefaultHeight = 680;
    static constexpr int kMinWidth = 860;
    static constexpr int kMinHeight = 600;

    /// Settings keys.
    static const char* const kGeometryKey;  // ZeroCreateInstanceGeometry
    static const char* const kKindKey;      // ZeroCreateInstanceKind
    static const char* const kAdvancedKey;  // ZeroCreateInstanceAdvanced

    CreateInstanceDialog(const QString& initialGroup, Kind initialKind, QWidget* parent = nullptr);
    CreateInstanceDialog(CreateInstanceEnvironment env, const QString& initialGroup, Kind initialKind, QWidget* parent = nullptr);
    ~CreateInstanceDialog() override;

    /// The kind remembered from the last use (Vanilla when none).
    static Kind rememberedKind(const CreateInstanceEnvironment& env);
    static QString kindKey(Kind kind);
    static std::optional<Kind> kindFromKey(const QString& key);

    /// The task to run (caller owns it), with name, original name, group, icon, target folder and template set exactly as
    /// NewInstanceDialog::extractTask() sets them; nullptr when there is nothing to create.
    InstanceTask* extractTask();
    QString instGroup() const;
    QString instDir() const;
    QString instName() const;
    QString iconKey() const { return m_iconKey; }
    bool copyTemplateDir() const;

    Kind kind() const { return m_kind; }
    /// Switches the kind programmatically (not remembered for the next time; the cards and keys 1-4 are the user's choice).
    void setKind(Kind kind);
    /// A task can be built and the name is valid (the state of the Create button).
    bool canCreate() const;
    /// The name the dialog suggests for the current choice.
    QString suggestedName() const { return m_suggestedName; }
    /// Calls `done` once the lists the current kind needs have loaded or failed, or after `timeoutMs`.
    void callWhenSettled(std::function<void()> done, int timeoutMs);
    /// Preselects a Minecraft version (and loader) once the lists are in (dev screenshot aid).
    void preselect(const QString& minecraft, std::optional<Loader> loader);

    // Parts, for tests.
    TypeCardRow* typeCards() const { return m_cards; }
    VersionPanel* versionPanel() const { return m_versions; }
    ImportPanel* importPanel() const { return m_import; }
    ModpackSourcesPanel* modpackPanel() const { return m_modpacks; }
    InstancePreviewPanel* preview() const { return m_preview; }
    QLineEdit* nameField() const { return m_name; }
    QComboBox* groupBox() const { return m_group; }
    QPushButton* createButton() const { return m_create; }
    QToolButton* advancedButton() const { return m_advanced; }
    QComboBox* locationBox() const { return m_location; }
    QCheckBox* templateBox() const { return m_template; }
    QString nameError() const;
    QString footerError() const;

   public slots:
    void accept() override;
    void reject() override;

   signals:
    /// Open the classic New Instance dialog on this page ("modrinth", "flame", "technic", "atl", "ftb", ...).
    void classicPageRequested(const QString& pageId);
    /// Open the classic importer with this link (curseforge://).
    void classicImportRequested(const QString& url);
    /// Open the modpack browser.
    void browseModpacksRequested();

   protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void buildHeader(QLayout* root);
    QWidget* buildFooter();
    void applyKind(bool animate);
    void chooseKind(Kind kind);  // the user picked a kind (card, arrows, 1-4)
    void refreshState();
    void refreshIcons();
    void refreshLocations();
    void setAdvancedExpanded(bool expanded);
    void pickIcon();
    void saveState();
    BaseVersion::Ptr findVersion(const QString& uid, const QString& id) const;

    CreateInstanceEnvironment m_env;
    MetaSource* m_meta = nullptr;
    Kind m_kind = Kind::Vanilla;
    QString m_iconKey = QStringLiteral("default");
    QString m_suggestedName;
    bool m_nameEdited = false;
    bool m_nameSelectedOnce = false;
    bool m_kindChosen = false;  // the user picked the kind: only then it is remembered (and never Modpack / Import)

    TypeCardRow* m_cards = nullptr;
    QWidget* m_body = nullptr;
    QStackedWidget* m_config = nullptr;
    VersionPanel* m_versions = nullptr;
    ModpackSourcesPanel* m_modpacks = nullptr;
    ImportPanel* m_import = nullptr;
    InstancePreviewPanel* m_preview = nullptr;

    QWidget* m_footer = nullptr;
    QWidget* m_advancedStrip = nullptr;
    QToolButton* m_iconButton = nullptr;
    QWidget* m_nameColumn = nullptr;
    QLineEdit* m_name = nullptr;
    QLabel* m_nameError = nullptr;
    QWidget* m_groupColumn = nullptr;
    QComboBox* m_group = nullptr;
    QToolButton* m_advanced = nullptr;
    QWidget* m_locationColumn = nullptr;
    QComboBox* m_location = nullptr;
    QCheckBox* m_template = nullptr;
    QLabel* m_footerError = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_create = nullptr;
};

}  // namespace Create
}  // namespace Zero
