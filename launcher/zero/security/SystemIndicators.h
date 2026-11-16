// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - read-only look for the documented traces a successful fractureiser infection leaves on the PC (the "system-level warning"
// of "Before you play"). Nothing here writes, deletes or runs anything; each trace is a path (or a startup entry) that exists only after
// the malware ran, taken from the investigation's docs (docs/users.md, docs/tech.md of
// https://github.com/fractureiser-investigation/fractureiser). No widgets.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace Zero {
namespace Security {

struct SystemEnv {
    QString localAppData;  ///< %LOCALAPPDATA%
    QString appData;       ///< %APPDATA% (Roaming)
    QString userProfile;   ///< %USERPROFILE%
    /// Values (data text) of HKCU\Software\Microsoft\Windows\CurrentVersion\Run; may be empty (tests, other systems).
    std::function<QStringList()> runKeyValues;

    /// The real folders of the current user.
    static SystemEnv current();
};

struct SystemTrace {
    QString id;           ///< "microsoft-edge-folder" ...
    QString path;         ///< the file / folder (or the registry key) that was found
    QString description;  ///< one short sentence
    QString infoUrl;
};

/// The traces that exist now, in a fixed order. Empty = nothing found.
QList<SystemTrace> findSystemTraces(const SystemEnv& env);

}  // namespace Security
}  // namespace Zero
