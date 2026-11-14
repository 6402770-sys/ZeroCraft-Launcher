// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

namespace ZeroSetup {

/// A payload path is a relative, '/'-separated path made of plain names. Rejected: empty, absolute ("/x", "\x"),
/// drive letters / streams (any ':'), backslashes, "." / ".." / empty segments, control characters, segments that
/// Windows would silently rewrite (trailing dot or space) and DOS device names (CON, NUL, COM1 ...).
bool isSafeRelativePath(const QString& rel, QString* why = nullptr);

/// True when root/rel (lexically cleaned) stays strictly inside root. Both arguments must already be absolute.
bool staysInside(const QString& root, const QString& rel);

}  // namespace ZeroSetup
