// SPDX-License-Identifier: GPL-3.0-only
// Fixtures of DependencyInspector_test.cpp. They live in a header because Qt AutoMoc mis-parses raw strings that contain these sequences.
#pragma once

#include <QByteArray>

inline const QByteArray kFmodToml = QByteArray(R"RAW(
modLoader="javafml"
loaderVersion="[47,)"
license="MIT"
[[mods]]
modId="fmod"
version="1.0.0"
displayName="Forge Mod"
[[dependencies.fmod]]
modId="forge"
mandatory=true
versionRange="[47.1.0,)"
side="BOTH"
[[dependencies.fmod]]
modId="minecraft"
mandatory=true
versionRange="[1.20.1,1.21)"
side="BOTH"
[[dependencies.fmod]]
modId="serveronly"
mandatory=true
versionRange="*"
side="SERVER"
[[dependencies.fmod]]
modId="optionalmod"
mandatory=false
versionRange="[2,3)"
[[dependencies.fmod]]
modId="flib"
mandatory=true
versionRange="[2,)"
side="BOTH"
)RAW");

inline const QByteArray kFlibToml = QByteArray(R"RAW(
modLoader="javafml"
loaderVersion="[47,)"
license="MIT"
[[mods]]
modId="flib"
version="1.0.0"
)RAW");

inline const QByteArray kForeignToml = QByteArray(R"RAW(
modLoader="javafml"
loaderVersion="[47,)"
license="MIT"
[[mods]]
modId="fmod"
version="1.0.0"
[[dependencies.fmod]]
modId="flib"
mandatory=true
versionRange="*"
side="BOTH"
)RAW");
