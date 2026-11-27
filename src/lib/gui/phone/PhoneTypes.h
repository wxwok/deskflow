/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QHash>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>

namespace deskflow::phone {

//! Which side of a computer monitor faces the phone.
enum class ScreenEdge
{
  Left,
  Right,
  Top,
  Bottom
};

enum class SessionPhase
{
  Idle,
  Prewarming,
  HiddenReady,
  Visible,
  Failed
};

enum class SessionEvent
{
  DeviceOnline,
  DeviceOffline,
  PrewarmOk,
  PrewarmFailed,
  EdgeEntered,
  HideRequested,
  Adopted
};

enum class SessionAction
{
  None,
  SpawnHidden,
  SpawnVisible,
  Show,
  Hide,
  Stop,
  Notify
};

enum class ClipboardIntent
{
  Send,
  SendAndPaste
};

enum class ClipOp
{
  SetComputerClipboard,
  SetDeviceClipboard,
  FocusWindow,
  InjectPaste,
  Error
};

enum class ExecutableSource
{
  Configured,
  Path,
  WellKnown,
  Missing
};

enum class WindowLimit
{
  None,
  Wayland,
  NoXdotool,
  UnsupportedOs
};

enum class LeaveEffect
{
  Stay,
  Hide
};

//! One row of `adb devices -l`.
struct AdbDeviceLine
{
  QString serial;
  QString state;
  bool usb = false;
  QString model;
  QString product;
};

//! One physical phone after USB and wireless endpoints are folded together.
struct ResolvedDevice
{
  QString id;
  QString label;
  QString model;
  bool online = false;
  bool usb = false;
  bool wireless = false;
  //! Serial passed to `scrcpy -s` and `adb -s`. USB wins when both are up.
  QString scrcpySerial;
  QString stateDetail;

  bool operator==(const ResolvedDevice &) const = default;
};

struct PhoneApp
{
  QString packageName;
  QString label;

  bool operator==(const PhoneApp &) const = default;
};

//! Saved registration. Disconnect never turns this into a Disabled computer.
struct PhoneRecord
{
  QString id;
  QString label;
  QString model;
  bool placed = false;
  int originX = 0;
  int originY = 0;
  int width = 1080;
  int height = 2640;
  int displayId = -1;
  bool pinned = false;
  bool keyboardHintSeen = false;
  bool audioMuted = false;
  QList<PhoneApp> apps;

  bool operator==(const PhoneRecord &) const = default;
};

struct PhoneLayoutItem
{
  QString id;
  QString label;
  bool placed = false;
  bool online = false;
  int originX = 0;
  int originY = 0;
  int width = 1080;
  int height = 2640;
};

struct PhoneRosterEntry
{
  QString id;
  QString label;
  QString detail;
  bool registered = false;
  bool online = false;
};

struct LayoutScreen
{
  QString computer;
  QString monitorName;
  bool server = false;
  int originX = 0;
  int originY = 0;
  QRect local;
};

struct HotEdge
{
  QString phoneId;
  QString monitorName;
  ScreenEdge side = ScreenEdge::Left;
  //! Overlap of the phone and this monitor edge, in the monitor's local pixels.
  int alongBegin = 0;
  int alongEnd = 0;
  int edgePosition = 0;
  int monitorX = 0;
  int monitorY = 0;
  int monitorWidth = 0;
  int monitorHeight = 0;
  int serverOriginX = 0;
  int serverOriginY = 0;
  int phoneOriginX = 0;
  int phoneOriginY = 0;
  int phoneWidth = 0;
  int phoneHeight = 0;
};

struct EdgeHit
{
  QString phoneId;
  ScreenEdge side = ScreenEdge::Left;
  double alongFraction = 0.0;
};

struct DockGeometry
{
  QRect video;
  QRect bezel;
  QRect combined;
  QPoint pointer;
  ScreenEdge side = ScreenEdge::Left;
};

struct SessionConfig
{
  bool prewarmHidden = false;
  bool hideKeepsProcess = false;
};

struct SessionState
{
  SessionPhase phase = SessionPhase::Idle;
  bool pinned = false;

  bool operator==(const SessionState &) const = default;
};

struct SessionStep
{
  SessionState state;
  SessionAction action = SessionAction::None;
  //! Always false. A failed prewarm must not capture the desktop pointer.
  bool pointerFrozen = false;
  QString notice;
};

struct ClipStep
{
  ClipOp op = ClipOp::Error;
  QString text;
  QString detail;

  bool operator==(const ClipStep &) const = default;
};

struct ClipPlan
{
  QList<ClipStep> steps;
  bool pastes = false;
};

struct AdbCommand
{
  QStringList args;

  bool operator==(const AdbCommand &) const = default;
};

struct PairPlan
{
  bool ok = false;
  QString error;
  QStringList pairArgs;
  QStringList connectArgs;
};

struct LaunchPlan
{
  bool showWindowFirst = false;
  AdbCommand command;
};

struct ScrcpyRequest
{
  QString serial;
  QString phoneId;
  int displayId = -1;
  bool placeWindow = false;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool audio = true;
};

struct PhoneDisplay
{
  int id = -1;
  int width = 0;
  int height = 0;
  bool current = false;

