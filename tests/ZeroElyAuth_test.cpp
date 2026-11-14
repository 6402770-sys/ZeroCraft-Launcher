// SPDX-License-Identifier: GPL-3.0-only
// Ely.by accounts: authlib-injector (latest.json parsing, SHA-256, the -javaagent argument, the download task and its cache rules),
// the Ely account type (save/load, old files still load, unknown types do not crash), the AuthFlow step lists, the token requests
// (client_secret only when configured) and the launch session flag. See docs/zero/ARCHITECTURE.md, "Ely.by accounts".
//
// Ely.by password sign-in (authserver bodies / answers, the authenticate + refresh steps and their two-factor, wrong-credential and
// network outcomes, the account data round trip - never the password) runs on a fake transport.
//
// A live check of the authlib-injector download runs when ZERO_LIVE_NETWORK_TESTS=1 (it fetches the real latest.json and jar).
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>
#include <functional>

#include "BuildConfig.h"
#include "minecraft/auth/AccountData.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/AuthFlow.h"
#include "minecraft/auth/AuthSession.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/auth/steps/ElyAuthserverRefreshStep.h"
#include "minecraft/auth/steps/ElyDeviceCodeStep.h"
#include "minecraft/auth/steps/ElyPasswordStep.h"
#include "minecraft/auth/steps/ElyRefreshStep.h"
#include "minecraft/auth/steps/MSADeviceCodeStep.h"
#include "zero/auth/AuthlibInjector.h"
#include "zero/auth/ElyAuth.h"
#include "zero/auth/ElyAuthserver.h"
#include "zero/auth/ElyAuthserverNet.h"

using namespace Zero::Auth;
using namespace Zero::Auth::AuthlibInjector;

namespace {

QByteArray readTestFile(const QString& name)
{
    QFile f(QFINDTESTDATA("testdata/ZeroElyAuth/" + name));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}

QByteArray latestJson(const QString& version, const QByteArray& jarBytes)
{
    QJsonObject o;
    o["version"] = version;
    o["download_url"] = "https://authlib-injector.yushi.moe/artifact/1/authlib-injector-" + version + ".jar";
    o["checksums"] = QJsonObject{ { "sha256", sha256Hex(jarBytes) } };
    return QJsonDocument(o).toJson();
}

QByteArray latestWith(const std::function<void(QJsonObject&)>& edit)
{
    auto doc = QJsonDocument::fromJson(readTestFile("latest.json"));
    auto o = doc.object();
    edit(o);
    return QJsonDocument(o).toJson();
}

/// Scripted network for the task.
class FakeFetcher : public Fetcher {
   public:
    bool latestOk = true;
    QByteArray latestBody;
    QString latestError = "no route to host";
    bool downloadOk = true;
    QByteArray jarBytes;
    QString downloadError = "connection reset";
    int getCalls = 0;
    int downloadCalls = 0;

    void getBytes(const QUrl&, std::function<void(bool, QByteArray, QString)> done) override
    {
        ++getCalls;
        done(latestOk, latestBody, latestError);
    }
    void downloadFile(const QUrl&, const QString& path, const QString&, std::function<void(bool, QString)> done) override
    {
        ++downloadCalls;
        if (!downloadOk) {
            done(false, downloadError);
            return;
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            done(false, "cannot write");
            return;
        }
        f.write(jarBytes);
        f.close();
        done(true, QString());
    }
};

/// Plain QNetworkAccessManager (the production fetcher needs the running Application); only for the live check.
class LiveFetcher : public Fetcher {
   public:
    void getBytes(const QUrl& url, std::function<void(bool, QByteArray, QString)> done) override
    {
        auto* reply = m_nam.get(request(url));
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done] {
            const bool ok = reply->error() == QNetworkReply::NoError;
            done(ok, reply->readAll(), reply->errorString());
            reply->deleteLater();
        });
    }
    void downloadFile(const QUrl& url, const QString& path, const QString&, std::function<void(bool, QString)> done) override
    {
        auto* reply = m_nam.get(request(url));
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, path, done] {
            if (reply->error() != QNetworkReply::NoError) {
                done(false, reply->errorString());
            } else {
                QFile f(path);
                if (f.open(QIODevice::WriteOnly) && f.write(reply->readAll()) >= 0) {
                    f.close();
                    done(true, QString());
                } else {
                    done(false, "cannot write");
                }
            }
            reply->deleteLater();
        });
    }

   private:
    static QNetworkRequest request(const QUrl& url)
    {
        QNetworkRequest r(url);
        r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        r.setTransferTimeout(60000);
        return r;
    }
    QNetworkAccessManager m_nam;
};

/// Runs the task to its end (the fakes finish inside start(); the live fetcher needs the event loop).
void runTask(Task& task, int timeoutMs = 120000)
{
    QEventLoop loop;
    QObject::connect(&task, &Task::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    task.start();
    if (!task.isFinished()) {
        loop.exec();
    }
}

QJsonObject profileJson(const QString& id, const QString& name)
{
    return QJsonObject{ { "id", id },
                        { "name", name },
                        { "skin", QJsonObject{ { "id", "" }, { "url", "" }, { "variant", "" } } },
                        { "capes", QJsonArray() } };
}

QJsonObject elyAccountJson(const QString& profileId, const QString& name)
{
    return QJsonObject{ { "type", "Ely" },
                        { "msa-client-id", "client-xyz" },
                        { "msa", QJsonObject{ { "token", "access-1" }, { "refresh_token", "refresh-1" } } },
                        { "ygg", QJsonObject{ { "token", "access-1" } } },
                        { "profile", profileJson(profileId, name) },
                        { "entitlement", QJsonObject{ { "ownsMinecraft", true }, { "canPlayMinecraft", true } } } };
}

QJsonObject msaAccountJson()
{
    return QJsonObject{ { "type", "MSA" },
                        { "msa-client-id", "c36a9fb6-4f2a-41ff-90bd-ae7cc92031eb" },
                        { "msa", QJsonObject{ { "token", "m" }, { "refresh_token", "mr" } } },
                        { "utoken", QJsonObject{ { "token", "u" } } },
                        { "xrp-mc", QJsonObject{ { "token", "x" } } },
                        { "ygg", QJsonObject{ { "token", "y" } } },
                        { "profile", profileJson("11111111111111111111111111111111", "MsaPlayer") },
                        { "entitlement", QJsonObject{ { "ownsMinecraft", true }, { "canPlayMinecraft", true } } } };
}

QJsonObject offlineAccountJson()
{
    return QJsonObject{ { "type", "Offline" },
                        { "ygg", QJsonObject{ { "token", "0" }, { "extra", QJsonObject{ { "userName", "Bob" }, { "clientToken", "abc" } } } } },
                        { "profile", profileJson("22222222222222222222222222222222", "Bob") } };
}

QUrlQuery parseForm(const QByteArray& body)
{
    return QUrlQuery(QString::fromUtf8(body));
}

/// The authserver on a script: every request is recorded and answered at once with the next scripted reply.
struct FakeAuthserver {
    QList<QUrl> urls;
    QList<QByteArray> bodies;
    QList<Ely::Authserver::HttpReply> script;

    Ely::Authserver::Transport transport()
    {
        return [this](const QUrl& url, const QByteArray& body, Ely::Authserver::Done done) -> Ely::Authserver::Cancel {
            urls << url;
            bodies << body;
            Ely::Authserver::HttpReply reply;
            if (!script.isEmpty())
                reply = script.takeFirst();
            done(reply);
            return {};
        };
    }
};

Ely::Authserver::HttpReply textReply(int status, const QByteArray& body)
{
    Ely::Authserver::HttpReply reply;
    reply.answered = true;
    reply.status = status;
    reply.body = body;
    return reply;
}

Ely::Authserver::HttpReply jsonReply(int status, const QJsonObject& object)
{
    return textReply(status, QJsonDocument(object).toJson(QJsonDocument::Compact));
}

/// What a step reported.
struct StepResult {
    bool done = false;
    AccountTaskState state = AccountTaskState::STATE_CREATED;
    QString message;
    Ely::Authserver::Failure failure = Ely::Authserver::Failure::None;

    void watch(AuthStep& step)
    {
        QObject::connect(&step, &AuthStep::finished, &step, [this](AccountTaskState s, QString m) {
            done = true;
            state = s;
            message = m;
        });
        if (auto* password = qobject_cast<ElyPasswordStep*>(&step))
            QObject::connect(password, &ElyPasswordStep::failure, password, [this](Ely::Authserver::Failure f) { failure = f; });
    }
};

/// MSADeviceCodeStep without the running Application (its protected constructor takes the client ID).
class TestMsaDeviceStep : public MSADeviceCodeStep {
   public:
    explicit TestMsaDeviceStep(AccountData* data) : MSADeviceCodeStep(data, "client-id") {}
};

}  // namespace

