// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - dev aid for ZERO_SCREENSHOT_ADD_ACCOUNT (see AddAccountScreenshot.h).
#include "AddAccountScreenshot.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>

#include "AddAccountDialog.h"
#include "Application.h"

namespace Zero {
namespace Accounts {

namespace {

enum class Script { Wait, Fail, Succeed, TwoFactor, WrongCredentials };

/// A flow that only plays a script: no steps, no network. It owns a dummy offline account data so the real steps are never built.
class ScriptedFlow : public AuthFlow {
   public:
    // No Q_OBJECT here: give tr() the same context lupdate records (the class name) instead of the base class's.
    Q_DECLARE_TR_FUNCTIONS(Zero::Accounts::ScriptedFlow)
   public:  // the macro leaves the section private
    ScriptedFlow(AccountData* real, Action action, Script script, bool ely)
        : AuthFlow(dummy(), action), m_real(real), m_action(action), m_script(script), m_ely(ely)
    {}

    // a password sign-in: the credentials are not kept (the script does not look at them)
    void setElyCredentials(const QString&, const QString&, const QString&) override {}

    void executeTask() override
    {
        if (m_real->type == AccountType::Ely && m_real->elyAuth == ElyAuthKind::Password) {
            // email + password: the script answers shortly after the click
            using Zero::Auth::Ely::Authserver::Failure;
            QTimer::singleShot(120, this, [this]() {
                if (m_script == Script::TwoFactor) {
                    setElyFailure(Failure::TwoFactorRequired);
                    emitFailed(tr("This account is protected with two-factor authentication."));
                } else if (m_script == Script::WrongCredentials) {
                    setElyFailure(Failure::WrongCredentials);
                    emitFailed(tr("Wrong email or password."));
                } else if (m_script == Script::Succeed) {
                    m_real->minecraftProfile.name = QStringLiteral("Steve");
                    emitSucceeded();
                }
            });
            return;
        }
        if (m_action == Action::Login) {
            emit authorizeWithBrowser(QUrl(QStringLiteral("https://login.example.invalid/authorize")));
        } else {
            emit authorizeWithBrowserWithExtra(
                m_ely ? QStringLiteral("https://account.ely.by/code?user_code=ZKTP-GLWR") : QStringLiteral("https://www.microsoft.com/link"),
                QStringLiteral("ZKTP-GLWR"), 900);
        }
        if (m_script == Script::Fail) {
            QTimer::singleShot(150, this, [this]() {
                emitFailed(tr("We couldn't reach %1. Check your connection and try again.").arg(m_ely ? tr("Ely.by") : tr("Microsoft")));
            });
        } else if (m_script == Script::Succeed && m_action == Action::Login) {
            QTimer::singleShot(150, this, [this]() { setStatus(tr("Checking your Xbox profile\xE2\x80\xA6")); });
            QTimer::singleShot(450, this, [this]() {
                m_real->minecraftProfile.name = QStringLiteral("Steve");
                emitSucceeded();
            });
        }
    }

   private:
    static AccountData* dummy()
    {
        static AccountData data;
        data.type = AccountType::Offline;
        return &data;
    }

    AccountData* m_real;
    Action m_action;
    Script m_script;
    bool m_ely;
};

}  // namespace

bool screenshotAddAccount(QWidget* parent, const QString& state, const QString& path)
{
    const QStringList known = { QStringLiteral("choose"),      QStringLiteral("microsoft"),  QStringLiteral("ely"),
                                QStringLiteral("offline"),     QStringLiteral("error"),      QStringLiteral("success"),
                                QStringLiteral("ely-password"), QStringLiteral("ely-2fa"),   QStringLiteral("ely-wrong") };
    if (!known.contains(state)) {
        qWarning() << "ZERO_SCREENSHOT_ADD_ACCOUNT: unknown state" << state
                   << "(choose, microsoft, ely, offline, error, success, ely-password, ely-2fa, ely-wrong)";
        return false;
    }
    Script script = Script::Wait;
    if (state == QLatin1String("error"))
        script = Script::Fail;
    else if (state == QLatin1String("success"))
        script = Script::Succeed;
    else if (state == QLatin1String("ely-2fa"))
        script = Script::TwoFactor;
    else if (state == QLatin1String("ely-wrong"))
        script = Script::WrongCredentials;
    const bool elyPassword = state.startsWith(QLatin1String("ely-"));

    AddAccountEnvironment env;
    env.microsoft = true;
    env.ely = true;
    env.elyCode = state == QLatin1String("ely");  // "ely" = the code step (a build with an Ely.by client ID); ely-* = the password form
    env.offlineAllowed = true;
    env.successDelayMs = 60000;  // the grab happens first
    env.makeFlow = [script](AccountData* data, AuthFlow::Action action) {
        return shared_qobject_ptr<AuthFlow>(new ScriptedFlow(data, action, script, data->type == AccountType::Ely));
    };
    env.openUrl = [](const QUrl&) { return true; };
    env.copyText = [](const QString&) {};
    env.makeOffline = [](const QString& name) { return MinecraftAccount::createOffline(name); };

    auto* dialog = new AddAccountDialog(env, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    if (state == QLatin1String("microsoft") || state == QLatin1String("error") || state == QLatin1String("success"))
        dialog->chooseType(AccountType::MSA);
    else if (state == QLatin1String("ely") || elyPassword)
        dialog->chooseType(AccountType::Ely);
    else if (state == QLatin1String("offline")) {
        dialog->chooseType(AccountType::Offline);
        dialog->nameField()->setText(QStringLiteral("Steve_01"));
    }
    if (elyPassword && state != QLatin1String("ely-password")) {
        // fake sample values (the script never looks at them); the click plays the scripted answer
        dialog->userField()->setText(QStringLiteral("steve@example.com"));
        dialog->passwordField()->setText(QStringLiteral("sample-only"));
        dialog->submitButton()->click();
    } else if (state == QLatin1String("ely-password")) {
        dialog->userField()->setText(QStringLiteral("steve@example.com"));
        dialog->passwordField()->setText(QStringLiteral("sample-only"));
    }
    dialog->show();
    QTimer::singleShot(1200, dialog, [dialog, path, state]() {
        QApplication::processEvents();
        if (state == QLatin1String("ely-2fa"))
            dialog->twoFactorField()->setText(QStringLiteral("482913"));
        const bool ok = dialog->grab().save(path);
        qInfo() << "ZERO screenshot" << (ok ? "saved to" : "FAILED for") << path;
        dialog->reject();
        APPLICATION->exit(0);
    });
    return true;
}

}  // namespace Accounts
}  // namespace Zero
