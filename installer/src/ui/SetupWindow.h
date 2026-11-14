// SPDX-License-Identifier: GPL-3.0-only
// The frameless ZERO Setup window: Welcome, Options, Installing, Done, Error and the uninstaller screens.
#pragma once

#include "CmdLine.h"
#include "Installer.h"
#include "Payload.h"

#include <QImage>
#include <QWidget>

class QLineEdit;
class QLabel;

namespace ZeroSetup {

class SetupCanvas;
class PixelButton;
class PixelCheck;
class PixelLink;
class PromptOverlay;
class XpBar;
class TitleButton;
class Worker;
class Page;

struct SetupConfig
{
    CmdLine cmd;
    bool uninstallMode = false;
    PayloadReader* payload = nullptr;  // null for the uninstaller and for screenshots
    QString exePath;
    QString version;           // the version this setup installs
    ExistingInstall existing;  // what is installed at targetDir right now
    QString targetDir;         // proposed install folder (or the folder being uninstalled)
    QString startupError;      // non-empty: open on the Error screen with this sentence
    QString startupErrorState; // "what state is the computer in" line for it
};

enum class Screen { Welcome, Options, Installing, Done, Error, UninstallConfirm, Uninstalling, UninstallDone };

class SetupWindow : public QWidget
{
    Q_OBJECT
   public:
    explicit SetupWindow(const SetupConfig& cfg, QWidget* parent = nullptr);
    ~SetupWindow() override;

    int exitCode() const { return m_exitCode; }

    // ---- screenshot support: render any screen at a given state without side effects
    /// "welcome", "options", "update", "update-options", "installing:45", "done", "error", "running", "stop", "uninstall",
    /// "uninstalling:60", "uninstall-done", "drop:40" (the mark 40 % built). Returns false for an unknown name.
    bool presentForScreenshot(const QString& name);
    QImage renderImage();

   protected:
    void keyPressEvent(QKeyEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void showEvent(QShowEvent*) override;

   private:
    void buildPages();
    void showScreen(Screen s);
    void setTaglineFor(Screen s);
    void slideOptions(bool in);
    void applySlide(double p);
    void refreshWelcomeLayout();
    void refreshUpdateState();
    QString currentTarget() const;
    bool isUpdate() const;

    void beginInstall();
    void beginUninstall();
    void startWorker(bool uninstall);
    void onProgress(const QString& step, double fraction);
    void onWorkerFinished();
    void showError(const QString& message, const QString& state, bool canRetry = true);
    void launchAndClose();
    void requestClose();
    void promptRunning(std::function<void()> retry);
    PixelButton* primaryFor(Screen s) const;

    SetupConfig m_cfg;
    Screen m_screen = Screen::Welcome;
    SetupCanvas* m_canvas = nullptr;
    PromptOverlay* m_prompt = nullptr;
    TitleButton* m_minBtn = nullptr;
    TitleButton* m_closeBtn = nullptr;

    Page* m_welcome = nullptr;
    Page* m_options = nullptr;
    Page* m_progress = nullptr;
    Page* m_done = nullptr;
    Page* m_error = nullptr;
    Page* m_confirm = nullptr;

    PixelButton* m_installBtn = nullptr;
    PixelLink* m_optionsLink = nullptr;
    QLineEdit* m_folderEdit = nullptr;
    PixelButton* m_browseBtn = nullptr;
    PixelCheck* m_desktopCheck = nullptr;
    PixelCheck* m_startMenuCheck = nullptr;
    PixelCheck* m_openCheck = nullptr;
    PixelButton* m_backBtn = nullptr;
    PixelButton* m_optInstallBtn = nullptr;
    XpBar* m_bar = nullptr;
    PixelButton* m_cancelBtn = nullptr;
    PixelButton* m_launchBtn = nullptr;
    PixelButton* m_doneCloseBtn = nullptr;
    PixelButton* m_retryBtn = nullptr;
    PixelButton* m_errCloseBtn = nullptr;
    PixelCheck* m_deleteDataCheck = nullptr;
    PixelButton* m_removeBtn = nullptr;
    PixelButton* m_keepBtn = nullptr;

    QString m_stepText;
    int m_percent = 0;
    QString m_errorMessage, m_errorState;
    QString m_doneNote;
    QString m_pathText;
    qint64 m_slideStart = 0;
    bool m_slideIn = true;
    bool m_sliding = false;
    class QTimer* m_slideTimer = nullptr;

    Worker* m_worker = nullptr;
    bool m_workerUninstall = false;
    bool m_closeWhenDone = false;
    bool m_retryIsUninstall = false;
    int m_exitCode = 0;
};

}  // namespace ZeroSetup