class ZeroElyAuthTest : public QObject {
    Q_OBJECT

    // ------------------------------------------------------------------------------------------------ latest.json

   private slots:
    void parseLatest_realFile()
    {
        const auto json = readTestFile("latest.json");
        QVERIFY(!json.isEmpty());
        const auto a = parseLatest(json);
        QVERIFY2(a.has_value(), qPrintable(a ? QString() : a.error()));
        QVERIFY(!a->version.isEmpty());
        QCOMPARE(a->downloadUrl.scheme(), QString("https"));
        QVERIFY(a->downloadUrl.path().endsWith(".jar"));
        QCOMPARE(a->sha256.size(), 64);
        QCOMPARE(a->sha256, a->sha256.toLower());
        QCOMPARE(latestUrl().toString(), QString("https://authlib-injector.yushi.moe/artifact/latest.json"));
    }

    void parseLatest_rejectsBadInput()
    {
        QVERIFY(!parseLatest(QByteArray()));
        QVERIFY(!parseLatest("not json"));
        QVERIFY(!parseLatest("[]"));
        QVERIFY(!parseLatest("{}"));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o.remove("version"); })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["version"] = ""; })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["version"] = 12; })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["version"] = "../evil"; })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["version"] = "1.2/3"; })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["version"] = "1..2"; })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o.remove("download_url"); })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["download_url"] = "http://authlib-injector.yushi.moe/a.jar"; })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["download_url"] = "file:///C:/a.jar"; })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["download_url"] = "not a url"; })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o.remove("checksums"); })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["checksums"] = QJsonObject(); })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["checksums"] = QJsonObject{ { "sha256", "abc123" } }; })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["checksums"] = QJsonObject{ { "sha256", QString(64, 'z') } }; })));
        QVERIFY(!parseLatest(latestWith([](QJsonObject& o) { o["checksums"] = QJsonObject{ { "sha1", QString(40, 'a') } }; })));
    }

    // ------------------------------------------------------------------------------------------------- hashing, argument

    void sha256_checks()
    {
        const QByteArray data("authlib-injector");
        const auto hex = sha256Hex(data);
        QCOMPARE(hex.size(), 64);
        QVERIFY(sha256Matches(data, hex));
        QVERIFY(sha256Matches(data, hex.toUpper()));
        QVERIFY(!sha256Matches(data + "x", hex));       // content changed
        QVERIFY(!sha256Matches(data, QString(64, '0')));  // mismatch
        QVERIFY(!sha256Matches(data, QString()));        // an empty expectation never matches
        QVERIFY(!sha256Matches(data, hex.left(63)));
        // known vector: sha256("abc")
        QCOMPARE(sha256Hex("abc"), QString("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("f.bin");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
        f.close();
        QVERIFY(fileMatchesSha256(path, hex));
        QVERIFY(!fileMatchesSha256(path, QString(64, '1')));
        QVERIFY(!fileMatchesSha256(dir.filePath("missing"), hex));
        QVERIFY(!fileSha256(dir.filePath("missing")));
    }

    void javaAgentArgument_text()
    {
        QCOMPARE(javaAgentArgument("C:/Users/Me/AppData/Roaming/ZERO/zero/authlib-injector/authlib-injector-1.2.8.jar"),
                 QString("-javaagent:C:/Users/Me/AppData/Roaming/ZERO/zero/authlib-injector/authlib-injector-1.2.8.jar=ely.by"));
        // a path with spaces stays ONE argument (QProcess quotes it), no extra quoting is added here
        QCOMPARE(javaAgentArgument("C:/Program Files/x.jar"), QString("-javaagent:C:/Program Files/x.jar=ely.by"));
        QCOMPARE(javaAgentArgument("/a/b.jar", "example.org"), QString("-javaagent:/a/b.jar=example.org"));
        QCOMPARE(elyApiServer(), QString("ely.by"));
    }

    void cacheLayout()
    {
        QCOMPARE(cacheDirectory("/data"), QString("/data/zero/authlib-injector"));
        QCOMPARE(jarFileName("1.2.8"), QString("authlib-injector-1.2.8.jar"));
        QCOMPARE(jarPath("/d", "1.2.8"), QString("/d/authlib-injector-1.2.8.jar"));
        QVERIFY(failureMessage("boom").startsWith("Ely.by accounts need authlib-injector, which couldn't be downloaded: boom"));
    }

    // ------------------------------------------------------------------------------------------------------- the task

    void task_downloadsVerifiesAndReuses()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray jar = "PK-fake-jar-bytes-1.2.8";

        {
            auto fake = std::make_unique<FakeFetcher>();
            auto* f = fake.get();
            f->latestBody = latestJson("1.2.8", jar);
            f->jarBytes = jar;
            AuthlibInjectorTask task(dir.path(), std::move(fake));
            runTask(task);
            QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
            QVERIFY(task.downloaded());
            QVERIFY(!task.usedCacheAfterFailure());
            QCOMPARE(task.version(), QString("1.2.8"));
            QCOMPARE(task.jarPath(), jarPath(dir.path(), "1.2.8"));
            QCOMPARE(f->downloadCalls, 1);
            QVERIFY(fileMatchesSha256(task.jarPath(), sha256Hex(jar)));
            QVERIFY(!QFile::exists(task.jarPath() + ".part"));  // renamed, not left behind
            QVERIFY(QFile::exists(task.jarPath() + ".sha256"));
        }
        {
            // second run: latest.json is fetched, the jar is already there and valid -> no download
            auto fake = std::make_unique<FakeFetcher>();
            auto* f = fake.get();
            f->latestBody = latestJson("1.2.8", jar);
            f->downloadOk = false;
            AuthlibInjectorTask task(dir.path(), std::move(fake));
            runTask(task);
            QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
            QCOMPARE(f->getCalls, 1);
            QCOMPARE(f->downloadCalls, 0);
            QVERIFY(!task.downloaded());
            QVERIFY(!task.usedCacheAfterFailure());
        }
        {
            // a corrupted cached jar of the current version is replaced, not trusted
            QFile bad(jarPath(dir.path(), "1.2.8"));
            QVERIFY(bad.open(QIODevice::WriteOnly | QIODevice::Truncate));
            bad.write("corrupt");
            bad.close();
            auto fake = std::make_unique<FakeFetcher>();
            auto* f = fake.get();
            f->latestBody = latestJson("1.2.8", jar);
            f->jarBytes = jar;
            AuthlibInjectorTask task(dir.path(), std::move(fake));
            runTask(task);
            QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
            QCOMPARE(f->downloadCalls, 1);
            QVERIFY(fileMatchesSha256(task.jarPath(), sha256Hex(jar)));
        }
    }

    void task_checksumMismatchIsRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto fake = std::make_unique<FakeFetcher>();
        auto* f = fake.get();
        f->latestBody = latestJson("1.2.8", "the real jar");
        f->jarBytes = "something else entirely";  // a tampered / truncated download
        AuthlibInjectorTask task(dir.path(), std::move(fake));
        runTask(task);
        QVERIFY(task.getState() == Task::State::Failed);
        QVERIFY2(task.failReason().startsWith("Ely.by accounts need authlib-injector, which couldn't be downloaded: "),
                 qPrintable(task.failReason()));
        QVERIFY2(task.failReason().contains("checksum"), qPrintable(task.failReason()));
        QVERIFY(!QFile::exists(jarPath(dir.path(), "1.2.8")));
        QVERIFY(!QFile::exists(jarPath(dir.path(), "1.2.8") + ".part"));
    }

    void task_networkFailureWithoutCacheFailsClearly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto fake = std::make_unique<FakeFetcher>();
        fake->latestOk = false;
        fake->latestError = "Host authlib-injector.yushi.moe not found";
        AuthlibInjectorTask task(dir.path(), std::move(fake));
        runTask(task);
        QVERIFY(task.getState() == Task::State::Failed);
        QCOMPARE(task.failReason(),
                 QString("Ely.by accounts need authlib-injector, which couldn't be downloaded: Host authlib-injector.yushi.moe not found"));
    }

    void task_badLatestJsonWithoutCacheFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto fake = std::make_unique<FakeFetcher>();
        fake->latestBody = "<html>captive portal</html>";
        AuthlibInjectorTask task(dir.path(), std::move(fake));
        runTask(task);
        QVERIFY(task.getState() == Task::State::Failed);
        QVERIFY2(task.failReason().contains("latest.json"), qPrintable(task.failReason()));
    }

    void task_offlineUsesVerifiedCache()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray jar = "PK-fake-jar-bytes-1.2.8";
        {
            auto fake = std::make_unique<FakeFetcher>();
            fake->latestBody = latestJson("1.2.8", jar);
            fake->jarBytes = jar;
            AuthlibInjectorTask prime(dir.path(), std::move(fake));
            runTask(prime);
            QVERIFY(prime.wasSuccessful());
        }
        {
            // no network at all: the verified cached jar is used
            auto fake = std::make_unique<FakeFetcher>();
            fake->latestOk = false;
            AuthlibInjectorTask task(dir.path(), std::move(fake));
            runTask(task);
            QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
            QVERIFY(task.usedCacheAfterFailure());
            QCOMPARE(task.jarPath(), jarPath(dir.path(), "1.2.8"));
        }
        {
            // a newer release is announced but its download is tampered with: the old verified jar still works
            auto fake = std::make_unique<FakeFetcher>();
            fake->latestBody = latestJson("1.2.9", "the 1.2.9 jar");
            fake->jarBytes = "tampered";
            AuthlibInjectorTask task(dir.path(), std::move(fake));
            runTask(task);
            QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
            QVERIFY(task.usedCacheAfterFailure());
            QCOMPARE(task.version(), QString("1.2.8"));
            QVERIFY(!QFile::exists(jarPath(dir.path(), "1.2.9")));
            QVERIFY(!QFile::exists(jarPath(dir.path(), "1.2.9") + ".part"));
        }
        {
            // a cached jar that was modified afterwards is NOT trusted offline
            QFile f(jarPath(dir.path(), "1.2.8"));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("modified after the check");
            f.close();
            auto fake = std::make_unique<FakeFetcher>();
            fake->latestOk = false;
            AuthlibInjectorTask task(dir.path(), std::move(fake));
            runTask(task);
            QVERIFY(task.getState() == Task::State::Failed);
        }
    }

    void findCachedJar_picksNewestVerified()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(!findCachedJar(dir.path()).has_value());
        QVERIFY(!findCachedJar(dir.filePath("nope")).has_value());
        const auto put = [&](const QString& version, const QByteArray& bytes, bool sidecarOk) {
            const auto path = jarPath(dir.path(), version);
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
            f.close();
            QVERIFY(writeChecksumFile(path, sidecarOk ? sha256Hex(bytes) : sha256Hex("other")));
        };
        put("1.2.9", "nine", true);
        put("1.2.10", "ten", false);  // newest, but its record does not match: skipped
        put("1.2.8", "eight", true);
        const auto best = findCachedJar(dir.path());
        QVERIFY(best.has_value());
        QCOMPARE(best->version, QString("1.2.9"));  // numeric ordering, not alphabetical
    }

    // ------------------------------------------------------------------------------------------------- Ely token forms

    void tokenForms_clientSecretOnlyWhenSet()
    {
        Ely::Config noSecret{ "cid", "" };
        Ely::Config withSecret{ "cid", "s3 cret&=" };

        auto dev = parseForm(Ely::deviceCodeForm(noSecret));
        QCOMPARE(dev.queryItemValue("client_id"), QString("cid"));
        QCOMPARE(dev.queryItemValue("scope", QUrl::FullyDecoded), Ely::scopes());
        QVERIFY(!dev.hasQueryItem("client_secret"));
        QCOMPARE(Ely::scopes(), QString("account_info offline_access minecraft_server_session"));

        dev = parseForm(Ely::deviceCodeForm(withSecret));
        QCOMPARE(dev.queryItemValue("client_secret", QUrl::FullyDecoded), QString("s3 cret&="));

        auto tok = parseForm(Ely::deviceTokenForm(noSecret, "devcode"));
        QCOMPARE(tok.queryItemValue("grant_type", QUrl::FullyDecoded), QString("urn:ietf:params:oauth:grant-type:device_code"));
        QCOMPARE(tok.queryItemValue("device_code"), QString("devcode"));
        QVERIFY(!tok.hasQueryItem("client_secret"));
        QVERIFY(parseForm(Ely::deviceTokenForm(withSecret, "devcode")).hasQueryItem("client_secret"));

        auto ref = parseForm(Ely::refreshForm(noSecret, "rtok"));
        QCOMPARE(ref.queryItemValue("grant_type"), QString("refresh_token"));
        QCOMPARE(ref.queryItemValue("refresh_token"), QString("rtok"));
        QCOMPARE(ref.queryItemValue("scope", QUrl::FullyDecoded), Ely::scopes());
        QVERIFY(!ref.hasQueryItem("client_secret"));
        QVERIFY(parseForm(Ely::refreshForm(withSecret, "rtok")).hasQueryItem("client_secret"));

        QCOMPARE(Ely::deviceCodeUrl().toString(), QString("https://account.ely.by/api/oauth2/v1/devicecode"));
        QCOMPARE(Ely::tokenUrl().toString(), QString("https://account.ely.by/api/oauth2/v1/token"));
        QCOMPARE(Ely::profileUrl().toString(), QString("https://account.ely.by/api/mojang/services/minecraft/profile"));
    }

    void steps_sendClientSecretOnlyWhenSet()
    {
        AccountData data;
        data.type = AccountType::Ely;
        data.msaToken.refresh_token = "stored-refresh";

        ElyDeviceCodeStep plain(&data, Ely::Config{ "cid", "" });
        QVERIFY(!parseForm(plain.deviceCodePayload()).hasQueryItem("client_secret"));
        QVERIFY(!parseForm(plain.tokenPayload()).hasQueryItem("client_secret"));
        QCOMPARE(parseForm(plain.deviceCodePayload()).queryItemValue("client_id"), QString("cid"));

        ElyDeviceCodeStep secret(&data, Ely::Config{ "cid", "topsecret" });
        QCOMPARE(parseForm(secret.deviceCodePayload()).queryItemValue("client_secret"), QString("topsecret"));
        QCOMPARE(parseForm(secret.tokenPayload()).queryItemValue("client_secret"), QString("topsecret"));
        QCOMPARE(parseForm(secret.tokenPayload()).queryItemValue("grant_type", QUrl::FullyDecoded),
                 QString("urn:ietf:params:oauth:grant-type:device_code"));

        ElyRefreshStep refreshPlain(&data, Ely::Config{ "cid", "" });
        QVERIFY(!parseForm(refreshPlain.requestPayload()).hasQueryItem("client_secret"));
        QCOMPARE(parseForm(refreshPlain.requestPayload()).queryItemValue("refresh_token"), QString("stored-refresh"));
        ElyRefreshStep refreshSecret(&data, Ely::Config{ "cid", "topsecret" });
        QCOMPARE(parseForm(refreshSecret.requestPayload()).queryItemValue("client_secret"), QString("topsecret"));
    }

    void msaDeviceStep_requestsUnchanged()
    {
        // the Microsoft device code requests must stay byte-for-byte what they were before Ely.by existed
        AccountData data;
        TestMsaDeviceStep step(&data);
        QCOMPARE(step.deviceCodePayload(), QByteArray("client_id=client-id&scope=XboxLive.SignIn%20XboxLive.offline_access"));
        QCOMPARE(step.tokenPayload(),
                 QByteArray("client_id=client-id&grant_type=urn:ietf:params:oauth:grant-type:device_code&device_code"));
    }

    void parseTokenResponse_cases()
    {
        auto ok = Ely::parseTokenResponse(R"({"access_token":"a","refresh_token":"r2","expires_in":3600,"token_type":"Bearer"})", "r1");
        QVERIFY(ok.ok);
        QCOMPARE(ok.accessToken, QString("a"));
        QCOMPARE(ok.refreshToken, QString("r2"));
        QCOMPARE(ok.expiresIn, 3600);
        QVERIFY(!ok.extra.contains("access_token"));
        QVERIFY(!ok.extra.contains("refresh_token"));
        QCOMPARE(ok.extra.value("token_type").toString(), QString("Bearer"));

        // a refresh answer that omits refresh_token keeps the old one
        auto kept = Ely::parseTokenResponse(R"({"access_token":"a","expires_in":3600})", "r1");
        QVERIFY(kept.ok);
        QCOMPARE(kept.refreshToken, QString("r1"));

        auto err = Ely::parseTokenResponse(R"({"error":"invalid_grant","error_description":"The refresh token is invalid."})", "r1");
        QVERIFY(!err.ok);
        QCOMPARE(err.errorText(), QString("The refresh token is invalid."));

        auto onlyCode = Ely::parseTokenResponse(R"({"error":"authorization_pending"})");
        QVERIFY(!onlyCode.ok);
        QCOMPARE(onlyCode.errorText(), QString("authorization_pending"));

        QVERIFY(!Ely::parseTokenResponse("").ok);
        QVERIFY(!Ely::parseTokenResponse("<html>").ok);
        QVERIFY(!Ely::parseTokenResponse(R"({"expires_in":5})").ok);  // no access token
    }

    void config_emptyIdHidesEveryEntryPoint()
    {
        // AccountListPage hides "Add Ely.by" unless Application::SupportsEly is set, and updateCapabilities() sets it only when
        // getElyClientID() (= configFromBuild().clientId) is not empty; Config::isConfigured() is that same test.
        QVERIFY(!Ely::Config{}.isConfigured());
        const Ely::Config secretOnly{ "", "secret-without-id" };
        const Ely::Config idOnly{ "some-client-id", "" };
        QVERIFY(!secretOnly.isConfigured());
        QVERIFY(idOnly.isConfigured());
        QCOMPARE(Ely::configFromBuild().isConfigured(), !BuildConfig.ELY_CLIENT_ID.isEmpty());
        QCOMPARE(Ely::configFromBuild().clientId, BuildConfig.ELY_CLIENT_ID);
    }

    // ------------------------------------------------------------------------------------------------- the account type

    void accountData_elyRoundTrip()
    {
        AccountData a;
        a.type = AccountType::Ely;
        a.msaClientID = "client-xyz";
        a.msaToken.token = "access-1";
        a.msaToken.refresh_token = "refresh-1";
        a.msaToken.issueInstant = QDateTime::fromSecsSinceEpoch(1700000000);
        a.msaToken.notAfter = QDateTime::fromSecsSinceEpoch(1700003600);
        a.yggdrasilToken.token = "access-1";
        a.minecraftProfile.id = "33333333333333333333333333333333";
        a.minecraftProfile.name = "ElyPlayer";
        a.minecraftProfile.validity = Validity::Certain;
        a.minecraftEntitlement.ownsMinecraft = true;
        a.minecraftEntitlement.canPlayMinecraft = true;
        a.minecraftEntitlement.validity = Validity::Certain;

        const auto saved = a.saveState();
        QCOMPARE(saved.value("type").toString(), QString("Ely"));
        QCOMPARE(saved.value("msa-client-id").toString(), QString("client-xyz"));
        QCOMPARE(saved.value("msa").toObject().value("refresh_token").toString(), QString("refresh-1"));
        QCOMPARE(saved.value("ygg").toObject().value("token").toString(), QString("access-1"));
        QVERIFY(!saved.contains("utoken"));
        QVERIFY(!saved.contains("xrp-mc"));

        AccountData b;
        QVERIFY(b.resumeStateFromV3(saved));
        QCOMPARE(b.type, AccountType::Ely);
        QCOMPARE(b.msaClientID, QString("client-xyz"));
        QCOMPARE(b.msaToken.token, QString("access-1"));
        QCOMPARE(b.msaToken.refresh_token, QString("refresh-1"));
        QCOMPARE(b.msaToken.notAfter, a.msaToken.notAfter);
        QCOMPARE(b.accessToken(), QString("access-1"));
        QCOMPARE(b.profileId(), QString("33333333333333333333333333333333"));
        QCOMPARE(b.profileName(), QString("ElyPlayer"));
        QVERIFY(b.minecraftEntitlement.ownsMinecraft);
        QCOMPARE(b.saveState(), saved);  // stable
    }

    void accountData_oldFilesStillLoad()
    {
        AccountData msa;
        QVERIFY(msa.resumeStateFromV3(msaAccountJson()));
        QCOMPARE(msa.type, AccountType::MSA);
        QCOMPARE(msa.msaToken.refresh_token, QString("mr"));
        QCOMPARE(msa.userToken.token, QString("u"));
        QCOMPARE(msa.mojangservicesToken.token, QString("x"));
        QCOMPARE(msa.saveState().value("type").toString(), QString("MSA"));

        AccountData off;
        QVERIFY(off.resumeStateFromV3(offlineAccountJson()));
        QCOMPARE(off.type, AccountType::Offline);
        QCOMPARE(off.profileName(), QString("Bob"));
        QCOMPARE(off.saveState().value("type").toString(), QString("Offline"));
    }

    void accountData_unknownTypeDoesNotCrash()
    {
        AccountData d;
        QVERIFY(!d.resumeStateFromV3(QJsonObject{ { "type", "Quantum" } }));
        QVERIFY(!d.resumeStateFromV3(QJsonObject{ { "type", 5 } }));
        QVERIFY(!d.resumeStateFromV3(QJsonObject()));
        QVERIFY(MinecraftAccount::loadFromJsonV3(QJsonObject{ { "type", "Quantum" } }).isNull());
        QVERIFY(!MinecraftAccount::loadFromJsonV3(elyAccountJson("44444444444444444444444444444444", "E")).isNull());
    }

    void accountList_loadsOldAndNewTypes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject unknown{ { "type", "Quantum" } };
        QJsonObject root{ { "formatVersion", 3 },
                          { "accounts",
                            QJsonArray{ msaAccountJson(), unknown, offlineAccountJson(),
                                        elyAccountJson("33333333333333333333333333333333", "ElyPlayer") } } };
        const auto path = dir.filePath("accounts.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(root).toJson());
        f.close();

        AccountList list;
        list.setListFilePath(path, false);
        QVERIFY(list.loadList());
        QCOMPARE(list.count(), 3);  // the unknown type is skipped, nothing else is lost
        QCOMPARE(list.data(list.index(0, AccountList::TypeColumn), Qt::DisplayRole).toString(), QString("MSA"));
        QCOMPARE(list.data(list.index(1, AccountList::TypeColumn), Qt::DisplayRole).toString(), QString("Offline"));
        QCOMPARE(list.data(list.index(2, AccountList::TypeColumn), Qt::DisplayRole).toString(), QString("Ely.by"));
        QCOMPARE(list.at(2)->accountType(), AccountType::Ely);
    }

    void minecraftAccount_elyBehaviour()
    {
        auto ely = MinecraftAccount::loadFromJsonV3(elyAccountJson("33333333333333333333333333333333", "ElyPlayer"));
        auto msa = MinecraftAccount::loadFromJsonV3(msaAccountJson());
        auto off = MinecraftAccount::loadFromJsonV3(offlineAccountJson());
        QVERIFY(ely && msa && off);

        QCOMPARE(ely->accountType(), AccountType::Ely);
        QVERIFY(ely->ownsMinecraft());  // a working Ely.by account with a profile can play
        QVERIFY(ely->hasProfile());
        QVERIFY(!off->ownsMinecraft());
        QCOMPARE(ely->typeString(), QString("msa"));
        QCOMPARE(MinecraftAccount::createBlankEly()->accountType(), AccountType::Ely);

        // only Ely.by sessions ask for authlib-injector; Microsoft and offline launches are untouched
        auto s1 = std::make_shared<AuthSession>();
        ely->fillSession(s1);
        QVERIFY(s1->wantsAuthlibInjector);
        QCOMPARE(s1->access_token, QString("access-1"));
        QCOMPARE(s1->player_name, QString("ElyPlayer"));
        QVERIFY(s1->authlibInjectorJar.isEmpty());
        auto s2 = std::make_shared<AuthSession>();
        msa->fillSession(s2);
        QVERIFY(!s2->wantsAuthlibInjector);
        auto s3 = std::make_shared<AuthSession>();
        off->fillSession(s3);
        QVERIFY(!s3->wantsAuthlibInjector);
        // an Ely.by account launched offline (no network) or as demo plays without the agent
        s1->MakeOffline("Steve");
        QVERIFY(!s1->wantsAuthlibInjector);
        auto s4 = std::make_shared<AuthSession>();
        ely->fillSession(s4);
        s4->MakeDemo("Demo", "00000000000000000000000000000000");
        QVERIFY(!s4->wantsAuthlibInjector);
    }

    // ------------------------------------------------------------------------------------------------------ AuthFlow

    void authFlow_stepLists()
    {
        const QStringList login{ "ElyDeviceCodeStep", "ElyYggdrasilTokenStep", "MinecraftProfileStepEly" };
        const QStringList refresh{ "ElyRefreshStep", "ElyYggdrasilTokenStep", "MinecraftProfileStepEly" };

        auto ely = MinecraftAccount::createBlankEly();
        // Ely.by has only the device code flow, so a plain Login builds the same steps as DeviceCode
        QCOMPARE(AuthFlow(ely->accountData(), AuthFlow::Action::Login).stepNames(), login);
        QCOMPARE(AuthFlow(ely->accountData(), AuthFlow::Action::DeviceCode).stepNames(), login);
        QCOMPARE(AuthFlow(ely->accountData(), AuthFlow::Action::Refresh).stepNames(), refresh);

        // offline accounts still have nothing to do
        auto off = MinecraftAccount::createOffline("Bob");
        QVERIFY(AuthFlow(off->accountData(), AuthFlow::Action::Login).stepNames().isEmpty());
        QVERIFY(AuthFlow(off->accountData(), AuthFlow::Action::Refresh).stepNames().isEmpty());
    }

    // ------------------------------------------------------------------------------- Ely.by authserver (email + password)

    void authserver_urlsAndBodies()
    {
        using namespace Ely::Authserver;
        QCOMPARE(url("authenticate").toString(), QString("https://authserver.ely.by/auth/authenticate"));
        QCOMPARE(url("refresh").toString(), QString("https://authserver.ely.by/auth/refresh"));
        QCOMPARE(url("invalidate").toString(), QString("https://authserver.ely.by/auth/invalidate"));

        const auto token = newClientToken();
        QCOMPARE(token.size(), 32);
        QVERIFY(!token.contains('-'));
        QVERIFY(newClientToken() != token);

        QCOMPARE(passwordWithCode("pw", ""), QString("pw"));
        QCOMPARE(passwordWithCode("pw", "123456"), QString("pw:123456"));

        const auto auth = QJsonDocument::fromJson(authenticateBody("steve@example.com", "pw:1", "ct")).object();
        QCOMPARE(auth.value("username").toString(), QString("steve@example.com"));
        QCOMPARE(auth.value("password").toString(), QString("pw:1"));
        QCOMPARE(auth.value("clientToken").toString(), QString("ct"));
        QCOMPARE(auth.value("requestUser").toBool(), true);

        const auto ref = QJsonDocument::fromJson(refreshBody("at", "ct")).object();
        QCOMPARE(ref.value("accessToken").toString(), QString("at"));
        QCOMPARE(ref.value("clientToken").toString(), QString("ct"));
        QVERIFY(!ref.contains("password"));
        const auto inv = QJsonDocument::fromJson(invalidateBody("at", "ct")).object();
        QCOMPARE(inv.value("accessToken").toString(), QString("at"));
        QCOMPARE(inv.value("clientToken").toString(), QString("ct"));
    }

    void authserver_parseAnswers()
    {
        using namespace Ely::Authserver;
        // success, profile id with dashes -> without
        QJsonObject ok{ { "accessToken", "AT" },
                        { "clientToken", "CT" },
                        { "selectedProfile", QJsonObject{ { "id", "11111111-2222-3333-4444-555555555555" }, { "name", "Steve" } } },
                        { "availableProfiles", QJsonArray{ QJsonObject{ { "id", "x" }, { "name", "y" } } } },
                        { "user", QJsonObject{ { "id", "1" } } } };
        auto a = parseAnswer(200, QJsonDocument(ok).toJson());
        QCOMPARE(a.kind, Kind::Ok);
        QCOMPARE(a.accessToken, QString("AT"));
        QCOMPARE(a.clientToken, QString("CT"));
        QCOMPARE(a.profileId, QString("11111111222233334444555555555555"));
        QCOMPARE(a.profileName, QString("Steve"));
        // an undashed id stays as it is; a lone available profile is used when nothing is selected
        ok.remove("selectedProfile");
        ok["availableProfiles"] = QJsonArray{ QJsonObject{ { "id", "AABBCCDDEEFF00112233445566778899" }, { "name", "Alex" } } };
        a = parseAnswer(200, QJsonDocument(ok).toJson());
        QCOMPARE(a.kind, Kind::Ok);
        QCOMPARE(a.profileId, QString("aabbccddeeff00112233445566778899"));
        QCOMPARE(a.profileName, QString("Alex"));
        // a refresh answer may carry no profile: still Ok, the caller decides
        a = parseAnswer(200, R"({"accessToken":"AT2","clientToken":"CT"})");
        QCOMPARE(a.kind, Kind::Ok);
        QVERIFY(a.profileId.isEmpty());
        // no token in a 200, or no JSON: Malformed
        QCOMPARE(parseAnswer(200, R"({"clientToken":"CT"})").kind, Kind::Malformed);
        QCOMPARE(parseAnswer(200, "<html>").kind, Kind::Malformed);
        QCOMPARE(parseAnswer(200, "").kind, Kind::Malformed);

        // the real error shape (checked live against authserver.ely.by with a made-up user)
        a = parseAnswer(401, R"({"error":"ForbiddenOperationException","errorMessage":"Invalid credentials. Invalid username or password."})");
        QCOMPARE(a.kind, Kind::WrongCredentials);
        QCOMPARE(a.error, QString("ForbiddenOperationException"));
        QCOMPARE(a.errorText(), QString("Invalid credentials. Invalid username or password."));
        // two-factor: the message Ely.by documents, and the variants we also recognise
        for (const char* message : { "Account protected with two factor auth.", "Account protected with two-factor authentication.",
                                     "Two factor code required" }) {
            a = parseAnswer(401, QJsonDocument(QJsonObject{ { "error", "ForbiddenOperationException" }, { "errorMessage", message } }).toJson());
            QCOMPARE(a.kind, Kind::TwoFactorRequired);
        }
        // anything else is shown as given
        a = parseAnswer(401, R"({"error":"ForbiddenOperationException","errorMessage":"This account has been suspended."})");
        QCOMPARE(a.kind, Kind::Refused);
        QCOMPARE(a.errorText(), QString("This account has been suspended."));
        a = parseAnswer(400, R"({"error":"IllegalArgumentException"})");
        QCOMPARE(a.kind, Kind::Refused);
        QCOMPARE(a.errorText(), QString("IllegalArgumentException"));
        a = parseAnswer(403, "not json");
        QCOMPARE(a.kind, Kind::Refused);
        QCOMPARE(a.errorText(), QString("HTTP 403"));
    }

    void passwordStep_signsInAndStoresTokenClientTokenAndProfileButNeverThePassword()
    {
        using namespace Ely::Authserver;
        FakeAuthserver server;
        server.script << jsonReply(200, QJsonObject{ { "accessToken", "AT-1" },
                                                     { "clientToken", "" },
                                                     { "selectedProfile", QJsonObject{ { "id", "33333333-3333-3333-3333-333333333333" }, { "name", "ElyPlayer" } } } });
        auto account = MinecraftAccount::createBlankElyPassword();
        auto* data = account->accountData();
        QCOMPARE(data->elyAuth, ElyAuthKind::Password);

        const QString secret = "S3cret-pass-word";
        ElyPasswordStep step(data, server.transport());
        step.setCredentials("  steve@example.com ", secret);
        StepResult result;
        result.watch(step);
        step.perform();

        QVERIFY(result.done);
        QCOMPARE(result.state, AccountTaskState::STATE_WORKING);
        QCOMPARE(server.urls.size(), 1);
        QCOMPARE(server.urls.first().toString(), QString("https://authserver.ely.by/auth/authenticate"));
        const auto sent = QJsonDocument::fromJson(server.bodies.first()).object();
        QCOMPARE(sent.value("username").toString(), QString("steve@example.com"));  // trimmed
        QCOMPARE(sent.value("password").toString(), secret);
        QCOMPARE(sent.value("requestUser").toBool(), true);
        QCOMPARE(sent.value("clientToken").toString().size(), 32);

        QCOMPARE(data->accessToken(), QString("AT-1"));
        QCOMPARE(data->yggdrasilToken.validity, Validity::Certain);
        QCOMPARE(data->elyClientToken, sent.value("clientToken").toString());  // the one we made (the answer carried none)
        QCOMPARE(data->elyUsername, QString("steve@example.com"));
        QCOMPARE(data->profileId(), QString("33333333333333333333333333333333"));
        QCOMPARE(data->profileName(), QString("ElyPlayer"));
        QVERIFY(data->minecraftEntitlement.ownsMinecraft);
        QVERIFY(data->minecraftEntitlement.canPlayMinecraft);
        QVERIFY(account->ownsMinecraft());

        // what is saved has the token, the client token, the user name and the profile - and no password anywhere
        const QJsonObject saved = account->saveToJson();
        QCOMPARE(saved.value("type").toString(), QString("Ely"));
        QCOMPARE(saved.value("ely-auth").toString(), QString("password"));
        QCOMPARE(saved.value("ely-username").toString(), QString("steve@example.com"));
        QCOMPARE(saved.value("ely-client-token").toString(), data->elyClientToken);
        QCOMPARE(saved.value("ygg").toObject().value("token").toString(), QString("AT-1"));
        QVERIFY(!saved.contains("msa"));
        QVERIFY(!saved.contains("msa-client-id"));
        const QByteArray savedBytes = QJsonDocument(saved).toJson();
        QVERIFY2(!savedBytes.contains(secret.toUtf8()), "the password must not be saved");
        QVERIFY(!savedBytes.contains("\"password\":"));

        // the password was wiped once the request existed: performing the same step again has nothing to send
        StepResult again;
        again.watch(step);
        step.perform();
        QCOMPARE(again.state, AccountTaskState::STATE_FAILED_HARD);
        QCOMPARE(server.urls.size(), 1);
        QVERIFY(!again.message.contains(secret));
    }

    void passwordStep_accountRoundTripAndGameSession()
    {
        auto account = MinecraftAccount::createBlankElyPassword();
        auto* data = account->accountData();
        data->elyUsername = "steve";
        data->elyClientToken = "0123456789abcdef0123456789abcdef";
        data->yggdrasilToken.token = "AT-9";
        data->yggdrasilToken.validity = Validity::Certain;
        data->minecraftProfile.id = "33333333333333333333333333333333";
        data->minecraftProfile.name = "ElyPlayer";
        data->minecraftProfile.validity = Validity::Certain;
        data->minecraftEntitlement.ownsMinecraft = true;
        data->minecraftEntitlement.canPlayMinecraft = true;
        data->minecraftEntitlement.validity = Validity::Certain;

        const auto saved = account->saveToJson();
        auto loaded = MinecraftAccount::loadFromJsonV3(saved);
        QVERIFY(loaded);
        QCOMPARE(loaded->accountType(), AccountType::Ely);
        QCOMPARE(loaded->accountData()->elyAuth, ElyAuthKind::Password);
        QCOMPARE(loaded->accountData()->elyUsername, QString("steve"));
        QCOMPARE(loaded->accountData()->elyClientToken, QString("0123456789abcdef0123456789abcdef"));
        QCOMPARE(loaded->accountData()->accessToken(), QString("AT-9"));
        QCOMPARE(loaded->profileName(), QString("ElyPlayer"));
        QVERIFY(loaded->ownsMinecraft());
        QCOMPARE(loaded->saveToJson(), saved);  // stable

        // the game gets the authserver token and starts through authlib-injector, like the OAuth kind
        auto session = std::make_shared<AuthSession>();
        loaded->fillSession(session);
        QVERIFY(session->wantsAuthlibInjector);
        QCOMPARE(session->access_token, QString("AT-9"));
        QCOMPARE(session->uuid, QString("33333333333333333333333333333333"));
        QCOMPARE(session->player_name, QString("ElyPlayer"));
    }

    void accountData_olderElyFilesLoadAsOAuth()
    {
        // written before password sign-in existed: no "ely-auth"
        AccountData a;
        QVERIFY(a.resumeStateFromV3(elyAccountJson("33333333333333333333333333333333", "ElyPlayer")));
        QCOMPARE(a.type, AccountType::Ely);
        QCOMPARE(a.elyAuth, ElyAuthKind::OAuth);
        QCOMPARE(a.msaToken.refresh_token, QString("refresh-1"));
        QCOMPARE(a.msaClientID, QString("client-xyz"));
        QVERIFY(a.elyUsername.isEmpty());
        QVERIFY(a.elyClientToken.isEmpty());
        // and it is saved as "oauth" from now on, still with its token
        const auto saved = a.saveState();
        QCOMPARE(saved.value("ely-auth").toString(), QString("oauth"));
        QCOMPARE(saved.value("msa").toObject().value("refresh_token").toString(), QString("refresh-1"));
        QVERIFY(!saved.contains("ely-username"));
        QVERIFY(!saved.contains("ely-client-token"));
        // Microsoft and offline files carry nothing of it
        AccountData msa;
        QVERIFY(msa.resumeStateFromV3(msaAccountJson()));
        QVERIFY(!msa.saveState().contains("ely-auth"));
        AccountData off;
        QVERIFY(off.resumeStateFromV3(offlineAccountJson()));
        QVERIFY(!off.saveState().contains("ely-auth"));
    }

    void accountList_passwordAccountsLoadAndRemoveWithoutANetwork()
    {
        auto pw = MinecraftAccount::createBlankElyPassword();
        pw->accountData()->elyUsername = "steve";
        pw->accountData()->elyClientToken = "0123456789abcdef0123456789abcdef";
        pw->accountData()->yggdrasilToken.token = "AT-9";
        pw->accountData()->minecraftProfile.id = "33333333333333333333333333333333";
        pw->accountData()->minecraftProfile.name = "ElyPlayer";
        pw->accountData()->minecraftProfile.validity = Validity::Certain;

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject root{ { "formatVersion", 3 },
                          { "accounts", QJsonArray{ msaAccountJson(), pw->saveToJson(), offlineAccountJson(),
                                                    elyAccountJson("44444444444444444444444444444444", "OAuthElyPlayer") } } };
        const auto path = dir.filePath("accounts.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(root).toJson());
        f.close();

        AccountList list;
        list.setListFilePath(path, false);
        QVERIFY(list.loadList());
        QCOMPARE(list.count(), 4);
        QCOMPARE(list.at(1)->accountData()->elyAuth, ElyAuthKind::Password);
        QCOMPARE(list.at(3)->accountData()->elyAuth, ElyAuthKind::OAuth);
        // removing a password account tries to invalidate its token in the background; with no Application running (this test) that
        // does nothing and must not crash
        list.removeAccount(list.index(1));
        QCOMPARE(list.count(), 3);
        QCOMPARE(list.at(1)->accountType(), AccountType::Offline);
    }

    void invalidate_isBestEffortAndNeedsAnApplication()
    {
        using namespace Ely::Authserver;
        FakeAuthserver server;
        invalidateInBackground("AT", "CT", server.transport());  // no Application: nothing is sent
        QCOMPARE(server.urls.size(), 0);
        invalidateInBackground("", "CT", server.transport());
        QCOMPARE(server.urls.size(), 0);
    }

    void passwordStep_twoFactorFlow()
    {
        using namespace Ely::Authserver;
        FakeAuthserver server;
        server.script << jsonReply(401, QJsonObject{ { "error", "ForbiddenOperationException" },
                                                     { "errorMessage", "Account protected with two factor auth." } })
                      << jsonReply(200, QJsonObject{ { "accessToken", "AT-2" },
                                                     { "clientToken", "CT" },
                                                     { "selectedProfile", QJsonObject{ { "id", "55555555555555555555555555555555" }, { "name", "Tfa" } } } });
        auto account = MinecraftAccount::createBlankElyPassword();
        auto* data = account->accountData();

        // 1. the plain password: Ely.by says the account wants a code
        ElyPasswordStep first(data, server.transport());
        first.setCredentials("steve", "pw-sample");
        StepResult r1;
        r1.watch(first);
        first.perform();
        QCOMPARE(r1.failure, Failure::TwoFactorRequired);
        QCOMPARE(r1.state, AccountTaskState::STATE_FAILED_SOFT);
        QCOMPARE(QJsonDocument::fromJson(server.bodies.at(0)).object().value("password").toString(), QString("pw-sample"));
        QVERIFY(data->accessToken().isEmpty());
        const QString clientToken = data->elyClientToken;
        QCOMPARE(clientToken.size(), 32);

        // 2. the same password with the code: password:code, the same client token
        ElyPasswordStep second(data, server.transport());
        second.setCredentials("steve", "pw-sample", "123456");
        StepResult r2;
        r2.watch(second);
        second.perform();
        QCOMPARE(r2.state, AccountTaskState::STATE_WORKING);
        QCOMPARE(r2.failure, Failure::None);
        const auto sent = QJsonDocument::fromJson(server.bodies.at(1)).object();
        QCOMPARE(sent.value("password").toString(), QString("pw-sample:123456"));
        QCOMPARE(sent.value("clientToken").toString(), clientToken);
        QCOMPARE(data->accessToken(), QString("AT-2"));
        QCOMPARE(data->profileName(), QString("Tfa"));
        QVERIFY(!QJsonDocument(account->saveToJson()).toJson().contains("pw-sample"));
    }

    void passwordStep_wrongCredentialsWrongCodeAndOtherAnswers()
    {
        using namespace Ely::Authserver;
        const QJsonObject invalid{ { "error", "ForbiddenOperationException" },
                                   { "errorMessage", "Invalid credentials. Invalid username or password." } };
        struct Case {
            const char* name;
            HttpReply reply;
            QString code;
            Failure failure;
            AccountTaskState state;
        };
        HttpReply down;  // no answer
        down.errorString = "Host ely.by not found";
        down.error = QNetworkReply::HostNotFoundError;
        const QList<Case> cases = {
            { "wrong password", jsonReply(401, invalid), "", Failure::WrongCredentials, AccountTaskState::STATE_FAILED_HARD },
            { "wrong code", jsonReply(401, invalid), "000000", Failure::WrongCode, AccountTaskState::STATE_FAILED_HARD },
            { "other message",
              jsonReply(401, QJsonObject{ { "error", "ForbiddenOperationException" }, { "errorMessage", "This account has been suspended." } }), "",
              Failure::Refused, AccountTaskState::STATE_FAILED_HARD },
            { "no network", down, "", Failure::Network, AccountTaskState::STATE_OFFLINE },
            { "server error", jsonReply(502, QJsonObject{}), "", Failure::Network, AccountTaskState::STATE_OFFLINE },
            { "rate limited", jsonReply(429, QJsonObject{}), "", Failure::Network, AccountTaskState::STATE_OFFLINE },
            { "garbage answer", textReply(200, "<html>"), "", Failure::Refused, AccountTaskState::STATE_FAILED_SOFT },
            { "no profile", jsonReply(200, QJsonObject{ { "accessToken", "AT" }, { "clientToken", "CT" } }), "", Failure::Refused,
              AccountTaskState::STATE_FAILED_SOFT },
        };
        for (const Case& c : cases) {
            FakeAuthserver server;
            server.script << c.reply;
            auto account = MinecraftAccount::createBlankElyPassword();
            ElyPasswordStep step(account->accountData(), server.transport());
            step.setCredentials("steve", "pw-sample", c.code);
            StepResult r;
            r.watch(step);
            step.perform();
            QVERIFY2(r.done, c.name);
            QVERIFY2(r.failure == c.failure, c.name);
            QVERIFY2(r.state == c.state, c.name);
            QVERIFY2(account->accountData()->accessToken().isEmpty(), c.name);
            QVERIFY2(!account->accountData()->minecraftEntitlement.ownsMinecraft, c.name);
            QVERIFY2(!r.message.contains("pw-sample"), c.name);
        }

        // the message of a refusal is carried as Ely.by gave it
        FakeAuthserver server;
        server.script << jsonReply(401, QJsonObject{ { "error", "ForbiddenOperationException" }, { "errorMessage", "This account has been suspended." } });
        auto account = MinecraftAccount::createBlankElyPassword();
        ElyPasswordStep step(account->accountData(), server.transport());
        step.setCredentials("steve", "pw-sample");
        StepResult r;
        r.watch(step);
        step.perform();
        QCOMPARE(r.message, QString("This account has been suspended."));

        // empty credentials never reach the network
        FakeAuthserver none;
        ElyPasswordStep empty(MinecraftAccount::createBlankElyPassword()->accountData(), none.transport());
        StepResult e;
        e.watch(empty);
        empty.perform();
        QCOMPARE(e.state, AccountTaskState::STATE_FAILED_HARD);
        QCOMPARE(none.urls.size(), 0);
    }

    void refreshStep_successRefusedAndOffline()
    {
        using namespace Ely::Authserver;
        auto makeAccount = []() {
            auto account = MinecraftAccount::createBlankElyPassword();
            auto* d = account->accountData();
            d->elyUsername = "steve";
            d->elyClientToken = "CT";
            d->yggdrasilToken.token = "AT-old";
            d->yggdrasilToken.validity = Validity::Certain;
            d->minecraftProfile.id = "33333333333333333333333333333333";
            d->minecraftProfile.name = "ElyPlayer";
            d->minecraftProfile.validity = Validity::Certain;
            d->minecraftEntitlement.ownsMinecraft = true;
            d->minecraftEntitlement.canPlayMinecraft = true;
            d->minecraftEntitlement.validity = Validity::Certain;
            return account;
        };

        { // success: a new access token (no profile in the answer: the stored one stays)
            FakeAuthserver server;
            server.script << jsonReply(200, QJsonObject{ { "accessToken", "AT-new" }, { "clientToken", "CT" } });
            auto account = makeAccount();
            ElyAuthserverRefreshStep step(account->accountData(), server.transport());
            StepResult r;
            r.watch(step);
            step.perform();
            QCOMPARE(r.state, AccountTaskState::STATE_WORKING);
            QCOMPARE(server.urls.first().toString(), QString("https://authserver.ely.by/auth/refresh"));
            const auto sent = QJsonDocument::fromJson(server.bodies.first()).object();
            QCOMPARE(sent.value("accessToken").toString(), QString("AT-old"));
            QCOMPARE(sent.value("clientToken").toString(), QString("CT"));
            QVERIFY(!sent.contains("password"));
            QCOMPARE(account->accountData()->accessToken(), QString("AT-new"));
            QCOMPARE(account->profileName(), QString("ElyPlayer"));
            QVERIFY(account->ownsMinecraft());
        }
        { // success with a profile in the answer
            FakeAuthserver server;
            server.script << jsonReply(200, QJsonObject{ { "accessToken", "AT-new" },
                                                         { "clientToken", "CT" },
                                                         { "selectedProfile", QJsonObject{ { "id", "33333333-3333-3333-3333-333333333333" }, { "name", "Renamed" } } } });
            auto account = makeAccount();
            ElyAuthserverRefreshStep step(account->accountData(), server.transport());
            step.perform();
            QCOMPARE(account->profileName(), QString("Renamed"));
            QCOMPARE(account->accountData()->profileId(), QString("33333333333333333333333333333333"));
        }
        { // refused: sign in again
            FakeAuthserver server;
            server.script << jsonReply(401, QJsonObject{ { "error", "ForbiddenOperationException" }, { "errorMessage", "Token expired." } });
            auto account = makeAccount();
            ElyAuthserverRefreshStep step(account->accountData(), server.transport());
            StepResult r;
            r.watch(step);
            step.perform();
            QCOMPARE(r.state, AccountTaskState::STATE_FAILED_HARD);
            QCOMPARE(r.message, QString("Your Ely.by session ended. Sign in again."));
        }
        { // no answer / server down: the account is fine
            HttpReply down;
            down.errorString = "timeout";
            down.error = QNetworkReply::TimeoutError;
            for (const HttpReply& reply : { down, jsonReply(503, QJsonObject{}) }) {
                FakeAuthserver server;
                server.script << reply;
                auto account = makeAccount();
                ElyAuthserverRefreshStep step(account->accountData(), server.transport());
                StepResult r;
                r.watch(step);
                step.perform();
                QCOMPARE(r.state, AccountTaskState::STATE_OFFLINE);
                QCOMPARE(account->accountData()->accessToken(), QString("AT-old"));
            }
        }
        { // nothing to refresh with
            FakeAuthserver server;
            auto account = MinecraftAccount::createBlankElyPassword();
            ElyAuthserverRefreshStep step(account->accountData(), server.transport());
            StepResult r;
            r.watch(step);
            step.perform();
            QCOMPARE(r.state, AccountTaskState::STATE_FAILED_HARD);
            QCOMPARE(server.urls.size(), 0);
        }
    }

    void authFlow_passwordLoginAndRefresh()
    {
        using namespace Ely::Authserver;
        // step lists: authenticate / refresh only (the password steps set the token and the profile themselves)
        auto blank = MinecraftAccount::createBlankElyPassword();
        QCOMPARE(AuthFlow(blank->accountData(), AuthFlow::Action::Login).stepNames(), QStringList{ "ElyPasswordStep" });
        QCOMPARE(AuthFlow(blank->accountData(), AuthFlow::Action::DeviceCode).stepNames(), QStringList{ "ElyPasswordStep" });
        QCOMPARE(AuthFlow(blank->accountData(), AuthFlow::Action::Refresh).stepNames(), QStringList{ "ElyAuthserverRefreshStep" });

        { // login through the flow: credentials in, account signed in
            FakeAuthserver server;
            server.script << jsonReply(200, QJsonObject{ { "accessToken", "AT-1" },
                                                         { "clientToken", "CT" },
                                                         { "selectedProfile", QJsonObject{ { "id", "66666666666666666666666666666666" }, { "name", "FlowPlayer" } } } });
            auto account = MinecraftAccount::createBlankElyPassword();
            AuthFlow flow(account->accountData(), AuthFlow::Action::Login, server.transport());
            flow.setElyCredentials("steve", "pw-sample");
            runTask(flow, 5000);
            QVERIFY(flow.wasSuccessful());
            QCOMPARE(flow.elyFailure(), Failure::None);
            QCOMPARE(account->accountData()->accountState, AccountState::Online);
            QCOMPARE(account->accountData()->accessToken(), QString("AT-1"));
            QCOMPARE(account->profileName(), QString("FlowPlayer"));
        }
        { // wrong password: the flow fails and says why
            FakeAuthserver server;
            server.script << jsonReply(401, QJsonObject{ { "error", "ForbiddenOperationException" },
                                                         { "errorMessage", "Invalid credentials. Invalid username or password." } });
            auto account = MinecraftAccount::createBlankElyPassword();
            AuthFlow flow(account->accountData(), AuthFlow::Action::Login, server.transport());
            flow.setElyCredentials("steve", "pw-sample");
            runTask(flow, 5000);
            QVERIFY(!flow.wasSuccessful());
            QCOMPARE(flow.elyFailure(), Failure::WrongCredentials);
            QCOMPARE(flow.failReason(), QString("Wrong email or password."));
        }
        { // refresh refused: FAILED_HARD, the account is Expired (the re-login path opens the dialog at the Ely.by step)
            FakeAuthserver server;
            server.script << jsonReply(401, QJsonObject{ { "error", "ForbiddenOperationException" }, { "errorMessage", "Invalid token." } });
            auto account = MinecraftAccount::createBlankElyPassword();
            account->accountData()->elyClientToken = "CT";
            account->accountData()->yggdrasilToken.token = "AT-old";
            AuthFlow flow(account->accountData(), AuthFlow::Action::Refresh, server.transport());
            runTask(flow, 5000);
            QVERIFY(!flow.wasSuccessful());
            QCOMPARE(flow.taskState(), AccountTaskState::STATE_FAILED_HARD);
            QCOMPARE(account->accountData()->accountState, AccountState::Expired);
            QCOMPARE(flow.failReason(), QString("Your Ely.by session ended. Sign in again."));
        }
        { // refresh with no network: OFFLINE
            HttpReply down;
            down.errorString = "no route";
            down.error = QNetworkReply::UnknownNetworkError;
            FakeAuthserver server;
            server.script << down;
            auto account = MinecraftAccount::createBlankElyPassword();
            account->accountData()->elyClientToken = "CT";
            account->accountData()->yggdrasilToken.token = "AT-old";
            AuthFlow flow(account->accountData(), AuthFlow::Action::Refresh, server.transport());
            runTask(flow, 5000);
            QCOMPARE(flow.taskState(), AccountTaskState::STATE_OFFLINE);
            QCOMPARE(account->accountData()->accountState, AccountState::Offline);
            QCOMPARE(account->accountData()->accessToken(), QString("AT-old"));
        }
        { // refresh success
            FakeAuthserver server;
            server.script << jsonReply(200, QJsonObject{ { "accessToken", "AT-new" }, { "clientToken", "CT" } });
            auto account = MinecraftAccount::createBlankElyPassword();
            account->accountData()->elyClientToken = "CT";
            account->accountData()->yggdrasilToken.token = "AT-old";
            AuthFlow flow(account->accountData(), AuthFlow::Action::Refresh, server.transport());
            runTask(flow, 5000);
            QVERIFY(flow.wasSuccessful());
            QCOMPARE(account->accountData()->accessToken(), QString("AT-new"));
        }
        // OAuth accounts keep their steps
        auto oauth = MinecraftAccount::createBlankEly();
        QCOMPARE(AuthFlow(oauth->accountData(), AuthFlow::Action::Login).stepNames().first(), QString("ElyDeviceCodeStep"));
        QCOMPARE(AuthFlow(oauth->accountData(), AuthFlow::Action::Refresh).stepNames().first(), QString("ElyRefreshStep"));
    }

    // ---------------------------------------------------------------------------------------------------- live check

    void live_authlibInjectorDownload()
    {
        if (qEnvironmentVariable("ZERO_LIVE_NETWORK_TESTS") != "1") {
            QSKIP("set ZERO_LIVE_NETWORK_TESTS=1 to download the real authlib-injector");
        }
        if (QHostInfo::fromName("authlib-injector.yushi.moe").error() != QHostInfo::NoError) {
            QSKIP("no network");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        AuthlibInjectorTask task(dir.path(), std::make_unique<LiveFetcher>());
        runTask(task);
        QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
        QVERIFY(task.downloaded());
        qInfo() << "LIVE authlib-injector" << task.version() << task.jarPath();

        // what the API says (an independent fetch) against what landed on disk
        QNetworkAccessManager nam;
        QEventLoop loop;
        auto* reply = nam.get(QNetworkRequest(latestUrl()));
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        const auto latest = parseLatest(reply->readAll());
        QVERIFY(latest.has_value());
        QCOMPARE(task.version(), latest->version);
        const auto actual = fileSha256(task.jarPath());
        QVERIFY(actual.has_value());
        qInfo() << "LIVE sha256 on disk" << *actual << "published" << latest->sha256;
        QCOMPARE(*actual, latest->sha256);

        QFile jar(task.jarPath());
        QVERIFY(jar.open(QIODevice::ReadOnly));
        QCOMPARE(jar.read(2), QByteArray("PK"));  // a zip/jar
        QVERIFY(!QFile::exists(task.jarPath() + ".part"));
        QVERIFY(findCachedJar(dir.path()).has_value());
    }
};

QTEST_GUILESS_MAIN(ZeroElyAuthTest)

#include "ZeroElyAuth_test.moc"
