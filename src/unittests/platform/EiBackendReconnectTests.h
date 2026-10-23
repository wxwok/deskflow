/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class EiBackendReconnectTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void freshContext_attaches();
  void attachedBackend_mustBeReplacedBeforeTheNextFd();
  void secondEisFd_attachesOnANewContext();
};
