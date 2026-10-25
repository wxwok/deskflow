/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MonitorLayoutTests.h"

#include "arch/Arch.h"
#include "base/Log.h"
#include "common/Settings.h"
#include "deskflow/MonitorSnap.h"
#include "server/Config.h"
#include "server/MonitorLayout.h"

#include <QDir>
#include <QFile>

#include <sstream>

using namespace deskflow::server;

namespace {

deskflow::MonitorRect monitor(const char *name, int32_t x, int32_t y, int32_t width, int32_t height)
{
  return {name, x, y, width, height};
}

MonitorLayout userLayout()
{
  MonitorLayout layout;
  layout.setComputer(
      "work", 0, 0,
      {
          monitor("HDMI-1", 0, 0, 1080, 1920),
          monitor("DP-1", 1080, 0, 3840, 2160),
          monitor("eDP-1", 1080, 2160, 3840, 2160),
          monitor("DP-5", 4920, 459, 1920, 1080),
      }
  );
  layout.setComputer("sur3", -1080, 1920, {monitor("sur3", 0, 0, 2160, 1440)});
  layout.setComputer(
      "workwin", 4920, 1539,
      {
          monitor("laptop", 0, 0, 1920, 1080),
          monitor("VX2263", 1920, 0, 1920, 1080),
          monitor("Samsung", 3840, 0, 3840, 2160),
      }
  );
  layout.setComputer("dream", 12600, 1539, {monitor("dream", 0, 0, 1920, 1080)});
  return layout;
}

std::optional<deskflow::MonitorCrossing> hit(const MonitorLayout &layout, const char *computer, int32_t x, int32_t y)
{
  std::optional<deskflow::MonitorCrossing> horizontal;
  std::optional<deskflow::MonitorCrossing> vertical;
  layout.hitEdges(computer, x, y, 1, horizontal, vertical);
  if (horizontal.has_value()) {
    return horizontal;
  }
  return vertical;
}

} // namespace

void MonitorLayoutTests::initTestCase()
{
  QDir dir;
  QVERIFY(dir.mkpath(m_settingsPath));
  QFile::remove(m_settingsFile);
  Settings::setSettingsFile(m_settingsFile);
  Settings::setStateFile(m_stateFile);
}

void MonitorLayoutTests::edgeAdjacency()
{
  const auto layout = userLayout();

  const auto hdmiDown = hit(layout, "work", 100, 1919);
  QVERIFY(hdmiDown.has_value());
  QVERIFY(hdmiDown->computer == "sur3");
  QCOMPARE(hdmiDown->travel, Direction::Bottom);
  QCOMPARE(hdmiDown->x, 100 - -1080);
  QCOMPARE(hdmiDown->y, 0);

  const auto edpLeft = hit(layout, "work", 1080, 2500);
  QVERIFY(edpLeft.has_value());
  QVERIFY(edpLeft->computer == "sur3");
  QCOMPARE(edpLeft->travel, Direction::Left);

  QVERIFY(!hit(layout, "work", 1080, 4000).has_value());

  // The strip of DP-1 beside sur3 and below HDMI is a real shared edge.
  const auto dp1Left = hit(layout, "work", 1080, 2000);
  QVERIFY(dp1Left.has_value());
  QVERIFY(dp1Left->computer == "sur3");

  const auto edpRight = hit(layout, "work", 4919, 2200);
  QVERIFY(edpRight.has_value());
  QVERIFY(edpRight->computer == "workwin");
  QCOMPARE(edpRight->travel, Direction::Right);
  QCOMPARE(edpRight->y, 2200 - 1539);

  QVERIFY(!hit(layout, "work", 4919, 3000).has_value());

  const auto dp5Down = hit(layout, "work", 5000, 1538);
  QVERIFY(dp5Down.has_value());
  QVERIFY(dp5Down->computer == "workwin");
  QCOMPARE(dp5Down->travel, Direction::Bottom);
  QCOMPARE(dp5Down->x, 5000 - 4920);
  QCOMPARE(dp5Down->y, 0);

  QVERIFY(!hit(layout, "work", 4919, 800).has_value());

  const auto samsungRight = hit(layout, "workwin", 3840 + 3840 - 1, 100);
  QVERIFY(samsungRight.has_value());
  QVERIFY(samsungRight->computer == "dream");
  QCOMPARE(samsungRight->x, 0);

  QVERIFY(!hit(layout, "workwin", 3840 + 3840 - 1, 2000).has_value());

  const auto sur3Up = hit(layout, "sur3", 1080, 0);
  QVERIFY(sur3Up.has_value());
  QVERIFY(sur3Up->computer == "work");
  QCOMPARE(sur3Up->travel, Direction::Top);
  QCOMPARE(sur3Up->x, 0);
  QCOMPARE(sur3Up->y, 1919);

  QVERIFY(!hit(layout, "sur3", 0, 0).has_value());
}

