/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EiRelativePointerTests.h"

#include "platform/EiRelativePointer.h"

#include <cmath>
#include <cstdint>
#include <limits>

using deskflow::accumulateRelativePointer;
using deskflow::kMaxRelativePointerDelta;
using deskflow::RelativePointerDelta;

// QVERIFY must live in the test function; a helper would return only from itself.
#define EXPECT_REMAINDER(actual, expected) QVERIFY(std::fabs((actual) - (expected)) < 1e-9)

void EiRelativePointerTests::fractionalMotion_emitsTruncatedPixelsAndKeepsRemainder()
{
  double bufferX = 0;
  double bufferY = 0;

  // 0.25 is exact in binary, so four samples must sum to one pixel.
  for (int i = 0; i < 3; ++i) {
    const RelativePointerDelta motion = accumulateRelativePointer(bufferX, bufferY, 0.25, -0.25);
    QCOMPARE(motion.x, 0);
    QCOMPARE(motion.y, 0);
    QVERIFY(!motion.droppedX);
    QVERIFY(!motion.droppedY);
    QVERIFY(!motion.reset);
  }
  EXPECT_REMAINDER(bufferX, 0.75);
  EXPECT_REMAINDER(bufferY, -0.75);

  const RelativePointerDelta motion = accumulateRelativePointer(bufferX, bufferY, 0.25, -0.5);
  QCOMPARE(motion.x, 1);
  QCOMPARE(motion.y, -1);
  EXPECT_REMAINDER(bufferX, 0);
  EXPECT_REMAINDER(bufferY, -0.25);

  // Truncation is toward zero, including for a value already past one pixel.
  const RelativePointerDelta jumped = accumulateRelativePointer(bufferX, bufferY, 2.9, -1.1);
  QCOMPARE(jumped.x, 2);
  QCOMPARE(jumped.y, -1);
  EXPECT_REMAINDER(bufferX, 0.9);
  EXPECT_REMAINDER(bufferY, -0.35);
}

void EiRelativePointerTests::nonFiniteDelta_isDroppedAndDoesNotStick()
{
  const double quietNaN = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();

  double bufferX = 0.4;
  double bufferY = -0.4;

  const RelativePointerDelta nanSample = accumulateRelativePointer(bufferX, bufferY, quietNaN, -quietNaN);
  QCOMPARE(nanSample.x, 0);
  QCOMPARE(nanSample.y, 0);
  QVERIFY(nanSample.droppedX);
  QVERIFY(nanSample.droppedY);
  QVERIFY(!nanSample.reset);
  EXPECT_REMAINDER(bufferX, 0.4);
  EXPECT_REMAINDER(bufferY, -0.4);

  const RelativePointerDelta infSample = accumulateRelativePointer(bufferX, bufferY, infinity, -infinity);
  QCOMPARE(infSample.x, 0);
  QCOMPARE(infSample.y, 0);
  QVERIFY(infSample.droppedX);
  QVERIFY(infSample.droppedY);
  EXPECT_REMAINDER(bufferX, 0.4);
  EXPECT_REMAINDER(bufferY, -0.4);

  // The next real sample still moves by a single pixel. It must not report INT_MIN.
  const RelativePointerDelta recovered = accumulateRelativePointer(bufferX, bufferY, 0.7, -0.7);
  QCOMPARE(recovered.x, 1);
  QCOMPARE(recovered.y, -1);
  QVERIFY(recovered.x != std::numeric_limits<std::int32_t>::min());
  QVERIFY(recovered.y != std::numeric_limits<std::int32_t>::min());
  EXPECT_REMAINDER(bufferX, 0.1);
  EXPECT_REMAINDER(bufferY, -0.1);
}

void EiRelativePointerTests::outOfRangeFiniteDelta_isDroppedAndDoesNotStick()
{
  double bufferX = 0.4;
  double bufferY = 0.2;

  const RelativePointerDelta huge = accumulateRelativePointer(bufferX, bufferY, 1e20, -3e9);
  QCOMPARE(huge.x, 0);
  QCOMPARE(huge.y, 0);
  QVERIFY(huge.droppedX);
  QVERIFY(huge.droppedY);
  QVERIFY(!huge.reset);
  EXPECT_REMAINDER(bufferX, 0.4);
  EXPECT_REMAINDER(bufferY, 0.2);

  const RelativePointerDelta overCap = accumulateRelativePointer(bufferX, bufferY, kMaxRelativePointerDelta + 1.0, 0);
  QVERIFY(overCap.droppedX);
  QVERIFY(!overCap.droppedY);
  QCOMPARE(overCap.x, 0);
  EXPECT_REMAINDER(bufferX, 0.4);

  const RelativePointerDelta atCap = accumulateRelativePointer(bufferX, bufferY, kMaxRelativePointerDelta, 0.0);
  QVERIFY(!atCap.droppedX);
  QCOMPARE(atCap.x, static_cast<std::int32_t>(kMaxRelativePointerDelta));
  EXPECT_REMAINDER(bufferX, 0.4);

  const RelativePointerDelta recovered = accumulateRelativePointer(bufferX, bufferY, 0.6, 0.0);
  QCOMPARE(recovered.x, 1);
  QCOMPARE(recovered.y, 0);
  QVERIFY(recovered.x != std::numeric_limits<std::int32_t>::min());
  EXPECT_REMAINDER(bufferX, 0.0);
}

void EiRelativePointerTests::poisonedAccumulator_resetsInsteadOfEmittingIntMin()
{
  const auto intMin = std::numeric_limits<std::int32_t>::min();

  double bufferX = std::numeric_limits<double>::quiet_NaN();
  double bufferY = 1e20;

  const RelativePointerDelta recovered = accumulateRelativePointer(bufferX, bufferY, 4.25, 1.5);
  QVERIFY(recovered.reset);
  QVERIFY(!recovered.droppedX);
  QVERIFY(!recovered.droppedY);
  QCOMPARE(recovered.x, 4);
  QCOMPARE(recovered.y, 1);
  QVERIFY(recovered.x != intMin);
  QVERIFY(recovered.y != intMin);
  EXPECT_REMAINDER(bufferX, 0.25);
  EXPECT_REMAINDER(bufferY, 0.5);

  // A second sample stays on the real pointer, which is the failure mode this
  // guards: previously every later event repeated INT_MIN.
  const RelativePointerDelta next = accumulateRelativePointer(bufferX, bufferY, 1.0, -0.25);
  QCOMPARE(next.x, 1);
  QCOMPARE(next.y, 0);
  QVERIFY(!next.reset);
  EXPECT_REMAINDER(bufferX, 0.25);
  EXPECT_REMAINDER(bufferY, 0.25);
}

void EiRelativePointerTests::axesAreIndependent()
{
  double bufferX = 0.2;
  double bufferY = 0.2;

  const RelativePointerDelta motion =
      accumulateRelativePointer(bufferX, bufferY, std::numeric_limits<double>::quiet_NaN(), 3.0);
  QVERIFY(motion.droppedX);
  QVERIFY(!motion.droppedY);
  QCOMPARE(motion.x, 0);
  QCOMPARE(motion.y, 3);
  EXPECT_REMAINDER(bufferX, 0.2);
  EXPECT_REMAINDER(bufferY, 0.2);
}

QTEST_MAIN(EiRelativePointerTests)
