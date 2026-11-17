// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Tools > Playtime: how long you played, by day (last 14 days) and by instance. The numbers come from
// zero/history/PlaytimeStats (session history + Prism's per-instance totals); this view only draws them. Read-only.
#pragma once

#include <QDateTime>
#include <QIcon>
#include <QList>
#include <QStringList>
#include <QWidget>
#include <functional>
#include "zero/history/PlaytimeStats.h"

class QPushButton;
class QScrollArea;
class QStackedLayout;

namespace Zero {

class ZeroEmptyState;

/// The 14-day bar chart (custom painted, 180 px high).
class PlaytimeChart : public QWidget {
    Q_OBJECT
   public:
    explicit PlaytimeChart(QWidget* parent = nullptr);

    void setDays(const QList<History::Playtime::Day>& days);
    int barCount() const { return int(m_days.size()); }
    /// "Tue 12 Nov · 2 h 15 min · 3 sessions" for bar `index` (empty when out of range).
    QString tooltipFor(int index) const;
    /// Index of the bar under `pos` (the whole column of a day counts), -1 for none.
    int barAt(const QPoint& pos) const;
    int hoveredBar() const { return m_hover; }
    /// The gridline labels ("30 min", "1 h", ...), bottom to top.
    QStringList gridLabels() const;
    /// The weekday initials under the bars.
    QStringList axisLabels() const;

    QSize sizeHint() const override { return { 480, 180 }; }
    QSize minimumSizeHint() const override { return { 240, 180 }; }

   protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

   private:
    struct Geometry {
        QRectF plot;
        double slot = 0;
        double barWidth = 0;
        qint64 step = 0;
    };
    Geometry geometry() const;
    void updateAccessibleText();

    QList<History::Playtime::Day> m_days;
    int m_hover = -1;
};

class PlaytimeView : public QWidget {
    Q_OBJECT
   public:
    /// Where the numbers come from; the application's own sources are the default.
    struct Sources {
        std::function<QList<History::Session>()> sessions;
        std::function<QList<History::Playtime::InstanceTotal>()> totals;
        std::function<QDateTime()> now;
        std::function<QIcon(const QString& instanceId)> icon;  ///< the same icon the Library shows; empty = no icon
    };
    static Sources applicationSources();

    explicit PlaytimeView(QWidget* parent = nullptr);
    explicit PlaytimeView(Sources sources, QWidget* parent = nullptr);

    /// One-line description shown as the page subtitle while the tool is open.
    static QString description();

    /// Called whenever the tool is opened.
    void open() { refresh(); }
    void refresh();

    // state for tests
    bool isEmptyStateShown() const;
    /// Label, value and caption of the tiles ("This week" / "All time" / "Sessions (30 days)").
    QStringList tileTexts() const;
    PlaytimeChart* chart() const { return m_chart; }
    QStringList instanceRows() const;  ///< "Name\ttotal" of the visible rows
    QPushButton* showAllButton() const { return m_showAll; }
    void clickRow(int index);

   signals:
    void showInstanceRequested(const QString& instanceId);

   protected:
    void showEvent(QShowEvent* event) override;

   private:
    void rebuild();

    Sources m_sources;
    QStackedLayout* m_stack;
    QScrollArea* m_scroll;
    ZeroEmptyState* m_empty;
    PlaytimeChart* m_chart = nullptr;
    QPushButton* m_showAll = nullptr;
    bool m_expanded = false;
    QStringList m_tiles;
    QStringList m_rows;
    QList<QWidget*> m_rowWidgets;
};

}  // namespace Zero
