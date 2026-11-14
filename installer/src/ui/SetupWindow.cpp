// SPDX-License-Identifier: GPL-3.0-only
#include "SetupWindow.h"

#include "Brand.h"
#include "Footprint.h"
#include "Jobs.h"
#include "MemorySystem.h"
#include "Motion.h"
#include "PixelWidgets.h"
#include "SetupCanvas.h"
#include "SetupTokens.h"
#include "Uninstaller.h"
#include "Upgrade.h"
#include "Worker.h"
#include "ZeroPixelFont.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QTimer>
#include <QWindow>

namespace ZeroSetup {

namespace T = Tok;

/// A transparent full-window page that paints its static text through a callback.
class Page : public QWidget
{
   public:
    explicit Page(QWidget* parent) : QWidget(parent)
    {
        setGeometry(0, 0, T::winW, T::winH);
        hide();
    }
    std::function<void(QPainter&)> paint;

   protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!paint)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        paint(p);
    }
};

namespace {

void place(QWidget* w, int bodyX, int bodyY)
{
    w->move(bodyX - T::buttonMargin, bodyY - T::buttonMargin);
}

QString nativePath(const QString& p)
{
    return QDir::toNativeSeparators(p);
}

}  // namespace

SetupWindow::SetupWindow(const SetupConfig& cfg, QWidget* parent) : QWidget(parent), m_cfg(cfg)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint);
    setWindowTitle(cfg.uninstallMode ? QStringLiteral("Uninstall ZERO Launcher") : QStringLiteral("ZERO Launcher Setup"));
    setFixedSize(T::winW, T::winH);
    setFocusPolicy(Qt::StrongFocus);

    m_canvas = new SetupCanvas(this);
    m_canvas->move(0, 0);
    connect(m_canvas, &SetupCanvas::dragRequested, this, [this] {
        if (auto* h = windowHandle())
            h->startSystemMove();
    });
    m_minBtn = new TitleButton(TitleGlyph::Minimize, m_canvas);
    m_closeBtn = new TitleButton(TitleGlyph::Close, m_canvas);
    m_closeBtn->move(T::winW - T::titleButtonW - 4, 3);
    m_minBtn->move(T::winW - 2 * T::titleButtonW - 4, 3);
    connect(m_minBtn, &QAbstractButton::clicked, this, &QWidget::showMinimized);
    connect(m_closeBtn, &QAbstractButton::clicked, this, &SetupWindow::requestClose);

    m_slideTimer = new QTimer(this);
    m_slideTimer->setInterval(T::frameMs);
    connect(m_slideTimer, &QTimer::timeout, this, [this] {
        const double p = Motion::progress(m_slideStart, T::slideMs, QEasingCurve::OutCubic);
        applySlide(m_slideIn ? p : 1.0 - p);
        if (p >= 1.0) {
            m_slideTimer->stop();
            m_sliding = false;
            showScreen(m_slideIn ? Screen::Options : Screen::Welcome);
        }
    });

    buildPages();
    m_prompt = new PromptOverlay(this);

    m_pathText = nativePath(cfg.targetDir);
    if (!cfg.startupError.isEmpty()) {
        showError(cfg.startupError, cfg.startupErrorState, false);
    } else {
        showScreen(cfg.uninstallMode ? Screen::UninstallConfirm : Screen::Welcome);
    }
}

SetupWindow::~SetupWindow()
{
    if (m_worker) {
        m_worker->requestCancel();
        m_worker->wait();
    }
}

bool SetupWindow::isUpdate() const
{
    return m_cfg.existing.kind != InstallKind::None &&
           QDir::cleanPath(currentTarget()).compare(QDir::cleanPath(m_cfg.existing.dir), Qt::CaseInsensitive) == 0;
}

QString SetupWindow::currentTarget() const
{
    return m_folderEdit ? QDir::fromNativeSeparators(m_folderEdit->text().trimmed()) : m_cfg.targetDir;
}

