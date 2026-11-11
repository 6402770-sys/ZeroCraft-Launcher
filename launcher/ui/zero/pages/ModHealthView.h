// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Tools > Mod Health: shows what the Mod Intelligence core (zero/mods) found in the mods of one instance. The analysis is
// read-only. The only change the page can make is the repair action: after a preview dialog it renames a mod jar to `.jar.disabled`
// (and back); it never deletes or overwrites a file.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>
#include "zero/mods/ModHealth.h"

class QComboBox;
class QPushButton;
class QScrollArea;
class QStackedLayout;

namespace Zero {

class ZeroEmptyState;

namespace Mods {
class ModHealthScanner;
}

class ModHealthView : public QWidget {
    Q_OBJECT
   public:
    explicit ModHealthView(QWidget* parent = nullptr);

    /// One-line description shown as the page subtitle while the tool is open.
    static QString description();

    /// Top bar widget (the "Check again" button); owned by the view.
    QWidget* controlsWidget() const { return m_controls; }
    /// Called whenever the tool is opened: selects `instanceId` (when it is a known instance) and checks its mods.
    void open(const QString& instanceId);
    QString currentInstanceId() const;
    bool isScanning() const { return m_scanning; }
    bool hasReport() const { return m_hasReport; }
    Mods::ModHealthScanner* scanner() const { return m_scanner; }

   signals:
    /// The first / a new result (or an empty state) is on screen.
    void reportShown();

   private:
    void refreshInstances(const QString& preferred);
    void scan();
    void onReport(const QString& instanceId, const Mods::Report& report);
    void rebuild();
    void updateControls();
    void openModsFolder();
    bool instanceBusy(const QString& instanceId) const;
    /// Preview dialog, then rename the files of `actions` to `.disabled` (toast with Undo, then a new analysis).
    void askDisable(const QList<Mods::RepairAction>& actions);
    /// Renames `disabledNames` back (Undo / the "Enable" button), toast, new analysis.
    void restore(const QString& instanceId, const QString& modsDir, const QStringList& disabledNames);

    QWidget* m_controls;
    QPushButton* m_recheck;
    QComboBox* m_instances;
    QStackedLayout* m_stack;
    QScrollArea* m_scroll;
    QWidget* m_host = nullptr;
    QWidget* m_loading;
    ZeroEmptyState* m_noLoader;
    ZeroEmptyState* m_noMods;
    ZeroEmptyState* m_noInstances;
    Mods::ModHealthScanner* m_scanner;

    Mods::InstanceContext m_context;
    Mods::Report m_report;
    QString m_modsDir;  ///< mods folder of the instance of `m_report`
    struct DisabledFile {
        QString name;
        qint64 size = 0;
    };
    QList<DisabledFile> m_disabled;  ///< `*.disabled` files directly in `m_modsDir`, by name
    bool m_scanning = false;
    bool m_hasReport = false;
};

}  // namespace Zero
