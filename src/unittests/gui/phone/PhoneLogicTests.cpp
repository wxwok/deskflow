/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/phone/PhoneStore.h"
#include "gui/phone/PhoneTypes.h"

#include <QSettings>
#include <QTemporaryFile>
#include <QTest>

using namespace deskflow::phone;

namespace {

const char *kDevices = R"(List of devices attached
ABC123             device usb:1-2 product:b5q model:SM_F731U device:b5q transport_id:1
192.168.1.20:5555  device product:b5q model:SM_F731U device:b5q transport_id:4
adb-ABC123._adb-tls-connect._tcp device product:b5q model:SM_F731U device:b5q transport_id:5
DEF456             unauthorized usb:1-3
)";

LayoutScreen serverMonitor(const QString &name, int originX, int originY, int x, int y, int width, int height)
{
  LayoutScreen screen;
  screen.computer = name;
  screen.monitorName = name;
  screen.server = name == QLatin1String("work");
  screen.originX = originX;
  screen.originY = originY;
  screen.local = QRect(x, y, width, height);
  return screen;
}

PhoneLayoutItem phoneAt(const QString &id, int x, int y, int width, int height, bool online = true)
{
  PhoneLayoutItem phone;
  phone.id = id;
  phone.label = id;
  phone.placed = true;
  phone.online = online;
  phone.originX = x;
  phone.originY = y;
  phone.width = width;
  phone.height = height;
  return phone;
}

int indexOf(const ClipPlan &plan, ClipOp op)
{
  for (int index = 0; index < plan.steps.size(); ++index) {
    if (plan.steps.at(index).op == op) {
      return index;
    }
  }
  return -1;
}

} // namespace

class PhoneLogicTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void usbAndWirelessAreOnePhoneAndUsbWins();
  void wirelessAloneStaysTheSamePhone();
  void bothTransportsDownAreOfflineAndNotDisabled();
  void unauthorizedIsNotOnline();
  void lanPairingRejectsPublicAddresses();
  void foldableStaysOneScreenAndKeepsItsOrigin();
  void newPhoneIsPlacedBelowTheDeskRow();
  void unregisteredOrOfflinePhoneDoesNotArmAnEdge();
  void edgeMapsToThePhoneOnThatSide();
  void occupiedEdgeIsLeftToTheExistingComputer();
  void dockStaysOnTheEdgeWhenTheAspectChanges();
  void pointerAlignKeepsTheFlushEdge();
  void bezelAndDragCountAsInside();
  void sessionPrewarmFailureDoesNotFreezeThePointer();
  void sessionDoesNotSpawnASecondProcess();
  void pinAndHysteresis();
  void waylandLeaveUsesTheSensorOnlyAfterTheBezel();
  void clipboardSendDoesNotPaste();
  void clipboardSendAndPasteFocusesThenInjects();
  void clipboardRejectsNonText();
  void pinnedAppsRoundTripPerPhone();
  void scrcpyUsesUhidAndOfficialDisplayFlags();
  void keyPlanKeepsScreenOffOnScrcpy();
  void windowControlIsHonestAboutWayland();
};

void PhoneLogicTests::usbAndWirelessAreOnePhoneAndUsbWins()
{
  QHash<QString, QString> serials;
  serials.insert(QStringLiteral("192.168.1.20:5555"), QStringLiteral("ABC123"));
  const auto phones = resolveDevices(QString::fromLatin1(kDevices), serials);
  int abc = 0;
  const ResolvedDevice *match = nullptr;
  for (const auto &phone : phones) {
    if (phone.id == QLatin1String("ABC123")) {
      ++abc;
      match = &phone;
    }
  }
  QCOMPARE(abc, 1);
  QVERIFY(match != nullptr);
  QVERIFY(match->online);
  QVERIFY(match->usb);
  QVERIFY(match->wireless);
  QCOMPARE(match->scrcpySerial, QStringLiteral("ABC123"));
  QCOMPARE(match->label, QStringLiteral("SM F731U"));
}

