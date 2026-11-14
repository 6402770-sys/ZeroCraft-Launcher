// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the one toolbar row of the Discover sections (docs/zero/CONTENT.md, "ZERO Filter System" > Widgets):
//   [ leading widget ] [ search (240-420 px) ] [ Filters (count pill) ] ........ results caption   Sort: <option>   [grid | list]
// (the leading widget is Mods' "For <instance>" control; the other sections have none)
// 40 px high. The toolbar owns no filter state: the page tells it the search text, the number of active filters, the caption
// and the sort options, and reacts to its signals. FilterPanel anchors under filtersButton(); FilterChipStrip sits below.
#pragma once

#include <QList>
#include <QString>
#include <QTimer>
#include <QWidget>
#include "zero/content/ContentFilter.h"

class QAction;
class QHBoxLayout;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace Zero {
namespace Content {

class ContentToolbar : public QWidget {
    Q_OBJECT
   public:
    enum class ViewMode { Grid, List };
    static constexpr int kSearchDebounceMs = 300;
    static constexpr int kSearchMinWidth = 240;
    static constexpr int kSearchMaxWidth = 420;

    explicit ContentToolbar(QWidget* parent = nullptr);

    /// Placeholder and accessible name of the search field ("Search modpacks").
    void setSearchPlaceholder(const QString& text);
    /// Shows `text` in the search field; emits nothing and cancels a pending debounce.
    void setSearchText(const QString& text);
    QString searchText() const;
    /// Number of active filters: shown as a pill on the Filters button when > 0.
    void setFilterCount(int count);
    int filterCount() const { return m_filterCount; }
    /// "12,431 results" / "48 of 312 loaded" / "" (hidden); the page supplies the text.
    void setResultsCaption(const QString& text);
    QString resultsCaption() const;
    /// The sort combo; hidden when `options` is empty (Technic has no sorting). Emits nothing.
    void setSortOptions(const QList<SortOption>& options, const QString& currentId);
    QString sortId() const;
    /// The grid / list toggle exists only where a section has both views.
    void setViewModesAvailable(bool available);
    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_viewMode; }
    /// A widget at the left of the search field (Mods: the target instance control); the toolbar parents it. Null removes it.
    void setLeadingWidget(QWidget* widget);
    QWidget* leadingWidget() const { return m_leading; }
    /// Focuses and selects the search field (Ctrl+F does this while the toolbar is visible).
    void focusSearch();

    QPushButton* filtersButton() const { return m_filters; }
    QLineEdit* searchField() const { return m_search; }
    QComboBox* sortCombo() const { return m_sort; }
    QToolButton* gridButton() const { return m_gridButton; }
    QToolButton* listButton() const { return m_listButton; }
    QLabel* captionLabel() const { return m_caption; }

   signals:
    /// The search text, debounced by 300 ms (Enter, Esc and the clear button apply at once). Trimmed.
    void searchChanged(const QString& text);
    /// The Filters button was pressed: the page opens (or toggles) the FilterPanel.
    void filtersRequested();
    void sortChanged(const QString& sortId);
    void viewModeChanged(ViewMode mode);

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void flushSearch();
    void refreshIcons();

    QLineEdit* m_search;
    QPushButton* m_filters;
    QLabel* m_caption;
    QComboBox* m_sort;
    QHBoxLayout* m_row;
    QWidget* m_leading = nullptr;
    QWidget* m_viewToggle;
    QToolButton* m_gridButton;
    QToolButton* m_listButton;
    QTimer m_searchTimer;
    QString m_emittedSearch;
    QAction* m_searchIcon = nullptr;
    ViewMode m_viewMode = ViewMode::Grid;
    int m_filterCount = 0;
};

}  // namespace Content
}  // namespace Zero
