/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class ActiveClientWatchTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void silenceOnTheActiveClient_forcesReturn();
  void recentMessage_keepsThePointer();
  void reconnectDuringReturn_doesNotForceAgain();
  void clientThatDoesNotHoldThePointer_isIgnored();
  void disabledTimeout_keepsThePointer();
  void timeoutSetting_clampsToAFewSeconds();
};
