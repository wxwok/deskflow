/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

namespace deskflow {

/// What the server should do when it checks the computer that currently has
/// the pointer.
enum class ActiveClientWatchDecision
{
  Keep,
  ForceReturn
};

struct ActiveClientWatchInput
{
  /// The pointer is on a remote computer, not the server.
  bool pointerOnRemote = false;
  /// A return to the server is already in progress. A reconnect during that
  /// window must not start another one.
  bool returnInProgress = false;
  /// Seconds since that computer last sent any protocol message.
  double secondsSinceLastMessage = 0;
  /// Silence limit in seconds. Zero disables the watch.
  double timeoutSeconds = 0;
};

/// Force the pointer home when the active computer stops answering.
/// Keep-alives already arm a one-shot flatline timer; this is the periodic
/// check that still returns focus if that path is late, and it ignores a
/// client that has reconnected but does not hold the pointer.
inline ActiveClientWatchDecision decideActiveClientWatch(const ActiveClientWatchInput &input)
{
  if (!input.pointerOnRemote || input.returnInProgress || input.timeoutSeconds <= 0.0) {
    return ActiveClientWatchDecision::Keep;
  }
  if (input.secondsSinceLastMessage < input.timeoutSeconds) {
    return ActiveClientWatchDecision::Keep;
  }
  return ActiveClientWatchDecision::ForceReturn;
}

/// Clamp a millisecond setting to the silence limit, in seconds.
/// Zero or negative disables the limit. Anything else is kept inside 1s..120s.
inline double clampActiveClientTimeoutSeconds(int milliseconds)
{
  if (milliseconds <= 0) {
    return 0.0;
  }
  const double seconds = static_cast<double>(milliseconds) / 1000.0;
  if (seconds < 1.0) {
    return 1.0;
  }
  if (seconds > 120.0) {
    return 120.0;
  }
  return seconds;
}

} // namespace deskflow
