// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the "Before you play" dialog (docs/zero/ARCHITECTURE.md, "Before you play"): what the quick look at the instance found, shown
// when the user presses Play. The logic (items, fixes, the Play decision) is zero/prelaunch/PreLaunchCheck's Session; this is its view.
//
// Two states. "Checking": a spinner and "Checking 140 mods for known malware...", the full scan runs off the UI thread, Play is disabled.
// "Results": MUST FIX (Block rows) and WORTH A LOOK (Warn rows), one row per item with its fix buttons; a fix runs through the Session and the
// rows are rebuilt from it. Footer: "Don't check this instance again", Cancel, Play (primary only while no Block row is open; "Play anyway"
// with a Block row open asks a second question first).
//
// `checkBeforeLaunch` is the whole flow for Application::launch: instant when the cache says everything is fine (no dialog at all).
#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

#include "ui/zero/instance/ModsDialogs.h"
#include "zero/prelaunch/PreLaunchCheck.h"

class MinecraftInstance;
class QCheckBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace Zero {
class ZeroSpinner;
}

namespace Zero::PreLaunchUi {

using ScanFunction = std::function<PreLaunch::CheckResult(const PreLaunch::Env&, const PreLaunch::RunOptions&)>;

/// Everything the dialog needs from the application, so tests build it with temp folders and fakes.
struct PreLaunchEnvironment {
    PreLaunch::Env env;
    PreLaunch::Hooks hooks;
    QString instanceName;
    PreLaunch::Session::Rerun rerun;  ///< null = PreLaunch::runCategory(env, category)
    ScanFunction scan;                ///< null = PreLaunch::runChecks; runs off the UI thread in the "checking" state
    ModsUi::AskFunction ask;          ///< the second question of "Play anyway" with malware; null = a ChoiceDialog
};

class PreLaunchDialog : public QDialog {
    Q_OBJECT
   public:
    static constexpr int kWidth = 560;
    static constexpr int kMaxBodyHeight = 520;  ///< px of rows before the body scrolls

    enum class State { Empty, Checking, Results };

    explicit PreLaunchDialog(PreLaunchEnvironment env, QWidget* parent = nullptr);
    ~PreLaunchDialog() override;

    /// Shows `items` (the results state).
    void showResults(const QList<PreLaunch::Item>& items);
    /// Runs the full scan off the UI thread (the checking state). `modCount` < 0 = counts the jars of the mods folder. When it finishes without
    /// items the dialog accepts itself (Decision::Play), else it shows the results. A scan that finishes after the dialog was closed is ignored.
    void startChecking(int modCount = -1);

    /// What the user decided (valid after exec() / once the dialog closed); Cancel until a decision is made.
    PreLaunch::Decision decision() const { return m_decision; }

    // ---- state (tests, dev aids) ----
    State state() const { return m_state; }
    QString titleText() const;
    QString summaryText() const;       ///< "" while checking
    QString checkingText() const;      ///< "" outside the checking state
    QString progressText() const;      ///< "12 of 140" ("" = none yet)
    QStringList sectionTitles() const; ///< "Must fix", "Worth a look" (the ones with rows)
    QStringList rowIds() const;        ///< in display order
    QString rowTitle(const QString& id) const;
    QString rowDetail(const QString& id) const;
    QString rowSection(const QString& id) const;
    bool rowFixed(const QString& id) const;
    QString rowFixedText(const QString& id) const;  ///< "" when not fixed
    QString rowError(const QString& id) const;      ///< "" when none
    QString rowIconName(const QString& id) const;
    QList<QPushButton*> rowButtons(const QString& id) const;  ///< the fix buttons, in order (empty once fixed)
    QPushButton* playButton() const { return m_play; }
    QPushButton* cancelButton() const { return m_cancel; }
    QCheckBox* optOutBox() const { return m_optOut; }
    const PreLaunch::Session* session() const { return m_session.get(); }

   protected:
    void done(int result) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

   private:
    struct RowUi {
        QString id;
        QString section;
        QString title;
        QString detail;
        QString iconName;
        bool fixed = false;
        QString fixedText;
        QString error;
        QWidget* widget = nullptr;
        QList<QPushButton*> buttons;
    };

    void buildUi();
    void setState(State state);
    void rebuildRows();
    void buildRow(const PreLaunch::Item& item, QWidget* parent, QVBoxLayout* into, const QString& section);
    void refreshChrome();
    void fitBody();
    void onFix(const QString& itemId, int fixIndex);
    void onPlay();
    bool confirmPlayAnyway();
    void onScanFinished();
    void finish(PreLaunch::Decision decision);
    const RowUi* rowUi(const QString& id) const;

    PreLaunchEnvironment m_env;
    State m_state = State::Empty;
    PreLaunch::Decision m_decision = PreLaunch::Decision::Cancel;
    bool m_decided = false;
    std::unique_ptr<PreLaunch::Session> m_session;
    std::vector<RowUi> m_rows;
    QStringList m_sectionTitles;

    QFutureWatcher<PreLaunch::CheckResult>* m_watcher = nullptr;
    int m_modCount = 0;
    int m_progressTotal = 0;

    QLabel* m_title = nullptr;
    QLabel* m_summary = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QWidget* m_checkingBox = nullptr;
    Zero::ZeroSpinner* m_spinner = nullptr;
    QLabel* m_checkingLabel = nullptr;
    QLabel* m_progressLabel = nullptr;
    QWidget* m_resultsHost = nullptr;
    QVBoxLayout* m_resultsLayout = nullptr;
    QCheckBox* m_optOut = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_play = nullptr;
};

/// The whole flow for one Play press, without Application (tests): the quick cached look; nothing to show = Play at once, no dialog; else the
/// dialog (results at once, or "checking" first). Returns the decision.
PreLaunch::Decision runBeforeYouPlay(PreLaunchEnvironment env, QWidget* parent);

/// For Application::launch: false = do not launch (the user cancelled, or chose to open the Mods page, which is opened here). True = go on.
/// Instant when the setting `ZeroPreLaunchCheck` is off or there is nothing to show.
bool checkBeforeLaunch(MinecraftInstance* instance, QWidget* parent);

/// The production environment of `instance` (settings, pack rollback hooks, the per-instance opt-out).
PreLaunchEnvironment environmentFor(MinecraftInstance* instance);

}  // namespace Zero::PreLaunchUi
