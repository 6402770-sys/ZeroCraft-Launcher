// SPDX-License-Identifier: GPL-3.0-only
#include "ServerPinger.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include "ui/pages/instance/McClient.h"
#include "ui/pages/instance/McResolver.h"

namespace Zero {
namespace Servers {

namespace {
constexpr int kMaxMotdChars = 512;
constexpr int kMaxVersionChars = 64;
constexpr int kMaxChatDepth = 16;

void appendChat(const QJsonValue& value, QString& out, int depth)
{
    if (depth > kMaxChatDepth || out.size() > 4 * kMaxMotdChars)
        return;
    if (value.isString()) {
        out += value.toString();
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        out += object.value(QLatin1String("text")).toString();
        const QJsonArray extra = object.value(QLatin1String("extra")).toArray();
        for (const QJsonValue& part : extra)
            appendChat(part, out, depth + 1);
    } else if (value.isArray()) {
        const QJsonArray parts = value.toArray();
        for (const QJsonValue& part : parts)
            appendChat(part, out, depth + 1);
    }
}

/// Text from a remote server: no control characters (line breaks stay when `keepNewlines`), tabs become spaces.
QString sanitize(const QString& text, bool keepNewlines)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('\n') && keepNewlines)
            out += c;
        else if (c == QLatin1Char('\t'))
            out += QLatin1Char(' ');
        else if (c.category() == QChar::Other_Control || c == QLatin1Char('\n') || c == QChar::LineSeparator ||
                 c == QChar::ParagraphSeparator)
            continue;
        else
            out += c;
    }
    return out;
}

int intOr(const QJsonValue& value, int fallback)
{
    return value.isDouble() ? value.toInt(fallback) : fallback;
}
}  // namespace

QString stripFormatting(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == QChar(0x00A7)) {
            ++i;  // the code character goes too
            continue;
        }
        out += text.at(i);
    }
    return out;
}

QString flattenMotd(const QJsonValue& description)
{
    QString raw;
    appendChat(description, raw, 0);
    const QString plain = sanitize(stripFormatting(raw), true);
    QStringList lines = plain.split(QLatin1Char('\n'));
    for (QString& line : lines)
        line = line.trimmed();
    QString out = lines.join(QLatin1Char('\n')).trimmed();
    if (out.size() > kMaxMotdChars)
        out = out.left(kMaxMotdChars - 1).trimmed() + QChar(0x2026);
    return out;
}

PingResult resultFromStatus(const QJsonObject& status, int latencyMs)
{
    PingResult r;
    r.state = PingResult::State::Responded;
    r.latencyMs = latencyMs;
    const QJsonObject players = status.value(QLatin1String("players")).toObject();
    r.onlinePlayers = intOr(players.value(QLatin1String("online")), -1);
    r.maxPlayers = intOr(players.value(QLatin1String("max")), -1);
    QString version = sanitize(stripFormatting(status.value(QLatin1String("version")).toObject().value(QLatin1String("name")).toString()), false).trimmed();
    if (version.size() > kMaxVersionChars)
        version = version.left(kMaxVersionChars - 1) + QChar(0x2026);
    r.versionName = version;
    r.motd = flattenMotd(status.value(QLatin1String("description")));
    return r;
}

struct ServerPinger::Job {
    quint64 id = 0;
    quint64 round = 0;
    QString address;
    QTimer* timer = nullptr;
    QPointer<McResolver> resolver;
    QPointer<McClient> client;
    QElapsedTimer latency;
};

ServerPinger::ServerPinger(QObject* parent) : QObject(parent) {}

ServerPinger::~ServerPinger()
{
    const auto jobs = m_jobs.values();
    m_jobs.clear();
    for (Job* job : jobs) {
        delete job->timer;
        delete job->client.data();
        delete job->resolver.data();
        delete job;
    }
}

void ServerPinger::enqueue(const QString& address)
{
    if (address.isEmpty() || m_results.contains(address) || m_pending.contains(address))
        return;
    m_pending.insert(address);
    m_queue.append(address);
}

void ServerPinger::ping(const QString& address)
{
    enqueue(address);
    pump();
}

void ServerPinger::pingAll(const QStringList& addresses)
{
    for (const QString& address : addresses)
        enqueue(address);
    pump();
}

