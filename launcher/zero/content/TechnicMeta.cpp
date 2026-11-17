// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Technic Platform API parsing and preview text helpers (see TechnicMeta.h).
#include "TechnicMeta.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

#include "Json.h"
#include "modplatform/technic/SolderPackManifest.h"

namespace Zero {
namespace Content {

namespace {

/// A string field, empty when missing, null or not a string.
QString stringOf(const QJsonObject& obj, const QString& key)
{
    const QJsonValue v = obj.value(key);
    return v.isString() ? v.toString() : QString();
}

/// An id that the API sends as a number (details) or a string (lists).
QString idOf(const QJsonObject& obj)
{
    const QJsonValue v = obj.value("id");
    if (v.isString()) {
        return v.toString();
    }
    if (v.isDouble()) {
        return QString::number(qint64(v.toDouble()));
    }
    return {};
}

qint64 countOf(const QJsonObject& obj, const QString& key)
{
    const QJsonValue v = obj.value(key);
    if (v.isDouble()) {
        return std::max<qint64>(0, qint64(v.toDouble()));
    }
    if (v.isString()) {
        bool ok = false;
        const qint64 n = v.toString().toLongLong(&ok);
        return ok ? std::max<qint64>(0, n) : 0;
    }
    return 0;
}

bool boolOf(const QJsonObject& obj, const QString& key)
{
    return obj.value(key).toBool(false);
}

/// The `url` of an image object (`{"url": "..."}`), empty when missing.
QString imageUrlOf(const QJsonObject& obj, const QString& key)
{
    const QJsonValue v = obj.value(key);
    return v.isObject() ? stringOf(v.toObject(), "url") : QString();
}

QStringList tagsOf(const QJsonObject& obj)
{
    QStringList raw;
    const QJsonValue v = obj.value("tags");
    if (v.isString()) {
        raw = v.toString().split(',');
    } else if (v.isArray()) {
        for (const QJsonValue tag : v.toArray()) {
            if (tag.isString()) {
                raw.append(tag.toString());
            }
        }
    }
    QStringList tags;
    for (const QString& tag : raw) {
        const QString t = tag.trimmed();
        if (!t.isEmpty() && !tags.contains(t)) {
            tags.append(t);
        }
    }
    return tags;
}

QList<TechnicFeedEntry> feedOf(const QJsonObject& obj)
{
    QList<TechnicFeedEntry> feed;
    for (const QJsonValue raw : obj.value("feed").toArray()) {
        if (!raw.isObject()) {
            continue;
        }
        const QJsonObject e = raw.toObject();
        TechnicFeedEntry entry;
        entry.user = stringOf(e, "user");
        entry.date = countOf(e, "date");
        entry.content = stringOf(e, "content");
        entry.url = stringOf(e, "url");
        if (entry.content.isEmpty() && entry.url.isEmpty()) {
            continue;
        }
        feed.append(entry);
    }
    std::stable_sort(feed.begin(), feed.end(), [](const TechnicFeedEntry& a, const TechnicFeedEntry& b) { return a.date > b.date; });
    if (feed.size() > kMaxFeedEntries) {
        feed.resize(kMaxFeedEntries);
    }
    return feed;
}

QString stripTrailingSlashes(QString url)
{
    while (url.endsWith('/')) {
        url.chop(1);
    }
    return url;
}

bool isWebUrl(const QString& url)
{
    const QUrl parsed(url);
    return parsed.isValid() && (parsed.scheme() == "https" || parsed.scheme() == "http") && !parsed.host().isEmpty();
}

}  // namespace

QString TechnicPackDetails::installUrl() const
{
    return downloadUrl.isEmpty() ? stripTrailingSlashes(solderUrl) : downloadUrl;
}

Result<QList<TechnicPackSummary>> parseSearchResponse(const QByteArray& data)
{
    TRY_INTO(const QJsonObject root, Json::requireObject(data, "Technic search response"))
    TRY_INTO(const QJsonArray packs, Json::requireArray(root, "modpacks"))

    QList<TechnicPackSummary> out;
    for (const QJsonValue raw : packs) {
        TRY_INTO(const QJsonObject obj, Json::requireObject(raw, "Technic search result"))
        TechnicPackSummary pack;
        TRY_INTO(pack.name, Json::requireString(obj, "name"))
        TRY_INTO(pack.slug, Json::requireString(obj, "slug"))
        if (pack.slug == "vanilla") {
            continue;
        }
        pack.id = idOf(obj);
        pack.platformUrl = stringOf(obj, "url");
        pack.iconUrl = stringOf(obj, "iconUrl");
        out.append(pack);
    }
    return out;
}

Result<TechnicPackDetails> parsePackDetails(const QByteArray& data)
{
    TRY_INTO(const QJsonObject root, Json::requireObject(data, "Technic pack details"))
    if (root.contains("error")) {
        const QString error = root.value("error").toString().trimmed();
        return std::unexpected(error.isEmpty() ? QCoreApplication::translate("TechnicMeta", "Technic doesn't know this pack.") : QCoreApplication::translate("TechnicMeta", "Technic: %1").arg(error));
    }

    TechnicPackDetails d;
    TRY_INTO(d.slug, Json::requireString(root, "name"))
    if (d.slug.trimmed().isEmpty()) {
        return std::unexpected(QCoreApplication::translate("TechnicMeta", "Technic pack details have an empty name."));
    }
    d.id = idOf(root);
    d.displayName = stringOf(root, "displayName");
    d.author = stringOf(root, "user");
    d.description = stringOf(root, "description");
    d.minecraftVersion = stringOf(root, "minecraft");
    d.version = stringOf(root, "version");
    d.platformUrl = stringOf(root, "platformUrl");
    d.iconUrl = imageUrlOf(root, "icon");
    d.logoUrl = imageUrlOf(root, "logo");
    d.backgroundUrl = imageUrlOf(root, "background");
    d.installs = countOf(root, "installs");
    d.runs = countOf(root, "runs");
    d.ratings = countOf(root, "ratings");
    d.tags = tagsOf(root);
    d.isOfficial = boolOf(root, "isOfficial");
    d.isServer = boolOf(root, "isServer");
    d.downloadUrl = stringOf(root, "url").trimmed();
    d.solderUrl = stringOf(root, "solder").trimmed();
    d.feed = feedOf(root);
    return d;
}

Result<TechnicSolderVersions> parseSolderVersions(const QByteArray& data)
{
    TRY_INTO(const QJsonObject root, Json::requireObject(data, "Solder response"))
    TechnicSolder::Pack pack;
    TRY(TechnicSolder::loadPack(pack, root))
    TechnicSolderVersions out;
    out.recommended = pack.recommended;
    out.latest = pack.latest;
    out.builds = pack.builds;
    return out;
}

QString solderPackUrl(const QString& solderBase, const QString& slug)
{
    return stripTrailingSlashes(solderBase) + "/modpack/" + QString::fromLatin1(QUrl::toPercentEncoding(slug));
}

QString previewTitle(const TechnicPackDetails* details, const QString& listName, const QString& slug)
{
    if (details && !details->displayName.trimmed().isEmpty()) {
        return details->displayName.trimmed();
    }
    if (!listName.trimmed().isEmpty()) {
        return listName.trimmed();
    }
    return slug.trimmed();
}

QString plainText(const QString& html)
{
    static const QRegularExpression breaks(QStringLiteral("<\\s*(br|/?p|/?div|/?li|/?ul|/?ol|/?h[1-6])\\b[^>]*>"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    static const QRegularExpression numeric(QStringLiteral("&#(x[0-9a-fA-F]{1,6}|[0-9]{1,7});"));

    QString text = html;
    text.replace(breaks, QStringLiteral(" "));
    text.remove(tags);

    // Numeric entities, then the common named ones; &amp; last so "&amp;lt;" stays the literal text "&lt;".
    QString decoded;
    decoded.reserve(text.size());
    qsizetype last = 0;
    for (auto it = numeric.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        decoded += QStringView(text).mid(last, m.capturedStart() - last);
        const QString code = m.captured(1);
        bool ok = false;
        const uint cp = code.startsWith('x') ? code.mid(1).toUInt(&ok, 16) : code.toUInt(&ok, 10);
        if (ok && cp > 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
            const char32_t c = cp;
            decoded += QString::fromUcs4(&c, 1);
        } else {
            decoded += m.captured(0);
        }
        last = m.capturedEnd();
    }
    decoded += QStringView(text).mid(last);

    decoded.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    decoded.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    decoded.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    decoded.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    decoded.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    decoded.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return decoded.simplified();
}

QString plainTextSummary(const QString& html, int maxChars)
{
    if (maxChars <= 0) {
        return {};
    }
    const QString text = plainText(html);
    if (text.size() <= maxChars) {
        return text;
    }
    QString cut = text.left(maxChars - 1);
    if (!cut.isEmpty() && cut.back().isHighSurrogate()) {
        cut.chop(1);
    }
    const qsizetype space = cut.lastIndexOf(' ');
    if (space > maxChars / 2) {
        cut = cut.left(space);
    }
    return cut.trimmed() + QChar(0x2026);
}

QString previewHtml(const TechnicPackDetails& details, const QString& title)
{
    QString html = isWebUrl(details.platformUrl)
                       ? QStringLiteral("<a href=\"%1\">%2</a>").arg(details.platformUrl.toHtmlEscaped(), title.toHtmlEscaped())
                       : title.toHtmlEscaped();
    if (!details.author.trimmed().isEmpty()) {
        html += "<br>" + QCoreApplication::translate("TechnicMeta", "by %1").arg(details.author.trimmed().toHtmlEscaped());
    }
    QStringList facts;
    if (!details.minecraftVersion.trimmed().isEmpty()) {
        facts.append(QCoreApplication::translate("TechnicMeta", "Minecraft %1").arg(details.minecraftVersion.trimmed().toHtmlEscaped()));
    }
    if (!details.version.trimmed().isEmpty()) {
        facts.append(QCoreApplication::translate("TechnicMeta", "Version %1").arg(details.version.trimmed().toHtmlEscaped()));
    }
    if (!facts.isEmpty()) {
        html += "<br>" + facts.join(QStringLiteral(" &middot; "));
    }
    if (!details.isInstallable()) {
        html += "<br><br>" + QCoreApplication::translate("TechnicMeta", "This pack has no download, so it can't be installed.").toHtmlEscaped();
    }
    html += "<br><br>" + details.description;
    return html;
}

QString previewLoadingHtml(const QString& title)
{
    return title.toHtmlEscaped() + "<br><br>" + QCoreApplication::translate("TechnicMeta", "Loading details…").toHtmlEscaped();
}

QString previewFailedHtml(const QString& title, const QString& reason)
{
    QString html = title.toHtmlEscaped() + "<br><br>" + QCoreApplication::translate("TechnicMeta", "Couldn't load the details of this pack.").toHtmlEscaped();
    if (!reason.trimmed().isEmpty()) {
        html += "<br>" + reason.trimmed().toHtmlEscaped();
    }
    return html;
}

}  // namespace Content
}  // namespace Zero