void PhoneLogicTests::wirelessAloneStaysTheSamePhone()
{
  const auto phones =
      resolveDevices(QStringLiteral("adb-ABC123._adb-tls-connect._tcp device product:b5q model:SM_F731U\n"));
  QCOMPARE(phones.size(), 1);
  QCOMPARE(phones.first().id, QStringLiteral("ABC123"));
  QVERIFY(phones.first().online);
  QVERIFY(!phones.first().usb);
  QVERIFY(phones.first().wireless);
  QCOMPARE(phones.first().scrcpySerial, QStringLiteral("adb-ABC123._adb-tls-connect._tcp"));
}

void PhoneLogicTests::bothTransportsDownAreOfflineAndNotDisabled()
{
  const auto decision = decideLink(false, false);
  QVERIFY(!decision.online);
  QVERIFY(!decision.writeDisabled);
  QVERIFY(!decideLink(true, false).writeDisabled);
  QVERIFY(decideLink(false, true).online);
  QVERIFY(decideLink(true, true).online);

  const auto phones = resolveDevices(QStringLiteral("ABC123 offline usb:1-2\n"));
  QCOMPARE(phones.size(), 1);
  QVERIFY(!phones.first().online);
  QVERIFY(phones.first().scrcpySerial.isEmpty());
}

void PhoneLogicTests::unauthorizedIsNotOnline()
{
  const auto phones = resolveDevices(QStringLiteral("DEF456 unauthorized usb:1-3\n"));
  QCOMPARE(phones.size(), 1);
  QVERIFY(!phones.first().online);
  QCOMPARE(phones.first().stateDetail, QStringLiteral("unauthorized"));
}

void PhoneLogicTests::lanPairingRejectsPublicAddresses()
{
  QVERIFY(isPrivateLanHost(QStringLiteral("192.168.1.20")));
  QVERIFY(isPrivateLanHost(QStringLiteral("10.1.2.3")));
  QVERIFY(isPrivateLanHost(QStringLiteral("172.16.0.4")));
  QVERIFY(!isPrivateLanHost(QStringLiteral("8.8.8.8")));
  QVERIFY(!isPrivateLanHost(QStringLiteral("127.0.0.1")));
  QVERIFY(!isPrivateLanHost(QStringLiteral("phone.example.com")));

  const auto rejected = planWirelessPair(QStringLiteral("8.8.8.8"), 37123, QStringLiteral("123456"), 5555);
  QVERIFY(!rejected.ok);
  QVERIFY(rejected.pairArgs.isEmpty());

  const auto accepted = planWirelessPair(QStringLiteral("192.168.1.20"), 37123, QStringLiteral("123456"), 5555);
  QVERIFY(accepted.ok);
  QCOMPARE(accepted.pairArgs.at(0), QStringLiteral("pair"));
  QCOMPARE(accepted.connectArgs.at(1), QStringLiteral("192.168.1.20:5555"));
  QVERIFY(!accepted.pairArgs.join(QLatin1Char(' ')).contains(QStringLiteral("disabled")));
}

void PhoneLogicTests::foldableStaysOneScreenAndKeepsItsOrigin()
{
  const auto displays = parseListDisplays(QStringLiteral("* 0 1080x2640\n  1 748x720\n"));
  QCOMPARE(displays.size(), 2);
  const auto chosen = selectLogicalDisplay(displays);
  QCOMPARE(chosen.id, 0);
  QCOMPARE(chosen.width, 1080);
  QCOMPARE(chosen.height, 2640);

  const auto updated = applySizeInPlace(400, 2500, 1080, 2640, QSize(748, 720));
  QVERIFY(updated.changed);
  QCOMPARE(updated.originX, 400);
  QCOMPARE(updated.originY, 2500);
  QCOMPARE(updated.width, 748);
  QCOMPARE(updated.height, 720);

  const auto overrideSize = parseWmSize(QStringLiteral("Physical size: 1080x2640\nOverride size: 1080x2520\n"));
  QCOMPARE(overrideSize, QSize(1080, 2520));
}

