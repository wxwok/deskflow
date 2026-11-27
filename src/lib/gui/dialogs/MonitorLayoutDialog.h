/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/config/ServerConfig.h"

#include <QDialog>

class MonitorLayoutWidget;

//! Monitor layout by itself, opened from the tray without the rest of the server dialog.
class MonitorLayoutDialog : public QDialog
{
  Q_OBJECT

public:
  MonitorLayoutDialog(ServerConfig &config, QWidget *parent = nullptr);

  MonitorLayoutWidget *layoutWidget() const
  {
    return m_layout;
  }

private:
  MonitorLayoutWidget *m_layout = nullptr;
};
