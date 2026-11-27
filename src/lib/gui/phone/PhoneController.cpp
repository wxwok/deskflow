/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/phone/PhoneController.h"

#include "common/Settings.h"
#include "dialogs/MonitorLayoutDialog.h"
#include "gui/config/ServerConfig.h"
#include "gui/phone/ClipboardEditor.h"
#include "gui/phone/HotEdgeBar.h"
#include "gui/phone/PairingDialog.h"
#include "gui/phone/PhoneBezel.h"
#include "gui/phone/PhoneStore.h"
#include "gui/phone/PinnedAppsDialog.h"
#include "gui/widgets/MonitorLayoutWidget.h"

#include <QCheckBox>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace deskflow::phone {

namespace {

QProcessEnvironment toolEnvironment()
{
  auto environment = QProcessEnvironment::systemEnvironment();
  for (const auto &name : proxyVariablesToDrop()) {
    environment.remove(name);
  }
  return environment;
}

QString runTool(const QString &program, const QStringList &args, int timeoutMs, int *exitCode)
{
  if (exitCode != nullptr) {
    *exitCode = -1;
  }
  if (program.isEmpty()) {
    return {};
  }
  QProcess process;
  process.setProcessEnvironment(toolEnvironment());
  process.start(program, args);
  if (!process.waitForStarted(2000) || !process.waitForFinished(timeoutMs)) {
    process.kill();
    return {};
  }
  if (exitCode != nullptr) {
    *exitCode = process.exitCode();
  }
  auto text = QString::fromUtf8(process.readAllStandardOutput());
  if (text.trimmed().isEmpty()) {
    text = QString::fromUtf8(process.readAllStandardError());
  }
  return text;
}

QString sessionType()
{
  const auto type = qEnvironmentVariable("XDG_SESSION_TYPE");
  if (type.isEmpty() && !qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty()) {
    return QStringLiteral("wayland");
  }
  return type;
}

QRect sensorRect(const DockGeometry &dock)
{
  constexpr int thick = 10;
  switch (dock.side) {
  case ScreenEdge::Left:
    return {dock.bezel.right() + 1, dock.bezel.top(), thick, dock.bezel.height()};
  case ScreenEdge::Right:
    return {dock.bezel.left() - thick, dock.bezel.top(), thick, dock.bezel.height()};
  case ScreenEdge::Top:
    return {dock.bezel.left(), dock.bezel.bottom() + 1, dock.bezel.width(), thick};
  case ScreenEdge::Bottom:
    return {dock.bezel.left(), dock.bezel.top() - thick, dock.bezel.width(), thick};
  }
  return {};
}

QRect edgeBarRect(const HotEdge &edge)
{
  constexpr int thick = 4;
  const int span = std::max(1, edge.alongEnd - edge.alongBegin);
  switch (edge.side) {
  case ScreenEdge::Left:
    return {edge.edgePosition, edge.alongBegin, thick, span};
  case ScreenEdge::Right:
    return {edge.edgePosition - thick + 1, edge.alongBegin, thick, span};
  case ScreenEdge::Top:
    return {edge.alongBegin, edge.edgePosition, span, thick};
  case ScreenEdge::Bottom:
    return {edge.alongBegin, edge.edgePosition - thick + 1, span, thick};
  }
  return {};
}

QString readProcessTable()
{
#ifdef Q_OS_LINUX
  QStringList rows;
  const QDir proc(QStringLiteral("/proc"));
  const auto entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const auto &entry : entries) {
    bool pidOk = false;
    entry.toLongLong(&pidOk);
    if (!pidOk) {
      continue;
    }
    QFile command(QStringLiteral("/proc/%1/cmdline").arg(entry));
    if (!command.open(QIODevice::ReadOnly)) {
      continue;
    }
    auto bytes = command.readAll();
    bytes.replace('\0', ' ');
    rows.append(entry + QLatin1Char(' ') + QString::fromLocal8Bit(bytes));
  }
  return rows.join(QLatin1Char('\n'));
#else
  return {};
#endif
}

QString resolvedProgram(const QString &configured, const QString &name, const QStringList &wellKnown)
{
  QStringList usableKnown;
  for (const auto &candidate : wellKnown) {
    if (QFileInfo(candidate).isExecutable()) {
      usableKnown.append(candidate);
    }
  }
  const bool configuredOk = !configured.trimmed().isEmpty() && QFileInfo(configured.trimmed()).isExecutable();
  const auto choice = chooseExecutable(configured, configuredOk, QStandardPaths::findExecutable(name), usableKnown);
  return choice.path;
}

} // namespace

PhoneController::PhoneController(ServerConfig &config, QObject *parent) : QObject(parent), m_config(config)
{
  m_control = windowControlFor(
      sessionType(), !QStandardPaths::findExecutable(QStringLiteral("xdotool")).isEmpty(),
#if defined(Q_OS_UNIX)
      true
#else
      false
#endif
  );
  m_pollTimer = new QTimer(this);
  m_pollTimer->setInterval(2000);
  connect(m_pollTimer, &QTimer::timeout, this, &PhoneController::poll);
  m_leaveTimer = new QTimer(this);
  m_leaveTimer->setInterval(50);
  connect(m_leaveTimer, &QTimer::timeout, this, &PhoneController::watchLeave);
  const auto shortcut = Settings::value(Settings::Gui::PhoneClipboardShortcut).toString();
  auto *keys = new QShortcut(QKeySequence(shortcut), parent);
  keys->setContext(Qt::ApplicationShortcut);
  connect(keys, &QShortcut::activated, this, [this]() { openClipboardEditor(); });
  if (featureEnabled()) {
    m_pollTimer->start();
    m_leaveTimer->start();
    poll();
  }
}

PhoneController::~PhoneController()
{
  const auto ids = m_runtimes.keys();
  for (const auto &id : ids) {
    stopOwned(id);
  }
  qDeleteAll(m_edges);
  m_edges.clear();
}

bool PhoneController::featureEnabled() const
{
  return Settings::value(Settings::Gui::AndroidPhones).toBool();
}

SessionConfig PhoneController::sessionConfig() const
{
  SessionConfig config;
  config.prewarmHidden = m_control.hideWindow;
  config.hideKeepsProcess = m_control.hideWindow;
  return config;
}

