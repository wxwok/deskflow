/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/MonitorLayout.h"

#include "base/String.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace {

int32_t clampTo(int32_t value, int32_t origin, int32_t size)
{
  if (size <= 0) {
    return origin;
  }
  if (value < origin) {
    return origin;
  }
  if (value >= origin + size) {
    return origin + size - 1;
  }
  return value;
}

} // namespace

const char *deskflow::stackAvailabilityName(StackAvailability availability)
{
  switch (availability) {
  case StackAvailability::Available:
    return "available";
  case StackAvailability::Missing:
    return "missing";
  case StackAvailability::Disconnected:
    return "disconnected";
  case StackAvailability::Unreachable:
    return "unreachable";
  }
  return "unknown";
}

deskflow::StackDecision deskflow::chooseActiveStackLayer(std::vector<StackCandidate> candidates)
{
  std::sort(candidates.begin(), candidates.end(), [](const StackCandidate &left, const StackCandidate &right) {
    if (left.z != right.z) {
      return left.z > right.z;
    }
    if (left.order != right.order) {
      return left.order > right.order;
    }
    return left.name > right.name;
  });

  StackDecision decision;
  for (const auto &candidate : candidates) {
    if (decision.active.empty() && candidate.availability == StackAvailability::Available && !candidate.name.empty()) {
      decision.active = candidate.name;
      decision.x = candidate.x;
      decision.y = candidate.y;
      continue;
    }
    if (decision.active.empty()) {
      decision.skipped.push_back(candidate);
    }
  }
  return decision;
}

std::string
deskflow::describeStackActivation(const std::string &event, const std::string &previous, const StackDecision &decision)
{
  std::ostringstream out;
  out << event;
  for (const auto &skipped : decision.skipped) {
    out << ", skip \"" << skipped.name << "\" (" << stackAvailabilityName(skipped.availability) << ")";
  }
  if (decision.active.empty()) {
    out << ", no available layer";
    if (!previous.empty()) {
      out << "; fall back from \"" << previous << "\"";
    }
  } else {
    out << ", activate \"" << decision.active << "\" at " << decision.x << "," << decision.y;
    if (!previous.empty() && previous != decision.active) {
      out << " (was \"" << previous << "\")";
    }
  }
  return out.str();
}

bool deskflow::stackPointOnPlacement(
    int32_t originX, int32_t originY, const std::vector<MonitorRect> &monitors, int32_t globalX, int32_t globalY,
    int32_t &localX, int32_t &localY
)
{
  for (const auto &monitor : monitors) {
    if (!monitor.valid()) {
      continue;
    }
    const int32_t left = originX + monitor.x;
    const int32_t top = originY + monitor.y;
    if (globalX < left || globalY < top || globalX >= left + monitor.width || globalY >= top + monitor.height) {
      continue;
    }
    localX = clampTo(globalX - originX, monitor.x, monitor.width);
    localY = clampTo(globalY - originY, monitor.y, monitor.height);
    return true;
  }
  return false;
}

bool deskflow::appendStackCandidate(
    std::vector<StackCandidate> &candidates, const std::string &name, int32_t z, int order,
    StackAvailability availability, int32_t originX, int32_t originY, const std::vector<MonitorRect> &monitors,
    int32_t globalX, int32_t globalY
)
{
  if (name.empty()) {
    return false;
  }
  for (const auto &candidate : candidates) {
    if (string::CaselessCmp::equal(candidate.name, name)) {
      return false;
    }
  }
  int32_t localX = 0;
  int32_t localY = 0;
  if (!stackPointOnPlacement(originX, originY, monitors, globalX, globalY, localX, localY)) {
    return false;
  }
  candidates.push_back(StackCandidate{name, z, order, availability, localX, localY});
  return true;
}

void MonitorLayout::clear()
{
  m_computers.clear();
}

