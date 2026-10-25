/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LayoutClientsTests.h"

#include "common/Settings.h"
#include "gui/config/LayoutClients.h"
#include "gui/config/ServerConfig.h"
#include "gui/dialogs/ServerConfigDialog.h"
#include "gui/widgets/MonitorLayoutWidget.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
#include <QTranslator>

namespace {

void showFixedLayout(ServerConfigDialog &dialog)
{
  dialog.show();
  dialog.setWindowState(Qt::WindowNoState);
  dialog.resize(1200, 900);
  QApplication::processEvents();
}

QList<LayoutComputer> twoComputerLayout()
{
  LayoutComputer work;
  work.name = QStringLiteral("work");
  work.monitors.append({QStringLiteral("work"), 0, 0, 1920, 1080});
  LayoutComputer sp7;
  sp7.name = QStringLiteral("sp7");
  sp7.originX = 1920;
  sp7.monitors.append({QStringLiteral("sp7"), 0, 0, 2736, 1824});
  return {work, sp7};
}

QPushButton *saveButton(QWidget *dialog)
{
  const auto *box = dialog->findChild<QDialogButtonBox *>();
  return box == nullptr ? nullptr : box->button(QDialogButtonBox::Save);
}

bool dragPlacedComputer(QWidget *dialog, MonitorLayoutWidget *layout, const QString &name, const QPoint &delta)
{
  auto *tabs = dialog->findChild<QTabWidget *>();
  if (tabs == nullptr || layout == nullptr) {
    return false;
  }
  tabs->setCurrentWidget(layout);
  dialog->show();
  dialog->setWindowState(Qt::WindowNoState);
  dialog->resize(1200, 900);
  QApplication::processEvents();
  const QPoint start = layout->placedComputerCenter(name);
  if (!layout->rect().contains(start)) {
    return false;
  }
  const QPoint end = start + delta;
  QTest::mouseEvent(QTest::MousePress, layout, Qt::LeftButton, Qt::AltModifier, start);
  QTest::mouseEvent(QTest::MouseMove, layout, Qt::LeftButton, Qt::AltModifier, end);
  QTest::mouseEvent(QTest::MouseRelease, layout, Qt::LeftButton, Qt::AltModifier, end);
  QApplication::processEvents();
  return true;
}

int originXOf(const QList<LayoutComputer> &layout, const QString &name)
{
  for (const auto &computer : layout) {
    if (computer.name == name) {
      return computer.originX;
    }
  }
  return -1;
}

} // namespace

void LayoutClientsTests::initTestCase()
{
  QDir dir;
  QVERIFY(dir.mkpath(m_settingsPath));
  QFile::remove(m_settingsFile);
  Settings::setSettingsFile(m_settingsFile);
  Settings::setStateFile(m_stateFile);
}

void LayoutClientsTests::discoveryDiff()
{
  const auto found = deskflow::unplacedClients(
      {QStringLiteral("sp7"), QStringLiteral("sur3"), QStringLiteral("SP7")},
      {QStringLiteral("sp7"), QStringLiteral("dream"), QStringLiteral("work")},
      {QStringLiteral("sur3"), QStringLiteral("Work")}
  );
  QCOMPARE(found.size(), 2);
  QCOMPARE(found.at(0).name, QStringLiteral("dream"));
  QVERIFY(!found.at(0).connected);
  QVERIFY(found.at(0).reported);
  QCOMPARE(found.at(1).name, QStringLiteral("sp7"));
  QVERIFY(found.at(1).connected);
  QVERIFY(found.at(1).reported);
}