QString PhoneController::adbProgram() const
{
  return resolvedProgram(Settings::value(Settings::Gui::AdbExecutable).toString(), QStringLiteral("adb"), {});
}

QString PhoneController::scrcpyProgram() const
{
  return resolvedProgram(
      Settings::value(Settings::Gui::ScrcpyExecutable).toString(), QStringLiteral("scrcpy"),
      {QStringLiteral("/opt/scrcpy-v5.0.1/scrcpy")}
  );
}

void PhoneController::bind(MonitorLayoutWidget *widget)
{
  if (widget == nullptr) {
    return;
  }
  for (const auto &panel : m_panels) {
    if (panel.widget == widget) {
      return;
    }
  }
  Panel panel;
  panel.widget = widget;
  m_panels.append(panel);
  connect(widget, &QObject::destroyed, this, [this, widget]() {
    m_panels.erase(
        std::remove_if(
            m_panels.begin(), m_panels.end(), [widget](const Panel &panel) { return panel.widget == widget; }
        ),
        m_panels.end()
    );
  });
  connect(widget, &MonitorLayoutWidget::layoutChanged, this, &PhoneController::refreshEdges);
  connect(widget, &MonitorLayoutWidget::phoneMoved, this, [this](const QString &id, int originX, int originY) {
    PhoneStore store(Settings::store());
    auto record = store.phone(id);
    if (!record.has_value()) {
      return;
    }
    record->originX = originX;
    record->originY = originY;
    record->placed = true;
    store.save(*record);
    refreshEdges();
  });
  connect(widget, &MonitorLayoutWidget::unregisterPhoneRequested, this, [this](const QString &id) {
    unregisterPhone(id);
  });
  ensurePanel(widget);
  refreshPanels();
  refreshEdges();
}

void PhoneController::openMonitorLayout(QWidget *parent)
{
  if (m_layoutDialog == nullptr) {
    m_layoutDialog = new MonitorLayoutDialog(m_config, parent);
    m_layoutDialog->setAttribute(Qt::WA_DeleteOnClose);
    bind(m_layoutDialog->layoutWidget());
    connect(m_layoutDialog, &QObject::destroyed, this, [this]() { m_layoutDialog = nullptr; });
  }
  m_layoutDialog->show();
  m_layoutDialog->raise();
  m_layoutDialog->activateWindow();
}

void PhoneController::ensurePanel(MonitorLayoutWidget *widget)
{
  auto *host = widget->phonePanel();
  if (host == nullptr || host->layout() == nullptr || host->layout()->count() > 0) {
    return;
  }
  auto *layout = qobject_cast<QVBoxLayout *>(host->layout());
  if (layout == nullptr) {
    return;
  }
  auto *title = new QLabel(tr("Android phones"), host);
  auto *enabled = new QCheckBox(tr("Use Android phone screens"), host);
  enabled->setChecked(featureEnabled());
  auto *limit = new QLabel(limitationText(), host);
  limit->setWordWrap(true);
  auto *adb = new QLineEdit(Settings::value(Settings::Gui::AdbExecutable).toString(), host);
  adb->setPlaceholderText(tr("adb path, or empty to search PATH"));
  auto *scrcpy = new QLineEdit(Settings::value(Settings::Gui::ScrcpyExecutable).toString(), host);
  scrcpy->setPlaceholderText(tr("scrcpy path, or empty to search PATH and /opt/scrcpy-v5.0.1"));
  auto *pair = new QPushButton(tr("Pair wireless debugging"), host);
  auto *paths = new QFormLayout();
  paths->addRow(tr("adb"), adb);
  paths->addRow(tr("scrcpy"), scrcpy);
  layout->addWidget(title);
  layout->addWidget(enabled);
  layout->addWidget(limit);
  layout->addLayout(paths);
  layout->addWidget(pair);
  auto *roster = new QVBoxLayout();
  roster->setObjectName(QStringLiteral("phone-roster"));
  layout->addLayout(roster);
  connect(enabled, &QCheckBox::toggled, this, [this](bool checked) {
    Settings::setValue(Settings::Gui::AndroidPhones, checked);
    if (checked) {
      m_pollTimer->start();
      m_leaveTimer->start();
      poll();
    } else {
      m_pollTimer->stop();
      m_leaveTimer->stop();
      const auto ids = m_runtimes.keys();
      for (const auto &id : ids) {
        stopOwned(id);
      }
      qDeleteAll(m_edges);
      m_edges.clear();
      for (const auto &panel : m_panels) {
        if (panel.widget != nullptr) {
          panel.widget->setPhoneLayouts({});
        }
      }
      noteStatus(tr("Android phone screens are off. Existing computers are unchanged."));
    }
    for (auto &panel : m_panels) {
      panel.signature.clear();
    }
    refreshPanels();
  });
  connect(adb, &QLineEdit::editingFinished, this, [adb]() {
    Settings::setValue(Settings::Gui::AdbExecutable, adb->text().trimmed());
  });
  connect(scrcpy, &QLineEdit::editingFinished, this, [scrcpy]() {
    Settings::setValue(Settings::Gui::ScrcpyExecutable, scrcpy->text().trimmed());
  });
  connect(pair, &QPushButton::clicked, this, [this, widget]() {
    PairingDialog dialog(widget);
    if (dialog.exec() != QDialog::Accepted) {
      return;
    }
    const auto planned = dialog.plan();
    if (!planned.ok) {
      return;
    }
    const auto program = adbProgram();
    if (program.isEmpty()) {
      noteStatus(tr("adb was not found."));
      return;
    }
    int code = -1;
    runTool(program, planned.pairArgs, 8000, &code);
    if (code != 0) {
      noteStatus(tr("Wireless pairing failed. Check the LAN address and the code. The code was not saved."));
      return;
    }
    runTool(program, planned.connectArgs, 8000, &code);
    noteStatus(
        code == 0 ? tr("Wireless debugging connected.") : tr("Paired, but connecting failed. Check the connect port.")
    );
    poll();
  });
}

