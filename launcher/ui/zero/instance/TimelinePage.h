// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Timeline page of the instance window: what changed in this instance (mod updates, modpack updates, changes brought
// back from a test copy, resource pack order, installs, play sessions), newest first in day groups, with "Undo" on the newest change of
// each kind that already has an undo (zero/history/ChangeTimeline decides which). Read-only: Undo calls the flow of the page that owns
// the change (Mods, Resource Packs, the main window for a modpack), with its own question and toasts.
#pragma once

#include <QDateTime>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>

#include "ui/pages/BasePage.h"
#include "zero/history/ChangeTimeline.h"

class MinecraftInstance;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedLayout;
class QVBoxLayout;

namespace Zero {

class ZeroEmptyState;

namespace ModsUi {
class ModsPage;
}
namespace PacksUi {
class ResourcePacksPage;
}

namespace TimelineUi {

/// What the user asked for on the Timeline that a window outside the instance window answers (MainWindow connects once).
class Requests : public QObject {
    Q_OBJECT
   public:
    static Requests* instance();
   signals:
    void undoModpackRequested(const QString& instanceId);  ///< roll back the last modpack update / "bring changes back" (asks first)
   private:
    Requests() = default;
};

/// What the page needs besides the widgets (tests inject fakes; the application page fills them from the instance).
struct TimelineEnvironment {
    std::function<History::Timeline::Sources()> read;       ///< reads the histories (synchronous)
    std::function<QString()> busyReason;                    ///< non-empty: Undo is disabled and this is its tooltip
    std::function<void(const History::Timeline::Entry&)> undo;  ///< runs the existing flow of that kind (question, work, toasts)
    std::function<QDateTime()> now;                         ///< default: the clock
};

class TimelinePage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    /// The application page for `inst`. `mods` and `packs` are the sibling pages whose Undo flows are reused (either may be null).
    TimelinePage(MinecraftInstance* inst, ModsUi::ModsPage* mods, PacksUi::ResourcePacksPage* packs, QWidget* parent = nullptr);
    /// With an explicit environment (tests).
    explicit TimelinePage(const TimelineEnvironment& env, QWidget* parent = nullptr);
    ~TimelinePage() override;

    QString id() const override { return QStringLiteral("timeline"); }
    QString displayName() const override { return tr("Timeline"); }
    QIcon icon() const override;
    QString helpPage() const override { return QString(); }
    void openedImpl() override;

    /// Reads the sources and rebuilds the list now.
    void refresh();

    bool isEmptyStateShown() const;
    int rowCount() const;                 ///< rows drawn
    int totalCount() const { return m_total; }  ///< entries there are (rows drawn <= this)
    QStringList groupLabels() const;
    QStringList rowTitles() const;
    QStringList rowCaptions() const;
    QList<QPushButton*> undoButtons() const;  ///< one per undoable row, top first
    QPushButton* showMoreButton() const { return m_showMore; }

   protected:
    void showEvent(QShowEvent* event) override;

   private:
    void build();
    void rebuild();
    void onUndo(const History::Timeline::Entry& entry);
    void scheduleRefresh();

    TimelineEnvironment m_env;
    QStackedLayout* m_stack = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_list = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    ZeroEmptyState* m_empty = nullptr;
    QPushButton* m_showMore = nullptr;
    int m_limit = History::Timeline::kShownFirst;
    int m_total = 0;
    bool m_dirty = false;
    QTimer m_refreshTimer;
};

}  // namespace TimelineUi
}  // namespace Zero