void LayoutClientsTests::internalAddAndDisableRoundTrip()
{
  ServerConfig config;
  const QList<LayoutMonitor> monitors{{QStringLiteral("sp7"), 0, 0, 2736, 1824}};
  const auto added =
      deskflow::addComputerToActiveLayout(false, QString(), &config, {}, QStringLiteral("sp7"), monitors);
  QVERIFY(added.ok);
  QCOMPARE(added.layout.size(), 1);
  QCOMPARE(added.layout.at(0).name, QStringLiteral("sp7"));
  QCOMPARE(added.layout.at(0).originX, 0);
  QCOMPARE(added.layout.at(0).monitors.at(0).width, 2736);
  QVERIFY(Settings::knownComputers().contains(QStringLiteral("sp7")));
  QVERIFY(!deskflow::isComputerDisabled(QStringLiteral("sp7")));

  ServerConfig loaded;
  QCOMPARE(loaded.monitorLayout().size(), 1);
  QCOMPARE(loaded.monitorLayout().at(0).name, QStringLiteral("sp7"));
  QCOMPARE(loaded.monitorLayout().at(0).monitors.at(0).height, 1824);
  QCOMPARE(loaded.monitorLayout().at(0).originX, 0);

  deskflow::setComputerDisabled(QStringLiteral("sp7"), true);
  QVERIFY(deskflow::isComputerDisabled(QStringLiteral("sp7")));
  QVERIFY(deskflow::isComputerDisabled(QStringLiteral("SP7")));
  ServerConfig stillThere;
  QCOMPARE(stillThere.monitorLayout().size(), 1);
  QCOMPARE(stillThere.monitorLayout().at(0).name, QStringLiteral("sp7"));
  QCOMPARE(stillThere.monitorLayout().at(0).originX, 0);
  QCOMPARE(stillThere.monitorLayout().at(0).monitors.at(0).width, 2736);

  deskflow::setComputerDisabled(QStringLiteral("sp7"), false);
  QVERIFY(!deskflow::isComputerDisabled(QStringLiteral("sp7")));
  QVERIFY(Settings::knownComputers().contains(QStringLiteral("sp7")));
  ServerConfig enabledAgain;
  QCOMPARE(enabledAgain.monitorLayout().at(0).name, QStringLiteral("sp7"));
}

void LayoutClientsTests::externalAddKeepsOneLayout()
{
  ServerConfig before;
  const auto beforeLayout = before.monitorLayout();

  const auto path = QStringLiteral("%1/server.conf").arg(m_settingsPath);
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  const auto original = QStringLiteral("section: links\n\twork:\n\t\tright = sur3\nend\n\nsection: options\nend\n");
  QCOMPARE(file.write(original.toUtf8()), static_cast<qint64>(original.toUtf8().size()));
  file.close();

  Settings::setValue(Settings::Computer::Name.arg(QStringLiteral("work")), QStringLiteral("work"));
  Settings::proxy().setValue(Settings::Computer::Name.arg(QStringLiteral("work")), QStringLiteral("work"));

  LayoutComputer work;
  work.name = QStringLiteral("work");
  work.monitors.append({QStringLiteral("eDP-1"), 0, 0, 1920, 1080});
  const QList<LayoutMonitor> sp7Monitors{{QStringLiteral("sp7"), 0, 0, 2736, 1824}};
  const auto added =
      deskflow::addComputerToActiveLayout(true, path, nullptr, {work}, QStringLiteral("sp7"), sp7Monitors);
  QVERIFY(added.ok);
  QCOMPARE(added.layout.size(), 2);
  QCOMPARE(added.layout.at(1).name, QStringLiteral("sp7"));
  QCOMPARE(added.layout.at(1).originX, 1920);

  QFile written(path);
  QVERIFY(written.open(QIODevice::ReadOnly));
  const auto text = QString::fromUtf8(written.readAll());
  written.close();
  QCOMPARE(text.count(QStringLiteral("section: layout")), 1);
  QVERIFY(text.contains(QStringLiteral("section: links")));
  QVERIFY(text.contains(QStringLiteral("right = sur3")));
  QVERIFY(text.contains(QStringLiteral("section: options")));
  QVERIFY(text.contains(QStringLiteral("sp7:")));
  QVERIFY(text.contains(QStringLiteral("2736, 1824")));
  QVERIFY(text.contains(QStringLiteral("work:")));
  QVERIFY(Settings::knownComputers().contains(QStringLiteral("sp7")));

  QFile backup(added.backupPath);
  QVERIFY(backup.open(QIODevice::ReadOnly));
  const auto backed = QString::fromUtf8(backup.readAll());
  backup.close();
  QCOMPARE(backed, original);

  const QList<LayoutMonitor> dreamMonitors{{QStringLiteral("dream"), 0, 0, 1920, 1080}};
  const auto again =
      deskflow::addComputerToActiveLayout(true, path, nullptr, added.layout, QStringLiteral("dream"), dreamMonitors);
  QVERIFY(again.ok);
  QFile second(path);
  QVERIFY(second.open(QIODevice::ReadOnly));
  const auto text2 = QString::fromUtf8(second.readAll());
  second.close();
  QCOMPARE(text2.count(QStringLiteral("section: layout")), 1);
  QVERIFY(text2.contains(QStringLiteral("dream:")));
  QVERIFY(text2.contains(QStringLiteral("sp7:")));
  QVERIFY(text2.contains(QStringLiteral("right = sur3")));
  QVERIFY(Settings::knownComputers().contains(QStringLiteral("dream")));

  ServerConfig after;
  QCOMPARE(after.monitorLayout(), beforeLayout);

  const auto rejected =
      deskflow::addComputerToActiveLayout(true, path, nullptr, again.layout, QStringLiteral("not a name"), {});
  QVERIFY(!rejected.ok);
  QCOMPARE(rejected.error, deskflow::LayoutWriteError::InvalidName);
}