QList<LayoutScreen> PhoneController::currentScreens() const
{
  QList<LayoutComputer> layout = m_config.monitorLayout();
  for (const auto &panel : m_panels) {
    if (panel.widget != nullptr) {
      layout = panel.widget->layout();
      break;
    }
  }
  QList<LayoutScreen> screens;
  const auto server = m_config.getServerName();
  for (const auto &computer : layout) {
    for (const auto &monitor : computer.monitors) {
      LayoutScreen screen;
      screen.computer = computer.name;
      screen.monitorName = monitor.name;
      screen.server = computer.name.compare(server, Qt::CaseInsensitive) == 0;
      screen.originX = computer.originX;
      screen.originY = computer.originY;
      screen.local = QRect(monitor.x, monitor.y, monitor.width, monitor.height);
      screens.append(screen);
    }
  }
  return screens;
}

QList<QRect> PhoneController::currentRects() const
{
  QList<QRect> rects;
  for (const auto &screen : currentScreens()) {
    rects.append(QRect(
        screen.originX + screen.local.x(), screen.originY + screen.local.y(), screen.local.width(),
        screen.local.height()
    ));
  }
  return rects;
}

PhoneController::Runtime &PhoneController::runtimeFor(const QString &id)
{
  return m_runtimes[id];
}

void PhoneController::noteStatus(const QString &text)
{
  m_status = text;
  for (const auto &panel : m_panels) {
    if (panel.widget != nullptr) {
      panel.widget->setPhoneStatus(text);
    }
  }
}

QString PhoneController::noticeText(const QString &code) const
{
  if (code == QLatin1String("prewarm-failed")) {
    return tr("The phone window could not be kept hidden, so it was closed. The desktop pointer was not captured.");
  }
  if (code == QLatin1String("cannot-focus-phone-window")) {
    return tr(
        "The text is on the phone clipboard. This desktop cannot focus the scrcpy window to paste it. Press Alt+V in "
        "the phone window."
    );
  }
  if (code == QLatin1String("not-plain-text")) {
    return tr("Only plain text can be sent to the phone.");
  }
  return code;
}

QString PhoneController::limitationText() const
{
  switch (m_control.limit) {
  case WindowLimit::Wayland:
    return tr(
        "Wayland cannot show, hide, raise, or warp scrcpy, and cannot send keys into it. "
        "The phone window is started when the pointer enters the edge and closed when it leaves. "
        "Screen off, keyboard layout, and paste need Alt+O, Alt+K, and Alt+V in that window."
    );
  case WindowLimit::NoXdotool:
    return tr(
        "xdotool is not installed, so Deskflow cannot hide, raise, or control the scrcpy window. "
        "Install xdotool on X11, or use the scrcpy shortcuts inside the phone window."
    );
  case WindowLimit::UnsupportedOs:
    return tr(
        "Hiding and raising the scrcpy window is not available on this system. "
        "The official scrcpy window is still started at the edge."
    );
  case WindowLimit::None:
    break;
  }
  return {};
}

void PhoneController::poll()
{
  if (!featureEnabled() || m_busy) {
    return;
  }
  const auto program = adbProgram();
  if (program.isEmpty()) {
    noteStatus(tr("adb was not found. Set its path in Monitor layout, or install adb on PATH."));
    return;
  }
  m_busy = true;
  auto *process = new QProcess(this);
  process->setProcessEnvironment(toolEnvironment());
  connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
    if (process->property("handled").toBool()) {
      return;
    }
    process->setProperty("handled", true);
    const auto text = QString::fromUtf8(process->readAllStandardOutput());
    process->deleteLater();
    m_busy = false;
    acceptDevices(text);
  });
  connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || process->property("handled").toBool()) {
      return;
    }
    process->setProperty("handled", true);
    process->deleteLater();
    m_busy = false;
    noteStatus(tr("adb could not be started."));
  });
  process->start(program, {QStringLiteral("devices"), QStringLiteral("-l")});
}

void PhoneController::rememberSerials()
{
  const auto program = adbProgram();
  for (const auto &device : m_devices) {
    if (!device.id.contains(QLatin1Char(':')) || m_serialByEndpoint.contains(device.id)) {
      continue;
    }
    const auto serial = runTool(
                            program,
                            {QStringLiteral("-s"), device.scrcpySerial.isEmpty() ? device.id : device.scrcpySerial,
                             QStringLiteral("shell"), QStringLiteral("getprop"), QStringLiteral("ro.serialno")},
                            2000, nullptr
    )
                            .trimmed();
    static const QRegularExpression hardwareSerial(QStringLiteral("^[A-Za-z0-9]{4,32}$"));
    if (hardwareSerial.match(serial).hasMatch()) {
      m_serialByEndpoint.insert(device.id, serial);
    }
  }
}

void PhoneController::acceptDevices(const QString &text)
{
  m_devices = resolveDevices(text, m_serialByEndpoint);
  rememberSerials();
  if (!m_serialByEndpoint.isEmpty()) {
    m_devices = resolveDevices(text, m_serialByEndpoint);
  }
  PhoneStore identityStore(Settings::store());
  for (auto it = m_serialByEndpoint.cbegin(); it != m_serialByEndpoint.cend(); ++it) {
    auto moved = identityStore.phone(it.key());
    if (!moved.has_value() || it.key() == it.value()) {
      continue;
    }
    if (identityStore.phone(it.value()).has_value()) {
      identityStore.remove(it.key());
    } else {
      moved->id = it.value();
      identityStore.remove(it.key());
      identityStore.save(*moved);
    }
    if (m_runtimes.contains(it.key())) {
      m_runtimes.insert(it.value(), m_runtimes.take(it.key()));
    }
  }
  ++m_pollGeneration;
  if (m_pollGeneration % 5 == 0) {
    PhoneStore store(Settings::store());
    const auto program = adbProgram();
    for (auto &record : store.phones()) {
      const ResolvedDevice *device = nullptr;
      for (const auto &candidate : m_devices) {
        if (candidate.id == record.id && candidate.online) {
          device = &candidate;
          break;
        }
      }
      if (device == nullptr || device->scrcpySerial.isEmpty()) {
        continue;
      }
      const auto battery = parseBatteryPercent(runTool(
          program,
          {QStringLiteral("-s"), device->scrcpySerial, QStringLiteral("shell"), QStringLiteral("dumpsys"),
           QStringLiteral("battery")},
          2000, nullptr
      ));
      runtimeFor(record.id).battery = battery;
      const auto size = parseWmSize(runTool(
          program,
          {QStringLiteral("-s"), device->scrcpySerial, QStringLiteral("shell"), QStringLiteral("wm"),
           QStringLiteral("size")},
          2000, nullptr
      ));
      const auto updated = applySizeInPlace(record.originX, record.originY, record.width, record.height, size);
      bool changed = updated.changed;
      record.width = updated.width;
      record.height = updated.height;
      const auto displays = parseListDisplays(runTool(
          scrcpyProgram(), {QStringLiteral("-s"), device->scrcpySerial, QStringLiteral("--list-displays")}, 4000,
          nullptr
      ));
      const auto logical = selectLogicalDisplay(displays);
      if (logical.id >= 0 && logical.id != record.displayId) {
        record.displayId = logical.id;
        changed = true;
      }
      if (logical.width > 0 && logical.height > 0 &&
          (logical.width != record.width || logical.height != record.height)) {
        const auto fromDisplay = applySizeInPlace(
            record.originX, record.originY, record.width, record.height, QSize(logical.width, logical.height)
        );
        record.width = fromDisplay.width;
        record.height = fromDisplay.height;
        changed = changed || fromDisplay.changed;
      }
      if (changed) {
        store.save(record);
        auto &runtime = runtimeFor(record.id);
        if (runtime.state.phase == SessionPhase::Visible && runtime.monitor.isValid()) {
          runtime.dock =
              dockOnMonitor(runtime.monitor, runtime.side, record.width, record.height, runtime.fraction, 148);
          present(record.id);
        }
      }
      if (auto *bezel = runtimeFor(record.id).bezel) {
        bezel->setBattery(battery);
      }
    }
  }
  syncRuntimes();
  refreshPanels();
  refreshEdges();
}

