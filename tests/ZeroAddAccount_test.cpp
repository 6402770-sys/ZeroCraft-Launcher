// SPDX-License-Identifier: GPL-3.0-only
// "Add account" dialog (offscreen, no network, no browser): which kinds are offered, keyboard, the flows each kind runs and their
// abort, success / failure / try again, the code and its Copy button, the offline name rules, opening straight at step 2.
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

#include "ui/zero/ZeroCard.h"
#include "zero/auth/ElyAuthserver.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/accounts/AddAccountDialog.h"

using namespace Zero;
using namespace Zero::Accounts;

namespace {

/// An AuthFlow that only does what the test tells it to: no steps, no network. It is built on a dummy offline account data
/// so the real steps (which need APPLICATION) are never created.
class FakeFlow : public AuthFlow {
   public:
    FakeFlow(AccountData* real, Action action) : AuthFlow(dummy(), action), m_real(real), m_action(action) {}

    void executeTask() override { started = true; }

    Action action() const { return m_action; }
    AccountData* realData() const { return m_real; }
    void browser(const QString& url) { emit authorizeWithBrowser(QUrl(url)); }
    void code(const QString& url, const QString& code, int expiresIn = 900) { emit authorizeWithBrowserWithExtra(url, code, expiresIn); }
    void say(const QString& status) { setStatus(status); }
    void succeed(const QString& profileName)
    {
        m_real->minecraftProfile.name = profileName;
        emitSucceeded();
    }
    void fail(const QString& reason) { emitFailed(reason); }
    /// An Ely.by password sign-in that ended for `failure` (what the real step reports).
    void failElyPassword(Zero::Auth::Ely::Authserver::Failure failure, const QString& reason = QString())
    {
        setElyFailure(failure);
        emitFailed(reason);
    }
    /// What the dialog handed to the sign-in step (password flows only).
    void setElyCredentials(const QString& u, const QString& p, const QString& c) override
    {
        user = u;
        pass = p;
        twoFactor = c;
        ++credentialCalls;
    }

    QString user, pass, twoFactor;
    int credentialCalls = 0;

    bool started = false;

   private:
    static AccountData* dummy()
    {
        static AccountData data;
        data.type = AccountType::Offline;
        return &data;
    }
    AccountData* m_real;
    Action m_action;
};

struct Rig {
    QList<QPointer<FakeFlow>> flows;
    QList<QUrl> opened;
    QStringList copied;
    QStringList offlineNames;

    AddAccountEnvironment env(bool ms, bool ely, bool offlineAllowed = true)
    {
        AddAccountEnvironment e;
        e.microsoft = ms;
        e.ely = ely;
        e.elyCode = ely;  // tests of the code step; the password form tests clear it
        e.offlineAllowed = offlineAllowed;
        e.successDelayMs = 30;
        e.makeFlow = [this](AccountData* data, AuthFlow::Action action) {
            auto* flow = new FakeFlow(data, action);
            flows.append(flow);
            return shared_qobject_ptr<AuthFlow>(flow);
        };
        e.openUrl = [this](const QUrl& url) {
            opened.append(url);
            return true;
        };
        e.copyText = [this](const QString& text) { copied.append(text); };
        e.makeOffline = [this](const QString& name) {
            offlineNames.append(name);
            return MinecraftAccount::createOffline(name);
        };
        return e;
    }

    FakeFlow* flow(AuthFlow::Action action, int nth = 0) const
    {
        int seen = 0;
        for (const auto& f : flows)
            if (f && f->action() == action && seen++ == nth)
                return f;
        return nullptr;
    }
    int count(AuthFlow::Action action) const
    {
        int n = 0;
        for (const auto& f : flows)
            if (f && f->action() == action)
                ++n;
        return n;
    }
};

QStringList texts(QWidget* w)
{
    QStringList out;
    for (auto* l : w->findChildren<QLabel*>())
        if (!l->text().isEmpty())
            out << l->text();
    return out;
}

void showDialog(QDialog& dlg)
{
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    QCoreApplication::processEvents();
}

}  // namespace

class ZeroAddAccountTest : public QObject {
    Q_OBJECT
   private slots:
    void init()
    {
        Theme::setCurrent(ThemeSpec::obsidian());
        Theme::setReduceMotion(true);  // steps switch at once unless a test asks for the slide
    }
    void cleanup() { Theme::setReduceMotion(false); }

    // ---------------------------------------------------------------------------------------------- step 1

    void offersTheKindsThisBuildCanSignInWith_data()
    {
        QTest::addColumn<bool>("microsoft");
        QTest::addColumn<bool>("ely");
        QTest::addColumn<QList<int>>("expected");
        const int ms = int(AccountType::MSA), ely = int(AccountType::Ely), off = int(AccountType::Offline);
        QTest::newRow("nothing configured") << false << false << QList<int>{ off };
        QTest::newRow("Microsoft only") << true << false << QList<int>{ ms, off };
        QTest::newRow("Ely.by only") << false << true << QList<int>{ ely, off };
        QTest::newRow("both") << true << true << QList<int>{ ms, ely, off };
    }
    void offersTheKindsThisBuildCanSignInWith()
    {
        QFETCH(bool, microsoft);
        QFETCH(bool, ely);
        QFETCH(QList<int>, expected);
        Rig rig;
        AddAccountDialog dlg(rig.env(microsoft, ely));
        QList<int> got;
        for (AccountType t : dlg.offeredTypes())
            got << int(t);
        QCOMPARE(got, expected);
        QCOMPARE(dlg.optionCard(AccountType::MSA) != nullptr, microsoft);
        QCOMPARE(dlg.optionCard(AccountType::Ely) != nullptr, ely);
        QVERIFY(dlg.optionCard(AccountType::Offline));
        QCOMPARE(dlg.page(), AddAccountDialog::Page::Choose);
    }