void MonitorLayoutTests::travelMapping()
{
  const auto layout = userLayout();
  int32_t clampX = 0;
  int32_t clampY = 0;

  const auto intoSur3 = layout.travel("work", 540, 1919, 540, 1930, clampX, clampY);
  QVERIFY(intoSur3.has_value());
  QVERIFY(intoSur3->computer == "sur3");
  QCOMPARE(intoSur3->x, 540 - -1080);
  QCOMPARE(intoSur3->y, 10);

  const auto internalMove = layout.travel("work", 4910, 800, 4930, 800, clampX, clampY);
  QVERIFY(!internalMove.has_value());
  QCOMPARE(clampX, 4930);
  QCOMPARE(clampY, 800);

  const auto intoLaptop = layout.travel("work", 4919, 2200, 5000, 2200, clampX, clampY);
  QVERIFY(intoLaptop.has_value());
  QVERIFY(intoLaptop->computer == "workwin");
  QCOMPARE(intoLaptop->x, 80);
  QCOMPARE(intoLaptop->y, 2200 - 1539);

  const auto blocked = layout.travel("sur3", 0, 100, -30, 100, clampX, clampY);
  QVERIFY(!blocked.has_value());
  QCOMPARE(clampX, 0);
  QCOMPARE(clampY, 100);
}

void MonitorLayoutTests::configRoundTrip()
{
  const char *text = R"(section: layout
	work:
		origin = 0, 0
		monitor = HDMI-1, 0, 0, 1080, 1920
		monitor = DP-1, 1080, 0, 3840, 2160
		monitor = eDP-1, 1080, 2160, 3840, 2160
		monitor = DP-5, 4920, 459, 1920, 1080
	sur3:
		origin = -1080, 1920
		monitor = sur3, 0, 0, 2160, 1440
	workwin:
		origin = 4920, 1539
		monitor = laptop, 0, 0, 1920, 1080
		monitor = VX2263, 1920, 0, 1920, 1080
		monitor = Samsung, 3840, 0, 3840, 2160
	dream:
		origin = 12600, 1539
		monitor = dream, 0, 0, 1920, 1080
end
)";

  std::stringstream input(text);
  Config parsed(nullptr);
  input >> parsed;
  QVERIFY(parsed.hasMonitorLayout());
  QVERIFY(parsed.isComputer("dream"));
  const auto workwin = parsed.monitorPlacements().find("workwin");
  QVERIFY(workwin != parsed.monitorPlacements().end());
  QCOMPARE(workwin->second.originX, 4920);
  QCOMPARE(workwin->second.originY, 1539);
  QCOMPARE(static_cast<int>(workwin->second.monitors.size()), 3);
  QVERIFY(workwin->second.monitors[2].name == "Samsung");

  std::stringstream output;
  output << parsed;
  QVERIFY(output.str().find("section: layout") != std::string::npos);
  QVERIFY(output.str().find("origin = 12600, 1539") != std::string::npos);

  std::stringstream second(text);
  Config again(nullptr);
  second >> again;
  QVERIFY(again == parsed);
}

deskflow::SnapRect placed(int group, int32_t x, int32_t y, int32_t width, int32_t height)
{
  return {x, y, width, height, group};
}

