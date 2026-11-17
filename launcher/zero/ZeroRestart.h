// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - starting the launcher again (used when the language changes, which needs a fresh start).
//
// The new process is started before this one has quit. It does not own the single-instance lock yet, so it waits for this one to end
// first (the predecessor's process id travels in an environment variable); without the wait it would just hand its start to the old
// process and exit.
#pragma once

#include <QString>

namespace Zero::Restart {

/// Name of the environment variable that carries the process id of the launcher the new one replaces.
inline constexpr char kWaitPidVariable[] = "ZERO_RESTART_WAIT_PID";

/// Starts a new launcher (same executable; `dataPath`, when not empty, is passed as --dir) that waits for this process to end. True when it
/// was started.
/// The caller quits right after.
bool startSuccessor(const QString& dataPath);

/// Called by the launcher at start: when it was started by startSuccessor, waits (at most `timeoutMs`) until the process that started it
/// has ended. A start that is not a restart returns at once.
void waitForPredecessor(int timeoutMs = 20000);

}  // namespace Zero::Restart
