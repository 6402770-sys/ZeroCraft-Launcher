// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - project icons of the installed shader packs (docs/zero/ARCHITECTURE.md, "Shader pack icons"). A shader zip has no
// icon of its own; a pack installed from Modrinth / CurseForge has its project id in the packwiz metadata, and the provider knows the
// icon URL. The pure part: collect the ids per provider, map the API answers to URLs. The impure part (the network lookup, one batched
// request per provider) is a `Lookup` function the page can have injected, so tests need no network.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

#include "modplatform/ModIndex.h"

namespace Zero::ShaderIcons {

/// A pack's platform project, as its metadata says.
struct Ref {
    ModPlatform::ResourceProvider provider = ModPlatform::ResourceProvider::MODRINTH;
    QString projectId;
};

/// "modrinth:<id>" / "curseforge:<id>"; empty when the id is empty. The key of every map below.
QString keyOf(ModPlatform::ResourceProvider provider, const QString& projectId);

/// Project ids still to ask, per provider (unique, sorted).
struct Ids {
    QStringList modrinth;
    QStringList curseforge;
    bool isEmpty() const { return modrinth.isEmpty() && curseforge.isEmpty(); }
};

/// The ids of `refs` whose key is in neither `known` (url already resolved) nor `asked` (already requested this session).
Ids collectIds(const QList<Ref>& refs, const QHash<QString, QString>& known, const QSet<QString>& asked);

/// Modrinth `GET /v2/projects?ids=[...]` -> key -> `icon_url` (projects without an icon are left out; bad JSON -> empty).
QHash<QString, QString> parseModrinth(const QByteArray& json);
/// CurseForge `POST /v1/mods` -> key -> `logo.thumbnailUrl` (else `logo.url`).
QHash<QString, QString> parseCurseForge(const QByteArray& json);

/// Asks the providers for `ids` and calls `done` (GUI thread, at most once per provider, only on success) with key -> URL.
using Lookup = std::function<void(const Ids& ids, std::function<void(const QHash<QString, QString>&)> done)>;
/// The production lookup: Prism's network stack (ModrinthAPI / FlameAPI::getProjects), asynchronous, batches of 50 ids.
Lookup networkLookup();

/// The session-wide key -> URL map and the set of keys already requested (so reopening the page never asks again).
QHash<QString, QString>& sessionUrls();
QSet<QString>& sessionAsked();

}  // namespace Zero::ShaderIcons