void LayoutClientsTests::hotkeyTargetsFollowTheComputerTable()
{
  deskflow::setComputerDisabled(QStringLiteral("sp7"), true);
  const auto targets = deskflow::hotkeyTargetNames();
  QVERIFY(targets.contains(QStringLiteral("work")));
  QVERIFY(targets.contains(QStringLiteral("dream")));
  QVERIFY(!targets.contains(QStringLiteral("sp7")));
  deskflow::setComputerDisabled(QStringLiteral("sp7"), false);
  QVERIFY(deskflow::hotkeyTargetNames().contains(QStringLiteral("sp7")));
}

void LayoutClientsTests::movingOneComputerEnablesSave()
{
  Settings::setValue(Settings::Core::ComputerName, QStringLiteral("work"));
  Settings::setValue(Settings::Server::ExternalConfig, false);
  const auto placed = twoComputerLayout();

  ServerConfig internal;
  internal.setUseMonitorLayout(true);
  internal.setMonitorLayout(placed);
  internal.addClient(QStringLiteral("sp7"));
  const auto internalBefore = internal.monitorLayout();
  {
    ServerConfigDialog dialog(nullptr, internal);
    auto *layout = dialog.findChild<MonitorLayoutWidget *>();
    auto *save = saveButton(&dialog);
    QVERIFY(layout != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(!save->isEnabled());
    QVERIFY(dragPlacedComputer(&dialog, layout, QStringLiteral("sp7"), QPoint(160, 120)));
    QVERIFY(save->isEnabled());
    QVERIFY(originXOf(layout->layout(), QStringLiteral("sp7")) != 1920);
    QCOMPARE(internal.monitorLayout(), internalBefore);
    const auto movedX = originXOf(layout->layout(), QStringLiteral("sp7"));
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(originXOf(internal.monitorLayout(), QStringLiteral("sp7")), movedX);
  }

  const auto path = QStringLiteral("%1/move-server.conf").arg(m_settingsPath);
  const auto original = QStringLiteral(
      "section: links\nend\n\nsection: layout\n"
      "\twork:\n\t\torigin = 0, 0\n\t\tmonitor = work, 0, 0, 1920, 1080\n"
      "\tsp7:\n\t\torigin = 1920, 0\n\t\tmonitor = sp7, 0, 0, 2736, 1824\n"
      "end\n"
  );
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(file.write(original.toUtf8()), static_cast<qint64>(original.toUtf8().size()));
  file.close();

  Settings::setValue(Settings::Server::ExternalConfig, true);
  Settings::setValue(Settings::Server::ExternalConfigFile, path);
  ServerConfig external;
  external.addClient(QStringLiteral("sp7"));
  const auto externalInternalLayout = external.monitorLayout();
  {
    ServerConfigDialog dialog(nullptr, external);
    auto *layout = dialog.findChild<MonitorLayoutWidget *>();
    auto *save = saveButton(&dialog);
    QVERIFY(layout != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(!save->isEnabled());
    QVERIFY(dragPlacedComputer(&dialog, layout, QStringLiteral("sp7"), QPoint(160, 120)));
    QVERIFY(save->isEnabled());
    QVERIFY(layout->hasPendingLayoutWrite());
    QVERIFY(originXOf(layout->layout(), QStringLiteral("sp7")) != 1920);
    QCOMPARE(external.monitorLayout(), externalInternalLayout);

    QFile untouched(path);
    QVERIFY(untouched.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(untouched.readAll()), original);
    untouched.close();

    QTest::mouseClick(save, Qt::LeftButton);
    QVERIFY(!layout->hasPendingLayoutWrite());
  }

  QFile written(path);
  QVERIFY(written.open(QIODevice::ReadOnly));
  const auto text = QString::fromUtf8(written.readAll());
  written.close();
  QCOMPARE(text.count(QStringLiteral("section: layout")), 1);
  QVERIFY(text.contains(QStringLiteral("section: links")));
  QVERIFY(!text.contains(QStringLiteral("origin = 1920, 0")));
  QVERIFY(text.contains(QStringLiteral("sp7:")));
  ServerConfig stillInternal;
  QCOMPARE(stillInternal.monitorLayout(), externalInternalLayout);

  Settings::setValue(Settings::Server::ExternalConfig, false);
}

void LayoutClientsTests::stackingOrderAndSave()
{
  Settings::setValue(Settings::Core::ComputerName, QStringLiteral("work"));
  Settings::setValue(Settings::Server::ExternalConfig, false);

  ServerConfig apart;
  apart.setUseMonitorLayout(true);
  apart.setMonitorLayout(twoComputerLayout());
  apart.addClient(QStringLiteral("sp7"));
  {
    ServerConfigDialog dialog(nullptr, apart);
    auto *layout = dialog.findChild<MonitorLayoutWidget *>();
    auto *tabs = dialog.findChild<QTabWidget *>();
    auto *front = dialog.findChild<QPushButton *>(QStringLiteral("stack-front"));
    QVERIFY(layout != nullptr);
    QVERIFY(tabs != nullptr);
    QVERIFY(front != nullptr);
    tabs->setCurrentWidget(layout);
    showFixedLayout(dialog);
    const QPoint sp7 = layout->placedComputerCenter(QStringLiteral("sp7"));
    QVERIFY(layout->rect().contains(sp7));
    QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, sp7);
    QVERIFY(!front->isEnabled());
    QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("stack-back"))->isEnabled());
  }

  LayoutComputer work;
  work.name = QStringLiteral("work");
  work.z = 0;
  work.monitors.append({QStringLiteral("work"), 0, 0, 800, 600});
  LayoutComputer sp7;
  sp7.name = QStringLiteral("sp7");
  sp7.z = 1;
  sp7.monitors.append({QStringLiteral("sp7"), 0, 0, 800, 600});
  ServerConfig stacked;
  stacked.setUseMonitorLayout(true);
  stacked.setMonitorLayout({work, sp7});
  stacked.addClient(QStringLiteral("sp7"));
  const auto before = stacked.monitorLayout();
  {
    ServerConfigDialog dialog(nullptr, stacked);
    auto *layout = dialog.findChild<MonitorLayoutWidget *>();
    auto *tabs = dialog.findChild<QTabWidget *>();
    auto *front = dialog.findChild<QPushButton *>(QStringLiteral("stack-front"));
    auto *back = dialog.findChild<QPushButton *>(QStringLiteral("stack-back"));
    auto *save = saveButton(&dialog);
    QVERIFY(layout != nullptr);
    QVERIFY(front != nullptr);
    QVERIFY(back != nullptr);
    QVERIFY(save != nullptr);
    tabs->setCurrentWidget(layout);
    showFixedLayout(dialog);
    QVERIFY(!save->isEnabled());
    layout->update();
    QApplication::processEvents();
    const QRect sp7Chip = layout->stackRosterChip(QStringLiteral("sp7"));
    const QRect workChip = layout->stackRosterChip(QStringLiteral("work"));
    QVERIFY(sp7Chip.isValid());
    QVERIFY(workChip.isValid());
    QVERIFY(sp7Chip.top() < workChip.top());
    QVERIFY(layout->rect().contains(workChip.center()));
    QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, workChip.center());
    QApplication::processEvents();
    QVERIFY(front->isEnabled());
    const QPoint center = layout->placedComputerCenter(QStringLiteral("sp7"));
    QVERIFY(layout->rect().contains(center));
    QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, center);
    QApplication::processEvents();
    QVERIFY(front->isEnabled() == false);
    QVERIFY(back->isEnabled());
    QTest::mouseClick(back, Qt::LeftButton);
    QApplication::processEvents();
    int sp7Z = -1;
    int workZ = -1;
    for (const auto &computer : layout->layout()) {
      if (computer.name == QStringLiteral("sp7")) {
        sp7Z = computer.z;
      }
      if (computer.name == QStringLiteral("work")) {
        workZ = computer.z;
      }
    }
    QVERIFY(sp7Z < workZ);
    QVERIFY(save->isEnabled());
    QCOMPARE(stacked.monitorLayout(), before);
    QTest::mouseClick(save, Qt::LeftButton);
    QVERIFY(stacked.monitorLayout() != before);
  }

  const auto path = QStringLiteral("%1/stack-server.conf").arg(m_settingsPath);
  const auto original = QStringLiteral(
      "section: links\nend\n\nsection: layout\n"
      "\twork:\n\t\torigin = 0, 0\n\t\tz = 0\n\t\tmonitor = work, 0, 0, 800, 600\n"
      "\tsp7:\n\t\torigin = 0, 0\n\t\tz = 2\n\t\tmonitor = sp7, 0, 0, 800, 600\n"
      "end\n"
  );
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(file.write(original.toUtf8()), static_cast<qint64>(original.toUtf8().size()));
  file.close();
  Settings::setValue(Settings::Server::ExternalConfig, true);
  Settings::setValue(Settings::Server::ExternalConfigFile, path);
  ServerConfig external;
  external.addClient(QStringLiteral("sp7"));
  const auto internalSnapshot = external.monitorLayout();
  {
    ServerConfigDialog dialog(nullptr, external);
    auto *layout = dialog.findChild<MonitorLayoutWidget *>();
    auto *tabs = dialog.findChild<QTabWidget *>();
    auto *back = dialog.findChild<QPushButton *>(QStringLiteral("stack-back"));
    auto *save = saveButton(&dialog);
    QVERIFY(layout != nullptr);
    QVERIFY(back != nullptr);
    tabs->setCurrentWidget(layout);
    showFixedLayout(dialog);
    QVERIFY(!save->isEnabled());
    const QPoint center = layout->placedComputerCenter(QStringLiteral("sp7"));
    QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, center);
    QVERIFY(back->isEnabled());
    QTest::mouseClick(back, Qt::LeftButton);
    QVERIFY(layout->hasPendingLayoutWrite());
    QVERIFY(save->isEnabled());
    QFile untouched(path);
    QVERIFY(untouched.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(untouched.readAll()), original);
    untouched.close();
    QCOMPARE(external.monitorLayout(), internalSnapshot);
    QTest::mouseClick(save, Qt::LeftButton);
    QVERIFY(!layout->hasPendingLayoutWrite());
  }
  QFile written(path);
  QVERIFY(written.open(QIODevice::ReadOnly));
  const auto text = QString::fromUtf8(written.readAll());
  written.close();
  QCOMPARE(text.count(QStringLiteral("section: layout")), 1);
  QVERIFY(text.contains(QStringLiteral("section: links")));
  QVERIFY(text.contains(QStringLiteral("z = 0")));
  QVERIFY(text.contains(QStringLiteral("z = 1")));
  QVERIFY(!text.contains(QStringLiteral("z = 2")));
  ServerConfig stillInternal;
  QCOMPARE(stillInternal.monitorLayout(), internalSnapshot);
  Settings::setValue(Settings::Server::ExternalConfig, false);
}