void PhoneLogicTests::newPhoneIsPlacedBelowTheDeskRow()
{
  const QList<QRect> desks = {QRect(-1920, 0, 1920, 1080), QRect(0, 0, 1920, 1080), QRect(1920, 0, 1920, 1080)};
  const auto origin = placeBelowExisting(desks, 1080, 2640, 240);
  const QRect phone(origin.x, origin.y, 1080, 2640);
  for (const auto &desk : desks) {
    QVERIFY(!phone.intersects(desk));
    QVERIFY(phone.top() >= desk.bottom() + 1);
  }
  QVERIFY(phone.top() >= 1080 + 240);
}

void PhoneLogicTests::unregisteredOrOfflinePhoneDoesNotArmAnEdge()
{
  const auto screens = QList<LayoutScreen>{serverMonitor(QStringLiteral("work"), 0, 0, 0, 0, 1920, 1080)};
  auto offline = phoneAt(QStringLiteral("ABC123"), 1920, 0, 1080, 1920, false);
  QVERIFY(hotEdges(screens, {offline}).isEmpty());
  auto unplaced = offline;
  unplaced.online = true;
  unplaced.placed = false;
  QVERIFY(hotEdges(screens, {unplaced}).isEmpty());
  QVERIFY(hotEdges(screens, {}).isEmpty());
}

void PhoneLogicTests::edgeMapsToThePhoneOnThatSide()
{
  const auto screens = QList<LayoutScreen>{serverMonitor(QStringLiteral("work"), 0, 0, 0, 0, 1920, 1080)};
  const auto right = phoneAt(QStringLiteral("RIGHT"), 1920, 0, 1080, 1920);
  const auto bottom = phoneAt(QStringLiteral("BOTTOM"), 0, 1080, 1920, 1080);
  const auto edges = hotEdges(screens, {right, bottom});
  const HotEdge *rightEdge = nullptr;
  const HotEdge *bottomEdge = nullptr;
  for (const auto &edge : edges) {
    if (edge.phoneId == QLatin1String("RIGHT")) {
      rightEdge = &edge;
    }
    if (edge.phoneId == QLatin1String("BOTTOM")) {
      bottomEdge = &edge;
    }
  }
  QVERIFY(rightEdge != nullptr);
  QVERIFY(bottomEdge != nullptr);
  QCOMPARE(rightEdge->side, ScreenEdge::Right);
  QCOMPARE(bottomEdge->side, ScreenEdge::Bottom);

  const auto hit = hitTestEdge(*rightEdge, QPoint(1918, 100), 4);
  QVERIFY(hit.has_value());
  QCOMPARE(hit->phoneId, QStringLiteral("RIGHT"));
  QVERIFY(!hitTestEdge(*rightEdge, QPoint(100, 100), 4).has_value());
  QVERIFY(!hitTestEdge(*bottomEdge, QPoint(1918, 100), 4).has_value());
}

void PhoneLogicTests::occupiedEdgeIsLeftToTheExistingComputer()
{
  QList<LayoutScreen> screens;
  screens.append(serverMonitor(QStringLiteral("work"), 0, 0, 0, 0, 1920, 1080));
  auto sur3 = serverMonitor(QStringLiteral("sur3"), -1920, 0, 0, 0, 1920, 1080);
  sur3.server = false;
  screens.append(sur3);
  const auto phone = phoneAt(QStringLiteral("ABC123"), -1080, 0, 1080, 1920);
  QVERIFY(hotEdges(screens, {phone}).isEmpty());
}