void PhoneController::syncRuntimes()
{
  PhoneStore store(Settings::store());
  const auto config = sessionConfig();
  for (const auto &record : store.phones()) {
    const ResolvedDevice *device = nullptr;
    for (const auto &candidate : m_devices) {
      if (candidate.id == record.id) {
        device = &candidate;
        break;
      }
    }
    const bool online = device != nullptr && device->online;
    const auto serial = device == nullptr ? QString() : device->scrcpySerial;
    auto &runtime = runtimeFor(record.id);
    runtime.state.pinned = record.pinned;
    if (online && !serial.isEmpty() && !runtime.scrcpySerial.isEmpty() && serial != runtime.scrcpySerial &&
        runtime.spawnedByUs) {
      const auto dock = runtime.dock;
      const auto monitor = runtime.monitor;
      const auto fraction = runtime.fraction;
      const auto side = runtime.side;
      stopOwned(record.id);
      auto &restarted = runtimeFor(record.id);
      restarted.state.phase = SessionPhase::Idle;
      restarted.wasOnline = false;
      restarted.dock = dock;
      restarted.monitor = monitor;
      restarted.fraction = fraction;
      restarted.side = side;
    }
    runtime.scrcpySerial = serial;
    if (online && !runtime.wasOnline) {
      applyStep(record.id, reduceSession(runtime.state, config, SessionEvent::DeviceOnline), false, nullptr);
    } else if (!online && runtime.wasOnline) {
      applyStep(record.id, reduceSession(runtime.state, config, SessionEvent::DeviceOffline), false, nullptr);
    }
    runtimeFor(record.id).wasOnline = online;
  }
}

void PhoneController::applyStep(const QString &id, const SessionStep &step, bool show, const DockGeometry *dock)
{
  auto &runtime = runtimeFor(id);
  runtime.state = step.state;
  if (!step.notice.isEmpty()) {
    noteStatus(noticeText(step.notice));
  }
  switch (step.action) {
  case SessionAction::SpawnHidden:
    spawn(id, false, nullptr);
    break;
  case SessionAction::SpawnVisible:
    spawn(id, true, dock);
    break;
  case SessionAction::Show:
    if (dock != nullptr) {
      runtime.dock = *dock;
    }
    present(id);
    break;
  case SessionAction::Hide:
    conceal(id);
    break;
  case SessionAction::Stop:
    stopOwned(id);
    break;
  case SessionAction::Notify:
  case SessionAction::None:
    if (show) {
      present(id);
    }
    break;
  }
}

qint64 PhoneController::findRunningScrcpy(const QString &serial) const
{
  for (const auto &running : parseScrcpyProcessList(readProcessTable())) {
    if (scrcpyCommandOwnsSerial(running.args, serial)) {
      return running.pid;
    }
  }
  return 0;
}

void PhoneController::spawn(const QString &id, bool show, const DockGeometry *dock)
{
  auto &runtime = runtimeFor(id);
  if (runtime.process != nullptr && runtime.process->state() != QProcess::NotRunning) {
    if (show) {
      present(id);
    }
    return;
  }
  const auto serial = runtime.scrcpySerial;
  if (const auto existing = findRunningScrcpy(serial); existing > 0) {
    runtime.adoptedPid = existing;
    runtime.spawnedByUs = false;
    runtime.state = reduceSession(runtime.state, sessionConfig(), SessionEvent::Adopted).state;
    if (show) {
      present(id);
    }
    noteStatus(tr("Using the scrcpy window that is already open for this phone."));
    return;
  }
  const auto program = scrcpyProgram();
  if (program.isEmpty()) {
    runtime.state.phase = SessionPhase::Failed;
    noteStatus(tr("scrcpy was not found. Set its path in Monitor layout. The desktop pointer was not captured."));
    return;
  }
  PhoneStore store(Settings::store());
  const auto record = store.phone(id);
  ScrcpyRequest request;
  request.serial = serial;
  request.phoneId = id;
  request.displayId = record.has_value() ? record->displayId : -1;
  request.audio = !(record.has_value() && record->audioMuted);
  request.placeWindow = true;
  if (show && dock != nullptr) {
    request.x = dock->video.x();
    request.y = dock->video.y();
    request.width = dock->video.width();
    request.height = dock->video.height();
    runtime.dock = *dock;
  } else {
    request.x = -32000;
    request.y = -32000;
    request.width = 200;
    request.height = 400;
  }
  auto *process = new QProcess(this);
  process->setProcessEnvironment(toolEnvironment());
  process->setProgram(program);
  process->setArguments(scrcpyArguments(request));
  runtime.process = process;
  runtime.spawnedByUs = true;
  runtime.adoptedPid = 0;
  connect(process, &QProcess::finished, this, [this, id, process](int, QProcess::ExitStatus) {
    auto &runtime = runtimeFor(id);
    if (runtime.process == process) {
      runtime.process = nullptr;
      runtime.spawnedByUs = false;
      if (runtime.state.phase != SessionPhase::Idle && runtime.state.phase != SessionPhase::Failed) {
        runtime.state.phase = SessionPhase::Idle;
        conceal(id);
        noteStatus(tr("scrcpy closed."));
      }
    }
    process->deleteLater();
  });
  process->start();
  if (show) {
    present(id);
  } else {
    QTimer::singleShot(700, this, [this, id]() {
      auto &runtime = runtimeFor(id);
      if (runtime.state.phase != SessionPhase::Prewarming && runtime.state.phase != SessionPhase::HiddenReady) {
        return;
      }
      const bool hidden = moveForeignWindow(scrcpyWindowTitle(id), QRect(-32000, -32000, 200, 400), false);
      if (!hidden) {
        applyStep(id, reduceSession(runtime.state, sessionConfig(), SessionEvent::PrewarmFailed), false, nullptr);
        stopOwned(id);
        return;
      }
      runtime.state = reduceSession(runtime.state, sessionConfig(), SessionEvent::PrewarmOk).state;
    });
  }
}

