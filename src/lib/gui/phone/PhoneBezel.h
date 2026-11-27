/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/phone/PhoneTypes.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace deskflow::phone {

class PhoneBezel : public QWidget
{
  Q_OBJECT

public:
  explicit PhoneBezel(const QString &phoneId, QWidget *parent = nullptr);

  QString phoneId() const
  {
    return m_phoneId;
  }
  void setPinned(bool pinned);
  void setBattery(int percent);
  void setMuted(bool muted);
  void setApps(const QList<PhoneApp> &apps);
  bool dragActive() const
  {
    return m_dragActive;
  }

Q_SIGNALS:
  void pinToggled(const QString &phoneId, bool pinned);
  void keyRequested(const QString &phoneId, deskflow::phone::PhoneKey key);
  void clipboardRequested(const QString &phoneId);
  void pasteRequested(const QString &phoneId);
  void hideRequested(const QString &phoneId);
  void muteToggled(const QString &phoneId, bool muted);
  void appsRequested(const QString &phoneId);
  void appLaunchRequested(const QString &phoneId, const QString &packageName);
  void filesDropped(const QString &phoneId, const QStringList &paths);

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  QPushButton *addButton(const QString &text, PhoneKey key);
  void rebuildApps();

  QString m_phoneId;
  bool m_pinned = false;
  bool m_muted = false;
  bool m_dragActive = false;
  QLabel *m_battery = nullptr;
  QPushButton *m_pin = nullptr;
  QPushButton *m_mute = nullptr;
  QVBoxLayout *m_appsLayout = nullptr;
  QList<PhoneApp> m_apps;
};

//! Sits on the desktop side of the bezel so leaving the phone can be seen without a global cursor.
class PhoneExitSensor : public QWidget
{
  Q_OBJECT

public:
  explicit PhoneExitSensor(QWidget *parent = nullptr);

protected:
  void paintEvent(QPaintEvent *event) override;
};

} // namespace deskflow::phone
