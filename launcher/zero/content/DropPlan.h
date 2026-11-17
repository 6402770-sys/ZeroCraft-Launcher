// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "smart drag and drop": the pure part. What a dropped file is, what the chosen instance thinks of it (a mod made for another
// loader or Minecraft version, a file that would replace another) and the texts of the "Add to an instance" dialog. No widgets, no APPLICATION.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "modplatform/ResourceType.h"
#include "zero/content/ModTarget.h"

namespace Zero {
namespace Mods {
struct ModMetadata;
}
namespace Content {
namespace Drop {

enum class Kind { Mod, ResourcePack, ShaderPack, World, DataPack, TexturePack };

/// Kind of a resource the launcher can take; ModPlatform::ResourceType::Unknown / Modpack / Screenshots have no kind (false).
bool kindFromResource(ModPlatform::ResourceType type, Kind* out);
/// "Mod", "Resource pack", "Shader pack", "World", "Data pack", "Texture pack".
QString kindLabel(Kind kind);
/// Instance window page that shows this kind ("mods", "resourcepacks", "shaderpacks", "worlds", "texturepacks"); empty for data packs.
QString pageId(Kind kind);
/// Data packs go inside a world: they are listed, not installed.
bool installable(Kind kind);

/// What a mod jar says about itself (read with Zero::Mods::parseJar). Empty fields = unknown.
struct ModInfo {
    QString displayName;
    ModPlatform::ModLoaderTypes loaders;           ///< loaders the jar has metadata for (Legacy Forge counts as Forge)
    struct Requirement {
        QString predicate;
        bool maven = false;  ///< Forge / NeoForge range instead of Fabric / Quilt predicate
    };
    QList<Requirement> minecraft;                  ///< the jar's requirement on "minecraft", one per metadata format that states it
};

struct DropFile {
    QString path;       ///< native absolute path
    QString fileName;
    QString name;       ///< what the row calls it: the mod's display name, else the file name
    qint64 size = -1;   ///< bytes; -1 for a folder (a world dropped unzipped)
    Kind kind = Kind::Mod;
    ModInfo mod;        ///< only for Kind::Mod
    bool isDir = false;
};

/// Reads the file (a mod jar is parsed read-only). Never throws; a jar that cannot be read just has no metadata.
DropFile inspectFile(const QString& path, Kind kind);
/// The metadata part alone (testable without a jar): built from a parsed jar.
ModInfo modInfoFrom(const Zero::Mods::ModMetadata& metadata);

/// Warning caption of a mod against the instance: "Made for Forge — this instance uses Fabric" or "Made for Minecraft 1.20.1 — this instance
/// is 1.21.1". Empty when it fits or when the jar or the instance does not say (unknown never warns). Other kinds: always empty.
QString fitCaption(const DropFile& file, const TargetInstance& target);
/// "Replaces the file with the same name" when `folder` already holds a file of that name (not for worlds, nor data packs).
QString replaceCaption(const DropFile& file, const QString& folder);
/// "Data packs go inside a world. Open the world's folder to add it." for a data pack, else empty.
QString notInstallableCaption(const DropFile& file);

QString titleText(int count);                          ///< "Add 1 file" / "Add 3 files"
QString summaryText(int ticked);                       ///< "Adds 1 file" / "Adds 3 files"; "Nothing to add" for 0
QString primaryText(const QString& instanceName);      ///< "Add to Fabric Fun"
QString addedToastText(int count, const QString& instanceName);  ///< "Added 3 files to Fabric Fun"
QString failedToastText(const QString& fileName);      ///< "Could not add sodium.jar"

/// Page the toast's "Open" goes to: "mods" when any added file is a mod, else the page of the first added kind.
QString openPage(const QList<DropFile>& added);

}  // namespace Drop
}  // namespace Content
}  // namespace Zero
