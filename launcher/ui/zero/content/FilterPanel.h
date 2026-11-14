// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the filter popover (docs/zero/CONTENT.md, "ZERO Filter System" > Widgets), opened under the toolbar's Filters button.
//
// A Qt::Popup, 360 px wide, at most 70 % of the window high (the facets scroll), `elevated` with a 1 px border, radius lg and the
// popover shadow; it fades in while rising 6 px (160 ms). One group per facet (ContentFilter::facetsFor), each under an overline
// header: Multi = toggle chips (OR), Single = a segmented row, Toggle = a switch row (all of them under "Status"), the Minecraft
// version facet = a searchable list with a "Releases only" switch. A facet with a disabledReason is shown greyed, with the reason
// as text under its header and as tooltip. The panel edits a copy of the FilterState and emits stateChanged() for it:
// debounced by 300 ms while the user clicks (so a few clicks make one search), at once on Reset, and flushed when it closes.
// Footer: Reset (back to the section defaults) | result count | Done. Esc, Done and a click outside close it.
// Keyboard: Tab moves between groups, arrows inside a group, Space / Enter toggle.
//
// Everything comes from the facets and the defaults: there is no provider logic in here.
#pragma once

#include <QHash>
#include <QList>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>
#include "zero/content/ContentFilter.h"

class QAbstractButton;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QVariantAnimation;

namespace Zero {
namespace Content {

/// One-of-N control: equal segments in one outlined row. One tab stop, arrows move the focus, Space / Enter / click choose.
class SegmentedControl : public QWidget {
   public:
    explicit SegmentedControl(const QList<FacetOption>& options, QWidget* parent = nullptr);

    void setSelected(const QString& value);
    /// Replaces the segments (the selection stays when its value is still there, else the first segment is selected).
    void setOptions(const QList<FacetOption>& options);
    QString selected() const { return m_selected; }
    int count() const { return int(m_options.size()); }
    QString valueAt(int index) const { return m_options.at(index).value; }
    QRect segmentRect(int index) const;
    /// true: every segment is as wide as its own label (plus padding) and extra width is shared equally; false (default): all segments
    /// have the width of the widest. The Discover switcher uses natural widths so four sections fit the top bar.
    void setNaturalWidths(bool natural);
    /// Called when the user chooses a segment (also the selected one: the owner decides whether that deselects).
    std::function<void(const QString&)> onActivated;

    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

   private:
    int indexAt(const QPoint& pos) const;
    int naturalWidth(int index) const;

    QList<FacetOption> m_options;
    bool m_natural = false;
    QString m_selected;
    int m_hover = -1;
    int m_pressed = -1;
    int m_focus = 0;
};

class FilterPanel : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kWidth = 360;
    static constexpr int kOpenMs = 160;
    static constexpr int kRisePx = 6;
    static constexpr int kApplyDebounceMs = 300;
    static constexpr int kListRows = 8;
    static constexpr int kListRowHeight = 28;

    explicit FilterPanel(QWidget* parent = nullptr);

    void setFacets(const QList<Facet>& facets);
    /// The section defaults: Reset goes back to them, and a Single facet that has a default value cannot be emptied.
    void setDefaults(const FilterState& defaults);
    /// Shows `state`; emits nothing and drops a pending apply.
    void setState(const FilterState& state);
    FilterState state() const { return m_state; }
    /// The footer's result count ("48 of 312 loaded"); empty hides it.
    void setResultText(const QString& text);
    /// The version list hides snapshots while on (a selected snapshot always stays listed). Default on.
    void setReleasesOnly(bool on);
    bool releasesOnly() const { return m_releasesOnly; }

    /// Opens under `anchor` (fades in, rises 6 px) and focuses the first control. No-op without facets.
    void popup(QWidget* anchor);
    /// Opens it, or closes it when it is open.
    void toggle(QWidget* anchor);
    bool isOpen() const { return isVisible(); }
    /// Sends a pending change now.
    void flush();

    // Test / automation access.
    QList<QAbstractButton*> pillsOf(const QString& facetId) const;
    SegmentedControl* segmentedOf(const QString& facetId) const;
    QAbstractButton* switchOf(const QString& facetId) const;
    QListWidget* listOf(const QString& facetId) const;
    QLineEdit* listFilterOf(const QString& facetId) const;
    QAbstractButton* releasesOnlySwitch() const { return m_releasesSwitch; }
    /// The group widget of a facet (it is disabled for a facet with a disabledReason) and its reason line.
    QWidget* groupOf(const QString& facetId) const { return m_groups.value(facetId); }
    QLabel* reasonLabelOf(const QString& facetId) const { return m_reasons.value(facetId); }
    QAbstractButton* resetButton() const { return m_reset; }
    QAbstractButton* doneButton() const { return m_done; }
    QLabel* resultLabel() const { return m_result; }
    /// The panel inside the transparent shadow margin.
    QWidget* panel() const { return m_panel; }
    /// Release versions are "1.20.1" / "1.21"; everything else (snapshots, pre-releases, "b1.7.3") is not.
    static bool isReleaseVersion(const QString& version);

   signals:
    void stateChanged(const FilterState& state);
    void releasesOnlyChanged(bool on);
    void closed();

   protected:
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    struct ListEntry {
        QString facetId;
        QListWidget* list = nullptr;
        QLineEdit* filter = nullptr;
    };

    void rebuild();
    QWidget* buildPills(const Facet& facet, QWidget* parent);
    QWidget* buildSegmented(const Facet& facet, QWidget* parent);
    QWidget* buildVersionList(const Facet& facet, QWidget* parent);
    void applyListFilter(const ListEntry& entry);
    void updateRoving(const QString& facetId, QAbstractButton* current);
    void userToggled(const Facet& facet, const QString& value);
    void edit(const FilterState& next, bool immediate);
    void syncWidgets();
    void place();
    void layoutPanel(int riseOffset);
    const Facet* facetById(const QString& id) const;

    QPointer<QWidget> m_anchor;
    QList<Facet> m_facets;
    FilterState m_defaults;
    FilterState m_state;
    QWidget* m_shadow;
    QWidget* m_panel;
    QScrollArea* m_scroll;
    QWidget* m_content = nullptr;
    QAbstractButton* m_reset;
    QAbstractButton* m_done;
    QLabel* m_result;
    QAbstractButton* m_releasesSwitch = nullptr;
    QHash<QString, QList<QAbstractButton*>> m_pills;
    QHash<QString, SegmentedControl*> m_segments;
    QHash<QString, QAbstractButton*> m_switches;
    QHash<QString, QWidget*> m_groups;
    QHash<QString, QLabel*> m_reasons;
    QList<ListEntry> m_lists;
    QVariantAnimation* m_open;
    QTimer m_applyTimer;
    bool m_dirty = false;
    bool m_syncing = false;
    bool m_releasesOnly = true;
};

}  // namespace Content
}  // namespace Zero
