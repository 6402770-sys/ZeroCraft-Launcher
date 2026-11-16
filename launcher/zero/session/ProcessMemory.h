// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - memory use of the game process (Windows working set). Nullopt when it cannot be read: the card then shows no RAM line.
#pragma once

#include <QtGlobal>
#include <optional>

namespace Zero {
namespace Session {

/// Working set in bytes of process `pid`, or nullopt (not Windows, no such process, no access, pid <= 0).
std::optional<qint64> processWorkingSetBytes(qint64 pid);

}  // namespace Session
}  // namespace Zero