void MonitorLayoutTests::snapAdjacencyAndAlignment()
{
  const std::vector<deskflow::SnapRect> still = {placed(0, 0, 0, 100, 100)};
  const int32_t threshold = 15;

  // Opposite edges meet, and the tops line up, on independent axes.
  const auto both = deskflow::snapMonitorPlacement({placed(1, 90, 12, 50, 40)}, still, threshold, true);
  QCOMPARE(both.dx, 10);
  QCOMPARE(both.dy, -12);
  QVERIFY(both.snappedX);
  QVERIFY(both.snappedY);
  QCOMPARE(static_cast<int>(both.guides.size()), 2);

  // Farther than the threshold on every edge: stay put.
  const auto far = deskflow::snapMonitorPlacement({placed(1, 130, 0, 50, 40)}, still, threshold, true);
  QCOMPARE(far.dx, 0);
  QCOMPARE(far.dy, 0);
  QVERIFY(!far.snappedX);
  QVERIFY(!far.snappedY);

  // Two horizontal candidates: the nearer edge wins.
  const std::vector<deskflow::SnapRect> pair = {placed(0, 0, 0, 100, 100), placed(0, 160, 0, 100, 100)};
  const auto nearest = deskflow::snapMonitorPlacement({placed(1, 90, 0, 40, 100)}, pair, 40, true);
  QCOMPARE(nearest.dx, 10);
  QCOMPARE(nearest.dy, 0);

  // A second monitor in the dragged group supplies the edge that is in range.
  const auto grouped = deskflow::snapMonitorPlacement(
      {placed(1, 50, 0, 100, 100), placed(1, 150, 0, 100, 100)}, {placed(0, 0, 0, 40, 100)}, threshold, true
  );
  QCOMPARE(grouped.dx, -10);
  QCOMPARE(grouped.dy, 0);

  // Already touching horizontally, so only the parallel top edges move.
  const auto flush = deskflow::snapMonitorPlacement({placed(1, 100, 14, 80, 40)}, still, threshold, true);
  QCOMPARE(flush.dx, 0);
  QCOMPARE(flush.dy, -14);
  QVERIFY(flush.snappedX);
  QVERIFY(flush.snappedY);

  // Exact entry (snapping off) keeps a position that does not overlap.
  const auto exact = deskflow::snapMonitorPlacement({placed(1, 100, 14, 80, 40)}, still, threshold, false);
  QCOMPARE(exact.dx, 0);
  QCOMPARE(exact.dy, 0);
  QVERIFY(exact.guides.empty());
}

void MonitorLayoutTests::snapAllowsOverlap()
{
  const std::vector<deskflow::SnapRect> still = {placed(0, 0, 0, 1000, 1000)};

  // Inside, and no edge is within the threshold: the overlap stays.
  const auto kept = deskflow::snapMonitorPlacement({placed(1, 10, 10, 100, 100)}, still, 5, true);
  QCOMPARE(kept.dx, 0);
  QCOMPARE(kept.dy, 0);
  QVERIFY(!kept.snappedX);
  QVERIFY(kept.guides.empty());

  // Aligning both edges stays buried inside. That is a stack, so the snap is kept.
  const std::vector<deskflow::SnapRect> host = {placed(0, 0, 0, 200, 200)};
  const auto aligned = deskflow::snapMonitorPlacement({placed(1, 5, 5, 100, 100)}, host, 15, true);
  QCOMPARE(aligned.dx, -5);
  QCOMPARE(aligned.dy, -5);
  QVERIFY(aligned.snappedX);
  QVERIFY(aligned.snappedY);

  // An edge that is close enough still snaps out to a touch.
  const auto pulled = deskflow::snapMonitorPlacement({placed(1, 190, 50, 100, 100)}, host, 15, true);
  QCOMPARE(pulled.dx, 10);
  QCOMPARE(pulled.dy, 0);
  QVERIFY(pulled.snappedX);

  // Snapping off keeps the overlap instead of pushing it apart.
  const auto free = deskflow::snapMonitorPlacement({placed(1, 10, 10, 100, 100)}, still, 15, false);
  QCOMPARE(free.dx, 0);
  QCOMPARE(free.dy, 0);
}