void MonitorLayout::setComputer(
    const std::string &name, int32_t originX, int32_t originY, std::vector<deskflow::MonitorRect> monitors,
    bool enabled, int32_t z
)
{
  std::erase_if(monitors, [](const deskflow::MonitorRect &monitor) { return !monitor.valid(); });
  if (name.empty() || monitors.empty()) {
    std::erase_if(m_computers, [&name](const Placement &placement) { return placement.name == name; });
    return;
  }

  for (auto &placement : m_computers) {
    if (placement.name == name) {
      placement.originX = originX;
      placement.originY = originY;
      placement.monitors = std::move(monitors);
      placement.enabled = enabled;
      placement.z = z;
      return;
    }
  }

  m_computers.push_back(Placement{name, originX, originY, std::move(monitors), enabled, z});
}

bool MonitorLayout::enabled() const
{
  return !m_computers.empty();
}

bool MonitorLayout::contains(const std::string &name) const
{
  return find(name) != nullptr;
}

const MonitorLayout::Placement *MonitorLayout::find(const std::string &name) const
{
  for (const auto &placement : m_computers) {
    if (placement.name == name) {
      return &placement;
    }
  }
  return nullptr;
}

std::vector<MonitorLayout::GlobalMonitor> MonitorLayout::globals() const
{
  std::vector<GlobalMonitor> all;
  for (const auto &placement : m_computers) {
    if (!placement.enabled) {
      continue;
    }
    for (const auto &monitor : placement.monitors) {
      all.push_back(GlobalMonitor{placement.name, monitor, placement.originX, placement.originY});
    }
  }
  return all;
}

const MonitorLayout::GlobalMonitor *
MonitorLayout::monitorAt(const std::vector<GlobalMonitor> &all, const std::string &computer, int32_t x, int32_t y) const
{
  for (const auto &monitor : all) {
    if (monitor.computer == computer && monitor.local.contains(x, y)) {
      return &monitor;
    }
  }
  return nullptr;
}

MonitorLayout::Neighbor MonitorLayout::neighborAcross(
    const std::vector<GlobalMonitor> &all, const GlobalMonitor &from, Direction side, int32_t along
) const
{
  const int32_t globalAlong =
      (side == Direction::Left || side == Direction::Right) ? from.originY + along : from.originX + along;
  int32_t boundary = 0;
  using enum Direction;
  switch (side) {
  case Left:
    boundary = from.gx();
    break;
  case Right:
    boundary = from.gright();
    break;
  case Top:
    boundary = from.gy();
    break;
  case Bottom:
    boundary = from.gbottom();
    break;
  case NoDirection:
    return {};
  }

  Neighbor same;
  Neighbor remote;
  for (const auto &other : all) {
    if (&other == &from) {
      continue;
    }
    bool touches = false;
    bool covers = false;
    switch (side) {
    case Left:
      touches = other.gright() == boundary;
      covers = other.gy() <= globalAlong && globalAlong < other.gbottom();
      break;
    case Right:
      touches = other.gx() == boundary;
      covers = other.gy() <= globalAlong && globalAlong < other.gbottom();
      break;
    case Top:
      touches = other.gbottom() == boundary;
      covers = other.gx() <= globalAlong && globalAlong < other.gright();
      break;
    case Bottom:
      touches = other.gy() == boundary;
      covers = other.gx() <= globalAlong && globalAlong < other.gright();
      break;
    case NoDirection:
      break;
    }
    if (!touches || !covers) {
      continue;
    }
    if (other.computer == from.computer) {
      same = {NeighborKind::Same, &other};
    } else if (remote.kind != NeighborKind::Remote || other.computer < remote.monitor->computer) {
      remote = {NeighborKind::Remote, &other};
    }
  }

  // An edge continued by this computer is never a switch, even if a
  // remote monitor also touches it (overlapping layouts).
  if (same.kind == NeighborKind::Same) {
    return same;
  }
  return remote;
}

