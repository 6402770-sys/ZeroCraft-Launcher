// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Add account" dialog (docs/zero/ARCHITECTURE.md, "Add account"). One dialog for every way of adding an account:
// step 1 asks which kind (Microsoft, Ely.by, Offline; the first two only when this build can sign in with them), step 2 signs in.
// Microsoft runs Prism's two flows at once (browser sign-in + device code) and takes whichever finishes; Ely.by runs the device code
// flow when this build has an Ely.by client ID and otherwise (always available) asks for email / username + password, with an
// optional two-factor code (Page::Password). Leaving step 2 aborts the running flows. The dialog never touches the account list: run() returns the new
// account (or nullptr) and the caller adds it.
#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <optional>
#include <vector>

#include "QObjectPtr.h"
#include "minecraft/auth/AuthFlow.h"
#include "minecraft/auth/MinecraftAccount.h"

class QAbstractButton;
class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QVariantAnimation;

namespace Zero {

class ZeroCard;

namespace Accounts {

class CodePanel;
class OptionCard;
class PageStack;
class SlideOverlay;
class StatusGlyph;

/// Everything the dialog needs from the application, so tests can build it without APPLICATION, network or a browser.
struct AddAccountEnvironment {
    using FlowFactory = std::function<shared_qobject_ptr<AuthFlow>(AccountData*, AuthFlow::Action)>;

    bool microsoft = false;        ///< this build has a Microsoft client ID
    bool ely = false;              ///< the Ely.by card is offered (Application::SupportsEly: always, email + password needs no registration)
    bool elyCode = false;          ///< this build also has an Ely.by client ID: step 2 starts with the code flow (Application::SupportsElyOAuth)
    bool offlineAllowed = true;    ///< Prism's rule: an offline account needs another account that owns Minecraft
    int successDelayMs = 900;      ///< how long "Signed in as ..." stays before the dialog closes
    FlowFactory makeFlow;                                           ///< the AuthFlow for an account and an action
    std::function<bool(const QUrl&)> openUrl;                       ///< opens the browser; false = could not (the link is copied instead)
    std::function<void(const QString&)> copyText;                   ///< the clipboard
    std::function<MinecraftAccountPtr(const QString&)> makeOffline; ///< a new offline account for a name (null = could not)

    /// The real thing: client IDs and accounts of APPLICATION, real AuthFlows, the desktop browser and clipboard.
    static AddAccountEnvironment fromApplication();
};

class AddAccountDialog : public QDialog {
    Q_OBJECT
   public:
    static constexpr int kWidth = 440;
    static constexpr int kSlideMs = 320;
    static constexpr int kCopiedMs = 1500;

    enum class Page { Choose, SignIn, Offline, Password };

    /// Where an Ely.by sign-in that opens straight at step 2 starts (used to sign in again).
    struct SignInHint {
        QString username;       ///< prefills "Email or username"
        bool password = false;  ///< the account signs in with a password: open the password form even when a client ID exists
    };

    /// Opens at step 1 and returns the account the user added, or nullptr (cancelled).
    static MinecraftAccountPtr run(QWidget* parent);
    /// Opens directly at step 2 for `type` (no back arrow); used to sign in again with an account whose session ran out.
    static MinecraftAccountPtr run(QWidget* parent, AccountType type);
    static MinecraftAccountPtr run(QWidget* parent, AccountType type, const SignInHint& hint);

    explicit AddAccountDialog(QWidget* parent = nullptr);
    explicit AddAccountDialog(AddAccountEnvironment env, QWidget* parent = nullptr);
    /// `startAt`: open at step 2 of that kind (no back arrow, no "step" overline).
    AddAccountDialog(AddAccountEnvironment env, std::optional<AccountType> startAt, QWidget* parent = nullptr);
    AddAccountDialog(AddAccountEnvironment env, std::optional<AccountType> startAt, const SignInHint& hint, QWidget* parent = nullptr);
    ~AddAccountDialog() override;

    Page page() const { return m_page; }
    /// The kind step 2 is about (meaningful when page() != Choose).
    AccountType accountType() const { return m_type; }
    /// The account being signed in (blank until a flow finished) or added; nullptr at step 1.
    MinecraftAccountPtr account() const { return m_account; }