void ServerPinger::refresh(const QStringList& addresses)
{
    ++m_round;
    m_queue.clear();
    m_pending.clear();
    m_results.clear();
    if (addresses.isEmpty()) {
        emit settled();
        return;
    }
    pingAll(addresses);
}

void ServerPinger::pump()
{
    if (m_pumping)  // deliver() may call back into ping()
        return;
    m_pumping = true;
    while (!m_queue.isEmpty()) {
        const QString address = m_queue.first();
        if (!parseAddress(address).valid) {  // never touches the network, so it takes no slot
            m_queue.removeFirst();
            PingResult r;
            r.state = PingResult::State::InvalidAddress;
            deliver(address, r);
            continue;
        }
        if (static_cast<int>(m_jobs.size()) >= kMaxInFlight)
            break;
        m_queue.removeFirst();
        start(address);
    }
    m_pumping = false;
}

void ServerPinger::start(const QString& address)
{
    const HostPort hp = parseAddress(address);
    auto* job = new Job;
    job->id = m_nextJob++;
    job->round = m_round;
    job->address = address;
    const quint64 id = job->id;
    m_jobs.insert(id, job);
    qDebug() << "Server Vault: pinging" << address;

    job->timer = new QTimer(this);
    job->timer->setSingleShot(true);
    job->timer->setInterval(kTimeoutMs);
    connect(job->timer, &QTimer::timeout, this, [this, id]() {
        PingResult r;
        r.state = PingResult::State::NoResponse;
        finish(id, r);
    });
    job->timer->start();

    auto fail = [this, id]() {
        PingResult r;
        r.state = PingResult::State::NoResponse;
        finish(id, r);
    };
    auto startClient = [this, id, fail](const QString& domain, const QString& ip, int port) {
        const auto it = m_jobs.constFind(id);
        if (it == m_jobs.constEnd())
            return;
        Job* job = *it;
        auto* client = new McClient(nullptr, domain, ip, static_cast<uint16_t>(port));
        job->client = client;
        connect(client, &McClient::succeeded, this, [this, id](const QJsonObject& data) {
            const auto found = m_jobs.constFind(id);
            if (found == m_jobs.constEnd())
                return;
            finish(id, resultFromStatus(data, static_cast<int>((*found)->latency.elapsed())));
        });
        connect(client, &McClient::failed, this, [fail](const QString&) { fail(); });
        connect(client, &McClient::finished, client, &QObject::deleteLater);
        job->latency.start();  // connect + status request: the round trip the player would see in the server list
        client->getStatusData();
    };

    if (QHostAddress literal; literal.setAddress(hp.host)) {  // an IP: nothing to resolve (and no DNS query leaves the machine)
        startClient(hp.host, hp.host, hp.port);
        return;
    }
    auto* resolver = new McResolver(nullptr, hp.host, hp.port);
    job->resolver = resolver;
    connect(resolver, &McResolver::succeeded, this, [startClient, hp](const QString& ip, int port) { startClient(hp.host, ip, port); });
    connect(resolver, &McResolver::failed, this, [fail](const QString&) { fail(); });
    connect(resolver, &McResolver::finished, resolver, &QObject::deleteLater);
    resolver->ping();
}

void ServerPinger::finish(quint64 jobId, const PingResult& result)
{
    const auto it = m_jobs.find(jobId);
    if (it == m_jobs.end())
        return;  // already finished (a timeout racing a late socket error, ...)
    Job* job = *it;
    m_jobs.erase(it);
    const QString address = job->address;
    const bool current = job->round == m_round;

    job->timer->stop();
    job->timer->deleteLater();
    for (QObject* object : { static_cast<QObject*>(job->client.data()), static_cast<QObject*>(job->resolver.data()) }) {
        if (!object)
            continue;
        QObject::disconnect(object, nullptr, nullptr, nullptr);  // no signal of a finished job may reach us again
        object->deleteLater();
    }
    delete job;

    if (current)
        deliver(address, result);
    pump();
}

void ServerPinger::deliver(const QString& address, const PingResult& result)
{
    m_results.insert(address, result);
    m_pending.remove(address);
    qDebug() << "Server Vault: ping of" << address << "->"
             << (result.state == PingResult::State::Responded         ? "responded"
                 : result.state == PingResult::State::InvalidAddress ? "invalid address"
                                                                      : "no response");
    emit this->result(address, result);
    if (m_pending.isEmpty())
        emit settled();
}

}  // namespace Servers
}  // namespace Zero