std::optional<deskflow::MonitorCrossing> MonitorLayout::crossAt(
    const std::vector<GlobalMonitor> &all, const GlobalMonitor &from, Direction side, int32_t along, int32_t overshoot
) const
{
  const Neighbor neighbor = neighborAcross(all, from, side, along);
  if (neighbor.kind != NeighborKind::Remote || neighbor.monitor == nullptr) {
    return std::nullopt;
  }

  const GlobalMonitor &target = *neighbor.monitor;
  const int32_t globalAlong =
      (side == Direction::Left || side == Direction::Right) ? from.originY + along : from.originX + along;
  const int32_t inward = std::max<int32_t>(0, overshoot - 1);

  deskflow::MonitorCrossing crossing;
  crossing.computer = target.computer;
  crossing.travel = side;
  using enum Direction;
  switch (side) {
  case Left:
    crossing.x = target.local.x + target.local.width - 1 - inward;
    crossing.y = globalAlong - target.originY;
    break;
  case Right:
    crossing.x = target.local.x + inward;
    crossing.y = globalAlong - target.originY;
    break;
  case Top:
    crossing.x = globalAlong - target.originX;
    crossing.y = target.local.y + target.local.height - 1 - inward;
    break;
  case Bottom:
    crossing.x = globalAlong - target.originX;
    crossing.y = target.local.y + inward;
    break;
  case NoDirection:
    return std::nullopt;
  }

  crossing.x = clampTo(crossing.x, target.local.x, target.local.width);
  crossing.y = clampTo(crossing.y, target.local.y, target.local.height);
  return crossing;
}

std::vector<deskflow::EdgeSpan> MonitorLayout::switchEdges(const std::string &computer) const
{
  std::vector<deskflow::EdgeSpan> spans;
  const auto all = globals();
  for (const auto &monitor : all) {
    if (monitor.computer != computer) {
      continue;
    }

    const struct Side
    {
      Direction direction;
      int32_t position;
      int32_t begin;
      int32_t end;
    } sides[] = {
        {Direction::Left, monitor.local.x, monitor.local.y, monitor.local.y + monitor.local.height},
        {Direction::Right, monitor.local.x + monitor.local.width - 1, monitor.local.y,
         monitor.local.y + monitor.local.height},
        {Direction::Top, monitor.local.y, monitor.local.x, monitor.local.x + monitor.local.width},
        {Direction::Bottom, monitor.local.y + monitor.local.height - 1, monitor.local.x,
         monitor.local.x + monitor.local.width},
    };

    for (const auto &side : sides) {
      int32_t cursor = side.begin;
      while (cursor < side.end) {
        const Neighbor neighbor = neighborAcross(all, monitor, side.direction, cursor);
        int32_t run = cursor + 1;
        while (run < side.end) {
          const Neighbor next = neighborAcross(all, monitor, side.direction, run);
          const bool sameRemote = neighbor.kind == next.kind && neighbor.kind == NeighborKind::Remote &&
                                  neighbor.monitor != nullptr && next.monitor != nullptr &&
                                  neighbor.monitor->computer == next.monitor->computer;
          const bool sameNone = neighbor.kind == NeighborKind::None && next.kind == NeighborKind::None;
          const bool sameSame = neighbor.kind == NeighborKind::Same && next.kind == NeighborKind::Same;
          if (!sameRemote && !sameNone && !sameSame) {
            break;
          }
          ++run;
        }
        if (neighbor.kind == NeighborKind::Remote) {
          spans.push_back(deskflow::EdgeSpan{side.direction, side.position, cursor, run - 1});
        }
        cursor = run;
      }
    }
  }
  return spans;
}

void MonitorLayout::hitEdges(
    const std::string &computer, int32_t x, int32_t y, int32_t zone,
    std::optional<deskflow::MonitorCrossing> &horizontal, std::optional<deskflow::MonitorCrossing> &vertical
) const
{
  horizontal.reset();
  vertical.reset();
  if (zone < 1) {
    zone = 1;
  }

  const auto all = globals();
  for (const auto &monitor : all) {
    if (monitor.computer != computer || !monitor.local.contains(x, y)) {
      continue;
    }

    const auto &local = monitor.local;
    if (x < local.x + zone) {
      horizontal = crossAt(all, monitor, Direction::Left, y, 1);
    } else if (x >= local.x + local.width - zone) {
      horizontal = crossAt(all, monitor, Direction::Right, y, 1);
    }
    if (y < local.y + zone) {
      vertical = crossAt(all, monitor, Direction::Top, x, 1);
    } else if (y >= local.y + local.height - zone) {
      vertical = crossAt(all, monitor, Direction::Bottom, x, 1);
    }
    return;
  }
}

