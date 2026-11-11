// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Crash Detective view (inside the Tools page). Presentation only: the analysis lives in
// launcher/zero/diagnostics/ and runs off the UI thread.
#pragma once

#include <QFutureWatcher>
#include <QWidget>
#include <optional>
#include "zero/diagnostics/DiagnosticsTypes.h"

class QComboBox;
class QLabel;
class QPushButton;
class QStackedLayout;
class QVBoxLayout;

namespace Zero {

class ZeroEmptyState;
class ZeroSpinner;

class CrashDetectiveView : public QWidget {
    Q_OBJECT
   public:
    explicit CrashDetectiveView(QWidget* parent = nullptr);

    /// One-line description of the tool (the Tools page shows it as the top bar subtitle).
    static QString description();
    /// Spinner, instance combo and Analyze. Hidden and owned by this view; the Tools page lends it to the top bar.
    QWidget* controlsWidget() const { return m_controls; }

    /// Rebuilds the instance combo from the instance list, keeping the current selection when it still exists.
    void refreshInstances();
    /// Selects `instanceId` (if it exists) and forgets the previous result.
    void setInstance(const QString& instanceId);
    QString currentInstanceId() const;
    /// Analyzes the current instance. `exitCode` is the game's exit code when known (crash hook).
    void analyze(std::optional<int> exitCode = std::nullopt);
    bool isAnalyzing() const { return m_running; }
    /// True once an analysis result (including "nothing recognised") is on screen.
    bool hasResult() const;

    /// Dev aid for screenshots: expand the evidence of every finding card.
    void expandAllEvidence();

   signals:
    void instanceSettingsRequested(const QString& instanceId);
    /// A Java related finding asks for the Java Doctor.
    void javaDoctorRequested();
    /// Emitted when an analysis has finished and the result is on screen (used by tests / screenshots).
    void analysisFinished();

   private:
    struct Result {
        QString instanceId;
        quint64 token = 0;
        QList<Diagnostics::LogSource> sources;
        QList<Diagnostics::Finding> findings;
    };

    void showResult(const Result& result);
    void showInitial();
    void setRunning(bool running);
    void runAction(Diagnostics::Action action);
    void openBestLog();
    QString crashSourcePath() const;
    QString describeSources(const QList<Diagnostics::LogSource>& sources) const;

    QWidget* m_controls;
    QComboBox* m_instances;
    QPushButton* m_analyze;
    ZeroSpinner* m_spinner;
    QLabel* m_caption;
    QStackedLayout* m_stack;  // 0 initial, 1 findings, 2 nothing recognised, 3 no logs
    QWidget* m_findingsHost;
    QVBoxLayout* m_findingsLayout;
    ZeroEmptyState* m_initial;
    ZeroEmptyState* m_nothing;
    ZeroEmptyState* m_noLogs;

    QFutureWatcher<Result> m_watcher;
    quint64 m_token = 0;
    bool m_running = false;
    QList<Diagnostics::LogSource> m_sources;
};

}  // namespace Zero