void PhoneLogicTests::dockStaysOnTheEdgeWhenTheAspectChanges()
{
  const QRect monitor(0, 0, 1920, 1080);
  const auto tall = dockOnMonitor(monitor, ScreenEdge::Left, 1080, 2640, 0.4, 56);
  const auto wide = dockOnMonitor(monitor, ScreenEdge::Left, 2640, 1080, 0.4, 56);
  QCOMPARE(tall.video.left(), monitor.left());
  QCOMPARE(wide.video.left(), monitor.left());
  QVERIFY(tall.video.height() > tall.video.width());
  QVERIFY(wide.video.width() > wide.video.height());
  QVERIFY(tall.bezel.left() >= tall.video.right());
  QVERIFY(tall.combined.contains(tall.video));
  QVERIFY(tall.combined.contains(tall.bezel));
  QVERIFY(tall.pointer.x() < monitor.center().x());
  QVERIFY(wide.pointer.x() < monitor.center().x());
}

void PhoneLogicTests::pointerAlignKeepsTheFlushEdge()
{
  const QRect monitor(0, 0, 1920, 1080);
  const auto dock = dockOnMonitor(monitor, ScreenEdge::Left, 1080, 2640, 0.2, 56);
  const auto moved = alignPointer(dock, QPoint(3, 40), monitor);
  QCOMPARE(moved.video.left(), dock.video.left());
  QVERIFY(moved.video.top() <= 40);
  QVERIFY(moved.pointer.y() >= monitor.top());
  QVERIFY(moved.pointer.y() <= monitor.bottom());
}

void PhoneLogicTests::bezelAndDragCountAsInside()
{
  const QRect video(0, 0, 400, 800);
  const QRect bezel(400, 0, 56, 800);
  const QRect edge(0, 0, 4, 800);
  QVERIFY(combinedContains(video, bezel, edge, QPoint(420, 20), false));
  QVERIFY(combinedContains(video, bezel, edge, QPoint(20, 20), false));
  QVERIFY(!combinedContains(video, bezel, edge, QPoint(500, 20), false));
  QVERIFY(combinedContains(video, bezel, edge, QPoint(500, 20), true));
}

void PhoneLogicTests::sessionPrewarmFailureDoesNotFreezeThePointer()
{
  SessionConfig config;
  config.prewarmHidden = true;
  config.hideKeepsProcess = true;
  const auto started = reduceSession(SessionState{}, config, SessionEvent::DeviceOnline);
  QCOMPARE(started.state.phase, SessionPhase::Prewarming);
  QCOMPARE(started.action, SessionAction::SpawnHidden);
  QVERIFY(!started.pointerFrozen);

  const auto failed = reduceSession(started.state, config, SessionEvent::PrewarmFailed);
  QCOMPARE(failed.state.phase, SessionPhase::Failed);
  QCOMPARE(failed.action, SessionAction::Notify);
  QVERIFY(!failed.pointerFrozen);

  const auto retry = reduceSession(failed.state, config, SessionEvent::EdgeEntered);
  QCOMPARE(retry.action, SessionAction::SpawnVisible);
  QVERIFY(!retry.pointerFrozen);

  const SessionEvent events[] = {SessionEvent::DeviceOnline,  SessionEvent::DeviceOffline, SessionEvent::PrewarmOk,
                                 SessionEvent::PrewarmFailed, SessionEvent::EdgeEntered,   SessionEvent::HideRequested,
                                 SessionEvent::Adopted};
  for (const auto phase :
       {SessionPhase::Idle, SessionPhase::Prewarming, SessionPhase::HiddenReady, SessionPhase::Visible,
        SessionPhase::Failed}) {
    for (const auto event : events) {
      SessionState state;
      state.phase = phase;
      state.pinned = true;
      QVERIFY(!reduceSession(state, config, event).pointerFrozen);
    }
  }
}