void SetupWindow::buildPages()
{
    const int W = T::winW;

    // ---------------------------------------------------------------- Welcome
    m_welcome = new Page(m_canvas);
    m_installBtn = new PixelButton(QStringLiteral("Install"), ButtonKind::Primary, QSize(T::buttonW, T::buttonH), m_welcome);
    place(m_installBtn, (W - T::buttonW) / 2, T::primaryTop);
    m_optionsLink = new PixelLink(QStringLiteral("Options"), m_welcome);
    m_welcome->paint = [this](QPainter& p) {
        p.setFont(uiFont(T::pxCaption));
        p.setPen(T::c(T::textTertiary));
        const QFontMetrics fm(p.font());
        const QString elided = fm.elidedText(m_pathText, Qt::ElideMiddle, 500);
        const int pathW = fm.horizontalAdvance(elided);
        const int sepW = fm.horizontalAdvance(QStringLiteral("  \u00B7  "));
        const int total = pathW + sepW + m_optionsLink->width() - 10;
        const int x = (T::winW - total) / 2;
        const QRect row(x, T::primaryTop + T::buttonH + 22, pathW + sepW, 24);
        p.drawText(row, Qt::AlignVCenter | Qt::AlignLeft, elided + QStringLiteral("  \u00B7  "));
    };
    connect(m_installBtn, &QAbstractButton::clicked, this, &SetupWindow::beginInstall);
    connect(m_optionsLink, &QAbstractButton::clicked, this, [this] { slideOptions(true); });

    // ---------------------------------------------------------------- Options
    m_options = new Page(m_canvas);
    const QRect panel(T::panelX, 64, T::panelW, 334);
    const int px = panel.left() + 24;
    m_options->paint = [panel, px](QPainter& p) {
        paintBlock(p, QRectF(panel), T::c(T::surface), false, true);
        QFont f(Zero::pixelFamily());
        f.setPixelSize(20);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 3);
        f.setStyleStrategy(QFont::NoAntialias);
        p.setFont(f);
        p.setPen(T::c(T::textSecondary));
        p.drawText(QRect(px, panel.top() + 16, 300, 30), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("OPTIONS"));
        p.setFont(uiFont(T::pxCaption, QFont::DemiBold));
        p.setPen(T::c(T::textSecondary));
        p.drawText(QRect(px, panel.top() + 58, 300, 20), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Install folder"));
    };
    m_folderEdit = new QLineEdit(m_options);
    m_folderEdit->setStyleSheet(lineEditStyle());
    m_folderEdit->setFont(uiFont(T::pxCaption + 1));
    m_folderEdit->setGeometry(px, panel.top() + 82, 336, 34);
    m_folderEdit->setText(nativePath(m_cfg.targetDir));
    m_folderEdit->setCursorPosition(0);
    m_browseBtn = new PixelButton(QStringLiteral("Browse"), ButtonKind::Secondary, QSize(100, 34), m_options);
    place(m_browseBtn, px + 346, panel.top() + 82);
    m_desktopCheck = new PixelCheck(QStringLiteral("Desktop shortcut"), m_options);
    m_startMenuCheck = new PixelCheck(QStringLiteral("Start menu shortcut"), m_options);
    m_openCheck = new PixelCheck(QStringLiteral("Open ZERO when done"), m_options);
    int cy = panel.top() + 134;
    for (PixelCheck* c : { m_desktopCheck, m_startMenuCheck, m_openCheck }) {
        c->setChecked(true);
        c->setGeometry(px - 8, cy, 300, 30);
        cy += 36;
    }
    m_desktopCheck->setChecked(!m_cfg.cmd.noShortcuts);
    m_startMenuCheck->setChecked(!m_cfg.cmd.noShortcuts);
    m_backBtn = new PixelButton(QStringLiteral("Back"), ButtonKind::Secondary, QSize(110, 40), m_options);
    place(m_backBtn, px, panel.bottom() - 24 - 40);
    m_optInstallBtn = new PixelButton(QStringLiteral("Install"), ButtonKind::Primary, QSize(150, 40), m_options);
    place(m_optInstallBtn, panel.right() - 24 - 150, panel.bottom() - 24 - 40);
    connect(m_backBtn, &QAbstractButton::clicked, this, [this] { slideOptions(false); });
    connect(m_optInstallBtn, &QAbstractButton::clicked, this, &SetupWindow::beginInstall);
    connect(m_browseBtn, &QAbstractButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose the install folder"), currentTarget());
        if (!d.isEmpty()) {
            // a folder that is not empty and not ours gets a ZEROLauncher sub-folder, so nothing foreign is ever mixed in
            QString chosen = QDir::cleanPath(d);
            const QDir dir(chosen);
            MemorySystem probe;  // file-based detection only: no registry is read here
            if (!dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden) && detectExisting(probe, chosen).kind == InstallKind::None)
                chosen += QStringLiteral("/") + Brand::commonName();
            m_folderEdit->setText(nativePath(chosen));
        }
    });
    connect(m_folderEdit, &QLineEdit::textChanged, this, [this] {
        m_pathText = nativePath(currentTarget());
        refreshUpdateState();
        m_welcome->update();
    });
    setTabOrder(m_folderEdit, m_browseBtn);
    setTabOrder(m_browseBtn, m_desktopCheck);
    setTabOrder(m_desktopCheck, m_startMenuCheck);
    setTabOrder(m_startMenuCheck, m_openCheck);
    setTabOrder(m_openCheck, m_backBtn);
    setTabOrder(m_backBtn, m_optInstallBtn);

    // ---------------------------------------------------------------- Installing / Uninstalling
    m_progress = new Page(m_canvas);
    m_bar = new XpBar(m_progress);
    m_bar->move((W - T::barW) / 2, 290);
    m_cancelBtn = new PixelButton(QStringLiteral("Cancel"), ButtonKind::Secondary, QSize(T::smallButtonW, T::smallButtonH), m_progress);
    place(m_cancelBtn, (W - T::smallButtonW) / 2, 366);
    m_progress->paint = [this](QPainter& p) {
        const int x = (T::winW - T::barW) / 2;
        p.setFont(uiFont(T::pxBody));
        p.setPen(T::c(T::textSecondary));
        p.drawText(QRect(x, 290 + T::barH + 10, 320, 22), Qt::AlignLeft | Qt::AlignVCenter, m_stepText);
        p.drawText(QRect(x + T::barW - 100, 290 + T::barH + 10, 100, 22), Qt::AlignRight | Qt::AlignVCenter, QString::number(m_percent) + QStringLiteral(" %"));
    };
    connect(m_cancelBtn, &QAbstractButton::clicked, this, [this] {
        if (m_worker) {
            m_cancelBtn->setEnabled(false);
            m_worker->requestCancel();
        }
    });

    // ---------------------------------------------------------------- Done
    m_done = new Page(m_canvas);
    m_launchBtn = new PixelButton(QStringLiteral("Launch ZERO"), ButtonKind::Primary, QSize(T::buttonW, T::buttonH), m_done);
    place(m_launchBtn, (W - T::buttonW) / 2, T::primaryTop);
    m_doneCloseBtn = new PixelButton(QStringLiteral("Close"), ButtonKind::Secondary, QSize(T::smallButtonW, T::smallButtonH), m_done);
    m_done->paint = [this](QPainter& p) {
        if (m_doneNote.isEmpty())
            return;
        p.setFont(uiFont(T::pxCaption));
        p.setPen(T::c(T::textTertiary));
        p.drawText(QRect(130, 396 - 22, 500, 18), Qt::AlignHCenter | Qt::AlignVCenter, m_doneNote);
    };
    connect(m_launchBtn, &QAbstractButton::clicked, this, &SetupWindow::launchAndClose);
    connect(m_doneCloseBtn, &QAbstractButton::clicked, this, [this] { close(); });

    // ---------------------------------------------------------------- Error
    m_error = new Page(m_canvas);
    m_retryBtn = new PixelButton(QStringLiteral("Retry"), ButtonKind::Primary, QSize(150, 44), m_error);
    place(m_retryBtn, W / 2 - 8 - 150 + 4, 350);
    m_errCloseBtn = new PixelButton(QStringLiteral("Close"), ButtonKind::Secondary, QSize(150, 44), m_error);
    place(m_errCloseBtn, W / 2 + 8 + 4, 350);
    m_error->paint = [this](QPainter& p) {
        p.setFont(uiFont(T::pxTagline));
        p.setPen(T::c(T::text));
        p.drawText(QRect(130, 226, 500, 68), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_errorMessage);
        p.setFont(uiFont(T::pxBody));
        p.setPen(T::c(T::textSecondary));
        p.drawText(QRect(130, 300, 500, 40), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_errorState);
    };
    connect(m_retryBtn, &QAbstractButton::clicked, this, [this] {
        if (m_retryIsUninstall)
            beginUninstall();
        else
            beginInstall();
    });
    connect(m_errCloseBtn, &QAbstractButton::clicked, this, [this] {
        m_exitCode = 1;
        close();
    });

    // ---------------------------------------------------------------- Uninstall confirmation
    m_confirm = new Page(m_canvas);
    m_deleteDataCheck = new PixelCheck(QStringLiteral("Also move my worlds and settings to the Recycle Bin"), m_confirm);  // OFF by default
    m_deleteDataCheck->setChecked(m_cfg.cmd.deleteUserData);
    m_deleteDataCheck->setGeometry((W - m_deleteDataCheck->sizeHint().width()) / 2, 262, m_deleteDataCheck->sizeHint().width(), 30);
    m_removeBtn = new PixelButton(QStringLiteral("Remove"), ButtonKind::Danger, QSize(160, 44), m_confirm);
    place(m_removeBtn, W / 2 - 8 - 160, 322);
    m_keepBtn = new PixelButton(QStringLiteral("Cancel"), ButtonKind::Secondary, QSize(160, 44), m_confirm);
    place(m_keepBtn, W / 2 + 8, 322);
    connect(m_removeBtn, &QAbstractButton::clicked, this, &SetupWindow::beginUninstall);
    connect(m_keepBtn, &QAbstractButton::clicked, this, [this] { close(); });
    m_confirm->paint = [this](QPainter& p) {
        if (!m_deleteDataCheck->isChecked())
            return;
        p.setFont(uiFont(T::pxCaption));
        p.setPen(T::c(T::textTertiary));
        p.drawText(QRect(130, 296, 500, 18), Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("Nothing is deleted for good: you can restore it from the Recycle Bin."));
    };
    connect(m_deleteDataCheck, &QAbstractButton::toggled, m_confirm, [this] { m_confirm->update(); });
}