void MonitorLayoutTests::switchEdgeSegments()
{
  const auto touch = deskflow::switchEdgeSegments({placed(0, 0, 0, 100, 100), placed(1, 100, 40, 80, 30)});
  QCOMPARE(static_cast<int>(touch.size()), 1);
  QVERIFY(touch[0].vertical);
  QCOMPARE(touch[0].position, 100);
  QCOMPARE(touch[0].begin, 40);
  QCOMPARE(touch[0].end, 70);

  const auto gap = deskflow::switchEdgeSegments({placed(0, 0, 0, 100, 100), placed(1, 110, 40, 80, 30)});
  QVERIFY(gap.empty());

  // The same computer continues the top of the edge, so only the rest switches.
  const auto blocked =
      deskflow::switchEdgeSegments({placed(0, 0, 0, 100, 200), placed(0, 100, 0, 40, 80), placed(1, 100, 0, 50, 200)});
  QCOMPARE(static_cast<int>(blocked.size()), 1);
  QVERIFY(blocked[0].vertical);
  QCOMPARE(blocked[0].position, 100);
  QCOMPARE(blocked[0].begin, 80);
  QCOMPARE(blocked[0].end, 200);

  const auto bottom = deskflow::switchEdgeSegments({placed(0, 0, 0, 100, 100), placed(1, 10, 100, 30, 40)});
  QCOMPARE(static_cast<int>(bottom.size()), 1);
  QVERIFY(!bottom[0].vertical);
  QCOMPARE(bottom[0].position, 100);
  QCOMPARE(bottom[0].begin, 10);
  QCOMPARE(bottom[0].end, 40);
}

void MonitorLayoutTests::stackTopReceivesTheOverlap()
{
  MonitorLayout layout;
  layout.setComputer("work", 0, 0, {monitor("work", 0, 0, 1920, 1080)}, true, 0);
  layout.setComputer("sur3", 100, 100, {monitor("sur3", 0, 0, 800, 600)}, true, 1);
  layout.setComputer("sp7", 100, 100, {monitor("sp7", 0, 0, 400, 300)}, true, 2);

  const auto covered = layout.stackTarget("work", 150, 150);
  QVERIFY(covered.has_value());
  QVERIFY(covered->computer == "sp7");
  QCOMPARE(covered->x, 50);
  QCOMPARE(covered->y, 50);

  // The part of work that nothing covers is still work.
  QVERIFY(!layout.stackTarget("work", 10, 10).has_value());

  int32_t clampX = 0;
  int32_t clampY = 0;
  const auto entered = layout.travel("work", 10, 10, 150, 150, clampX, clampY);
  QVERIFY(entered.has_value());
  QVERIFY(entered->computer == "sp7");

  // Stepping off sp7 lands on sur3, which is still above work. sp7 does not keep the pointer.
  const auto dropped = layout.travel("sp7", 200, 100, 400, 100, clampX, clampY);
  QVERIFY(dropped.has_value());
  QVERIFY(dropped->computer == "sur3");
  QCOMPARE(dropped->x, 400);
  QCOMPARE(dropped->y, 100);

  // A disabled front computer is not a target. The next one down receives the point.
  layout.setComputer("sp7", 100, 100, {monitor("sp7", 0, 0, 400, 300)}, false, 2);
  const auto skipped = layout.stackTarget("work", 150, 150);
  QVERIFY(skipped.has_value());
  QVERIFY(skipped->computer == "sur3");

  // Raising work above the stack gives the overlap back to work.
  layout.setComputer("work", 0, 0, {monitor("work", 0, 0, 1920, 1080)}, true, 5);
  QVERIFY(!layout.stackTarget("work", 150, 150).has_value());
  const auto under = layout.stackTarget("sur3", 50, 50);
  QVERIFY(under.has_value());
  QVERIFY(under->computer == "work");
}

void MonitorLayoutTests::layoutZOrderRoundTrip()
{
  const char *text = R"(section: layout
	work:
		origin = 0, 0
		z = 0
		monitor = work, 0, 0, 1920, 1080
	sp7:
		origin = 0, 0
		z = 3
		monitor = sp7, 0, 0, 800, 600
end
)";
  std::stringstream input(text);
  Config parsed(nullptr);
  input >> parsed;
  const auto sp7 = parsed.monitorPlacements().find("sp7");
  QVERIFY(sp7 != parsed.monitorPlacements().end());
  QCOMPARE(sp7->second.z, 3);
  QCOMPARE(parsed.monitorPlacements().find("work")->second.z, 0);

  std::stringstream output;
  output << parsed;
  QVERIFY(output.str().find("z = 3") != std::string::npos);

  const char *legacy = R"(section: layout
	work:
		origin = 1, 2
		monitor = work, 0, 0, 100, 80