void PhoneLogicTests::sessionDoesNotSpawnASecondProcess()
{
  SessionConfig config;
  config.prewarmHidden = true;
  config.hideKeepsProcess = true;
  SessionState visible;
  visible.phase = SessionPhase::Visible;
  const auto again = reduceSession(visible, config, SessionEvent::EdgeEntered);
  QCOMPARE(again.action, SessionAction::None);
  QCOMPARE(again.state.phase, SessionPhase::Visible);

  SessionState hidden;
  hidden.phase = SessionPhase::HiddenReady;
  const auto adopted = reduceSession(hidden, config, SessionEvent::Adopted);
  QCOMPARE(adopted.action, SessionAction::None);
  const auto shown = reduceSession(adopted.state, config, SessionEvent::EdgeEntered);
  QCOMPARE(shown.action, SessionAction::Show);

  SessionConfig cold;
  const auto fromIdle = reduceSession(SessionState{}, cold, SessionEvent::DeviceOnline);
  QCOMPARE(fromIdle.action, SessionAction::None);
  QCOMPARE(fromIdle.state.phase, SessionPhase::Idle);
  const auto entered = reduceSession(fromIdle.state, cold, SessionEvent::EdgeEntered);
  QCOMPARE(entered.action, SessionAction::SpawnVisible);
}

void PhoneLogicTests::pinAndHysteresis()
{
  LeaveTracker tracker;
  LeaveSample inside;
  inside.insideCombined = true;
  inside.hysteresisMs = 280;
  tracker = advanceLeave(tracker, inside, 50).tracker;

  LeaveSample outside;
  outside.hysteresisMs = 280;
  auto first = advanceLeave(tracker, outside, 100);
  QCOMPARE(first.effect, LeaveEffect::Stay);
  auto second = advanceLeave(first.tracker, outside, 180);
  QCOMPARE(second.effect, LeaveEffect::Hide);

  LeaveSample pinned;
  pinned.pinned = true;
  pinned.hysteresisMs = 280;
  QCOMPARE(advanceLeave({}, pinned, 5000).effect, LeaveEffect::Stay);

  LeaveSample bezel;
  bezel.insideCombined = true;
  bezel.hysteresisMs = 280;
  auto onBezel = advanceLeave(first.tracker, bezel, 1000);
  QCOMPARE(onBezel.effect, LeaveEffect::Stay);
  QVERIFY(!onBezel.tracker.outside);
}

void PhoneLogicTests::waylandLeaveUsesTheSensorOnlyAfterTheBezel()
{
  WaylandPointer approaching;
  approaching.onSensor = true;
  const auto approach = waylandPointerInside(approaching);
  QVERIFY(approach.inside);
  QVERIFY(!approach.bezelHot);

  WaylandPointer bezel;
  bezel.onBezel = true;
  const auto onBezel = waylandPointerInside(bezel);
  QVERIFY(onBezel.inside);
  QVERIFY(onBezel.bezelHot);

  WaylandPointer leaving;
  leaving.onSensor = true;
  leaving.bezelHot = true;
  const auto left = waylandPointerInside(leaving);
  QVERIFY(!left.inside);

  WaylandPointer video;
  video.bezelHot = true;
  const auto overPicture = waylandPointerInside(video);
  QVERIFY(overPicture.inside);

  WaylandPointer drag;
  drag.drag = true;
  QVERIFY(waylandPointerInside(drag).inside);
}

void PhoneLogicTests::clipboardSendDoesNotPaste()
{
  const auto plan = planClipboard(ClipboardIntent::Send, QStringLiteral("你好"), true);
  QCOMPARE(plan.steps.size(), 1);
  QCOMPARE(plan.steps.first().op, ClipOp::SetDeviceClipboard);
  QVERIFY(!plan.pastes);
  QCOMPARE(indexOf(plan, ClipOp::InjectPaste), -1);
  QVERIFY(writeClipboardCommand(QStringLiteral("ABC123"), QStringLiteral("it's a test"))
              .args.join(QLatin1Char(' '))
              .contains(QStringLiteral("'it'\\''s a test'")));
}

