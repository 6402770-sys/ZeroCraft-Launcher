// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Downloads panel (docs/zero/SHELL.md, "Downloads center"): a popover under the top bar's Downloads button.
//
// A Qt::Popup, 380 px wide, at most 480 px high (the rows scroll), `surface` with a 1 px border, radius lg and the popover shadow; it
// fades in while rising 6 px (160 ms). Header "Downloads" + a "Clear history" text button (only with history). "In progress": one row
// per running job (kind icon, title, caption "Fabric Fun · 12.4 MB of 30 MB · 2.1 MB/s", a 4 px progress bar and a cancel button).
// "Recent": the history (kind icon, title, caption, a state pill, the error of a failed job as a second line); a Done or Failed row opens
// the instance (openInstanceRequested) when the instance still exists. Esc and a click outside close it. It closes itself when there is
// nothing left to show (history cleared while nothing runs).
// Everything comes from the DownloadCenter: the panel keeps no state of its own.
#pragma once

#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>
#include <functional>
#include "zero/downloads/DownloadText.h"

class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QVariantAnimation;

namespace Zero {

namespace Downloads {
class DownloadCenter;
}

class DownloadsPanel : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kWidth = 380;
    static constexpr int kMaxHeight = 480;
    static constexpr int kOpenMs = 160;
    static constexpr int kRisePx = 6;

    explicit DownloadsPanel(Downloads::DownloadCenter* center, QWidget* parent = nullptr);

    /// Tells the panel whether an instance still exists (a finished row of a deleted instance is not clickable). Default: any non-empty id.
    void setInstanceExists(std::function<bool(const QString& instanceId)> exists);

    /// Opens the panel under `anchor`, right-aligned to it.
    void popup(QWidget* anchor);
    void toggle(QWidget* anchor);

    // what is shown (tests)
    int activeRowCount() const { return int(m_activeRows.size()); }
    int historyRowCount() const { return int(m_historyRows.size()); }
    QWidget* activeRow(const QString& jobId) const { return m_activeRows.value(jobId); }
    QWidget* historyRow(int index) const { return m_historyRows.value(index); }
    QPushButton* clearButton() const { return m_clear; }
    /// The panel body (the rounded surface), for screenshots without the shadow margin.
    QWidget* body() const { return m_panel; }

   signals:
    /// A finished row was clicked. `kind` tells which page of the instance to open. The panel closes right after.
    void openInstanceRequested(const QString& instanceId, Zero::Downloads::Kind kind);
    void opened();
    void closed();

   protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

   private:
    void rebuild();
    void updateRow(const QString& jobId);
    void place();
    void layoutPanel(int riseOffset);
    int contentHeight() const;
    QString signature() const;

    QPointer<Downloads::DownloadCenter> m_center;
    QWidget* m_shadow;
    QWidget* m_panel;
    QScrollArea* m_scroll;
    QWidget* m_content = nullptr;
    QPushButton* m_clear;
    QVariantAnimation* m_open;
    QPointer<QWidget> m_anchor;
    std::function<bool(const QString&)> m_exists;
    QHash<QString, QWidget*> m_activeRows;
    QList<QWidget*> m_historyRows;
    QString m_signature;
};

}  // namespace Zero