void SetupWindow::refreshUpdateState()
{
    const bool up = isUpdate();
    const QString label = up ? QStringLiteral("Update") : QStringLiteral("Install");
    m_installBtn->setText(label);
    m_optInstallBtn->setText(label);
    if (m_screen == Screen::Welcome || m_screen == Screen::Options)
        setTaglineFor(m_screen);
}

void SetupWindow::setTaglineFor(Screen s)
{
    QString t;
    switch (s) {
        case Screen::Welcome:
        case Screen::Options:
            if (isUpdate()) {
                const QString old = m_cfg.existing.version.isEmpty() ? QStringLiteral("ZERO") : QStringLiteral("ZERO ") + m_cfg.existing.version;
                t = QStringLiteral("%1 is installed. Update to %2.").arg(old, m_cfg.version);
            } else {
                t = QStringLiteral("Your worlds, ready in one click.");
            }
            break;
        case Screen::Installing: t = isUpdate() ? QStringLiteral("Updating ZERO.") : QStringLiteral("Getting ZERO ready."); break;
        case Screen::Done: t = QStringLiteral("Ready to play."); break;
        case Screen::UninstallConfirm: t = QStringLiteral("Remove ZERO Launcher?"); break;
        case Screen::Uninstalling: t = QStringLiteral("Removing ZERO."); break;
        case Screen::UninstallDone: t = QStringLiteral("ZERO Launcher was removed."); break;
        case Screen::Error: t = QString(); break;
    }
    m_canvas->setTagline(t);
}

