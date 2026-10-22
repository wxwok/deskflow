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
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
#include <QTranslator>

namespace {

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
  dialog->resize(1200, 900);
  dialog->show();
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
}

QTEST_MAIN(LayoutClientsTests)