void LayoutClientsTests::serverDialogOpensMaximized()
{
  ServerConfig config;
  ServerConfigDialog dialog(nullptr, config);
  QVERIFY(dialog.windowState().testFlag(Qt::WindowMaximized));
  dialog.show();
  QApplication::processEvents();
  QVERIFY(dialog.isMaximized());
}

void LayoutClientsTests::renderPreview()
{
  const auto directory = qEnvironmentVariable("DESKFLOW_LAYOUT_PREVIEW");
  if (directory.isEmpty()) {
    return;
  }
  QDir().mkpath(directory);

  QTranslator translator;
  const auto qm = qEnvironmentVariable("DESKFLOW_LAYOUT_PREVIEW_QM");
  if (!qm.isEmpty()) {
    QVERIFY(translator.load(qm));
    QApplication::installTranslator(&translator);
  }

  Settings::setValue(Settings::Core::ComputerName, QStringLiteral("work"));
  Settings::setValue(QStringLiteral("internalConfig/reportedGeometry/sp7"), QStringLiteral("sp7,0,0,2736,1824"));
  Settings::setValue(Settings::InternalConfig::ConnectedClients, QStringList{QStringLiteral("sp7")});
  Settings::proxy().setValue(
      QStringLiteral("internalConfig/reportedGeometry/sp7"), QStringLiteral("sp7,0,0,2736,1824")
  );
  Settings::proxy().setValue(Settings::InternalConfig::ConnectedClients, QStringList{QStringLiteral("sp7")});
  deskflow::setComputerDisabled(QStringLiteral("dream"), true);

  ServerConfig config;
  QList<LayoutComputer> layout;
  LayoutComputer work;
  work.name = QStringLiteral("work");
  work.monitors.append({QStringLiteral("HDMI-1"), 0, 0, 1080, 1920});
  work.monitors.append({QStringLiteral("eDP-1"), 1080, 0, 1920, 1080});
  layout.append(work);
  LayoutComputer sur3;
  sur3.name = QStringLiteral("sur3");
  sur3.originX = -2160;
  sur3.monitors.append({QStringLiteral("sur3"), 0, 0, 2160, 1440});
  layout.append(sur3);
  LayoutComputer dream;
  dream.name = QStringLiteral("dream");
  dream.originX = 3000;
  dream.monitors.append({QStringLiteral("dream"), 0, 0, 1920, 1080});
  layout.append(dream);
  config.setMonitorLayout(layout);
  config.setUseMonitorLayout(true);

  MonitorLayoutWidget widget(config);
  widget.resize(1100, 820);
  widget.show();
  QApplication::processEvents();
  QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  widget.render(&image);
  QVERIFY(!image.isNull());
  const auto fileName =
      qm.isEmpty() ? QStringLiteral("monitor-layout-discovery.png") : QStringLiteral("monitor-layout-discovery-zh.png");
  QVERIFY(image.save(QStringLiteral("%1/%2").arg(directory, fileName)));

  if (!qm.isEmpty()) {
    return;
  }
  QList<LayoutComputer> stacked;
  LayoutComputer sp7;
  sp7.name = QStringLiteral("sp7");
  sp7.z = 2;
  sp7.monitors.append({QStringLiteral("sp7"), 0, 0, 3840, 2160});
  LayoutComputer sur3Stack;
  sur3Stack.name = QStringLiteral("sur3");
  sur3Stack.z = 1;
  sur3Stack.monitors.append({QStringLiteral("sur3"), 0, 0, 2560, 1440});
  LayoutComputer dreamStack;
  dreamStack.name = QStringLiteral("dream");
  dreamStack.z = 0;
  dreamStack.monitors.append({QStringLiteral("dream"), 0, 0, 1920, 1080});
  stacked.append(sp7);
  stacked.append(sur3Stack);
  stacked.append(dreamStack);
  ServerConfig stackConfig;
  stackConfig.setMonitorLayout(stacked);
  stackConfig.setUseMonitorLayout(true);
  MonitorLayoutWidget stackWidget(stackConfig);
  stackWidget.resize(1100, 720);
  stackWidget.show();
  QApplication::processEvents();
  QImage idle(stackWidget.size(), QImage::Format_ARGB32_Premultiplied);
  idle.fill(Qt::white);
  stackWidget.render(&idle);
  QVERIFY(idle.save(QStringLiteral("%1/stack-idle.png").arg(directory)));

  const QPoint hover = stackWidget.placedComputerCenter(QStringLiteral("sp7"));
  QMouseEvent move(
      QEvent::MouseMove, QPointF(hover), stackWidget.mapToGlobal(hover), Qt::NoButton, Qt::NoButton, Qt::NoModifier
  );
  QApplication::sendEvent(&stackWidget, &move);
  QApplication::processEvents();
  QImage fanned(stackWidget.size(), QImage::Format_ARGB32_Premultiplied);
  fanned.fill(Qt::white);
  stackWidget.render(&fanned);
  QVERIFY(fanned.save(QStringLiteral("%1/stack-hover.png").arg(directory)));
}

QTEST_MAIN(LayoutClientsTests)