PixelButton* SetupWindow::primaryFor(Screen s) const
{
    switch (s) {
        case Screen::Welcome: return m_installBtn;
        case Screen::Options: return m_optInstallBtn;
        case Screen::Installing:
        case Screen::Uninstalling: return m_cancelBtn;
        case Screen::Done: return m_launchBtn->isVisible() ? m_launchBtn : m_doneCloseBtn;
        case Screen::UninstallDone: return m_doneCloseBtn;
        case Screen::Error: return m_retryBtn->isVisible() ? m_retryBtn : m_errCloseBtn;
        case Screen::UninstallConfirm: return m_removeBtn;
    }
    return nullptr;
}

void SetupWindow::showScreen(Screen s)
{
    m_screen = s;
    for (Page* pg : { m_welcome, m_options, m_progress, m_done, m_error, m_confirm })
        pg->hide();
    const bool un = m_cfg.uninstallMode;
    m_canvas->setWindowCaption(un ? QStringLiteral("ZERO Uninstall") : QStringLiteral("ZERO Setup"));
    m_canvas->setBobbing(s == Screen::Installing || s == Screen::Uninstalling);
    m_canvas->setHeroShift(s == Screen::Options ? 1.0 : 0.0);
    m_canvas->setMarkVisible(true);

    switch (s) {
        case Screen::Welcome:
            m_welcome->move(0, 0);
            m_welcome->show();
            refreshWelcomeLayout();
            break;
        case Screen::Options:
            m_options->move(0, 0);
            m_options->show();
            break;
        case Screen::Installing:
        case Screen::Uninstalling:
            m_progress->show();
            break;
        case Screen::Done:
        case Screen::UninstallDone: {
            const bool launch = s == Screen::Done && m_openCheck->isChecked();
            m_launchBtn->setVisible(launch);
            if (launch) {
                m_doneCloseBtn->setFixedSize(QSize(T::smallButtonW, T::smallButtonH) + QSize(2 * T::buttonMargin, 2 * T::buttonMargin));
                place(m_doneCloseBtn, (T::winW - T::smallButtonW) / 2, T::primaryTop + T::buttonH + 20);
                m_doneCloseBtn->setKind(ButtonKind::Secondary);
            } else {
                m_doneCloseBtn->setKind(ButtonKind::Primary);
                m_doneCloseBtn->setFixedSize(QSize(T::buttonW, T::buttonH) + QSize(2 * T::buttonMargin, 2 * T::buttonMargin));
                place(m_doneCloseBtn, (T::winW - T::buttonW) / 2, T::primaryTop);
            }
            m_done->show();
            break;
        }
        case Screen::Error: m_error->show(); break;
        case Screen::UninstallConfirm: m_confirm->show(); break;
    }
    setTaglineFor(s);
    if (auto* b = primaryFor(s))
        b->setFocus(Qt::ActiveWindowFocusReason);
    update();
}

