/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/config/ServerConfig.h"

#include <QList>
#include <QString>
#include <QStringList>

class ServerConfig;

namespace deskflow {

//! A client that can be placed, and why it showed up.
struct ClientPresence
{
  QString name;
  bool connected = false;
  bool reported = false;

  bool operator==(const ClientPresence &) const = default;
};

enum class ComputerRegistration
{
  Created,
  Existing,
  Rejected
};

enum class LayoutWriteError
{
  None,
  InvalidName,
  AlreadyPlaced,
  ReadFailed,
  BackupFailed,
  WriteFailed
};

struct LayoutWriteResult
{
  bool ok = false;
  LayoutWriteError error = LayoutWriteError::None;
  QString backupPath;
};

struct AddComputerResult
{
  bool ok = false;
  LayoutWriteError error = LayoutWriteError::None;
  QString backupPath;
  QList<LayoutComputer> layout;
};

//! connected ∪ reported, minus names already placed. Case-insensitive. Sorted by name.
QList<ClientPresence>
unplacedClients(const QStringList &connected, const QStringList &reported, const QStringList &placed);

QStringList reportedComputerNames();
QStringList connectedComputerNames();

//! Enabled computers from the general [computer_*] table. This is the only hotkey target list.
QStringList hotkeyTargetNames();

bool isValidLayoutComputerName(const QString &name);
bool isComputerDisabled(const QString &name);
void setComputerDisabled(const QString &name, bool disabled);
ComputerRegistration registerComputer(const QString &name);
void unregisterComputer(const QString &name);

QList<LayoutMonitor> parseMonitorBody(const QString &body);
QList<LayoutMonitor> reportedMonitors(const QString &computerName);
QList<LayoutComputer> parseLayoutSection(const QString &configText);
QString replaceLayoutSection(const QString &configText, const QList<LayoutComputer> &layout);
LayoutComputer
placeToTheRight(const QList<LayoutComputer> &layout, const QString &name, const QList<LayoutMonitor> &monitors);

//! Replace section: layout. Copies the previous file to path + ".bak" first.
LayoutWriteResult writeLayoutFile(const QString &path, const QList<LayoutComputer> &layout);

//! Register [computer_<name>] and write the one authoritative layout.
/*!
When \p external is set, \p configPath's \c section: layout is that source and \p internal is left
untouched. Otherwise \p internal's monitor layout is saved. A failed write rolls back a computer
entry this call created.
*/
AddComputerResult addComputerToActiveLayout(
    bool external, const QString &configPath, ServerConfig *internal, const QList<LayoutComputer> &layout,
    const QString &name, const QList<LayoutMonitor> &monitors
);

//! Persist the internal monitor layout without rewriting screens, hotkeys, or reported geometry.
void saveInternalMonitorLayout(const ServerConfig &config);

} // namespace deskflow
