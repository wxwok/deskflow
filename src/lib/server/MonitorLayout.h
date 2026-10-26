/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/MonitorGeometry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deskflow {

//! Why a stacked computer can or cannot take the pointer.
enum class StackAvailability
{
  Available,
  Missing,
  Disconnected,
  Unreachable,
};

const char *stackAvailabilityName(StackAvailability availability);

//! One computer that covers a shared point, with the z-order inputs.
struct StackCandidate
{
  std::string name;
  int32_t z = 0;
  //! Later placements are in front when \c z ties.
  int order = 0;
  StackAvailability availability = StackAvailability::Available;
  int32_t x = 0;
  int32_t y = 0;
};

//! The layer that should receive the pointer, and the layers passed over.
struct StackDecision
{
  std::string active;
  int32_t x = 0;
  int32_t y = 0;
  std::vector<StackCandidate> skipped;
};

//! Front to back: highest \c z, then highest \c order. The first available layer wins.
StackDecision chooseActiveStackLayer(std::vector<StackCandidate> candidates);

//! Text after the \c stack version tag: who changed, who was skipped, and who is active.
std::string
describeStackActivation(const std::string &event, const std::string &previous, const StackDecision &decision);

//! Map a shared point into one placement. False when no monitor contains it.
bool stackPointOnPlacement(
    int32_t originX, int32_t originY, const std::vector<MonitorRect> &monitors, int32_t globalX, int32_t globalY,
    int32_t &localX, int32_t &localY
);

//! Add \p name when its saved monitors cover the point and it is not already listed.
bool appendStackCandidate(
    std::vector<StackCandidate> &candidates, const std::string &name, int32_t z, int order,
    StackAvailability availability, int32_t originX, int32_t originY, const std::vector<MonitorRect> &monitors,
    int32_t globalX, int32_t globalY
);

} // namespace deskflow

//! Places each computer's monitors on one shared virtual desktop.
/*!
A computer's monitors stay in the arrangement the operating system
reports (local coordinates). \c originX/Y is added to those coordinates
to place the whole group. \c z is the stack order: a higher value is in
front. Missing or equal values keep file order, with a later computer
in front. Where monitors overlap, the highest computer that is connected
and available receives the pointer. A computer underneath is used only
when every computer above it is missing, disconnected, or unreachable,
and a higher computer takes the pointer back when it becomes available
again. Switching also happens where a monitor edge is not continued by
another monitor of the same computer and does overlap a monitor of a
different computer.

An empty layout leaves the legacy neighbour links in charge.
*/
class MonitorLayout
{
public:
  struct Placement
  {
    std::string name;
    int32_t originX = 0;
    int32_t originY = 0;
    std::vector<deskflow::MonitorRect> monitors;
    //! Disabled computers keep their placement, but their edges are not switch targets.
    bool enabled = true;
    //! Higher values are in front. Equal values keep insertion order, later in front.
    int32_t z = 0;
  };

  void clear();
  void setComputer(
      const std::string &name, int32_t originX, int32_t originY, std::vector<deskflow::MonitorRect> monitors,
      bool enabled = true, int32_t z = 0
  );
  bool enabled() const;
  bool contains(const std::string &name) const;
  const std::vector<Placement> &computers() const
  {
    return m_computers;
  }

  //! Edges of \p computer that touch a monitor of some other computer.
  std::vector<deskflow::EdgeSpan> switchEdges(const std::string &computer) const;

  //! Primary-computer hit test. The pointer is still inside a monitor.
  /*!
  Returns a horizontal crossing and a vertical crossing when the point
  lies in the jump zone of a switchable edge. Either may be empty.
  */
  void hitEdges(
      const std::string &computer, int32_t x, int32_t y, int32_t zone,
      std::optional<deskflow::MonitorCrossing> &horizontal, std::optional<deskflow::MonitorCrossing> &vertical
  ) const;

  //! Secondary-computer motion. \p toX/Y may lie outside every monitor.
  /*!
  Returns a crossing when the segment leaves this computer onto another.
  Otherwise \p clampX/Y is a point that remains on this computer (either
  \p to, when it is still on a monitor, or the edge that blocked the move).
  */
  std::optional<deskflow::MonitorCrossing> travel(
      const std::string &computer, int32_t fromX, int32_t fromY, int32_t toX, int32_t toY, int32_t &clampX,
      int32_t &clampY
  ) const;

  //! Another computer that should receive this point because it is stacked above \p computer.
  /*!
  \p x and \p y are local to \p computer. Empty when \p computer is already the front
  available computer at that point, or when the point is on no other computer.
  Disabled computers are skipped. Connection state is applied by the server.
  */
  std::optional<deskflow::MonitorCrossing> stackTarget(const std::string &computer, int32_t x, int32_t y) const;

  //! Computers whose monitors contain the shared point, front first.
  struct StackMember
  {
    std::string name;
    int32_t z = 0;
    int order = 0;
    int32_t x = 0;
    int32_t y = 0;
    bool enabled = true;
  };

  std::vector<StackMember> stackMembersAt(int32_t globalX, int32_t globalY) const;

  //! Pull \p x,\p y inside \p computer. Returns false if the computer has no monitors.
  bool clampToComputer(const std::string &computer, int32_t &x, int32_t &y) const;

private:
  struct GlobalMonitor
  {
    std::string computer;
    deskflow::MonitorRect local;
    int32_t originX = 0;
    int32_t originY = 0;

    int32_t gx() const
    {
      return originX + local.x;
    }
    int32_t gy() const
    {
      return originY + local.y;
    }
    int32_t gright() const
    {
      return gx() + local.width;
    }
    int32_t gbottom() const
    {
      return gy() + local.height;
    }
    bool containsGlobal(int32_t x, int32_t y) const
    {
      return x >= gx() && y >= gy() && x < gright() && y < gbottom();
    }
  };

  enum class NeighborKind
  {
    None,
    Same,
    Remote
  };

  struct Neighbor
  {
    NeighborKind kind = NeighborKind::None;
    const GlobalMonitor *monitor = nullptr;
  };

  const Placement *find(const std::string &name) const;
  std::vector<GlobalMonitor> globals() const;
  const GlobalMonitor *
  monitorAt(const std::vector<GlobalMonitor> &all, const std::string &computer, int32_t x, int32_t y) const;
  Neighbor
  neighborAcross(const std::vector<GlobalMonitor> &all, const GlobalMonitor &from, Direction side, int32_t along) const;
  std::optional<deskflow::MonitorCrossing> crossAt(
      const std::vector<GlobalMonitor> &all, const GlobalMonitor &from, Direction side, int32_t along, int32_t overshoot
  ) const;

  std::vector<Placement> m_computers;
};
