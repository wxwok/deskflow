/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ActiveClientWatchTests.h"

#include "server/ActiveClientWatch.h"

using deskflow::ActiveClientWatchDecision;
using deskflow::ActiveClientWatchInput;
using deskflow::clampActiveClientTimeoutSeconds;
using deskflow::decideActiveClientWatch;

void ActiveClientWatchTests::silenceOnTheActiveClient_forcesReturn()
{
  // Heartbeat flatline and the periodic watch share this rule: the computer
  // that has the pointer, and no message inside the limit, must come home.
  const ActiveClientWatchInput input{true, false, 5.0, 5.0};
  QCOMPARE(decideActiveClientWatch(input), ActiveClientWatchDecision::ForceReturn);

  const ActiveClientWatchInput longer{true, false, 9.0, 5.0};
  QCOMPARE(decideActiveClientWatch(longer), ActiveClientWatchDecision::ForceReturn);
}

void ActiveClientWatchTests::recentMessage_keepsThePointer()
{
  // A keep-alive every 3s stays under a 5s limit.
  const ActiveClientWatchInput input{true, false, 3.0, 5.0};
  QCOMPARE(decideActiveClientWatch(input), ActiveClientWatchDecision::Keep);
}

void ActiveClientWatchTests::reconnectDuringReturn_doesNotForceAgain()
{
  // The return has started. A reconnect of the same computer does not hold
  // the pointer, and a second tick must not start another return.
  const ActiveClientWatchInput inProgress{true, true, 30.0, 5.0};
  QCOMPARE(decideActiveClientWatch(inProgress), ActiveClientWatchDecision::Keep);

  const ActiveClientWatchInput reconnectedButIdle{false, false, 30.0, 5.0};
  QCOMPARE(decideActiveClientWatch(reconnectedButIdle), ActiveClientWatchDecision::Keep);
}

void ActiveClientWatchTests::clientThatDoesNotHoldThePointer_isIgnored()
{
  const ActiveClientWatchInput input{false, false, 100.0, 5.0};
  QCOMPARE(decideActiveClientWatch(input), ActiveClientWatchDecision::Keep);
}

void ActiveClientWatchTests::disabledTimeout_keepsThePointer()
{
  const ActiveClientWatchInput input{true, false, 100.0, 0.0};
  QCOMPARE(decideActiveClientWatch(input), ActiveClientWatchDecision::Keep);
}

void ActiveClientWatchTests::timeoutSetting_clampsToAFewSeconds()
{
  QCOMPARE(clampActiveClientTimeoutSeconds(5000), 5.0);
  QVERIFY(clampActiveClientTimeoutSeconds(0) == 0.0);
  QVERIFY(clampActiveClientTimeoutSeconds(-1) == 0.0);
  QCOMPARE(clampActiveClientTimeoutSeconds(500), 1.0);
  QCOMPARE(clampActiveClientTimeoutSeconds(600000), 120.0);
}

QTEST_MAIN(ActiveClientWatchTests)