void SetupWindow::refreshWelcomeLayout()
{
    m_pathText = nativePath(currentTarget());
    QFontMetrics fm(uiFont(T::pxCaption));
    const QString elided = fm.elidedText(m_pathText, Qt::ElideMiddle, 500);
    const int pathW = fm.horizontalAdvance(elided);
    const int sepW = fm.horizontalAdvance(QStringLiteral("  \u00B7  "));
    const int total = pathW + sepW + m_optionsLink->width() - 10;
    const int x = (T::winW - total) / 2;
    m_optionsLink->move(x + pathW + sepW - 5, T::primaryTop + T::buttonH + 22);
    m_welcome->update();
}

void SetupWindow::slideOptions(bool in)
{
    if (m_sliding)
        return;
    if (!Motion::enabled()) {
        showScreen(in ? Screen::Options : Screen::Welcome);
        return;
    }
    m_sliding = true;
    m_slideIn = in;
    m_slideStart = Motion::now();
    m_welcome->show();
    m_options->show();
    m_options->raise();
    m_prompt->raise();
    applySlide(in ? 0.0 : 1.0);
    m_slideTimer->start();
}

void SetupWindow::applySlide(double p)
{
    m_options->move(int((1.0 - p) * T::winW), 0);
    m_welcome->move(int(-p * 220), 0);
    m_welcome->setVisible(p < 0.98);
    m_canvas->setHeroShift(p);
}