bool PhoneController::moveForeignWindow(const QString &title, const QRect &geometry, bool show)
{
  if (!m_control.findWindow) {
    return false;
  }
  const auto listed =
      runTool(QStringLiteral("xdotool"), {QStringLiteral("search"), QStringLiteral("--name"), title}, 1500, nullptr);
  const auto id = listed.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts).value(0);
  if (id.isEmpty()) {
    return false;
  }
  if (!show) {
    int code = -1;
    runTool(QStringLiteral("xdotool"), {QStringLiteral("windowunmap"), id}, 1500, &code);
    return code == 0;
  }
  runTool(
      QStringLiteral("xdotool"),
      {QStringLiteral("windowmap"), id, QStringLiteral("windowmove"), id, QString::number(geometry.x()),
       QString::number(geometry.y()), QStringLiteral("windowsize"), id, QString::number(geometry.width()),
       QString::number(geometry.height()), QStringLiteral("windowactivate"), id},
      2000, nullptr
  );
  return true;
}

bool PhoneController::injectKeys(const QString &title, const QString &combo)
{
  if (!m_control.injectKeys) {
    return false;
  }
  int code = -1;
  runTool(
      QStringLiteral("xdotool"),
      {QStringLiteral("search"), QStringLiteral("--name"), title, QStringLiteral("windowactivate"),
       QStringLiteral("--sync"), QStringLiteral("key"), QStringLiteral("--clearmodifiers"), combo},
      2000, &code
  );
  return code == 0;
}

void PhoneController::positionForeign(const QString &id)
{
  const auto &runtime = runtimeFor(id);
  moveForeignWindow(scrcpyWindowTitle(id), runtime.dock.video, true);
  if (m_control.warpPointer) {
    runTool(
        QStringLiteral("xdotool"),
        {QStringLiteral("mousemove"), QString::number(runtime.dock.pointer.x()),
         QString::number(runtime.dock.pointer.y())},
        1000, nullptr
    );
  }
}

void PhoneController::present(const QString &id)
{
  auto &runtime = runtimeFor(id);
  if (!runtime.dock.video.isValid()) {
    return;
  }
  if (runtime.bezel == nullptr) {
    runtime.bezel = new PhoneBezel(id);
    runtime.sensor = new PhoneExitSensor();
    connect(runtime.bezel, &PhoneBezel::pinToggled, this, [this](const QString &phoneId, bool pinned) {
      PhoneStore store(Settings::store());
      auto record = store.phone(phoneId);
      if (record.has_value()) {
        record->pinned = pinned;
        store.save(*record);
      }
      runtimeFor(phoneId).state.pinned = pinned;
    });
    connect(runtime.bezel, &PhoneBezel::keyRequested, this, [this](const QString &phoneId, PhoneKey key) {
      sendKey(phoneId, key);
    });
    connect(runtime.bezel, &PhoneBezel::clipboardRequested, this, [this](const QString &phoneId) {
      openClipboardEditor(phoneId);
    });
    connect(runtime.bezel, &PhoneBezel::pasteRequested, this, [this](const QString &phoneId) {
      const auto text = QGuiApplication::clipboard()->text();
      executeClipboard(phoneId, ClipboardIntent::SendAndPaste, text);
    });
    connect(runtime.bezel, &PhoneBezel::hideRequested, this, [this](const QString &phoneId) {
      applyStep(
          phoneId, reduceSession(runtimeFor(phoneId).state, sessionConfig(), SessionEvent::HideRequested), false,
          nullptr
      );
    });
    connect(runtime.bezel, &PhoneBezel::muteToggled, this, [this](const QString &phoneId, bool muted) {
      toggleMute(phoneId, muted);
    });
    connect(runtime.bezel, &PhoneBezel::appsRequested, this, [this](const QString &phoneId) {
      const auto serial = runtimeFor(phoneId).scrcpySerial;
      const auto listed = runTool(
          adbProgram(),
          {QStringLiteral("-s"), serial, QStringLiteral("shell"), QStringLiteral("pm"), QStringLiteral("list"),
           QStringLiteral("packages"), QStringLiteral("-3")},
          4000, nullptr
      );
      PhoneStore store(Settings::store());
      const auto record = store.phone(phoneId);
      PinnedAppsDialog dialog(
          parseLauncherPackages(listed), record.has_value() ? record->apps : QList<PhoneApp>{},
          runtimeFor(phoneId).bezel
      );
      if (dialog.exec() != QDialog::Accepted) {
        return;
      }
      store.setApps(phoneId, dialog.selected());
      if (auto *bezel = runtimeFor(phoneId).bezel) {
        bezel->setApps(dialog.selected());
      }
    });
    connect(
        runtime.bezel, &PhoneBezel::appLaunchRequested, this,
        [this](const QString &phoneId, const QString &packageName) { launchApp(phoneId, packageName); }
    );
    connect(runtime.bezel, &PhoneBezel::filesDropped, this, [this](const QString &phoneId, const QStringList &paths) {
      const auto serial = runtimeFor(phoneId).scrcpySerial;
      for (const auto &path : paths) {
        const auto command = pushFileCommand(serial, path);
        if (!command.args.isEmpty()) {
          runTool(adbProgram(), command.args, 20000, nullptr);
        }
      }
      noteStatus(tr("Files were copied to Download on the phone. Dropping on the picture uses scrcpy itself."));
    });
  }
  PhoneStore store(Settings::store());
  const auto record = store.phone(id);
  runtime.bezel->setPinned(runtime.state.pinned);
  runtime.bezel->setMuted(record.has_value() && record->audioMuted);
  runtime.bezel->setBattery(runtime.battery);
  if (record.has_value()) {
    runtime.bezel->setApps(record->apps);
  }
  runtime.bezel->setGeometry(runtime.dock.bezel);
  runtime.sensor->setGeometry(sensorRect(runtime.dock));
  runtime.bezel->show();
  runtime.sensor->show();
  positionForeign(id);
  showKeyboardHint(id);
}

