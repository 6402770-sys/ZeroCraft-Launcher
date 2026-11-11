// SPDX-License-Identifier: GPL-3.0-only
// Fixtures of ModHealth_test.cpp. They live in a header because Qt AutoMoc mis-parses raw strings that contain these sequences.
#pragma once

#include <QByteArray>

inline const QByteArray kForgeToml = QByteArray(R"RAW(
modLoader="javafml"
loaderVersion="[47,)"
license="MIT"
[[mods]]
modId="forgemod"
version="3.1.4"
displayName="Forge Mod"
[[dependencies.forgemod]]
modId="forge"
mandatory=true
versionRange="[47.1.0,)"
ordering="NONE"
side="BOTH"
[[dependencies.forgemod]]
modId="minecraft"
mandatory=true
versionRange="[1.20.1,1.21)"
side="BOTH"
[[dependencies.forgemod]]
modId="optionalmod"
mandatory=false
versionRange="[2,3)"
[[dependencies.forgemod]]
modId="serveronly"
mandatory=true
versionRange="*"
side="SERVER"
[[dependencies.forgemod]]
modId="noflag"
versionRange="*"
)RAW");

inline const QByteArray kNeoForgeToml = QByteArray(R"RAW(
modLoader="javafml"
loaderVersion="[2,)"
license="MIT"
[[mods]]
modId="neomod"
version="1.0.0"
[[dependencies.neomod]]
modId="neoforge"
type="required"
versionRange="[21.1,)"
side="BOTH"
[[dependencies.neomod]]
modId="soft"
type="optional"
versionRange="[1,)"
[[dependencies.neomod]]
modId="bad"
type="incompatible"
versionRange="[1,2)"
[[dependencies.neomod]]
modId="meh"
type="discouraged"
versionRange="*"
[[dependencies.neomod]]
modId="untyped"
versionRange="*"
)RAW");

inline const QByteArray kMultiModToml = QByteArray(R"RAW(
modLoader="javafml"
loaderVersion="[47,)"
license="MIT"
[[mods]]
modId="first"
version="1.0"
[[mods]]
modId="second"
version="2.0"
[[dependencies.first]]
modId="lib"
mandatory=true
versionRange="[1,)"
[[dependencies.second]]
modId="other"
mandatory=true
versionRange="[2,)"
)RAW");

inline const QByteArray kJarJarJson = QByteArray(R"RAW({"jars":[{"identifier":{"group":"g","artifact":"a"},"version":{"range":"[1,)","artifactVersion":"1.0"},"path":"META-INF/jarjar/a-1.0.jar"}]})RAW");