// ---------------------------------------------------------------------------------------------- install / uninstall

void SetupWindow::promptRunning(std::function<void()> retry)
{
    m_prompt->ask(QStringLiteral("ZERO is running"), QStringLiteral("Close ZERO to continue. Setup never closes it for you, so your game and your work are safe."),
                  QStringLiteral("Retry"), QStringLiteral("Cancel"), [retry](bool primary) {
                      if (primary)
                          retry();
                  });
}

void SetupWindow::beginInstall()
{
    if (m_worker || m_sliding)
        return;
    const QString target = QDir::cleanPath(currentTarget());
    if (target.isEmpty() || QFileInfo(target).isRelative()) {
        m_retryIsUninstall = false;
        showError(QStringLiteral("That install folder is not valid. Pick a full folder path, like C:/Users/you/ZERO."), QStringLiteral("Nothing was changed."));
        return;
    }
    if (!m_cfg.cmd.demo && !lockedProgramFiles(target).isEmpty()) {
        promptRunning([this] { beginInstall(); });
        return;
    }
    m_pathText = nativePath(target);
    m_retryIsUninstall = false;
    startWorker(false);
}

void SetupWindow::beginUninstall()
{
    if (m_worker)
        return;
    if (!m_cfg.cmd.demo && !lockedProgramFiles(m_cfg.targetDir).isEmpty()) {
        promptRunning([this] { beginUninstall(); });
        return;
    }
    m_retryIsUninstall = true;
    startWorker(true);
}

void SetupWindow::startWorker(bool uninstall)
{
    m_workerUninstall = uninstall;
    m_percent = 0;
    m_stepText = uninstall ? QStringLiteral("Removing shortcuts") : QStringLiteral("Copying files");
    m_bar->setFraction(0);
    m_cancelBtn->setEnabled(!uninstall);
    m_cancelBtn->setVisible(!uninstall);
    showScreen(uninstall ? Screen::Uninstalling : Screen::Installing);

    const CmdLine cmd = m_cfg.cmd;
    Worker::Job job;
    if (uninstall) {
        UninstallJob uj;
        uj.installDir = m_cfg.targetDir;
        uj.deleteUserData = m_deleteDataCheck->isChecked();
        uj.noSystem = cmd.noSystem;
        uj.planFile = cmd.planFile;
        uj.demo = cmd.demo;
        job = [uj](const std::atomic<bool>* c, const JobProgress& p) { return runUninstallJob(uj, c, p); };
    } else {
        InstallJob ij;
        ij.payload = m_cfg.payload;
        ij.exePath = m_cfg.exePath;
        ij.targetDir = currentTarget();
        ij.desktopShortcut = m_desktopCheck->isChecked() && !cmd.noShortcuts;
        ij.startMenuShortcut = m_startMenuCheck->isChecked() && !cmd.noShortcuts;
        ij.shellAssoc = cmd.shellAssoc;
        ij.writeUninstaller = !cmd.noUninstaller;
        ij.noSystem = cmd.noSystem;
        ij.planFile = cmd.planFile;
        ij.demo = cmd.demo;
        ij.demoFail = cmd.demoFail;
        job = [ij](const std::atomic<bool>* c, const JobProgress& p) { return runInstallJob(ij, c, p); };
    }
    m_worker = new Worker(job, this);
    connect(m_worker, &Worker::progress, this, &SetupWindow::onProgress, Qt::QueuedConnection);
    connect(m_worker, &QThread::finished, this, &SetupWindow::onWorkerFinished);
    m_worker->start();
}

