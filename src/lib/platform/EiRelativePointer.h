/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace deskflow {

// Per-event motion beyond this is not a real pointer sample. One million
// logical pixels is well past a frame-long flick on a large desktop, and
// adding it to a cursor position cannot overflow int32_t.
inline constexpr double kMaxRelativePointerDelta = 1000000.0;

// Whole pixels to forward for one libei relative-pointer sample.
// A dropped axis contributed nothing. reset is set when an axis accumulator
// was non-finite or out of range and has been cleared.
struct RelativePointerDelta
{
  std::int32_t x = 0;
  std::int32_t y = 0;
  bool droppedX = false;
  bool droppedY = false;
  bool reset = false;
};

namespace detail {

// Apply one axis. Returns true when the sample was folded in (the pixel
// count may still be zero when only a fraction of a pixel accumulated).
inline bool accumulateRelativeAxis(double &buffer, double delta, std::int32_t &pixels, bool &reset)
{
  pixels = 0;

  // A healthy accumulator only ever holds a fraction of a pixel. Anything
  // else was stored by an earlier bad sample and must not be reused.
  if (!std::isfinite(buffer) || std::fabs(buffer) > kMaxRelativePointerDelta) {
    buffer = 0;
    reset = true;
  }

  if (!std::isfinite(delta) || std::fabs(delta) > kMaxRelativePointerDelta) {
    return false;
  }

  buffer += delta;

  // static_cast<int32_t> is undefined when the truncated value does not fit.
  // On x86, cvttsd2si returns INT_MIN (0x80000000) for NaN, infinity, and
  // every out-of-range finite value. Subtracting that INT_MIN from the
  // accumulator leaves NaN as NaN and pushes a huge finite value further
  // out of range, so every later event on this axis also reports INT_MIN.
  const double whole = std::trunc(buffer);
  constexpr double kMinPixel = static_cast<double>(std::numeric_limits<std::int32_t>::min());
  constexpr double kMaxPixel = static_cast<double>(std::numeric_limits<std::int32_t>::max());
  if (!std::isfinite(whole) || whole < kMinPixel || whole > kMaxPixel) {
    buffer = 0;
    reset = true;
    return false;
  }

  pixels = static_cast<std::int32_t>(whole);
  buffer -= whole;
  return true;
}

} // namespace detail

// Fold one libei relative-pointer sample into the sub-pixel accumulators.
//
// ei_pointer.motion_relative carries IEEE-754 floats. Mutter sends a NaN when
// input capture activates, and a finite sample can also fall outside the
// int32 range. Either value used to stick in the accumulator (see
// accumulateRelativeAxis). The server adds the resulting INT_MIN to the
// cursor and switches computers near -2^31, which pins the pointer to the
// client edge until the process restarts.
inline RelativePointerDelta accumulateRelativePointer(double &bufferX, double &bufferY, double dx, double dy)
{
  RelativePointerDelta result;
  bool resetX = false;
  bool resetY = false;
  result.droppedX = !detail::accumulateRelativeAxis(bufferX, dx, result.x, resetX);
  result.droppedY = !detail::accumulateRelativeAxis(bufferY, dy, result.y, resetY);
  result.reset = resetX || resetY;
  return result;
}

} // namespace deskflow
