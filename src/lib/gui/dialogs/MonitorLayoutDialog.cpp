/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MonitorLayoutDialog.h"

#include "gui/config/LayoutClients.h"
#include "gui/widgets/MonitorLayoutWidget.h"

#include <QVBoxLayout>

MonitorLayoutDialog::MonitorLayoutDialog(ServerConfig &config, QWidget *parent)
    : QDialog(parent),
      m_layout(new MonitorLayoutWidget(config, this))
{
  setWindowTitle(tr("Monitor layout"));
  setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(m_layout);
  resize(1100, 800);
  connect(m_layout, &MonitorLayoutWidget::layoutChanged, this, [&config, this]() {
    if (config.useExternalConfig()) {
      return;
    }
    deskflow::saveInternalMonitorLayout(config);
  });
  connect(m_layout, &MonitorLayoutWidget::authoritativeLayoutChanged, this, [&config]() {
    if (!config.useExternalConfig()) {
      deskflow::saveInternalMonitorLayout(config);
    }
  });
}