    /// Moves to step 2 of `type` as if the user chose that card (or back to step 1 with chooseStep()).
    void chooseType(AccountType type);
    void chooseStep();
    /// The kinds whose cards are shown, in order.
    QList<AccountType> offeredTypes() const;

    /// Offline name rules (Prism's ChooseOfflineNameDialog): 3 to 16 English letters, numbers or underscores.
    static bool isValidOfflineName(const QString& name);

    // Parts, for tests and the screenshot aid.
    ZeroCard* optionCard(AccountType type) const;
    QLabel* titleLabel() const;     ///< of the visible step
    QLabel* overlineLabel() const;  ///< "STEP n OF 2" of the visible step
    QToolButton* backButton() const;
    QPushButton* primaryButton() const { return m_primary; }
    QLabel* captionLabel() const { return m_caption; }
    QLabel* codeLabel() const;
    QAbstractButton* copyButton() const;
    QLabel* statusLabel() const { return m_status; }
    QPushButton* retryButton() const { return m_retry; }
    QLineEdit* nameField() const { return m_name; }
    QLabel* nameHint() const { return m_nameHint; }
    QCheckBox* allowInvalidBox() const { return m_allowInvalid; }
    QPushButton* addButton() const { return m_add; }
    QWidget* codePanel() const;
    /// The text the QR code encodes (empty until a code arrived).
    QString qrText() const;
    /// "Signed in" state: the status row shows the success line and the dialog is about to close.
    bool isSignedIn() const { return m_state == SignInState::Succeeded; }
    bool hasFailed() const { return m_state == SignInState::Failed; }
    // Ely.by password form (Page::Password)
    QLineEdit* userField() const { return m_pwUser; }
    QLineEdit* passwordField() const { return m_pwPass; }
    QLineEdit* twoFactorField() const { return m_pwCode; }
    QPushButton* submitButton() const { return m_pwSubmit; }
    QLabel* passwordStatusLabel() const { return m_pwStatus; }
    QLabel* twoFactorCaption() const { return m_pwCodeCaption; }
    QLabel* privacyLabel() const { return m_pwPrivacy; }
    QAction* showPasswordAction() const { return m_pwEye; }
    QAbstractButton* createAccountLink() const { return m_pwCreate; }
    QAbstractButton* forgotPasswordLink() const { return m_pwForgot; }
    QAbstractButton* useCodeLink() const { return m_pwUseCode; }          ///< on the password form: "Use a code instead"
    QAbstractButton* usePasswordLink() const { return m_codeUsePassword; }  ///< on the code step: "Use email and password instead"
    /// The two-factor field is open (Ely.by asked for the code).
    bool twoFactorVisible() const { return m_twoFactorShown; }
    /// The password form is busy: the request runs and the fields are disabled.
    bool isSigningIn() const { return m_state == SignInState::Working && m_page == Page::Password; }
    /// Number of flows currently running (tests: back / close abort them).
    int runningFlows() const;
    int slideOverlayCount() const;

   public slots:
    void done(int result) override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

   private:
    enum class SignInState { Idle, Starting, Waiting, Working, Succeeded, Failed };
    struct Flow {
        shared_qobject_ptr<AuthFlow> task;
        bool armed = false;      ///< it showed its link / code: from now on its status is the user's progress
        bool progressed = false; ///< the user confirmed: a later step runs
        bool finished = false;
    };

    void build(std::optional<AccountType> startAt);
    QWidget* buildChoosePage();
    QWidget* buildSignInPage();
    QWidget* buildOfflinePage();
    QWidget* buildPasswordPage();
    void goTo(Page page, AccountType type, bool forward, bool animate);
    Page pageFor(AccountType type) const;
    void refreshPageTexts();
    void refreshIcons();
    void moveCardFocus(int step);

    // sign-in
    void startFlows();
    void abortFlows();
    Flow* flowFor(AuthFlow* task);
    void resetSignInUi();
    void setState(SignInState state);
    void showStatus(const QString& text);
    void showError(const QString& message);
    void onBrowserUrl(AuthFlow* task, const QUrl& url);
    void onDeviceCode(AuthFlow* task, const QString& url, const QString& code, int expiresIn);
    void onFlowStatus(AuthFlow* task, const QString& status);
    void onFlowSucceeded(AuthFlow* task);
    void onFlowFailed(AuthFlow* task, const QString& reason);
    void onFlowAborted(AuthFlow* task);
    void onUserConfirmed(AuthFlow* task);
    void openSignInPage();
    void copyCode();
    QString providerName() const;