std::vector<MonitorLayout::StackMember> MonitorLayout::stackMembersAt(int32_t globalX, int32_t globalY) const
{
  std::vector<StackMember> found;
  int index = 0;
  for (const auto &placement : m_computers) {
    const int order = index++;
    for (const auto &monitor : placement.monitors) {
      int32_t localX = 0;
      int32_t localY = 0;
      if (!deskflow::stackPointOnPlacement(
              placement.originX, placement.originY, {monitor}, globalX, globalY, localX, localY
          )) {
        continue;
      }
      StackMember member;
      member.name = placement.name;
      member.z = placement.z;
      member.order = order;
      member.x = localX;
      member.y = localY;
      member.enabled = placement.enabled;
      found.push_back(std::move(member));
      break;
    }
  }

  std::sort(found.begin(), found.end(), [](const StackMember &left, const StackMember &right) {
    if (left.z != right.z) {
      return left.z > right.z;
    }
    if (left.order != right.order) {
      return left.order > right.order;
    }
    return left.name > right.name;
  });
  return found;
}

std::optional<deskflow::MonitorCrossing>
MonitorLayout::stackTarget(const std::string &computer, int32_t x, int32_t y) const
{
  const Placement *self = find(computer);
  if (self == nullptr) {
    return std::nullopt;
  }

  std::vector<deskflow::StackCandidate> candidates;
  for (const auto &member : stackMembersAt(self->originX + x, self->originY + y)) {
    deskflow::StackCandidate candidate;
    candidate.name = member.name;
    candidate.z = member.z;
    candidate.order = member.order;
    candidate.x = member.x;
    candidate.y = member.y;
    candidate.availability =
        member.enabled ? deskflow::StackAvailability::Available : deskflow::StackAvailability::Unreachable;
    candidates.push_back(std::move(candidate));
  }

  const auto decision = deskflow::chooseActiveStackLayer(std::move(candidates));
  if (decision.active.empty() || decision.active == computer) {
    return std::nullopt;
  }

  deskflow::MonitorCrossing crossing;
  crossing.computer = decision.active;
  crossing.x = decision.x;
  crossing.y = decision.y;
  crossing.travel = Direction::NoDirection;
  return crossing;
}

