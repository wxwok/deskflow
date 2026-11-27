/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/phone/PhoneBezel.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QVBoxLayout>

namespace deskflow::phone {

PhoneBezel::PhoneBezel(const QString &phoneId, QWidget *parent) : QWidget(parent), m_phoneId(phoneId)
{
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
  setAcceptDrops(true);
  setFocusPolicy(Qt::ClickFocus);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);
  m_battery = new QLabel(tr("Battery --"), this);
  layout->addWidget(m_battery);
  m_pin = new QPushButton(tr("Pin"), this);
  m_pin->setCheckable(true);
  layout->addWidget(m_pin);
  connect(m_pin, &QPushButton::toggled, this, [this](bool pinned) {
    m_pinned = pinned;
    m_pin->setText(pinned ? tr("Unpin") : tr("Pin"));
    Q_EMIT pinToggled(m_phoneId, pinned);
  });
  layout->addWidget(addButton(tr("Back"), PhoneKey::Back));
  layout->addWidget(addButton(tr("Home"), PhoneKey::Home));
  layout->addWidget(addButton(tr("Recents"), PhoneKey::Recents));
  layout->addWidget(addButton(tr("Power"), PhoneKey::Power));
  layout->addWidget(addButton(tr("Screen off"), PhoneKey::ScreenOff));
  layout->addWidget(addButton(tr("Screen on"), PhoneKey::ScreenOn));
  layout->addWidget(addButton(tr("Volume +"), PhoneKey::VolumeUp));
  layout->addWidget(addButton(tr("Volume -"), PhoneKey::VolumeDown));
  m_mute = new QPushButton(tr("Mute audio"), this);
  m_mute->setCheckable(true);
  layout->addWidget(m_mute);
  connect(m_mute, &QPushButton::toggled, this, [this](bool muted) {
    m_muted = muted;
    m_mute->setText(muted ? tr("Unmute audio") : tr("Mute audio"));
    Q_EMIT muteToggled(m_phoneId, muted);
  });
  layout->addWidget(addButton(tr("Notifications"), PhoneKey::Notifications));
  layout->addWidget(addButton(tr("Keyboard"), PhoneKey::KeyboardLayout));
  auto *paste = new QPushButton(tr("Paste clipboard"), this);
  layout->addWidget(paste);
  connect(paste, &QPushButton::clicked, this, [this]() { Q_EMIT pasteRequested(m_phoneId); });
  auto *editor = new QPushButton(tr("Clipboard editor"), this);
  layout->addWidget(editor);
  connect(editor, &QPushButton::clicked, this, [this]() { Q_EMIT clipboardRequested(m_phoneId); });
  auto *apps = new QPushButton(tr("Pin apps"), this);
  layout->addWidget(apps);
  connect(apps, &QPushButton::clicked, this, [this]() { Q_EMIT appsRequested(m_phoneId); });
  m_appsLayout = new QVBoxLayout();
  layout->addLayout(m_appsLayout);
  auto *hide = new QPushButton(tr("Hide"), this);
  layout->addWidget(hide);
  connect(hide, &QPushButton::clicked, this, [this]() { Q_EMIT hideRequested(m_phoneId); });
  layout->addStretch(1);
  auto *dropHint = new QLabel(tr("Drop files here or on the phone picture"), this);
  dropHint->setWordWrap(true);
  layout->addWidget(dropHint);
  setStyleSheet(
      QStringLiteral("PhoneBezel { background: rgba(20, 40, 48, 230); } QLabel, QPushButton { color: white; }")
  );
}

QPushButton *PhoneBezel::addButton(const QString &text, PhoneKey key)
{
  auto *button = new QPushButton(text, this);
  connect(button, &QPushButton::clicked, this, [this, key]() { Q_EMIT keyRequested(m_phoneId, key); });
  return button;
}

void PhoneBezel::setPinned(bool pinned)
{
  m_pinned = pinned;
  m_pin->blockSignals(true);
  m_pin->setChecked(pinned);
  m_pin->setText(pinned ? tr("Unpin") : tr("Pin"));
  m_pin->blockSignals(false);
}

void PhoneBezel::setBattery(int percent)
{
  if (percent < 0) {
    m_battery->setText(tr("Battery --"));
    return;
  }
  m_battery->setText(tr("Battery %1%").arg(percent));
}

void PhoneBezel::setMuted(bool muted)
{
  m_muted = muted;
  m_mute->blockSignals(true);
  m_mute->setChecked(muted);
  m_mute->setText(muted ? tr("Unmute audio") : tr("Mute audio"));
  m_mute->blockSignals(false);
}

void PhoneBezel::setApps(const QList<PhoneApp> &apps)
{
  m_apps = apps;
  rebuildApps();
}

void PhoneBezel::rebuildApps()
{
  while (QLayoutItem *item = m_appsLayout->takeAt(0)) {
    if (auto *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
  for (const auto &app : m_apps) {
    auto *button = new QPushButton(app.label.isEmpty() ? app.packageName : app.label, this);
    const auto package = app.packageName;
    connect(button, &QPushButton::clicked, this, [this, package]() { Q_EMIT appLaunchRequested(m_phoneId, package); });
    m_appsLayout->addWidget(button);
  }
}

void PhoneBezel::dragEnterEvent(QDragEnterEvent *event)
{
  if (event->mimeData()->hasUrls()) {
    m_dragActive = true;
    event->acceptProposedAction();
    return;
  }
  event->ignore();
}

void PhoneBezel::dragLeaveEvent(QDragLeaveEvent *event)
{
  m_dragActive = false;
  QWidget::dragLeaveEvent(event);
}

void PhoneBezel::dropEvent(QDropEvent *event)
{
  m_dragActive = false;
  QStringList paths;
  for (const auto &url : event->mimeData()->urls()) {
    if (url.isLocalFile()) {
      paths.append(url.toLocalFile());
    }
  }
  if (!paths.isEmpty()) {
    event->acceptProposedAction();
    Q_EMIT filesDropped(m_phoneId, paths);
    return;
  }
  event->ignore();
}

PhoneExitSensor::PhoneExitSensor(QWidget *parent) : QWidget(parent)
{
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  setAttribute(Qt::WA_TranslucentBackground, true);
}

void PhoneExitSensor::paintEvent(QPaintEvent *)
{
}

} // namespace deskflow::phone
