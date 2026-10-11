/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxy1_9.h"

#include "base/Log.h"
#include "client/Client.h"
#include "deskflow/MonitorGeometry.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <algorithm>

ServerProxy1_9::ServerProxy1_9(Client *client, deskflow::IStream *stream, IEventQueue *events)
    : ServerProxy1_8(client, stream, events)
{
}

void ServerProxy1_9::queryInfo()
{
  ServerProxy::queryInfo();
  sendMonitors();
}

void ServerProxy1_9::sendMonitors()
{
  const auto monitors = client()->getMonitors();
  const auto count = static_cast<int16_t>(std::min<size_t>(monitors.size(), 64));
  LOG_DEBUG("sending %d monitors", count);
  ProtocolUtil::writef(getStream(), kMsgDMonitors, count);
  for (int16_t i = 0; i < count; ++i) {
    const auto &monitor = monitors[static_cast<size_t>(i)];
    ProtocolUtil::writef(
        getStream(), "%s%4i%4i%4i%4i", &monitor.name, monitor.x, monitor.y, monitor.width, monitor.height
    );
  }
}
