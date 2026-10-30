/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

namespace deskflow {

/// What to do with an EIS fd from a new portal session.
///
/// libei installs exactly one backend per context. ei_setup_backend_fd()
/// asserts `!ei->backend` and abort()s (SIGABRT, process status 6) when a
/// second fd is attached to the context that already serves the previous
/// InputCapture session. The recovery path has to destroy that context first.
enum class EiBackendReconnect
{
  /// No backend yet. ei_setup_backend_fd() is safe.
  Attach,
  /// A backend is already installed. Replace the ei context, then attach.
  ReplaceThenAttach
};

inline EiBackendReconnect decideEiBackendReconnect(bool backendAlreadyAttached)
{
  if (backendAlreadyAttached) {
    return EiBackendReconnect::ReplaceThenAttach;
  }
  return EiBackendReconnect::Attach;
}

} // namespace deskflow