void PhoneLogicTests::clipboardSendAndPasteFocusesThenInjects()
{
  const auto plan = planClipboard(ClipboardIntent::SendAndPaste, QStringLiteral("hello"), true);
  QVERIFY(plan.pastes);
  const int device = indexOf(plan, ClipOp::SetDeviceClipboard);
  const int computer = indexOf(plan, ClipOp::SetComputerClipboard);
  const int focus = indexOf(plan, ClipOp::FocusWindow);
  const int paste = indexOf(plan, ClipOp::InjectPaste);
  QVERIFY(device >= 0);
  QVERIFY(computer > device);
  QVERIFY(focus > computer);
  QVERIFY(paste > focus);

  const auto blocked = planClipboard(ClipboardIntent::SendAndPaste, QStringLiteral("hello"), false);
  QVERIFY(!blocked.pastes);
  QCOMPARE(indexOf(blocked, ClipOp::InjectPaste), -1);
  QCOMPARE(blocked.steps.last().op, ClipOp::Error);
  QVERIFY(indexOf(blocked, ClipOp::SetDeviceClipboard) >= 0);
}

void PhoneLogicTests::clipboardRejectsNonText()
{
  QVERIFY(isPlainText(QStringLiteral("line\n\tmore")));
  QVERIFY(!isPlainText(QString(QChar(1))));
  const auto plan = planClipboard(ClipboardIntent::SendAndPaste, QString(QChar(1)), true);
  QCOMPARE(plan.steps.size(), 1);
  QCOMPARE(plan.steps.first().op, ClipOp::Error);
  QVERIFY(!plan.pastes);
}

void PhoneLogicTests::pinnedAppsRoundTripPerPhone()
{
  QTemporaryFile file;
  QVERIFY(file.open());
  QSettings settings(file.fileName(), QSettings::IniFormat);
  PhoneStore store(settings);
  PhoneRecord first;
  first.id = QStringLiteral("ABC123");
  first.label = QStringLiteral("Flip");
  first.placed = true;
  first.originX = 10;
  first.originY = 20;
  first.apps = {PhoneApp{QStringLiteral("com.example.mail"), QStringLiteral("Mail")}};
  PhoneRecord second;
  second.id = QStringLiteral("OTHER");
  second.apps = {PhoneApp{QStringLiteral("com.example.maps"), QStringLiteral("Maps")}};
  store.save(first);
  store.save(second);

  QSettings again(file.fileName(), QSettings::IniFormat);
  PhoneStore loaded(again);
  const auto mail = loaded.phone(QStringLiteral("ABC123"));
  const auto maps = loaded.phone(QStringLiteral("OTHER"));
  QVERIFY(mail.has_value());
  QVERIFY(maps.has_value());
  QCOMPARE(mail->apps.size(), 1);
  QCOMPARE(mail->apps.first().packageName, QStringLiteral("com.example.mail"));
  QCOMPARE(mail->originX, 10);
  QCOMPARE(maps->apps.first().packageName, QStringLiteral("com.example.maps"));
  QVERIFY(mail->apps.first().packageName != maps->apps.first().packageName);

  for (const auto &key : again.allKeys()) {
    QVERIFY(!key.contains(QStringLiteral("disabled")));
    QVERIFY(!key.startsWith(QStringLiteral("computer_")));
  }

  loaded.setApps(QStringLiteral("ABC123"), {PhoneApp{QStringLiteral("com.example.notes"), QStringLiteral("Notes")}});
  QCOMPARE(loaded.phone(QStringLiteral("ABC123"))->apps.first().packageName, QStringLiteral("com.example.notes"));
  QCOMPARE(loaded.phone(QStringLiteral("OTHER"))->apps.first().packageName, QStringLiteral("com.example.maps"));

  const auto hidden = planAppLaunch(QStringLiteral("ABC123"), QStringLiteral("com.example.notes"), true);
  QVERIFY(hidden.showWindowFirst);
  QVERIFY(hidden.command.args.contains(QStringLiteral("com.example.notes")));
  QVERIFY(planAppLaunch(QStringLiteral("ABC123"), QStringLiteral("not a package"), false).command.args.isEmpty());
}

