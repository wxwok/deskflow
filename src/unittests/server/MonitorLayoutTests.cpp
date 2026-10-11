/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MonitorLayoutTests.h"

#include "server/Config.h"
#include "server/MonitorLayout.h"

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

QTEST_MAIN(MonitorLayoutTests)