end
)";
  std::stringstream oldInput(legacy);
  Config oldConfig(nullptr);
  oldInput >> oldConfig;
  QCOMPARE(oldConfig.monitorPlacements().find("work")->second.z, 0);
  QCOMPARE(oldConfig.monitorPlacements().find("work")->second.originX, 1);
}

void MonitorLayoutTests::disabledComputerIsNotASwitchTarget()
{
  MonitorLayout layout;
  layout.setComputer("work", 0, 0, {monitor("work", 0, 0, 1920, 1080)}, true);
  layout.setComputer("sp7", 1920, 0, {monitor("sp7", 0, 0, 2736, 1824)}, false);
  QVERIFY(layout.contains("sp7"));
  QVERIFY(!hit(layout, "work", 1919, 100).has_value());
  QVERIFY(layout.switchEdges("work").empty());

  layout.setComputer("sp7", 1920, 0, {monitor("sp7", 0, 0, 2736, 1824)}, true);
  const auto crossed = hit(layout, "work", 1919, 100);
  QVERIFY(crossed.has_value());
  QVERIFY(crossed->computer == "sp7");
  QVERIFY(!layout.switchEdges("work").empty());
}

void MonitorLayoutTests::unknownOrDisabledSwitchIsSkipped()
{
  // Options parsing builds a listen address, and release builds still call Log::getInstance().
  static Arch arch;
#if defined(Q_OS_WIN)
  arch.init();
#else
  (void)arch;
#endif
  static Log log(true);

  Settings::setValue(Settings::Computer::Name.arg(QStringLiteral("work")), QStringLiteral("work"));
  Settings::setValue(Settings::Computer::Name.arg(QStringLiteral("sp7")), QStringLiteral("sp7"));
  Settings::setValue(Settings::Computer::Disabled.arg(QStringLiteral("sp7")), true);
  Settings::proxy().setValue(Settings::Computer::Name.arg(QStringLiteral("work")), QStringLiteral("work"));
  Settings::proxy().setValue(Settings::Computer::Name.arg(QStringLiteral("sp7")), QStringLiteral("sp7"));
  Settings::proxy().setValue(Settings::Computer::Disabled.arg(QStringLiteral("sp7")), true);

  const char *text = R"(section: links
	work:
		right = sp7
	sp7:
		left = work
end
section: layout
	work:
		origin = 0, 0
		monitor = work, 0, 0, 1920, 1080
	sp7:
		origin = 1920, 0
		monitor = sp7, 0, 0, 2736, 1824
end
section: options
	keystroke(F1) = switchToScreen(sp7)
	keystroke(F2) = switchToScreen(missing)
	keystroke(F3) = switchToScreen(work)
end
)";

  std::stringstream input(text);
  Config parsed(nullptr);
  input >> parsed;

  QVERIFY(parsed.isComputer("sp7"));
  QVERIFY(parsed.isComputer("work"));
  QVERIFY(parsed.isComputerDisabled("sp7"));
  QVERIFY(!parsed.isComputerDisabled("work"));
  QVERIFY(!parsed.isComputer("missing"));
  QCOMPARE(parsed.getNeighbor("work", Direction::Right, 0.5f, nullptr), std::string());
  QCOMPARE(parsed.getNeighbor("sp7", Direction::Left, 0.5f, nullptr), std::string());
  QVERIFY(parsed.monitorPlacements().find("sp7") != parsed.monitorPlacements().end());
  QCOMPARE(parsed.monitorPlacements().find("sp7")->second.originX, 1920);
  QCOMPARE(parsed.getInputFilter()->getNumRules(), 1u);
  const auto rule = parsed.getInputFilter()->getRule(0).format();
  QVERIFY(rule.find("switchToScreen(work)") != std::string::npos);
  QVERIFY(rule.find("sp7") == std::string::npos);
  QVERIFY(rule.find("missing") == std::string::npos);
}

QTEST_MAIN(MonitorLayoutTests)