std::optional<deskflow::MonitorCrossing> MonitorLayout::travel(
    const std::string &computer, int32_t fromX, int32_t fromY, int32_t toX, int32_t toY, int32_t &clampX,
    int32_t &clampY
) const
{
  clampX = toX;
  clampY = toY;
  const auto all = globals();

  // Still on this computer: only switch via z-order peek into a higher stack.
  // Do not consult stackTarget for points that have already left our monitors.
  // That path returns NoDirection and skips primary ReleaseAt insets, which
  // re-triggers portal barriers (sp7 right-edge enter, then exit onto work
  // output-3's concave bottom, bouncing to sp7's top-right).
  if (monitorAt(all, computer, toX, toY) != nullptr) {
    if (auto stacked = stackTarget(computer, toX, toY); stacked.has_value()) {
      return stacked;
    }
    return std::nullopt;
  }

  int32_t x = fromX;
  int32_t y = fromY;
  for (int guard = 0; guard < 8; ++guard) {
    const GlobalMonitor *monitor = monitorAt(all, computer, x, y);
    if (monitor == nullptr) {
      if (auto stacked = stackTarget(computer, toX, toY); stacked.has_value()) {
        return stacked;
      }
      clampToComputer(computer, clampX, clampY);
      return std::nullopt;
    }

    const auto &local = monitor->local;
    const bool exitLeft = toX < local.x;
    const bool exitRight = toX >= local.right();
    const bool exitTop = toY < local.y;
    const bool exitBottom = toY >= local.bottom();
    if (!exitLeft && !exitRight && !exitTop && !exitBottom) {
      clampX = toX;
      clampY = toY;
      return std::nullopt;
    }

    auto tFor = [](int32_t from, int32_t to, int32_t boundary) {
      const double denom = static_cast<double>(to) - static_cast<double>(from);
      if (denom == 0.0) {
        return 2.0;
      }
      return (static_cast<double>(boundary) - static_cast<double>(from)) / denom;
    };

    Direction side = Direction::NoDirection;
    double bestT = 2.0;
    auto consider = [&](bool exits, Direction candidate, double t) {
      if (!exits || t < 0.0 || t > 1.0) {
        return;
      }
      if (t < bestT) {
        bestT = t;
        side = candidate;
      }
    };
    consider(exitLeft, Direction::Left, tFor(x, toX, local.x));
    consider(exitRight, Direction::Right, tFor(x, toX, local.right()));
    consider(exitTop, Direction::Top, tFor(y, toY, local.y));
    consider(exitBottom, Direction::Bottom, tFor(y, toY, local.bottom()));

    if (side == Direction::NoDirection) {
      // The point is outside but the segment does not cross a boundary
      // (from was already outside). Clamp to this monitor.
      clampX = clampTo(toX, local.x, local.width);
      clampY = clampTo(toY, local.y, local.height);
      return std::nullopt;
    }

    int32_t along = 0;
    int32_t overshoot = 1;
    using enum Direction;
    switch (side) {
    case Left:
      along = clampTo(toY, local.y, local.height);
      overshoot = local.x - toX;
      break;
    case Right:
      along = clampTo(toY, local.y, local.height);
      overshoot = toX - (local.right() - 1);
      break;
    case Top:
      along = clampTo(toX, local.x, local.width);
      overshoot = local.y - toY;
      break;
    case Bottom:
      along = clampTo(toX, local.x, local.width);
      overshoot = toY - (local.bottom() - 1);
      break;
    case NoDirection:
      break;
    }
    if (overshoot < 1) {
      overshoot = 1;
    }

    const Neighbor neighbor = neighborAcross(all, *monitor, side, along);
    if (neighbor.kind == NeighborKind::Same && neighbor.monitor != nullptr) {
      // Step just inside the adjacent monitor of this computer and keep going.
      const auto &next = neighbor.monitor->local;
      using enum Direction;
      switch (side) {
      case Left:
        x = next.x + next.width - 1;
        y = along;
        break;
      case Right:
        x = next.x;
        y = along;
        break;
      case Top:
        x = along;
        y = next.y + next.height - 1;
        break;
      case Bottom:
        x = along;
        y = next.y;
        break;
      case NoDirection:
        break;
      }
      x = clampTo(x, next.x, next.width);
      y = clampTo(y, next.y, next.height);
      continue;
    }

    if (neighbor.kind == NeighborKind::Remote) {
      if (auto crossing = crossAt(all, *monitor, side, along, overshoot); crossing.has_value()) {
        return crossing;
      }
    }

    // No abutting remote neighbour on this side (common for stacked overlap
    // that shares area but not an edge). Drop through z-order instead.
    if (auto stacked = stackTarget(computer, toX, toY); stacked.has_value()) {
      return stacked;
    }

    clampX = clampTo(toX, local.x, local.width);
    clampY = clampTo(toY, local.y, local.height);
    return std::nullopt;
  }

  if (auto stacked = stackTarget(computer, toX, toY); stacked.has_value()) {
    return stacked;
  }
  clampToComputer(computer, clampX, clampY);
  return std::nullopt;
}

bool MonitorLayout::clampToComputer(const std::string &computer, int32_t &x, int32_t &y) const
{
  const Placement *placement = find(computer);
  if (placement == nullptr || placement->monitors.empty()) {
    return false;
  }
  if (monitorAt(globals(), computer, x, y) != nullptr) {
    return true;
  }

  const deskflow::MonitorRect *best = &placement->monitors.front();
  int64_t bestDistance = INT64_MAX;
  for (const auto &monitor : placement->monitors) {
    const int32_t cx = clampTo(x, monitor.x, monitor.width);
    const int32_t cy = clampTo(y, monitor.y, monitor.height);
    const int64_t dx = static_cast<int64_t>(x) - cx;
    const int64_t dy = static_cast<int64_t>(y) - cy;
    const int64_t distance = dx * dx + dy * dy;
    if (distance < bestDistance) {
      bestDistance = distance;
      best = &monitor;
    }
  }
  x = clampTo(x, best->x, best->width);
  y = clampTo(y, best->y, best->height);
  return true;
}