void SetupWindow::onProgress(const QString& step, double fraction)
{
    m_stepText = step;
    m_percent = int(fraction * 100.0 + 0.5);
    m_bar->setFraction(fraction);
    if (!m_workerUninstall && fraction >= 0.895)
        m_cancelBtn->setEnabled(false);  // the swap into place is quick and is not interrupted
    m_progress->update();
}

void SetupWindow::onWorkerFinished()
{
    Worker* w = m_worker;
    m_worker = nullptr;
    const JobResult r = w->result();
    w->deleteLater();
    if (m_closeWhenDone) {
        close();
        return;
    }
    if (r.ok) {
        m_bar->setFraction(1.0);
        m_percent = 100;
        m_doneNote = r.stillThere.isEmpty() ? QString() : QStringLiteral("A few files will be removed when Windows restarts.");
        if (!r.warnings.isEmpty() && !m_workerUninstall)
            m_doneNote = QStringLiteral("Installed. Some extras could not be set up: ") + r.warnings.first() + QStringLiteral(".");
        showScreen(m_workerUninstall ? Screen::UninstallDone : Screen::Done);
    } else if (r.cancelled) {
        m_bar->setFraction(0);
        showScreen(Screen::Welcome);
    } else {
        showError(r.error, r.stateNote);
    }
}

void SetupWindow::showError(const QString& message, const QString& state, bool canRetry)
{
    m_errorMessage = message;
    m_errorState = state;
    showScreen(Screen::Error);
    m_retryBtn->setVisible(canRetry);  // a damaged installer cannot be retried
    if (canRetry)
        m_retryBtn->setFocus(Qt::ActiveWindowFocusReason);
    else
        m_errCloseBtn->setFocus(Qt::ActiveWindowFocusReason);
    m_error->update();
}

void SetupWindow::launchAndClose()
{
    const QString exe = QDir(currentTarget()).filePath(Brand::appExe());
    QProcess::startDetached(exe, {}, currentTarget());
    close();
}

void SetupWindow::requestClose()
{
    if (m_prompt->isOpen())
        return;
    if (m_worker) {
        if (m_workerUninstall) {
            m_prompt->ask(QStringLiteral("Stop removing ZERO?"), QStringLiteral("Some of ZERO will already be gone. You can run the uninstaller again later."),
                          QStringLiteral("Keep going"), QStringLiteral("Stop"), [this](bool keep) {
                              if (!keep && m_worker) {
                                  m_closeWhenDone = true;
                                  m_worker->requestCancel();
                              }
                          });
        } else {
            m_prompt->ask(QStringLiteral("Stop installing?"), QStringLiteral("Everything that was copied is thrown away. Your computer stays as it was."),
                          QStringLiteral("Keep installing"), QStringLiteral("Stop"), [this](bool keep) {
                              if (!keep && m_worker) {
                                  m_closeWhenDone = true;
                                  m_worker->requestCancel();
                              }
                          }, ButtonKind::Primary, true);
        }
        return;
    }
    close();
}

void SetupWindow::closeEvent(QCloseEvent* e)
{
    if (m_worker && !m_closeWhenDone) {
        e->ignore();
        requestClose();
        return;
    }
    if (m_worker) {  // the user chose to stop: wait for the clean roll-back, then close
        e->ignore();
        return;
    }
    e->accept();
}

void SetupWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (m_screen == Screen::Options && !m_sliding)
            slideOptions(false);
        else
            requestClose();
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !m_prompt->isOpen()) {
        if (auto* b = primaryFor(m_screen))
            if (b->isEnabled() && b->isVisible()) {
                b->click();
                return;
            }
    }
    QWidget::keyPressEvent(e);
}

