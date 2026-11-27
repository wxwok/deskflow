/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/phone/PhoneTypes.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>

class MonitorLayoutDialog;
class MonitorLayoutWidget;
class QProcess;
class QTimer;
class QWidget;
class ServerConfig;

namespace deskflow::phone {

class ClipboardEditor;
class HotEdgeBar;
class PhoneBezel;
class PhoneExitSensor;

//! Discovers phones, registers them on the monitor layout, and docks official scrcpy.
class PhoneController : public QObject
{
  Q_OBJECT

public:
  PhoneController(ServerConfig &config, QObject *parent = nullptr);
  ~PhoneController() override;

  void bind(MonitorLayoutWidget *widget);
  void openMonitorLayout(QWidget *parent);
  void openClipboardEditor(const QString &phoneId = {});

private:
  struct Runtime
  {
    SessionState state;
    QString scrcpySerial;
    bool wasOnline = false;
    bool spawnedByUs = false;
    qint64 adoptedPid = 0;
    QProcess *process = nullptr;
    PhoneBezel *bezel = nullptr;
    PhoneExitSensor *sensor = nullptr;
    DockGeometry dock;
    QRect monitor;
    ScreenEdge side = ScreenEdge::Left;
    double fraction = 0.5;
    LeaveTracker leave;
    bool bezelHot = false;
    int battery = -1;
  };

  struct Panel
  {
    QPointer<MonitorLayoutWidget> widget;
    QString signature;
  };

  bool featureEnabled() const;
  SessionConfig sessionConfig() const;
  QString adbProgram() const;
  QString scrcpyProgram() const;
  QList<LayoutScreen> currentScreens() const;
  QList<QRect> currentRects() const;
  Runtime &runtimeFor(const QString &id);
  void ensurePanel(MonitorLayoutWidget *widget);
  void refreshPanels();
  void refreshEdges();
  void noteStatus(const QString &text);
  void poll();
  void acceptDevices(const QString &text);
  void rememberSerials();
  void syncRuntimes();
  void applyStep(const QString &id, const SessionStep &step, bool show, const DockGeometry *dock);
  void spawn(const QString &id, bool show, const DockGeometry *dock);
  void present(const QString &id);
  void conceal(const QString &id);
  void stopOwned(const QString &id);
  void enterPhone(const QString &id, double fraction, const QRect &monitor, ScreenEdge side);
  void watchLeave();
  void sendKey(const QString &id, PhoneKey key);
  void executeClipboard(const QString &id, ClipboardIntent intent, const QString &text);
  void launchApp(const QString &id, const QString &packageName);
  void toggleMute(const QString &id, bool muted);
  void registerPhone(const QString &id);
  void unregisterPhone(const QString &id);
  void showKeyboardHint(const QString &id);
  QString noticeText(const QString &code) const;
  QString limitationText() const;
  qint64 findRunningScrcpy(const QString &serial) const;
  bool moveForeignWindow(const QString &title, const QRect &geometry, bool show);
  bool injectKeys(const QString &title, const QString &combo);
  void positionForeign(const QString &id);

  ServerConfig &m_config;
  WindowControl m_control;
  QList<ResolvedDevice> m_devices;
  QHash<QString, QString> m_serialByEndpoint;
  QHash<QString, Runtime> m_runtimes;
  QList<Panel> m_panels;
  QList<HotEdgeBar *> m_edges;
  QTimer *m_pollTimer = nullptr;
  QTimer *m_leaveTimer = nullptr;
  QPointer<MonitorLayoutDialog> m_layoutDialog;
  QPointer<ClipboardEditor> m_editor;
  QString m_editorPhone;
  bool m_busy = false;
  int m_pollGeneration = 0;
  QString m_status;
};

} // namespace deskflow::phone