void PhoneController::conceal(const QString &id)
{
  auto &runtime = runtimeFor(id);
  if (runtime.bezel != nullptr) {
    runtime.bezel->hide();
  }
  if (runtime.sensor != nullptr) {
    runtime.sensor->hide();
  }
  if (m_control.hideWindow) {
    moveForeignWindow(scrcpyWindowTitle(id), runtime.dock.video, false);
  }
}

void PhoneController::stopOwned(const QString &id)
{
  auto &runtime = runtimeFor(id);
  conceal(id);
  if (runtime.spawnedByUs && runtime.process != nullptr) {
    runtime.process->kill();
    runtime.spawnedByUs = false;
  }
  delete runtime.bezel;
  delete runtime.sensor;
  runtime.bezel = nullptr;
  runtime.sensor = nullptr;
  runtime.state.phase = SessionPhase::Idle;
  runtime.leave = {};
  runtime.bezelHot = false;
}

void PhoneController::enterPhone(const QString &id, double fraction, const QRect &monitor, ScreenEdge side)
{
  PhoneStore store(Settings::store());
  const auto record = store.phone(id);
  if (!record.has_value() || !record->placed) {
    return;
  }
  auto &runtime = runtimeFor(id);
  if (!runtime.wasOnline || runtime.scrcpySerial.isEmpty()) {
    noteStatus(tr("That phone is offline, so the edge does nothing. It is not marked Disabled."));
    return;
  }
  runtime.fraction = fraction;
  runtime.side = side;
  runtime.monitor = monitor;
  auto dock = dockOnMonitor(monitor, side, record->width, record->height, fraction, 148);
  dock = alignPointer(dock, QCursor::pos(), monitor);
  runtime.dock = dock;
  const auto step = reduceSession(runtime.state, sessionConfig(), SessionEvent::EdgeEntered);
  applyStep(id, step, true, &dock);
}

void PhoneController::watchLeave()
{
  if (!featureEnabled()) {
    return;
  }
  const auto ids = m_runtimes.keys();
  for (const auto &id : ids) {
    auto &runtime = m_runtimes[id];
    if (runtime.state.phase != SessionPhase::Visible || runtime.bezel == nullptr) {
      continue;
    }
    bool inside = false;
    if (m_control.warpPointer) {
      QRect edge;
      for (const auto *bar : m_edges) {
        if (bar->phoneId() == id) {
          edge = edge.united(bar->geometry());
        }
      }
      inside =
          combinedContains(runtime.dock.video, runtime.dock.bezel, edge, QCursor::pos(), runtime.bezel->dragActive());
    } else {
      WaylandPointer pointer;
      pointer.onBezel = runtime.bezel->underMouse();
      pointer.onSensor = runtime.sensor != nullptr && runtime.sensor->underMouse();
      pointer.drag = runtime.bezel->dragActive();
      pointer.bezelHot = runtime.bezelHot;
      for (const auto *bar : m_edges) {
        if (bar->phoneId() == id && bar->underMouse()) {
          pointer.onEdge = true;
        }
      }
      const auto judged = waylandPointerInside(pointer);
      inside = judged.inside;
      runtime.bezelHot = judged.bezelHot;
    }
    LeaveSample sample;
    sample.pinned = runtime.state.pinned;
    sample.insideCombined = inside;
    sample.hysteresisMs = 280;
    const auto leave = advanceLeave(runtime.leave, sample, m_leaveTimer->interval());
    runtime.leave = leave.tracker;
    if (leave.effect == LeaveEffect::Hide) {
      applyStep(id, reduceSession(runtime.state, sessionConfig(), SessionEvent::HideRequested), false, nullptr);
    }
  }
}

void PhoneController::sendKey(const QString &id, PhoneKey key)
{
  const auto serial = runtimeFor(id).scrcpySerial;
  const auto plan = planKey(key, m_control.injectKeys, serial);
  if (plan.inject) {
    if (!injectKeys(scrcpyWindowTitle(id), plan.xdotoolKey)) {
      noteStatus(tr("Could not send the shortcut to scrcpy."));
    }
    return;
  }
  if (plan.unsupportedWithoutInject) {
    noteStatus(tr("Press the matching scrcpy shortcut in the phone window. This desktop cannot send it."));
    return;
  }
  if (!plan.adb.args.isEmpty()) {
    runTool(adbProgram(), plan.adb.args, 2000, nullptr);
  }
}

void PhoneController::executeClipboard(const QString &id, ClipboardIntent intent, const QString &text)
{
  const bool canPaste = m_control.injectKeys && m_control.activateWindow;
  const auto plan = planClipboard(intent, text, canPaste);
  const auto serial = runtimeFor(id).scrcpySerial;
  for (const auto &step : plan.steps) {
    switch (step.op) {
    case ClipOp::SetComputerClipboard:
      QGuiApplication::clipboard()->setText(step.text);
      break;
    case ClipOp::SetDeviceClipboard:
      runTool(adbProgram(), writeClipboardCommand(serial, step.text).args, 3000, nullptr);
      break;
    case ClipOp::FocusWindow:
      moveForeignWindow(scrcpyWindowTitle(id), runtimeFor(id).dock.video, true);
      break;
    case ClipOp::InjectPaste:
      injectKeys(scrcpyWindowTitle(id), QStringLiteral("alt+v"));
      break;
    case ClipOp::Error:
      noteStatus(noticeText(step.detail));
      break;
    }
  }
}