void SetupWindow::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (m_canvas && !Motion::frozen())
        m_canvas->setMarkStart(Motion::now());
}

// ---------------------------------------------------------------------------------------------- screenshots

bool SetupWindow::presentForScreenshot(const QString& nameIn)
{
    QString name = nameIn;
    int arg = -1;
    if (const int c = name.indexOf(':'); c >= 0) {
        arg = name.mid(c + 1).toInt();
        name = name.left(c);
    }
    m_canvas->setMarkStart(Motion::now() - 100000);
    auto fakeExisting = [this] {
        m_cfg.existing.kind = InstallKind::Nsis;
        m_cfg.existing.dir = m_cfg.targetDir;
        m_cfg.existing.version = QStringLiteral("0.1.0");
    };
    if (name == "welcome") {
        showScreen(Screen::Welcome);
    } else if (name == "drop") {
        const double total = m_canvas->markBlockCount() * std::min<double>(T::dropStaggerMs, double(T::dropMaxBuildMs) / std::max(1, m_canvas->markBlockCount())) + T::dropMs;
        m_canvas->setMarkStart(Motion::now() - qint64(total * std::clamp(arg, 0, 100) / 100.0));
        showScreen(Screen::Welcome);
    } else if (name == "options") {
        showScreen(Screen::Options);
    } else if (name == "update") {
        fakeExisting();
        refreshUpdateState();
        showScreen(Screen::Welcome);
    } else if (name == "update-options") {
        fakeExisting();
        refreshUpdateState();
        showScreen(Screen::Options);
    } else if (name == "installing") {
        const double f = std::clamp(arg < 0 ? 45 : arg, 0, 100) / 100.0;
        m_workerUninstall = false;
        m_stepText = f < 0.9 ? QStringLiteral("Copying files") : f < 0.94 ? QStringLiteral("Creating shortcuts") : f < 0.98 ? QStringLiteral("Registering ZERO") : QStringLiteral("Finishing");
        m_percent = int(f * 100);
        m_cancelBtn->setVisible(true);
        showScreen(Screen::Installing);
        m_bar->setFraction(f);
        m_bar->setNewestSegmentAge(110);
        m_cancelBtn->setEnabled(f < 0.9);
    } else if (name == "done") {
        showScreen(Screen::Done);
    } else if (name == "error") {
        showError(QStringLiteral("Could not write platforms/qwindows.dll (the disk is full)."), QStringLiteral("Your previous version is still installed."));
    } else if (name == "running") {
        showScreen(Screen::Welcome);
        promptRunning([] {});
    } else if (name == "stop") {
        m_stepText = QStringLiteral("Copying files");
        m_percent = 40;
        showScreen(Screen::Installing);
        m_bar->setFraction(0.4);
        m_prompt->ask(QStringLiteral("Stop installing?"), QStringLiteral("Everything that was copied is thrown away. Your computer stays as it was."), QStringLiteral("Keep installing"),
                      QStringLiteral("Stop"), [](bool) {}, ButtonKind::Primary, true);
    } else if (name == "uninstall") {
        m_cfg.uninstallMode = true;
        showScreen(Screen::UninstallConfirm);
    } else if (name == "uninstalling") {
        m_cfg.uninstallMode = true;
        m_workerUninstall = true;
        const double f = std::clamp(arg < 0 ? 60 : arg, 0, 100) / 100.0;
        m_stepText = QStringLiteral("Removing files");
        m_percent = int(f * 100);
        m_cancelBtn->setVisible(false);
        showScreen(Screen::Uninstalling);
        m_bar->setFraction(f);
        m_bar->setNewestSegmentAge(110);
    } else if (name == "uninstall-done") {
        m_cfg.uninstallMode = true;
        showScreen(Screen::UninstallDone);
    } else {
        return false;
    }
    m_canvas->settleTagline();
    return true;
}

QImage SetupWindow::renderImage()
{
    ensurePolished();
    QPixmap pm = grab();
    return pm.toImage();
}

}  // namespace ZeroSetup