void PhoneLogicTests::scrcpyUsesUhidAndOfficialDisplayFlags()
{
  ScrcpyRequest request;
  request.serial = QStringLiteral("ABC123");
  request.phoneId = QStringLiteral("ABC123");
  request.displayId = 1;
  request.placeWindow = true;
  request.x = 0;
  request.y = 10;
  request.width = 400;
  request.height = 900;
  request.audio = false;
  const auto args = scrcpyArguments(request);
  QVERIFY(args.contains(QStringLiteral("--keyboard=uhid")));
  QVERIFY(!args.contains(QStringLiteral("--keyboard=sdk")));
  QVERIFY(args.contains(QStringLiteral("--display-id=1")));
  QVERIFY(args.contains(QStringLiteral("--no-audio")));
  QVERIFY(args.contains(QStringLiteral("-s")));
  const auto joined = args.join(QLatin1Char(' '));
  QVERIFY(!joined.contains(QStringLiteral("rustdesk"), Qt::CaseInsensitive));
  QCOMPARE(scrcpyWindowTitle(QStringLiteral("ABC123")), QStringLiteral("deskflow-phone-ABC123"));

  const auto running =
      parseScrcpyProcessList(QStringLiteral("42 scrcpy -s ABC123 --keyboard=uhid\n7 scrcpy -s OTHER\n"));
  QCOMPARE(running.size(), 2);
  QVERIFY(scrcpyCommandOwnsSerial(running.first().args, QStringLiteral("ABC123")));
  QVERIFY(!scrcpyCommandOwnsSerial(running.at(1).args, QStringLiteral("ABC123")));

  const auto choice = chooseExecutable(
      QString(), false, QStringLiteral("/usr/bin/scrcpy"), {QStringLiteral("/opt/scrcpy-v5.0.1/scrcpy")}
  );
  QCOMPARE(choice.source, ExecutableSource::Path);
  QCOMPARE(choice.path, QStringLiteral("/usr/bin/scrcpy"));
}

void PhoneLogicTests::keyPlanKeepsScreenOffOnScrcpy()
{
  const auto injected = planKey(PhoneKey::ScreenOff, true, QStringLiteral("ABC123"));
  QVERIFY(injected.inject);
  QCOMPARE(injected.xdotoolKey, QStringLiteral("alt+o"));
  QVERIFY(injected.adb.args.isEmpty());

  const auto blocked = planKey(PhoneKey::ScreenOff, false, QStringLiteral("ABC123"));
  QVERIFY(!blocked.inject);
  QVERIFY(blocked.adb.args.isEmpty());
  QVERIFY(blocked.unsupportedWithoutInject);

  const auto back = planKey(PhoneKey::Back, false, QStringLiteral("ABC123"));
  QVERIFY(!back.adb.args.isEmpty());
  QCOMPARE(back.adb.args.last(), QStringLiteral("4"));
}

void PhoneLogicTests::windowControlIsHonestAboutWayland()
{
  const auto wayland = windowControlFor(QStringLiteral("wayland"), true, true);
  QVERIFY(!wayland.hideWindow);
  QVERIFY(!wayland.activateWindow);
  QVERIFY(!wayland.warpPointer);
  QVERIFY(!wayland.injectKeys);
  QCOMPARE(wayland.limit, WindowLimit::Wayland);

  const auto x11 = windowControlFor(QStringLiteral("x11"), true, true);
  QVERIFY(x11.hideWindow);
  QVERIFY(x11.warpPointer);
  QVERIFY(x11.injectKeys);
  QCOMPARE(x11.limit, WindowLimit::None);

  const auto missing = windowControlFor(QStringLiteral("x11"), false, true);
  QCOMPARE(missing.limit, WindowLimit::NoXdotool);
  QVERIFY(!missing.hideWindow);
}

QTEST_MAIN(PhoneLogicTests)

#include "PhoneLogicTests.moc"
