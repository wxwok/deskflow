/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/DirectionTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace deskflow {

//! One monitor in a computer's own coordinate space.
/*!
Coordinates are pixels. The rectangle is half-open: it covers
\c x .. \c x+width-1 and \c y .. \c y+height-1. Two monitors touch
when one rectangle's right/bottom edge equals the other's left/top
edge (no gap and no overlap).
*/
struct MonitorRect
{
  std::string name;
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;

  bool valid() const
  {
    return width > 0 && height > 0;
  }

  bool contains(int32_t px, int32_t py) const
  {
    return px >= x && py >= y && px < x + width && py < y + height;
  }

  int32_t right() const
  {
    return x + width;
  }

  int32_t bottom() const
  {
    return y + height;
  }

  bool operator==(const MonitorRect &) const = default;
};

//! A monitor edge segment that should switch to another computer.
/*!
\c position is the pixel column (left/right) or row (top/bottom) on this
computer. \c begin and \c end are inclusive coordinates along that edge.
*/
struct EdgeSpan
{
  Direction side = Direction::NoDirection;
  int32_t position = 0;
  int32_t begin = 0;
  int32_t end = 0;

  bool contains(int32_t along) const
  {
    return along >= begin && along <= end;
  }
};

//! Where the pointer should appear after crossing onto another computer.
struct MonitorCrossing
{
  std::string computer;
  int32_t x = 0;
  int32_t y = 0;
  //! Direction of travel on the computer that was left.
  Direction travel = Direction::NoDirection;
};

} // namespace deskflow