  bool operator==(const PhoneDisplay &) const = default;
};

struct ExecutableChoice
{
  ExecutableSource source = ExecutableSource::Missing;
  QString path;
};

struct WindowControl
{
  bool findWindow = false;
  bool hideWindow = false;
  bool activateWindow = false;
  bool warpPointer = false;
  bool injectKeys = false;
  WindowLimit limit = WindowLimit::UnsupportedOs;
};

struct LeaveTracker
{
  bool outside = false;
  int outsideMs = 0;

  bool operator==(const LeaveTracker &) const = default;
};

struct LeaveSample
{
  bool pinned = false;
  bool insideCombined = false;
  int hysteresisMs = 280;
};

struct LeaveResult
{
  LeaveEffect effect = LeaveEffect::Stay;
  LeaveTracker tracker;
};

//! Wayland cannot see the pointer over scrcpy, so only the exit sensor counts as leaving.
struct WaylandPointer
{
  bool onEdge = false;
  bool onBezel = false;
  bool onSensor = false;
  bool drag = false;
  bool bezelHot = false;
};

struct WaylandInside
{
  bool inside = false;
  bool bezelHot = false;
};

struct LinkDecision
{
  bool online = false;
  //! Disconnects are Offline. This stays false so nothing writes computer_* / disabled.
  bool writeDisabled = false;
};

struct PlacedOrigin
{
  int x = 0;
  int y = 0;
};

struct SizeUpdate
{
  int originX = 0;
  int originY = 0;
  int width = 0;
  int height = 0;
  bool changed = false;
};

struct RunningScrcpy
{
  qint64 pid = 0;
  QString args;
};

struct SinkInput
{
  int id = -1;
  QString application;
};

//! Official scrcpy shortcuts the bezel mirrors. Values that scrcpy only accepts as keys stay key-only.
enum class PhoneKey
{
  Back,
  Home,
  Recents,
  Power,
  ScreenOff,
  ScreenOn,
  VolumeUp,
  VolumeDown,
  Notifications,
  KeyboardLayout,
  Paste
};

struct KeyPlan
{
  bool inject = false;
  QString xdotoolKey;
  AdbCommand adb;
  bool unsupportedWithoutInject = false;
};

QList<AdbDeviceLine> parseAdbDevices(const QString &text);
QList<ResolvedDevice>
resolveDevices(const QString &adbDevicesText, const QHash<QString, QString> &serialByEndpoint = {});
LinkDecision decideLink(bool usbReady, bool wirelessReady);
bool isPrivateLanHost(const QString &host);
PairPlan planWirelessPair(const QString &host, int pairPort, const QString &code, int connectPort);

QList<PhoneDisplay> parseListDisplays(const QString &text);
PhoneDisplay selectLogicalDisplay(const QList<PhoneDisplay> &displays);
QSize parseWmSize(const QString &text);
SizeUpdate applySizeInPlace(int originX, int originY, int width, int height, const QSize &next);
int parseBatteryPercent(const QString &dumpsys);
QList<PhoneApp> parseLauncherPackages(const QString &text);

PlacedOrigin placeBelowExisting(const QList<QRect> &existing, int width, int height, int gap = 240);
QList<HotEdge> hotEdges(const QList<LayoutScreen> &screens, const QList<PhoneLayoutItem> &phones);
std::optional<EdgeHit> hitTestEdge(const HotEdge &edge, const QPoint &localPointer, int zone);
double alongFraction(const HotEdge &edge, const QPoint &localPointer);

DockGeometry dockOnMonitor(
    const QRect &monitor, ScreenEdge side, int phoneWidth, int phoneHeight, double alongFraction, int bezelThickness,
    double maxAlongFraction = 0.90
);
DockGeometry alignPointer(DockGeometry dock, const QPoint &cursor, const QRect &monitor);
bool combinedContains(
    const QRect &video, const QRect &bezel, const QRect &edgeBar, const QPoint &point, bool dragActive
);

SessionStep reduceSession(const SessionState &state, const SessionConfig &config, SessionEvent event);
LeaveResult advanceLeave(const LeaveTracker &tracker, const LeaveSample &sample, int dtMs);
WaylandInside waylandPointerInside(const WaylandPointer &pointer);

bool isPlainText(const QString &text);
ClipPlan planClipboard(ClipboardIntent intent, const QString &text, bool canFocusAndPaste);
AdbCommand readClipboardCommand(const QString &serial);
AdbCommand writeClipboardCommand(const QString &serial, const QString &text);
LaunchPlan planAppLaunch(const QString &serial, const QString &packageName, bool windowHidden);
AdbCommand pushFileCommand(const QString &serial, const QString &localPath);
QStringList scrcpyArguments(const ScrcpyRequest &request);
QString scrcpyWindowTitle(const QString &phoneId);
KeyPlan planKey(PhoneKey key, bool canInject, const QString &serial);

ExecutableChoice chooseExecutable(
    const QString &configured, bool configuredUsable, const QString &pathHit, const QStringList &wellKnown
);
WindowControl windowControlFor(const QString &sessionType, bool xdotoolFound, bool unixLike);
QList<RunningScrcpy> parseScrcpyProcessList(const QString &text);
bool scrcpyCommandOwnsSerial(const QString &args, const QString &serial);
QList<SinkInput> parsePactlSinkInputs(const QString &text);
QStringList proxyVariablesToDrop();

//! Storage key for a serial. Not a default serial.
QString storageKeyForId(const QString &id);

} // namespace deskflow::phone