void PhoneController::openClipboardEditor(const QString &phoneId)
{
  QString id = phoneId;
  if (id.isEmpty()) {
    PhoneStore store(Settings::store());
    const auto records = store.phones();
    for (auto it = m_runtimes.cbegin(); it != m_runtimes.cend(); ++it) {
      if (it->state.phase == SessionPhase::Visible) {
        id = it.key();
        break;
      }
    }
    if (id.isEmpty() && !records.isEmpty()) {
      id = records.first().id;
    }
  }
  if (id.isEmpty()) {
    noteStatus(tr("Register a phone before opening the clipboard editor."));
    return;
  }
  m_editorPhone = id;
  if (m_editor == nullptr) {
    m_editor = new ClipboardEditor(qobject_cast<QWidget *>(parent()));
    m_editor->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_editor, &QObject::destroyed, this, [this]() { m_editor = nullptr; });
    connect(m_editor, &ClipboardEditor::loadRequested, this, [this]() {
      if (m_editor == nullptr) {
        return;
      }
      int code = -1;
      const auto text =
          runTool(adbProgram(), readClipboardCommand(runtimeFor(m_editorPhone).scrcpySerial).args, 3000, &code);
      if (code != 0) {
        noteStatus(tr(
            "The phone clipboard could not be read. scrcpy has no read command; Android cmd clipboard get-text failed."
        ));
        return;
      }
      m_editor->setPhoneText(text.trimmed());
    });
    connect(m_editor, &ClipboardEditor::sendRequested, this, [this](const QString &text) {
      executeClipboard(m_editorPhone, ClipboardIntent::Send, text);
    });
    connect(m_editor, &ClipboardEditor::sendPasteRequested, this, [this](const QString &text) {
      if (m_editor != nullptr) {
        m_editor->hide();
      }
      executeClipboard(m_editorPhone, ClipboardIntent::SendAndPaste, text);
    });
  }
  m_editor->show();
  m_editor->raise();
  m_editor->activateWindow();
}

void PhoneController::launchApp(const QString &id, const QString &packageName)
{
  auto &runtime = runtimeFor(id);
  const bool hidden = runtime.state.phase != SessionPhase::Visible;
  if (hidden && runtime.monitor.isValid()) {
    enterPhone(id, runtime.fraction, runtime.monitor, runtime.side);
  }
  const auto plan = planAppLaunch(runtime.scrcpySerial, packageName, hidden);
  if (plan.command.args.isEmpty()) {
    noteStatus(tr("That app cannot be launched."));
    return;
  }
  runTool(adbProgram(), plan.command.args, 4000, nullptr);
}

void PhoneController::toggleMute(const QString &id, bool muted)
{
  PhoneStore store(Settings::store());
  auto record = store.phone(id);
  if (record.has_value()) {
    record->audioMuted = muted;
    store.save(*record);
  }
  int code = -1;
  const auto listed =
      runTool(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("sink-inputs")}, 2000, &code);
  if (code != 0) {
    noteStatus(
        tr("scrcpy has no mute shortcut. Mute applies with --no-audio the next time its window opens, "
           "or now if PulseAudio pactl can see the stream.")
    );
    return;
  }
  for (const auto &input : parsePactlSinkInputs(listed)) {
    if (input.application.contains(QLatin1String("scrcpy"), Qt::CaseInsensitive)) {
      runTool(
          QStringLiteral("pactl"),
          {QStringLiteral("set-sink-input-mute"), QString::number(input.id),
           muted ? QStringLiteral("1") : QStringLiteral("0")},
          2000, nullptr
      );
    }
  }
}

void PhoneController::registerPhone(const QString &id)
{
  const ResolvedDevice *device = nullptr;
  for (const auto &candidate : m_devices) {
    if (candidate.id == id) {
      device = &candidate;
      break;
    }
  }
  PhoneStore store(Settings::store());
  auto record = store.phone(id).value_or(PhoneRecord{});
  record.id = id;
  if (device != nullptr) {
    if (record.label.isEmpty() || record.label == record.id) {
      record.label = device->label;
    }
    record.model = device->model;
  }
  if (record.width < 1) {
    record.width = 1080;
  }
  if (record.height < 1) {
    record.height = 2640;
  }
  if (!record.placed) {
    const auto origin = placeBelowExisting(currentRects(), record.width, record.height);
    record.originX = origin.x;
    record.originY = origin.y;
    record.placed = true;
  }
  store.save(record);
  noteStatus(tr(
      "The phone was placed below the current screens. Drag it onto a free edge. It does not move the other computers."
  ));
  refreshPanels();
  refreshEdges();
  syncRuntimes();
}

void PhoneController::unregisterPhone(const QString &id)
{
  stopOwned(id);
  m_runtimes.remove(id);
  PhoneStore store(Settings::store());
  store.remove(id);
  refreshPanels();
  refreshEdges();
}

void PhoneController::showKeyboardHint(const QString &id)
{
  PhoneStore store(Settings::store());
  auto record = store.phone(id);
  if (!record.has_value() || record->keyboardHintSeen) {
    return;
  }
  record->keyboardHintSeen = true;
  store.save(*record);
  auto *box = new QMessageBox(runtimeFor(id).bezel);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setWindowTitle(tr("Phone keyboard"));
  box->setIcon(QMessageBox::Information);
  box->setText(tr("For Chinese, use the phone input method and switch the computer input method to English."));
  box->setInformativeText(
      tr("The phone window is started with --keyboard=uhid. Physical keyboard layout is scrcpy Alt+K.")
  );
  box->setModal(false);
  box->show();
}

