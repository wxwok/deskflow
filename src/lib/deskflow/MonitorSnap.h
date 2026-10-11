/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace deskflow {

//! One monitor rectangle in shared-desktop pixels. The rectangle is half-open.
struct SnapRect
{
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;
  //! Computers that share a group move together and do not snap to each other.
  int group = 0;

  int32_t right() const
  {
    return x + width;
  }

  int32_t bottom() const
  {
    return y + height;
  }
};

//! A guide line produced by a snap. Vertical lines have constant \c position x.
struct SnapGuide
{
  bool vertical = false;
  int32_t position = 0;
  int32_t begin = 0;
  int32_t end = 0;
};

//! How far to move a dragged monitor group, relative to the position that was tested.
struct MonitorSnapResult
{
  int32_t dx = 0;
  int32_t dy = 0;
  bool snappedX = false;
  bool snappedY = false;
  std::vector<SnapGuide> guides;
};

//! A touching edge that will switch the pointer. \c end is exclusive.
struct SwitchSegment
{
  bool vertical = false;
  int32_t position = 0;
  int32_t begin = 0;
  int32_t end = 0;
};

namespace detail {

inline int64_t snapAbs(int32_t value)
{
  return value < 0 ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
}

inline bool snapOverlap(const SnapRect &a, const SnapRect &b)
{
  if (a.width <= 0 || a.height <= 0 || b.width <= 0 || b.height <= 0) {
    return false;
  }
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

//! Gap between half-open intervals. Zero when they overlap.
inline int32_t snapGap(int32_t a0, int32_t a1, int32_t b0, int32_t b1)
{
  if (a0 < b1 && b0 < a1) {
    return 0;
  }
  if (a1 <= b0) {
    return b0 - a1;
  }
  return a0 - b1;
}

inline bool
groupOverlaps(const std::vector<SnapRect> &moving, const std::vector<SnapRect> &stationary, int32_t dx, int32_t dy)
{
  for (auto moved : moving) {
    if (moved.width <= 0 || moved.height <= 0) {
      continue;
    }
    moved.x += dx;
    moved.y += dy;
    for (const auto &still : stationary) {
      if (snapOverlap(moved, still)) {
        return true;
      }
    }
  }
  return false;
}

struct AxisCandidate
{
  int32_t delta = 0;
  int32_t gap = 0;
  bool adjacency = false;
  SnapGuide guide;
};

inline bool betterAxis(const AxisCandidate &candidate, const AxisCandidate &current)
{
  const int64_t candidateDistance = snapAbs(candidate.delta);
  const int64_t currentDistance = snapAbs(current.delta);
  if (candidateDistance != currentDistance) {
    return candidateDistance < currentDistance;
  }
  if (candidate.gap != current.gap) {
    return candidate.gap < current.gap;
  }
  return candidate.adjacency && !current.adjacency;
}

inline void considerAxis(std::vector<AxisCandidate> &axis, AxisCandidate candidate, int32_t threshold)
{
  if (candidate.gap > threshold || snapAbs(candidate.delta) > threshold) {
    return;
  }
  for (auto &existing : axis) {
    if (existing.delta != candidate.delta) {
      continue;
    }
    if (betterAxis(candidate, existing)) {
      existing = candidate;
    }
    return;
  }
  axis.push_back(candidate);
}

inline SnapGuide guideOn(bool vertical, int32_t position, int32_t a0, int32_t a1, int32_t b0, int32_t b1)
{
  SnapGuide guide;
  guide.vertical = vertical;
  guide.position = position;
  guide.begin = std::min(a0, b0);
  guide.end = std::max(a1, b1);
  return guide;
}

struct AxisOption
{
  int32_t delta = 0;
  bool snap = false;
  bool adjacency = false;
  SnapGuide guide;
};

inline std::vector<AxisOption> axisOptions(const std::vector<AxisCandidate> &candidates)
{
  std::vector<AxisOption> options;
  options.reserve(candidates.size() + 1);
  for (const auto &candidate : candidates) {
    options.push_back({candidate.delta, true, candidate.adjacency, candidate.guide});
  }
  options.push_back({});
  return options;
}

//! Smallest one-axis pushes that leave \p moving only touching \p stationary.
inline bool
separateGroup(const std::vector<SnapRect> &moving, const std::vector<SnapRect> &stationary, int32_t &dx, int32_t &dy)
{
  dx = 0;
  dy = 0;
  for (int iteration = 0; iteration < 8; ++iteration) {
    if (!groupOverlaps(moving, stationary, dx, dy)) {
      return true;
    }

    bool found = false;
    int32_t bestDx = dx;
    int32_t bestDy = dy;
    int64_t bestCost = 0;
    for (const auto &source : moving) {
      SnapRect moved = source;
      moved.x += dx;
      moved.y += dy;
      if (moved.width <= 0 || moved.height <= 0) {
        continue;
      }
      for (const auto &still : stationary) {
        if (!snapOverlap(moved, still)) {
          continue;
        }
        const struct Push
        {
          int32_t x;
          int32_t y;
        } pushes[] = {
            {still.x - moved.right(), 0},
            {still.right() - moved.x, 0},
            {0, still.y - moved.bottom()},
            {0, still.bottom() - moved.y},
        };
        for (const auto &push : pushes) {
          const int32_t nextDx = dx + push.x;
          const int32_t nextDy = dy + push.y;
          if (groupOverlaps(moving, stationary, nextDx, nextDy)) {
            continue;
          }
          const int64_t cost = snapAbs(nextDx) + snapAbs(nextDy);
          // Equal cost keeps the earlier push: left, then right, then up, then down.
          if (!found || cost < bestCost) {
            found = true;
            bestCost = cost;
            bestDx = nextDx;
            bestDy = nextDy;
          }
        }
      }
    }
    if (!found) {
      return false;
    }
    dx = bestDx;
    dy = bestDy;
  }
  return !groupOverlaps(moving, stationary, dx, dy);
}

struct Interval
{
  int32_t begin = 0;
  int32_t end = 0;
};

inline std::vector<Interval> mergeIntervals(std::vector<Interval> intervals)
{
  if (intervals.empty()) {
    return {};
  }
  std::sort(intervals.begin(), intervals.end(), [](const Interval &a, const Interval &b) { return a.begin < b.begin; });
  std::vector<Interval> merged;
  merged.push_back(intervals.front());
  for (size_t index = 1; index < intervals.size(); ++index) {
    if (intervals[index].begin <= merged.back().end) {
      merged.back().end = std::max(merged.back().end, intervals[index].end);
    } else {
      merged.push_back(intervals[index]);
    }
  }
  return merged;
}

inline std::vector<Interval> subtractIntervals(const std::vector<Interval> &base, std::vector<Interval> cuts)
{
  cuts = mergeIntervals(std::move(cuts));
  std::vector<Interval> result;
  for (const auto span : base) {
    int32_t cursor = span.begin;
    for (const auto cut : cuts) {
      if (cut.end <= cursor) {
        continue;
      }
      if (cut.begin >= span.end) {
        break;
      }
      if (cut.begin > cursor) {
        result.push_back({cursor, std::min(cut.begin, span.end)});
      }
      cursor = std::max(cursor, cut.end);
      if (cursor >= span.end) {
        break;
      }
    }
    if (cursor < span.end) {
      result.push_back({cursor, span.end});
    }
  }
  return result;
}

inline void collectContacts(
    std::vector<SwitchSegment> &segments, bool vertical, int32_t position, int32_t spanBegin, int32_t spanEnd,
    std::vector<Interval> remote, std::vector<Interval> blocked
)
{
  if (remote.empty()) {
    return;
  }
  const std::vector<Interval> shown = subtractIntervals(mergeIntervals(std::move(remote)), std::move(blocked));
  for (const auto interval : shown) {
    if (interval.begin >= interval.end) {
      continue;
    }
    const int32_t begin = std::max(interval.begin, spanBegin);
    const int32_t end = std::min(interval.end, spanEnd);
    if (begin < end) {
      segments.push_back(SwitchSegment{vertical, position, begin, end});
    }
  }
}

} // namespace detail

inline MonitorSnapResult snapMonitorPlacement(
    const std::vector<SnapRect> &moving, const std::vector<SnapRect> &stationary, int32_t threshold, bool enableSnap
)
{
  using namespace detail;
  if (threshold < 0) {
    threshold = 0;
  }

  MonitorSnapResult result;
  if (!enableSnap) {
    separateGroup(moving, stationary, result.dx, result.dy);
    return result;
  }

  std::vector<AxisCandidate> xCandidates;
  std::vector<AxisCandidate> yCandidates;
  for (const auto &moved : moving) {
    if (moved.width <= 0 || moved.height <= 0) {
      continue;
    }
    for (const auto &still : stationary) {
      if (still.width <= 0 || still.height <= 0) {
        continue;
      }
      const int32_t gapY = snapGap(moved.y, moved.bottom(), still.y, still.bottom());
      const int32_t gapX = snapGap(moved.x, moved.right(), still.x, still.right());
      considerAxis(
          xCandidates,
          {still.right() - moved.x, gapY, true,
           guideOn(true, still.right(), moved.y, moved.bottom(), still.y, still.bottom())},
          threshold
      );
      considerAxis(
          xCandidates,
          {still.x - moved.right(), gapY, true,
           guideOn(true, still.x, moved.y, moved.bottom(), still.y, still.bottom())},
          threshold
      );
      considerAxis(
          xCandidates,
          {still.x - moved.x, gapY, false, guideOn(true, still.x, moved.y, moved.bottom(), still.y, still.bottom())},
          threshold
      );
      considerAxis(
          xCandidates,
          {still.right() - moved.right(), gapY, false,
           guideOn(true, still.right(), moved.y, moved.bottom(), still.y, still.bottom())},
          threshold
      );
      considerAxis(
          yCandidates,
          {still.bottom() - moved.y, gapX, true,
           guideOn(false, still.bottom(), moved.x, moved.right(), still.x, still.right())},
          threshold
      );
      considerAxis(
          yCandidates,
          {still.y - moved.bottom(), gapX, true,
           guideOn(false, still.y, moved.x, moved.right(), still.x, still.right())},
          threshold
      );
      considerAxis(
          yCandidates,
          {still.y - moved.y, gapX, false, guideOn(false, still.y, moved.x, moved.right(), still.x, still.right())},
          threshold
      );
      considerAxis(
          yCandidates,
          {still.bottom() - moved.bottom(), gapX, false,
           guideOn(false, still.bottom(), moved.x, moved.right(), still.x, still.right())},
          threshold
      );
    }
  }

  const std::vector<AxisOption> xOptions = axisOptions(xCandidates);
  const std::vector<AxisOption> yOptions = axisOptions(yCandidates);
  bool chosen = false;
  int chosenSnaps = -1;
  int64_t chosenDistance = 0;
  int chosenAdjacency = -1;
  int64_t chosenAbsX = 0;
  for (const auto &xOption : xOptions) {
    for (const auto &yOption : yOptions) {
      if (groupOverlaps(moving, stationary, xOption.delta, yOption.delta)) {
        continue;
      }
      const int snaps = (xOption.snap ? 1 : 0) + (yOption.snap ? 1 : 0);
      const int64_t distance = snapAbs(xOption.delta) + snapAbs(yOption.delta);
      const int adjacency = (xOption.adjacency ? 1 : 0) + (yOption.adjacency ? 1 : 0);
      const int64_t absX = snapAbs(xOption.delta);
      const bool better = !chosen || snaps > chosenSnaps ||
                          (snaps == chosenSnaps &&
                           (distance < chosenDistance ||
                            (distance == chosenDistance &&
                             (adjacency > chosenAdjacency || (adjacency == chosenAdjacency && absX < chosenAbsX)))));
      if (!better) {
        continue;
      }
      chosen = true;
      chosenSnaps = snaps;
      chosenDistance = distance;
      chosenAdjacency = adjacency;
      chosenAbsX = absX;
      result.dx = xOption.delta;
      result.dy = yOption.delta;
      result.snappedX = xOption.snap;
      result.snappedY = yOption.snap;
      result.guides.clear();
      if (xOption.snap) {
        result.guides.push_back(xOption.guide);
      }
      if (yOption.snap) {
        result.guides.push_back(yOption.guide);
      }
    }
  }

  if (chosen) {
    return result;
  }

  separateGroup(moving, stationary, result.dx, result.dy);
  result.snappedX = false;
  result.snappedY = false;
  result.guides.clear();
  return result;
}

inline std::vector<SwitchSegment> switchEdgeSegments(const std::vector<SnapRect> &monitors)
{
  using namespace detail;
  std::vector<SwitchSegment> segments;
  for (const auto &monitor : monitors) {
    if (monitor.width <= 0 || monitor.height <= 0) {
      continue;
    }

    std::vector<Interval> rightRemote;
    std::vector<Interval> rightBlocked;
    std::vector<Interval> bottomRemote;
    std::vector<Interval> bottomBlocked;
    for (const auto &other : monitors) {
      if (&other == &monitor || other.width <= 0 || other.height <= 0) {
        continue;
      }
      if (other.x == monitor.right()) {
        const int32_t begin = std::max(monitor.y, other.y);
        const int32_t end = std::min(monitor.bottom(), other.bottom());
        if (begin < end) {
          (other.group == monitor.group ? rightBlocked : rightRemote).push_back({begin, end});
        }
      }
      if (other.y == monitor.bottom()) {
        const int32_t begin = std::max(monitor.x, other.x);
        const int32_t end = std::min(monitor.right(), other.right());
        if (begin < end) {
          (other.group == monitor.group ? bottomBlocked : bottomRemote).push_back({begin, end});
        }
      }
    }
    collectContacts(
        segments, true, monitor.right(), monitor.y, monitor.bottom(), std::move(rightRemote), std::move(rightBlocked)
    );
    collectContacts(
        segments, false, monitor.bottom(), monitor.x, monitor.right(), std::move(bottomRemote), std::move(bottomBlocked)
    );
  }
  return segments;
}

} // namespace deskflow
