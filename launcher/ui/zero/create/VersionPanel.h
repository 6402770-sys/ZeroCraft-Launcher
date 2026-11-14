// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Vanilla / Modded body of Create Instance: the Minecraft version list (filter chips, search) and, for
// Modded, the loader choice (only loaders with versions for the chosen Minecraft version, the others named in a caption)
// with the loader version list. Data comes from a MetaSource; loading shows row skeletons, a failed list an inline error
// with Retry.
#pragma once

#include <QWidget>
#include <optional>

#include "zero/content/CreateInstanceModel.h"

class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QAction;
class QPushButton;
class QVBoxLayout;

namespace Zero {
namespace Create {

class ChoiceChipRow;
class MetaSource;
class VersionListView;

class VersionPanel : public QWidget {
    Q_OBJECT
   public:
    VersionPanel(MetaSource* source, QWidget* parent = nullptr);

    /// Modded shows the loader column and loads the loader lists.
    void setModded(bool modded);
    bool isModded() const { return m_modded; }

    QString minecraftVersion() const { return m_minecraft; }
    std::optional<Loader> loader() const { return m_loader; }
    QString loaderVersion() const { return m_loaderVersion; }
    /// Vanilla: a Minecraft version is chosen. Modded: also a loader and one of its versions.
    bool hasSelection() const;

    /// Chooses these once the lists are there (the filter switches to the version's kind). Used by the dev screenshot aid.
    void preselect(const QString& minecraft, std::optional<Loader> loader);

    /// No list this mode needs is still loading (loaded or failed).
    bool isSettled() const;

    VersionFilter filter() const { return m_filter; }
    void setFilter(VersionFilter filter);
    void setSearchText(const QString& text);

    ChoiceChipRow* filterChips() const { return m_filterChips; }
    ChoiceChipRow* loaderChips() const { return m_loaderChips; }
    VersionListView* minecraftList() const { return m_mcList; }
    VersionListView* loaderList() const { return m_loaderList; }
    QLineEdit* searchField() const { return m_search; }
    /// "Not available for …" caption (empty when hidden).
    QString unavailableText() const;
    /// First focusable widget (tab order: cards -> this).
    QWidget* firstFocusWidget() const;

   signals:
    /// Minecraft version, loader or loader version changed.
    void selectionChanged();
    /// isSettled() may have changed.
    void settledChanged();

   private:
    void onSourceStatus(const QString& uid);
    void rebuildMinecraftRows();
    void applyMinecraftRows(bool preferCurrent);
    void chooseMinecraft(const QString& id);
    void refreshLoaders();
    void chooseLoader(std::optional<Loader> loader);
    void refreshLoaderVersions();
    void retryFailed();
    void refreshIcons();
    void placeSearch();

    MetaSource* m_source;
    bool m_modded = false;

    QList<VersionRow> m_allRows;
    QList<VersionRow> m_visibleRows;
    LoaderCatalog m_catalog;
    VersionFilter m_filter = VersionFilter::Releases;
    QString m_minecraft;
    std::optional<Loader> m_loader;
    std::optional<Loader> m_wantedLoader;  // the user's (or preselected) loader: kept across Minecraft versions when available
    QString m_loaderVersion;
    QString m_pendingMinecraft;

    QGridLayout* m_columns;
    QWidget* m_mcHeader;
    QVBoxLayout* m_mcHeaderLayout;
    QHBoxLayout* m_toolbar;
    ChoiceChipRow* m_filterChips;
    QLineEdit* m_search;
    QAction* m_searchIcon = nullptr;
    VersionListView* m_mcList;
    QWidget* m_loaderColumn;
    ChoiceChipRow* m_loaderChips;
    QLabel* m_unavailable;
    QWidget* m_failureRow;
    QLabel* m_failureText;
    QPushButton* m_failureRetry;
    VersionListView* m_loaderList;
};

}  // namespace Create
}  // namespace Zero