    // Ely.by password form
    void resetPasswordPage();
    void submitPassword();
    void onPasswordSucceeded(AuthFlow* task);
    void onPasswordFailed(AuthFlow* task, const QString& reason);
    void setPasswordBusy(bool busy);
    void showPasswordStatus(const QString& text, const char* tone, bool error);
    void setTwoFactorVisible(bool visible, bool animate);
    void clearPendingPassword();
    void openLink(const QUrl& url);
    bool elyUsesPassword() const;

    // offline
    void refreshOfflineState();
    void addOffline();

    AddAccountEnvironment m_env;
    Page m_page = Page::Choose;
    AccountType m_type = AccountType::MSA;
    bool m_direct = false;  ///< opened at step 2: no step 1 to go back to
    MinecraftAccountPtr m_account;

    PageStack* m_stack = nullptr;
    QWidget* m_choosePage = nullptr;
    QWidget* m_signInPage = nullptr;
    QWidget* m_offlinePage = nullptr;
    QWidget* m_passwordPage = nullptr;
    QList<OptionCard*> m_cards;
    QList<AccountType> m_cardTypes;

    QLabel* m_chooseOverline = nullptr;
    QLabel* m_chooseTitle = nullptr;
    QLabel* m_signInTitle = nullptr;
    QLabel* m_signInOverline = nullptr;
    QToolButton* m_signInBack = nullptr;
    QLabel* m_signInProvider = nullptr;
    QLabel* m_offlineTitle = nullptr;
    QLabel* m_offlineOverline = nullptr;
    QToolButton* m_offlineBack = nullptr;
    QLabel* m_offlineProvider = nullptr;

    QPushButton* m_primary = nullptr;
    QLabel* m_caption = nullptr;
    CodePanel* m_codePanel = nullptr;
    StatusGlyph* m_glyph = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_retry = nullptr;

    // Ely.by password form
    QToolButton* m_pwBack = nullptr;
    QLabel* m_pwProvider = nullptr;
    QLabel* m_pwOverline = nullptr;
    QLabel* m_pwTitle = nullptr;
    QLineEdit* m_pwUser = nullptr;
    QLineEdit* m_pwPass = nullptr;
    QAction* m_pwEye = nullptr;
    QWidget* m_twoFactorBox = nullptr;
    QLabel* m_pwCodeCaption = nullptr;
    QLineEdit* m_pwCode = nullptr;
    QPushButton* m_pwSubmit = nullptr;
    StatusGlyph* m_pwGlyph = nullptr;
    QLabel* m_pwStatus = nullptr;
    QLabel* m_pwPrivacy = nullptr;
    QAbstractButton* m_pwCreate = nullptr;
    QAbstractButton* m_pwForgot = nullptr;
    QAbstractButton* m_pwUseCode = nullptr;
    QAbstractButton* m_codeUsePassword = nullptr;
    QVariantAnimation* m_twoFactorAnim = nullptr;
    bool m_twoFactorShown = false;
    QString m_pendingPassword;  ///< kept in memory only while Ely.by may ask for the two-factor code; wiped on every other outcome
    SignInHint m_hint;

    QLineEdit* m_name = nullptr;
    QLabel* m_nameHint = nullptr;
    QCheckBox* m_allowInvalid = nullptr;
    QPushButton* m_add = nullptr;

    // sign-in state
    SignInState m_state = SignInState::Idle;
    std::vector<Flow> m_flows;
    QUrl m_browserUrl;
    QString m_deviceLink;  ///< what the QR code encodes and the device code page opens
    QString m_deviceCode;
    unsigned m_generation = 0;  ///< bumped whenever flows are replaced or aborted
    QString m_failure;
    QTimer m_expiry;
    QTimer m_closeTimer;
    QTimer m_copiedTimer;
};

}  // namespace Accounts
}  // namespace Zero