void PhoneController::refreshPanels()
{
  PhoneStore store(Settings::store());
  const auto records = store.phones();
  QStringList signatureParts;
  signatureParts.append(featureEnabled() ? QStringLiteral("on") : QStringLiteral("off"));
  for (const auto &device : m_devices) {
    signatureParts.append(
        device.id + QLatin1Char('|') + device.stateDetail + QLatin1Char('|') +
        (device.online ? QStringLiteral("1") : QStringLiteral("0"))
    );
  }
  for (const auto &record : records) {
    signatureParts.append(QStringLiteral("saved:") + record.id);
  }
  const auto signature = signatureParts.join(QLatin1Char(';'));
  QList<PhoneLayoutItem> placed;
  for (const auto &record : records) {
    if (!record.placed || !featureEnabled()) {
      continue;
    }
    bool online = false;
    for (const auto &device : m_devices) {
      if (device.id == record.id) {
        online = device.online;
      }
    }
    PhoneLayoutItem item;
    item.id = record.id;
    item.label = record.label.isEmpty() ? record.id : record.label;
    item.placed = true;
    item.online = online;
    item.originX = record.originX;
    item.originY = record.originY;
    item.width = record.width;
    item.height = record.height;
    placed.append(item);
  }
  for (auto &panel : m_panels) {
    if (panel.widget == nullptr) {
      continue;
    }
    panel.widget->setPhoneLayouts(featureEnabled() ? placed : QList<PhoneLayoutItem>{});
    if (panel.signature == signature) {
      continue;
    }
    panel.signature = signature;
    ensurePanel(panel.widget);
    auto *roster = panel.widget->phonePanel()->findChild<QVBoxLayout *>(QStringLiteral("phone-roster"));
    if (roster == nullptr) {
      continue;
    }
    while (QLayoutItem *item = roster->takeAt(0)) {
      if (auto *widget = item->widget()) {
        widget->deleteLater();
      }
      delete item;
    }
    if (!featureEnabled()) {
      continue;
    }
    QList<PhoneRosterEntry> rows;
    for (const auto &device : m_devices) {
      PhoneRosterEntry row;
      row.id = device.id;
      row.label = device.label;
      row.online = device.online;
      bool registered = false;
      for (const auto &record : records) {
        if (record.id == device.id) {
          registered = true;
        }
      }
      row.registered = registered;
      if (!device.online) {
        row.detail = device.stateDetail == QLatin1String("unauthorized") ? tr("Unauthorized") : tr("Offline");
      } else if (device.usb) {
        row.detail = tr("USB");
      } else {
        row.detail = tr("Wireless");
      }
      rows.append(row);
    }
    for (const auto &record : records) {
      bool seen = false;
      for (const auto &row : rows) {
        if (row.id == record.id) {
          seen = true;
        }
      }
      if (seen) {
        continue;
      }
      PhoneRosterEntry row;
      row.id = record.id;
      row.label = record.label.isEmpty() ? record.id : record.label;
      row.registered = true;
      row.online = false;
      row.detail = tr("Offline");
      rows.append(row);
    }
    if (rows.isEmpty()) {
      auto *empty = new QLabel(
          tr("No Android phones yet. Plug one in with USB debugging, or pair wireless debugging."),
          panel.widget->phonePanel()
      );
      empty->setWordWrap(true);
      roster->addWidget(empty);
    }
    for (const auto &row : rows) {
      auto *line = new QWidget(panel.widget->phonePanel());
      auto *lineLayout = new QHBoxLayout(line);
      lineLayout->setContentsMargins(0, 0, 0, 0);
      lineLayout->addWidget(new QLabel(QStringLiteral("%1 — %2").arg(row.label, row.detail), line));
      if (!row.registered) {
        auto *button = new QPushButton(tr("Register"), line);
        const auto id = row.id;
        connect(button, &QPushButton::clicked, this, [this, id]() { registerPhone(id); });
        lineLayout->addWidget(button);
      } else {
        auto *button = new QPushButton(tr("Unregister"), line);
        const auto id = row.id;
        connect(button, &QPushButton::clicked, this, [this, id]() { unregisterPhone(id); });
        lineLayout->addWidget(button);
      }
      roster->addWidget(line);
    }
  }
}

void PhoneController::refreshEdges()
{
  qDeleteAll(m_edges);
  m_edges.clear();
  if (!featureEnabled()) {
    return;
  }
  PhoneStore store(Settings::store());
  QList<PhoneLayoutItem> phones;
  bool placedOffline = false;
  bool placedWithoutEdge = false;
  for (const auto &record : store.phones()) {
    if (!record.placed) {
      continue;
    }
    bool online = false;
    for (const auto &device : m_devices) {
      if (device.id == record.id && device.online) {
        online = true;
      }
    }
    if (!online) {
      placedOffline = true;
      continue;
    }
    PhoneLayoutItem item;
    item.id = record.id;
    item.label = record.label;
    item.placed = true;
    item.online = true;
    item.originX = record.originX;
    item.originY = record.originY;
    item.width = record.width;
    item.height = record.height;
    phones.append(item);
  }
  const auto edges = hotEdges(currentScreens(), phones);
  QSet<QString> armed;
  for (const auto &edge : edges) {
    armed.insert(edge.phoneId);
    auto *bar = new HotEdgeBar(edge.phoneId);
    bar->place(edgeBarRect(edge));
    bar->show();
    m_edges.append(bar);
    connect(bar, &HotEdgeBar::entered, this, [this, edge](const QString &, double fraction) {
      QPoint local(edge.edgePosition, edge.alongBegin);
      const int span = std::max(1, edge.alongEnd - edge.alongBegin);
      const int along = edge.alongBegin + static_cast<int>(std::lround(fraction * (span - 1)));
      if (edge.side == ScreenEdge::Left || edge.side == ScreenEdge::Right) {
        local = QPoint(edge.edgePosition, along);
      } else {
        local = QPoint(along, edge.edgePosition);
      }
      const QRect monitor(edge.monitorX, edge.monitorY, edge.monitorWidth, edge.monitorHeight);
      enterPhone(edge.phoneId, alongFraction(edge, local), monitor, edge.side);
    });
  }
  for (const auto &phone : phones) {
    if (!armed.contains(phone.id)) {
      placedWithoutEdge = true;
    }
  }
  if (m_status.isEmpty()) {
    if (placedWithoutEdge) {
      noteStatus(
          tr("Drag the phone onto a free edge of this computer. Edges already used by another computer stay theirs.")
      );
    } else if (placedOffline) {
      noteStatus(tr("A phone is offline. Reconnect USB or wireless debugging. It is not marked Disabled."));
    }
  }
}

} // namespace deskflow::phone
