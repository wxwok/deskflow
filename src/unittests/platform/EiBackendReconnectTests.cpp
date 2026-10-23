/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EiBackendReconnectTests.h"

#include "platform/EiBackendReconnect.h"

#include <libei.h>

#include <sys/socket.h>

using deskflow::decideEiBackendReconnect;
using deskflow::EiBackendReconnect;

void EiBackendReconnectTests::freshContext_attaches()
{
  QCOMPARE(decideEiBackendReconnect(false), EiBackendReconnect::Attach);
}

void EiBackendReconnectTests::attachedBackend_mustBeReplacedBeforeTheNextFd()
{
  // ei_setup_backend_fd() asserts !ei->backend and abort()s. That is the
  // core dying with status 6 after "recreating input capture session".
  QCOMPARE(decideEiBackendReconnect(true), EiBackendReconnect::ReplaceThenAttach);
}

void EiBackendReconnectTests::secondEisFd_attachesOnANewContext()
{
  int fds[2] = {-1, -1};
  QVERIFY(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == 0);

  ei *first = ei_new_receiver(nullptr);
  QVERIFY(first != nullptr);
  QCOMPARE(ei_setup_backend_fd(first, fds[0]), 0);

  // Same context, second fd: libei asserts !ei->backend and abort()s.
  // Production replaces the context first, which is what this does.
  QVERIFY(decideEiBackendReconnect(true) == EiBackendReconnect::ReplaceThenAttach);
  ei *second = ei_new_receiver(nullptr);
  QVERIFY(second != nullptr);
  QVERIFY(decideEiBackendReconnect(false) == EiBackendReconnect::Attach);
  QCOMPARE(ei_setup_backend_fd(second, fds[1]), 0);

  // These contexts never completed an EIS handshake. ei_unref() then
  // crashes in ei_disconnect(). The process reclaims them on exit. The
  // failure this guards is the abort above, not context destruction.
  (void)first;
  (void)second;
}

QTEST_MAIN(EiBackendReconnectTests)