    void stepOneShowsTheExactTexts()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, true));
        QCOMPARE(dlg.windowTitle(), QStringLiteral("Add account"));
        const QStringList all = texts(&dlg);
        QCOMPARE(dlg.overlineLabel()->text().toUpper(), QStringLiteral("STEP 1 OF 2"));
        QCOMPARE(dlg.titleLabel()->text(), QStringLiteral("Choose your account"));
        QVERIFY(all.contains(QStringLiteral("You can add more later.")));
        QVERIFY(all.contains(QStringLiteral("Microsoft")));
        QVERIFY(all.contains(QStringLiteral("Official account. Every server.")));
        QVERIFY(all.contains(QStringLiteral("Ely.by")));
        QVERIFY(all.contains(QStringLiteral("Free account with skins. Servers that accept Ely.by.")));
        QVERIFY(all.contains(QStringLiteral("Offline")));
        QVERIFY(all.contains(QStringLiteral("Just a name. No online servers.")));
        // fixed width 440, height to content
        showDialog(dlg);
        QCOMPARE(dlg.width(), AddAccountDialog::kWidth);
        QVERIFY(dlg.height() > 200);
    }

    void arrowsMoveFocusAndEnterChooses()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, true));
        showDialog(dlg);
        auto* ms = dlg.optionCard(AccountType::MSA);
        auto* ely = dlg.optionCard(AccountType::Ely);
        auto* off = dlg.optionCard(AccountType::Offline);
        QCOMPARE(dlg.focusWidget(), static_cast<QWidget*>(ms));
        QTest::keyClick(dlg.focusWidget(), Qt::Key_Down);
        QCOMPARE(dlg.focusWidget(), static_cast<QWidget*>(ely));
        QTest::keyClick(dlg.focusWidget(), Qt::Key_Down);
        QCOMPARE(dlg.focusWidget(), static_cast<QWidget*>(off));
        QTest::keyClick(dlg.focusWidget(), Qt::Key_Down);  // the last card stays
        QCOMPARE(dlg.focusWidget(), static_cast<QWidget*>(off));
        QTest::keyClick(dlg.focusWidget(), Qt::Key_Up);
        QCOMPARE(dlg.focusWidget(), static_cast<QWidget*>(ely));
        QCOMPARE(dlg.page(), AddAccountDialog::Page::Choose);  // moving does not choose
        QTest::keyClick(dlg.focusWidget(), Qt::Key_Return);
        QCOMPARE(dlg.page(), AddAccountDialog::Page::SignIn);
        QCOMPARE(dlg.accountType(), AccountType::Ely);
    }

    void clickingACardChooses()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, true));
        showDialog(dlg);
        QTest::mouseClick(dlg.optionCard(AccountType::Offline), Qt::LeftButton);
        QCOMPARE(dlg.page(), AddAccountDialog::Page::Offline);
        QCOMPARE(dlg.accountType(), AccountType::Offline);
        QCOMPARE(dlg.titleLabel()->text(), QStringLiteral("Play offline"));
    }

    void stepTwoSlidesInAndInstantWhenMotionIsOff()
    {
        Rig rig;
        {
            AddAccountDialog dlg(rig.env(true, true));
            showDialog(dlg);
            dlg.chooseType(AccountType::MSA);  // motion off (init)
            QCOMPARE(dlg.slideOverlayCount(), 0);
        }
        Theme::setReduceMotion(false);
        AddAccountDialog dlg(rig.env(true, true));
        showDialog(dlg);
        dlg.chooseType(AccountType::MSA);
        QCOMPARE(dlg.slideOverlayCount(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(dlg.slideOverlayCount(), 0, 1500);
        dlg.chooseStep();
        QCOMPARE(dlg.slideOverlayCount(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(dlg.slideOverlayCount(), 0, 1500);
        QCOMPARE(dlg.page(), AddAccountDialog::Page::Choose);
    }

    // ---------------------------------------------------------------------------------------------- step 2: flows

    void microsoftRunsBothFlowsAndElyOnlyTheDeviceCode()
    {
        Rig rig;
        {
            AddAccountDialog dlg(rig.env(true, true));
            dlg.chooseType(AccountType::MSA);
            QCOMPARE(dlg.page(), AddAccountDialog::Page::SignIn);
            QCOMPARE(dlg.titleLabel()->text(), QStringLiteral("Sign in with Microsoft"));
            QCOMPARE(dlg.primaryButton()->text(), QStringLiteral("Open Microsoft sign-in"));
            QCOMPARE(rig.count(AuthFlow::Action::Login), 1);
            QCOMPARE(rig.count(AuthFlow::Action::DeviceCode), 1);
            QCOMPARE(rig.count(AuthFlow::Action::Refresh), 0);
            QVERIFY(rig.flow(AuthFlow::Action::Login)->started);
            QVERIFY(rig.flow(AuthFlow::Action::DeviceCode)->started);
            QCOMPARE(dlg.runningFlows(), 2);
            QCOMPARE(dlg.account()->accountType(), AccountType::MSA);
        }
        Rig ely;
        AddAccountDialog dlg(ely.env(true, true));
        dlg.chooseType(AccountType::Ely);
        QCOMPARE(dlg.titleLabel()->text(), QStringLiteral("Sign in with Ely.by"));
        QCOMPARE(dlg.primaryButton()->text(), QStringLiteral("Open Ely.by"));
        QCOMPARE(ely.count(AuthFlow::Action::Login), 0);
        QCOMPARE(ely.count(AuthFlow::Action::DeviceCode), 1);
        QVERIFY(ely.flow(AuthFlow::Action::DeviceCode)->started);
        QCOMPARE(dlg.runningFlows(), 1);
        QCOMPARE(dlg.account()->accountType(), AccountType::Ely);
    }

    void microsoftShowsTheCodeTheQrAndOpensTheBrowserFlow()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, false));
        dlg.chooseType(AccountType::MSA);
        QCOMPARE(dlg.overlineLabel()->text().toUpper(), QStringLiteral("STEP 2 OF 2"));
        QVERIFY(dlg.backButton()->isVisibleTo(&dlg));
        QVERIFY(!dlg.primaryButton()->isEnabled());  // no link yet
        QCOMPARE(dlg.captionLabel()->text(), QStringLiteral("or enter this code at microsoft.com/link"));
        QVERIFY(dlg.qrText().isEmpty());
        QVERIFY(!dlg.copyButton()->isEnabled());

        rig.flow(AuthFlow::Action::Login)->browser(QStringLiteral("https://login.example.invalid/authorize?x=1"));
        QVERIFY(dlg.primaryButton()->isEnabled());
        QCOMPARE(dlg.statusLabel()->text(), QStringLiteral("Waiting for you to confirm"));
        rig.flow(AuthFlow::Action::DeviceCode)->code(QStringLiteral("https://www.microsoft.com/link"), QStringLiteral("ZKTP-GLWR"));
        QCOMPARE(dlg.codeLabel()->text(), QStringLiteral("ZKTP-GLWR"));
        QCOMPARE(dlg.qrText(), QStringLiteral("https://www.microsoft.com/link?otc=ZKTP-GLWR"));  // Prism's way: the code comes filled in
        QVERIFY(dlg.copyButton()->isEnabled());

        dlg.primaryButton()->click();  // the browser flow's link
        QCOMPARE(rig.opened, QList<QUrl>{ QUrl(QStringLiteral("https://login.example.invalid/authorize?x=1")) });
    }

    void elyOpensTheLinkWithTheCodeFilledIn()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(false, true));
        dlg.chooseType(AccountType::Ely);
        QCOMPARE(dlg.captionLabel()->text(), QStringLiteral("or enter this code at ely.by/code"));
        QVERIFY(!dlg.primaryButton()->isEnabled());
        const QString url = QStringLiteral("https://ely.by/code?user_code=ZKTP-GLWR");
        rig.flow(AuthFlow::Action::DeviceCode)->code(url, QStringLiteral("ZKTP-GLWR"));
        QCOMPARE(dlg.captionLabel()->text(), QStringLiteral("or enter this code at ely.by/code"));
        QCOMPARE(dlg.qrText(), url);
        QVERIFY(dlg.primaryButton()->isEnabled());
        dlg.primaryButton()->click();
        QCOMPARE(rig.opened, QList<QUrl>{ QUrl(url) });
    }

    void aLinkThatCannotOpenGoesToTheClipboard()
    {
        Rig rig;
        auto env = rig.env(true, false);
        env.openUrl = [](const QUrl&) { return false; };
        AddAccountDialog dlg(env);
        dlg.chooseType(AccountType::MSA);
        rig.flow(AuthFlow::Action::Login)->browser(QStringLiteral("https://login.example.invalid/a"));
        dlg.primaryButton()->click();
        QCOMPARE(rig.copied, QStringList{ QStringLiteral("https://login.example.invalid/a") });
    }

    void copyCodeSaysCopiedForAWhile()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, false));
        dlg.chooseType(AccountType::MSA);
        rig.flow(AuthFlow::Action::DeviceCode)->code(QStringLiteral("https://www.microsoft.com/link"), QStringLiteral("ZKTP-GLWR"));
        QCOMPARE(dlg.copyButton()->text(), QStringLiteral("Copy code"));
        dlg.copyButton()->click();
        QCOMPARE(rig.copied, QStringList{ QStringLiteral("ZKTP-GLWR") });
        QCOMPARE(dlg.copyButton()->text(), QStringLiteral("Copied"));
        QTRY_COMPARE_WITH_TIMEOUT(dlg.copyButton()->text(), QStringLiteral("Copy code"), AddAccountDialog::kCopiedMs + 1500);
    }

    void statusFollowsTheFlowOnceTheUserConfirmed()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, false));
        dlg.chooseType(AccountType::MSA);
        auto* login = rig.flow(AuthFlow::Action::Login);
        auto* device = rig.flow(AuthFlow::Action::DeviceCode);
        login->say(QStringLiteral("Logging in with Microsoft account."));  // before its link: not the user's progress
        QCOMPARE(dlg.statusLabel()->text(), QStringLiteral("Preparing sign-in…"));
        login->browser(QStringLiteral("https://login.example.invalid/a"));
        QCOMPARE(dlg.statusLabel()->text(), QStringLiteral("Waiting for you to confirm"));
        login->say(QStringLiteral("Checking your Xbox profile"));
        QCOMPARE(dlg.statusLabel()->text(), QStringLiteral("Checking your Xbox profile"));
        // confirmed through the browser flow: the code flow stops
        QCOMPARE(device->getState(), Task::State::AbortedByUser);
        QCOMPARE(dlg.runningFlows(), 1);
    }

    void backAbortsTheRunningFlows()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, true));
        dlg.chooseType(AccountType::MSA);
        QCOMPARE(dlg.runningFlows(), 2);
        dlg.backButton()->click();
        QCOMPARE(dlg.page(), AddAccountDialog::Page::Choose);
        QCOMPARE(dlg.runningFlows(), 0);
        QCOMPARE(rig.flow(AuthFlow::Action::Login)->getState(), Task::State::AbortedByUser);
        QCOMPARE(rig.flow(AuthFlow::Action::DeviceCode)->getState(), Task::State::AbortedByUser);
        QVERIFY(!dlg.account());
        // choosing again starts fresh flows
        dlg.chooseType(AccountType::MSA);
        QCOMPARE(rig.count(AuthFlow::Action::Login), 2);
        QCOMPARE(dlg.runningFlows(), 2);
    }

    void closingAbortsTheRunningFlows()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(false, true));
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);
        QCOMPARE(dlg.runningFlows(), 1);
        QTest::keyClick(&dlg, Qt::Key_Escape);
        QCOMPARE(dlg.result(), int(QDialog::Rejected));
        QCOMPARE(rig.flow(AuthFlow::Action::DeviceCode)->getState(), Task::State::AbortedByUser);
        QCOMPARE(dlg.runningFlows(), 0);
    }

    void aFlowThatFailedLeavesTheOtherWayOpen()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, false));
        dlg.chooseType(AccountType::MSA);
        rig.flow(AuthFlow::Action::Login)->browser(QStringLiteral("https://login.example.invalid/a"));
        rig.flow(AuthFlow::Action::DeviceCode)->code(QStringLiteral("https://www.microsoft.com/link"), QStringLiteral("ABCD-EFGH"));
        rig.flow(AuthFlow::Action::Login)->fail(QStringLiteral("The browser sign-in could not start."));
        QVERIFY(!dlg.hasFailed());
        QCOMPARE(dlg.statusLabel()->text(), QStringLiteral("Waiting for you to confirm"));
        QCOMPARE(dlg.runningFlows(), 1);
    }

    // ---------------------------------------------------------------------------------------------- success, failure

    void successShowsTheProfileNameThenAccepts()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, false));
        showDialog(dlg);
        dlg.chooseType(AccountType::MSA);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        rig.flow(AuthFlow::Action::DeviceCode)->code(QStringLiteral("https://www.microsoft.com/link"), QStringLiteral("ZKTP-GLWR"));
        rig.flow(AuthFlow::Action::DeviceCode)->say(QStringLiteral("Checking your Xbox profile"));
        rig.flow(AuthFlow::Action::DeviceCode)->succeed(QStringLiteral("Steve"));
        QVERIFY(dlg.isSignedIn());
        QCOMPARE(dlg.statusLabel()->text(), QStringLiteral("Signed in as Steve"));
        QCOMPARE(accepted.count(), 0);  // the line stays for a moment
        // the browser flow, which did not finish, was stopped
        QCOMPARE(rig.flow(AuthFlow::Action::Login)->getState(), Task::State::AbortedByUser);
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 2000);
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QVERIFY(dlg.account());
        QCOMPARE(dlg.account()->accountType(), AccountType::MSA);
        QCOMPARE(dlg.account()->profileName(), QStringLiteral("Steve"));
    }

    void closingWhileSignedInKeepsTheAccount()
    {
        Rig rig;
        auto env = rig.env(true, false);
        env.successDelayMs = 60000;
        AddAccountDialog dlg(env);
        dlg.chooseType(AccountType::MSA);
        rig.flow(AuthFlow::Action::DeviceCode)->code(QStringLiteral("https://www.microsoft.com/link"), QStringLiteral("ZKTP-GLWR"));
        rig.flow(AuthFlow::Action::DeviceCode)->succeed(QStringLiteral("Steve"));
        QVERIFY(dlg.isSignedIn());
        QVERIFY(!dlg.backButton()->isEnabled());  // the account is real: no way back
        dlg.reject();                             // Esc / close during the "Signed in" moment
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QCOMPARE(dlg.account()->profileName(), QStringLiteral("Steve"));
    }

    void elySuccessReturnsAnElyAccount()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(false, true));
        dlg.chooseType(AccountType::Ely);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        auto* flow = rig.flow(AuthFlow::Action::DeviceCode);
        flow->code(QStringLiteral("https://ely.by/code?user_code=ABCD-EFGH"), QStringLiteral("ABCD-EFGH"));
        flow->succeed(QStringLiteral("Alex"));
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 2000);
        QCOMPARE(dlg.account()->accountType(), AccountType::Ely);
        QCOMPARE(dlg.account()->profileName(), QStringLiteral("Alex"));
    }

    void whenEveryFlowFailedTheMessageAndTryAgainShow()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, false));
        showDialog(dlg);
        dlg.chooseType(AccountType::MSA);
        QVERIFY(!dlg.retryButton()->isVisibleTo(&dlg));
        rig.flow(AuthFlow::Action::DeviceCode)->code(QStringLiteral("https://www.microsoft.com/link"), QStringLiteral("ZKTP-GLWR"));
        rig.flow(AuthFlow::Action::Login)->fail(QStringLiteral("We couldn't reach Microsoft.\nQNetworkReply::HostNotFoundError (raw detail)"));
        QVERIFY(!dlg.hasFailed());  // the code flow is still waiting
        rig.flow(AuthFlow::Action::DeviceCode)->fail(QStringLiteral("Device authorization failed: timeout"));
        QVERIFY(dlg.hasFailed());
        QCOMPARE(dlg.statusLabel()->text(), QStringLiteral("Device authorization failed: timeout"));
        QVERIFY(dlg.retryButton()->isVisibleTo(&dlg));
        QCOMPARE(dlg.retryButton()->text(), QStringLiteral("Try again"));
        QVERIFY(!dlg.codePanel()->isEnabled());  // greyed
        QVERIFY(!dlg.primaryButton()->isEnabled());
        QCOMPARE(dlg.statusLabel()->property("zeroTone").toString(), QStringLiteral("danger"));
        QCOMPARE(dlg.runningFlows(), 0);

        dlg.retryButton()->click();  // restarts both flows
        QCOMPARE(rig.count(AuthFlow::Action::Login), 2);
        QCOMPARE(rig.count(AuthFlow::Action::DeviceCode), 2);
        QVERIFY(rig.flow(AuthFlow::Action::Login, 1)->started);
        QVERIFY(rig.flow(AuthFlow::Action::DeviceCode, 1)->started);
        QVERIFY(!dlg.hasFailed());
        QVERIFY(!dlg.retryButton()->isVisibleTo(&dlg));
        QVERIFY(dlg.codePanel()->isEnabled());
        QVERIFY(dlg.qrText().isEmpty());
        QCOMPARE(dlg.runningFlows(), 2);
    }

    void aFailureAfterTheUserConfirmedIsTheAnswerAtOnce()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, false));
        dlg.chooseType(AccountType::MSA);
        auto* login = rig.flow(AuthFlow::Action::Login);
        login->browser(QStringLiteral("https://login.example.invalid/a"));
        login->say(QStringLiteral("Checking your Xbox profile"));
        login->fail(QStringLiteral("This Microsoft account does not own Minecraft."));
        QVERIFY(dlg.hasFailed());
        QCOMPARE(dlg.statusLabel()->text(), QStringLiteral("This Microsoft account does not own Minecraft."));
    }

    void aCodeThatExpiresFailsWithTryAgain()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(false, true));
        dlg.chooseType(AccountType::Ely);
        rig.flow(AuthFlow::Action::DeviceCode)->code(QStringLiteral("https://ely.by/code?user_code=ABCD-EFGH"), QStringLiteral("ABCD-EFGH"), 1);
        QVERIFY(!dlg.hasFailed());
        QTRY_VERIFY_WITH_TIMEOUT(dlg.hasFailed(), 3000);
        QCOMPARE(dlg.statusLabel()->text(), QStringLiteral("The code expired. Try again."));
        QCOMPARE(dlg.runningFlows(), 0);
    }

    // ---------------------------------------------------------------------------------------------- offline

    void offlineNameRulesAreTheOldDialogs()
    {
        QVERIFY(AddAccountDialog::isValidOfflineName(QStringLiteral("Steve_01")));
        QVERIFY(AddAccountDialog::isValidOfflineName(QStringLiteral("abc")));
        QVERIFY(AddAccountDialog::isValidOfflineName(QStringLiteral("abcdefghijklmnop")));  // 16
        QVERIFY(!AddAccountDialog::isValidOfflineName(QStringLiteral("ab")));
        QVERIFY(!AddAccountDialog::isValidOfflineName(QStringLiteral("abcdefghijklmnopq")));  // 17
        QVERIFY(!AddAccountDialog::isValidOfflineName(QStringLiteral("Steve 01")));
        QVERIFY(!AddAccountDialog::isValidOfflineName(QStringLiteral("Stevé")));
        QVERIFY(!AddAccountDialog::isValidOfflineName(QStringLiteral("Steve\n")));
    }

    void offlineAddsAnOfflineAccountOnlyForAValidName()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, true));
        showDialog(dlg);
        dlg.chooseType(AccountType::Offline);
        QCOMPARE(dlg.page(), AddAccountDialog::Page::Offline);
        QVERIFY(!dlg.addButton()->isEnabled());
        QCOMPARE(dlg.addButton()->text(), QStringLiteral("Add account"));
        QCOMPARE(dlg.focusWidget(), static_cast<QWidget*>(dlg.nameField()));
        QTest::keyClicks(dlg.nameField(), QStringLiteral("ab"));
        QVERIFY(!dlg.addButton()->isEnabled());
        QCOMPARE(dlg.nameHint()->property("zeroTone").toString(), QStringLiteral("danger"));
        QVERIFY(dlg.nameHint()->text().contains(QStringLiteral("from 3 to 16 characters")));
        QTest::keyClicks(dlg.nameField(), QStringLiteral("c_9"));
        QVERIFY(dlg.addButton()->isEnabled());
        QCOMPARE(dlg.nameHint()->property("zeroTone").toString(), QStringLiteral("secondary"));
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        QTest::keyClick(dlg.nameField(), Qt::Key_Return);  // Enter adds
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(rig.offlineNames, QStringList{ QStringLiteral("abc_9") });
        QCOMPARE(dlg.account()->accountType(), AccountType::Offline);
        QCOMPARE(dlg.account()->profileName(), QStringLiteral("abc_9"));
    }

    void allowInvalidUsernamesWarnsButAllows()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, true));
        dlg.chooseType(AccountType::Offline);
        dlg.nameField()->setText(QStringLiteral("Stevé Smith"));
        QVERIFY(!dlg.addButton()->isEnabled());
        dlg.allowInvalidBox()->setChecked(true);
        QVERIFY(dlg.addButton()->isEnabled());
        QCOMPARE(dlg.nameHint()->text(), QStringLiteral("An invalid username may prevent joining servers and singleplayer worlds."));
        QCOMPARE(dlg.nameHint()->property("zeroTone").toString(), QStringLiteral("warning"));
        dlg.nameField()->clear();
        QVERIFY(!dlg.addButton()->isEnabled());  // never an empty name
    }

    void offlineNeedsAnAccountThatOwnsMinecraft()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, true, /*offlineAllowed*/ false));
        dlg.chooseType(AccountType::Offline);
        QVERIFY(dlg.optionCard(AccountType::Offline));  // the card stays; step 2 says why
        QVERIFY(!dlg.nameField()->isEnabled());
        QVERIFY(!dlg.addButton()->isEnabled());
        QVERIFY(dlg.nameHint()->text().contains(QStringLiteral("owns Minecraft")));
        dlg.addButton()->click();
        QVERIFY(rig.offlineNames.isEmpty());
    }

    void backFromOfflineClearsTheName()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, true));
        dlg.chooseType(AccountType::Offline);
        dlg.nameField()->setText(QStringLiteral("Steve"));
        dlg.backButton()->click();
        QCOMPARE(dlg.page(), AddAccountDialog::Page::Choose);
        dlg.chooseType(AccountType::Offline);
        QVERIFY(dlg.nameField()->text().isEmpty());
    }

    // ---------------------------------------------------------------------------------------------- run(parent, type)

    void opensDirectlyAtStepTwoWithoutBack()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(true, true), AccountType::Ely);
        showDialog(dlg);
        QCOMPARE(dlg.page(), AddAccountDialog::Page::SignIn);
        QCOMPARE(dlg.accountType(), AccountType::Ely);
        QCOMPARE(dlg.titleLabel()->text(), QStringLiteral("Sign in with Ely.by"));
        QVERIFY(!dlg.backButton()->isVisibleTo(&dlg));
        QVERIFY(!dlg.overlineLabel()->isVisibleTo(&dlg));
        QCOMPARE(rig.count(AuthFlow::Action::DeviceCode), 1);
        QCOMPARE(rig.count(AuthFlow::Action::Login), 0);
        dlg.chooseStep();  // nothing to go back to
        QCOMPARE(dlg.page(), AddAccountDialog::Page::SignIn);

        AddAccountDialog microsoft(rig.env(true, true), AccountType::MSA);
        QCOMPARE(microsoft.page(), AddAccountDialog::Page::SignIn);
        QCOMPARE(microsoft.titleLabel()->text(), QStringLiteral("Sign in with Microsoft"));
        QVERIFY(!microsoft.backButton()->isVisibleTo(&microsoft));
    }

    // ---------------------------------------------------------------------------------- Ely.by with email and password

    void theElyCardShowsWithoutAClientIdAndOpensThePasswordForm()
    {
        Rig rig;
        auto e = rig.env(false, true);
        e.elyCode = false;  // no Launcher_ELY_CLIENT_ID
        AddAccountDialog dlg(e);
        showDialog(dlg);
        QCOMPARE(dlg.offeredTypes(), (QList<AccountType>{ AccountType::Ely, AccountType::Offline }));
        QVERIFY(dlg.optionCard(AccountType::Ely));
        QVERIFY(texts(&dlg).contains(QStringLiteral("Free account with skins. Servers that accept Ely.by.")));

        dlg.chooseType(AccountType::Ely);
        QCOMPARE(dlg.page(), AddAccountDialog::Page::Password);
        QCOMPARE(dlg.accountType(), AccountType::Ely);
        QCOMPARE(rig.flows.size(), 0);  // nothing runs until the user signs in
        QCOMPARE(dlg.overlineLabel()->text().toUpper(), QStringLiteral("STEP 2 OF 2"));
        QCOMPARE(dlg.titleLabel()->text(), QStringLiteral("Sign in with Ely.by"));
        QVERIFY(dlg.backButton()->isVisibleTo(&dlg));
        const QStringList all = texts(&dlg);
        QVERIFY(all.contains(QStringLiteral("Email or username")));
        QVERIFY(all.contains(QStringLiteral("Password")));
        QVERIFY(all.contains(QStringLiteral("ZERO sends your password only to Ely.by and never saves it.")));
        QCOMPARE(dlg.submitButton()->text(), QStringLiteral("Sign in"));
        QCOMPARE(dlg.createAccountLink()->text(), QStringLiteral("Create an Ely.by account"));
        QCOMPARE(dlg.forgotPasswordLink()->text(), QStringLiteral("Forgot password?"));
        QVERIFY(!dlg.twoFactorVisible());
        QVERIFY(!dlg.twoFactorField()->isVisibleTo(&dlg));
        QVERIFY(!dlg.useCodeLink()->isVisibleTo(&dlg));  // no client ID, no code flow to go back to
        QCOMPARE(dlg.passwordField()->echoMode(), QLineEdit::Password);
        QCOMPARE(dlg.width(), AddAccountDialog::kWidth);
    }

    void theEyeShowsAndHidesThePassword()
    {
        Rig rig;
        auto e = rig.env(false, true);
        e.elyCode = false;
        AddAccountDialog dlg(e);
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);
        dlg.passwordField()->setText(QStringLiteral("hunter2-sample"));
        QVERIFY(dlg.showPasswordAction());
        dlg.showPasswordAction()->setChecked(true);
        QCOMPARE(dlg.passwordField()->echoMode(), QLineEdit::Normal);
        dlg.showPasswordAction()->setChecked(false);
        QCOMPARE(dlg.passwordField()->echoMode(), QLineEdit::Password);
    }

    void theFooterLinksOpenElyPages()
    {
        Rig rig;
        auto e = rig.env(false, true);
        e.elyCode = false;
        AddAccountDialog dlg(e);
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);
        dlg.createAccountLink()->click();
        dlg.forgotPasswordLink()->click();
        QCOMPARE(rig.opened.size(), 2);
        QCOMPARE(rig.opened.at(0).toString(), QStringLiteral("https://account.ely.by/register"));
        QCOMPARE(rig.opened.at(1).toString(), QStringLiteral("https://account.ely.by/forgot-password"));
    }

    void enterSubmitsAndThePasswordFieldIsEmptiedAtOnce()
    {
        Rig rig;
        auto e = rig.env(false, true);
        e.elyCode = false;
        AddAccountDialog dlg(e);
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);
        QVERIFY(dlg.userField()->hasFocus());
        dlg.userField()->setText(QStringLiteral("steve@example.com"));
        dlg.passwordField()->setText(QStringLiteral("correct horse"));
        dlg.passwordField()->setFocus();

        QTest::keyClick(dlg.passwordField(), Qt::Key_Return);  // Enter submits
        QCOMPARE(rig.flows.size(), 1);
        auto* flow = rig.flow(AuthFlow::Action::Login);
        QVERIFY(flow && flow->started);
        QCOMPARE(flow->credentialCalls, 1);  // not twice (the line edit and the default button both see Enter)
        QCOMPARE(flow->user, QStringLiteral("steve@example.com"));
        QCOMPARE(flow->pass, QStringLiteral("correct horse"));
        QCOMPARE(flow->twoFactor, QString());
        QVERIFY(flow->realData()->type == AccountType::Ely);

        QVERIFY(dlg.passwordField()->text().isEmpty());  // cleared right after submit
        QVERIFY(dlg.isSigningIn());
        QVERIFY(!dlg.userField()->isEnabled());
        QVERIFY(!dlg.passwordField()->isEnabled());
        QVERIFY(!dlg.submitButton()->isEnabled());
        QCOMPARE(dlg.passwordStatusLabel()->text(), QString::fromUtf8("Signing in\xE2\x80\xA6"));
        // the password is nowhere on screen
        for (const QString& text : texts(&dlg))
            QVERIFY2(!text.contains(QStringLiteral("correct horse")), "the password must never be displayed");
    }

    void aSuccessfulPasswordSignInShowsTheNameAndAccepts()
    {
        Rig rig;
        auto e = rig.env(false, true);
        e.elyCode = false;
        AddAccountDialog dlg(e);
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);
        dlg.userField()->setText(QStringLiteral("steve"));
        dlg.passwordField()->setText(QStringLiteral("pw-sample"));
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        dlg.submitButton()->click();
        rig.flow(AuthFlow::Action::Login)->succeed(QStringLiteral("Steve"));
        QVERIFY(dlg.isSignedIn());
        QCOMPARE(dlg.passwordStatusLabel()->text(), QStringLiteral("Signed in as Steve"));
        QCOMPARE(accepted.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 2000);
        QVERIFY(dlg.account());
        QCOMPARE(dlg.account()->accountType(), AccountType::Ely);
        QCOMPARE(dlg.account()->profileName(), QStringLiteral("Steve"));
    }

    void wrongCredentialsAndNetworkAndOtherMessagesHaveTheirTexts()
    {
        using Zero::Auth::Ely::Authserver::Failure;
        Rig rig;
        auto e = rig.env(false, true);
        e.elyCode = false;
        AddAccountDialog dlg(e);
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);

        struct Case {
            Failure failure;
            QString reason;
            QString expected;
        };
        const QList<Case> cases = {
            { Failure::WrongCredentials, QStringLiteral("whatever"), QStringLiteral("Wrong email or password.") },
            { Failure::Network, QStringLiteral("Failed to reach Ely.by: timeout"),
              QStringLiteral("We couldn't reach Ely.by. Check your connection and try again.") },
            { Failure::Refused, QStringLiteral("This account is temporarily blocked.\nSecond line"),
              QStringLiteral("This account is temporarily blocked.") },
        };
        int n = 0;
        for (const Case& c : cases) {
            dlg.userField()->setText(QStringLiteral("steve"));
            dlg.passwordField()->setText(QStringLiteral("pw-sample"));
            dlg.submitButton()->click();
            QCOMPARE(rig.flows.size(), ++n);
            rig.flows.last()->failElyPassword(c.failure, c.reason);
            QVERIFY(dlg.hasFailed());
            QVERIFY(!dlg.isSigningIn());
            QCOMPARE(dlg.passwordStatusLabel()->text(), c.expected);
            // the form is usable again and the password was not kept
            QVERIFY(dlg.userField()->isEnabled());
            QVERIFY(dlg.passwordField()->isEnabled());
            QVERIFY(dlg.submitButton()->isEnabled());
            QVERIFY(dlg.passwordField()->text().isEmpty());
            QVERIFY(!dlg.twoFactorVisible());
        }
        // an empty password does not start another sign-in
        dlg.passwordField()->clear();
        dlg.submitButton()->click();
        QCOMPARE(rig.flows.size(), n);
    }

    void twoFactorAsksForTheCodeAndRetriesWithTheSamePassword()
    {
        using Zero::Auth::Ely::Authserver::Failure;
        Rig rig;
        auto e = rig.env(false, true);
        e.elyCode = false;
        AddAccountDialog dlg(e);
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);
        dlg.userField()->setText(QStringLiteral("steve"));
        dlg.passwordField()->setText(QStringLiteral("pw-sample"));
        dlg.submitButton()->click();
        QCOMPARE(rig.flows.last()->twoFactor, QString());

        rig.flows.last()->failElyPassword(Failure::TwoFactorRequired, QStringLiteral("protected"));
        QVERIFY(!dlg.isSigningIn());
        QVERIFY(!dlg.hasFailed());  // not an error
        QVERIFY(dlg.twoFactorVisible());
        QTRY_VERIFY(dlg.twoFactorField()->isVisibleTo(&dlg));
        QCOMPARE(dlg.twoFactorCaption()->text(), QStringLiteral("Enter the code from your authenticator app."));
        QVERIFY(texts(&dlg).contains(QStringLiteral("Two-factor code")));
        QVERIFY(dlg.twoFactorField()->hasFocus());
        QVERIFY(dlg.passwordField()->text().isEmpty());
        QVERIFY(dlg.passwordStatusLabel()->text().isEmpty());

        // letters and extra digits do not get in; 6 digits at most
        QTest::keyClicks(dlg.twoFactorField(), QStringLiteral("12ab34567"));
        QCOMPARE(dlg.twoFactorField()->text(), QStringLiteral("123456"));

        // a wrong code
        QTest::keyClick(dlg.twoFactorField(), Qt::Key_Return);
        QCOMPARE(rig.flows.size(), 2);
        QCOMPARE(rig.flows.last()->user, QStringLiteral("steve"));
        QCOMPARE(rig.flows.last()->pass, QStringLiteral("pw-sample"));  // the password of the first attempt, not retyped
        QCOMPARE(rig.flows.last()->twoFactor, QStringLiteral("123456"));
        rig.flows.last()->failElyPassword(Failure::WrongCode, QStringLiteral("x"));
        QCOMPARE(dlg.passwordStatusLabel()->text(), QStringLiteral("That code didn't work. Try again."));
        QVERIFY(dlg.twoFactorVisible());
        QVERIFY(dlg.twoFactorField()->text().isEmpty());
        QVERIFY(dlg.twoFactorField()->hasFocus());

        // the right code
        QTest::keyClicks(dlg.twoFactorField(), QStringLiteral("654321"));
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        dlg.submitButton()->click();
        QCOMPARE(rig.flows.size(), 3);
        QCOMPARE(rig.flows.last()->pass, QStringLiteral("pw-sample"));
        QCOMPARE(rig.flows.last()->twoFactor, QStringLiteral("654321"));
        rig.flows.last()->succeed(QStringLiteral("Steve"));
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 2000);
    }

    void aCodeIsNeverSentWithoutDigitsWhenTheFieldIsOpen()
    {
        using Zero::Auth::Ely::Authserver::Failure;
        Rig rig;
        auto e = rig.env(false, true);
        e.elyCode = false;
        AddAccountDialog dlg(e);
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);
        dlg.userField()->setText(QStringLiteral("steve"));
        dlg.passwordField()->setText(QStringLiteral("pw-sample"));
        dlg.submitButton()->click();
        rig.flows.last()->failElyPassword(Failure::TwoFactorRequired);
        dlg.submitButton()->click();  // the code field is empty: nothing is sent
        QCOMPARE(rig.flows.size(), 1);
        QVERIFY(dlg.twoFactorField()->hasFocus());
    }

    void leavingTheFormForgetsThePasswordOfATwoFactorSignIn()
    {
        using Zero::Auth::Ely::Authserver::Failure;
        Rig rig;
        auto e = rig.env(false, true);
        e.elyCode = false;
        AddAccountDialog dlg(e);
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);
        dlg.userField()->setText(QStringLiteral("steve"));
        dlg.passwordField()->setText(QStringLiteral("pw-sample"));
        dlg.submitButton()->click();
        rig.flows.last()->failElyPassword(Failure::TwoFactorRequired);
        dlg.chooseStep();
        dlg.chooseType(AccountType::Ely);
        QVERIFY(!dlg.twoFactorVisible());
        QTest::keyClicks(dlg.passwordField(), QStringLiteral("typed-again"));
        dlg.submitButton()->click();
        QCOMPARE(rig.flows.size(), 2);
        QCOMPARE(rig.flows.last()->pass, QStringLiteral("typed-again"));
        QCOMPARE(rig.flows.last()->twoFactor, QString());
    }

    void backAbortsARunningPasswordSignIn()
    {
        Rig rig;
        auto e = rig.env(false, true);
        e.elyCode = false;
        AddAccountDialog dlg(e);
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);
        dlg.userField()->setText(QStringLiteral("steve"));
        dlg.passwordField()->setText(QStringLiteral("pw-sample"));
        dlg.submitButton()->click();
        QCOMPARE(dlg.runningFlows(), 1);
        dlg.backButton()->click();
        QCOMPARE(dlg.page(), AddAccountDialog::Page::Choose);
        QCOMPARE(dlg.runningFlows(), 0);
        QCOMPARE(rig.flow(AuthFlow::Action::Login)->getState(), Task::State::AbortedByUser);
        QVERIFY(!dlg.account());
    }

    void withAClientIdTheCodeStepComesFirstAndTheLinkSwitchesToThePasswordForm()
    {
        Rig rig;
        AddAccountDialog dlg(rig.env(false, true));  // client ID configured
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);
        QCOMPARE(dlg.page(), AddAccountDialog::Page::SignIn);
        QCOMPARE(rig.count(AuthFlow::Action::DeviceCode), 1);
        QVERIFY(dlg.usePasswordLink()->isVisibleTo(&dlg));
        QCOMPARE(dlg.usePasswordLink()->text(), QStringLiteral("Use email and password instead"));
        // Microsoft's step has no such link
        Rig rig2;
        AddAccountDialog dlg2(rig2.env(true, true));
        showDialog(dlg2);
        dlg2.chooseType(AccountType::MSA);
        QVERIFY(!dlg2.usePasswordLink()->isVisibleTo(&dlg2));

        dlg.usePasswordLink()->click();
        QCOMPARE(dlg.page(), AddAccountDialog::Page::Password);
        QCOMPARE(dlg.runningFlows(), 0);  // the code flow stopped
        QCOMPARE(rig.flow(AuthFlow::Action::DeviceCode)->getState(), Task::State::AbortedByUser);
        QVERIFY(dlg.useCodeLink()->isVisibleTo(&dlg));
        QCOMPARE(dlg.useCodeLink()->text(), QStringLiteral("Use a code instead"));

        dlg.useCodeLink()->click();
        QCOMPARE(dlg.page(), AddAccountDialog::Page::SignIn);
        QCOMPARE(rig.count(AuthFlow::Action::DeviceCode), 2);  // a new code flow
        QCOMPARE(dlg.runningFlows(), 1);
    }

    void signingInAgainPrefillsTheUserNameAndOpensThePasswordFormEvenWithAClientId()
    {
        Rig rig;
        AddAccountDialog::SignInHint hint;
        hint.username = QStringLiteral("steve@example.com");
        hint.password = true;
        AddAccountDialog dlg(rig.env(false, true), AccountType::Ely, hint);  // client ID configured, account signs in with a password
        showDialog(dlg);
        QCOMPARE(dlg.page(), AddAccountDialog::Page::Password);
        QCOMPARE(dlg.userField()->text(), QStringLiteral("steve@example.com"));
        QVERIFY(dlg.passwordField()->hasFocus());  // the name is there: the cursor goes to the password
        QVERIFY(!dlg.backButton()->isVisibleTo(&dlg));
        QVERIFY(!dlg.overlineLabel()->isVisibleTo(&dlg));
        QCOMPARE(rig.flows.size(), 0);

        // an OAuth account (no hint) with a client ID starts at the code step
        AddAccountDialog oauth(rig.env(false, true), AccountType::Ely);
        QCOMPARE(oauth.page(), AddAccountDialog::Page::SignIn);

        // without a client ID the password form is the only way, hint or not
        auto e = rig.env(false, true);
        e.elyCode = false;
        AddAccountDialog noId(e, AccountType::Ely);
        showDialog(noId);
        QCOMPARE(noId.page(), AddAccountDialog::Page::Password);
        QVERIFY(noId.userField()->hasFocus());
    }

    void closingAPasswordSignInInTheMiddleAbortsIt()
    {
        Rig rig;
        auto e = rig.env(false, true);
        e.elyCode = false;
        AddAccountDialog dlg(e);
        showDialog(dlg);
        dlg.chooseType(AccountType::Ely);
        dlg.userField()->setText(QStringLiteral("steve"));
        dlg.passwordField()->setText(QStringLiteral("pw-sample"));
        dlg.submitButton()->click();
        dlg.reject();
        QCOMPARE(dlg.runningFlows(), 0);
        QCOMPARE(rig.flow(AuthFlow::Action::Login)->getState(), Task::State::AbortedByUser);
    }

    void bothThemesBuildAndKeepTheirFonts()
    {
        for (const ThemeSpec& spec : { ThemeSpec::obsidian(), ThemeSpec::minecraft() }) {
            Theme::setCurrent(spec);
            Rig rig;
            AddAccountDialog dlg(rig.env(true, true));
            showDialog(dlg);
            dlg.chooseType(AccountType::MSA);
            rig.flow(AuthFlow::Action::DeviceCode)->code(QStringLiteral("https://www.microsoft.com/link"), QStringLiteral("ZKTP-GLWR"));
            QCOMPARE(dlg.width(), AddAccountDialog::kWidth);
            QCOMPARE(dlg.codeLabel()->font().pixelSize(), 20);
            if (spec.usesPixelFont(Role::Title))
                QCOMPARE(dlg.codeLabel()->font().families().value(0), QStringLiteral("ZERO Pixel"));
        }
    }
};

QTEST_MAIN(ZeroAddAccountTest)
#include "ZeroAddAccount_test.moc"
