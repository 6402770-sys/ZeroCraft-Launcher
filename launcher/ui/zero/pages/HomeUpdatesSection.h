// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the "Updates" section of Home (docs/zero/SHELL.md, "Home > Updates"): the instances with mod updates the last check
// found (Zero::Updates::UpdateSweep), one row each with an "Update" button, plus "Check again" and "Update all". Hidden while there is
// nothing to show: no updates and no check running. While a check runs, a line with a spinner says which instance is being checked.
#pragma once

#include <QIcon>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <functional>

#include "zero/updates/UpdateSweep.h"

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace Zero {

class ZeroCard;
class ZeroSpinner;

class HomeUpdatesSection : public QWidget {
    Q_OBJECT
   public:
    /// `icons` gives the icon of an instance (default: the application's icon list, the same one Home's other rows use).
    using IconProvider = std::function<QIcon(const QString& instanceId)>;
    /// `sweep` null = the application's (APPLICATION->zeroUpdates()); nothing is shown without one.
    explicit HomeUpdatesSection(Updates::UpdateSweep* sweep = nullptr, IconProvider icons = {}, QWidget* parent = nullptr);

    /// Reads the engine again now (what its signals do, coalesced); also refreshes "Checked 2 hours ago".
    void refresh();

    // ---- for tests ----
    int rowCount() const { return int(m_rowIds.size()); }
    QStringList rowIds() const { return m_rowIds; }
    QString rowName(int row) const;
    QString rowCaption(int row) const;
    /// "Update" (the button), or what replaces it: "Checking…", "Queued", "Downloading…".
    QString rowStatus(int row) const;
    QPushButton* rowButton(int row) const;
    /// Clicks the row itself (not its button).
    void clickRow(int row);
    QString checkingText() const;  ///< "" while no check line is shown
    QString checkedText() const;   ///< "" while the caption under the card is hidden
    QPushButton* updateAllButton() const { return m_updateAll; }
    QPushButton* checkAgainButton() const { return m_checkAgain; }

   signals:
    /// A row was clicked (not its button): open the instance window on its mods page.
    void openInstanceRequested(const QString& instanceId);

   private:
    void rebuild();
    void schedule();
    Updates::UpdateSweep* engine() const;

    QPointer<Updates::UpdateSweep> m_sweep;
    IconProvider m_icons;
    QTimer m_timer;  // coalesces the engine's bursts of signals

    QLabel* m_title;
    QPushButton* m_checkAgain;
    QPushButton* m_updateAll;
    QWidget* m_checkingLine;
    ZeroSpinner* m_checkingSpinner;
    QLabel* m_checkingLabel;
    ZeroCard* m_card;
    QVBoxLayout* m_rows;
    QLabel* m_checked;

    QStringList m_rowIds;
    QStringList m_rowNames;
    QStringList m_rowCaptions;
    QStringList m_rowStatuses;
    QList<QPushButton*> m_rowButtons;
    QList<QWidget*> m_rowWidgets;
    bool m_shown = false;
};

}  // namespace Zero
