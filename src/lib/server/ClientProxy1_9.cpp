/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_9.h"

#include "base/Event.h"
#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/MonitorGeometry.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <cstring>
#include <vector>

namespace {

constexpr int16_t kMaxMonitors = 64;

}

ClientProxy1_9::ClientProxy1_9(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events)
    : ClientProxy1_8(name, stream, server, events)
{
}

bool ClientProxy1_9::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDMonitors, 4) == 0) {
    if (!recvMonitors()) {
      return false;
    }
    eventQueue()->addEvent(Event(EventTypes::ComputerShapeChanged, getEventTarget()));
    return true;
  }
  return ClientProxy1_8::parseMessage(code);
}

bool ClientProxy1_9::recvMonitors()
{
  int16_t count = 0;
  if (!ProtocolUtil::readf(getStream(), kMsgDMonitors + 4, &count)) {
    return false;
  }
  if (count < 0 || count > kMaxMonitors) {
    LOG_ERR("client \"%s\" sent an invalid monitor count %d", getName().c_str(), count);
    return false;
  }

  std::vector<deskflow::MonitorRect> monitors;
  monitors.reserve(static_cast<size_t>(count));
  for (int16_t i = 0; i < count; ++i) {
    std::string name;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    if (!ProtocolUtil::readf(getStream(), "%s%4i%4i%4i%4i", &name, &x, &y, &width, &height)) {
      return false;
    }
    if (width <= 0 || height <= 0) {
      LOG_WARN("client \"%s\" ignored monitor \"%s\" with non-positive size", getName().c_str(), name.c_str());
      continue;
    }
    monitors.push_back(deskflow::MonitorRect{std::move(name), x, y, width, height});
    LOG_DEBUG(
        "client \"%s\" monitor \"%s\" %d,%d %dx%d", getName().c_str(), monitors.back().name.c_str(), x, y, width, height
    );
  }

  if (monitors.empty()) {
    LOG_WARN("client \"%s\" reported no usable monitors", getName().c_str());
    return true;
  }

  setMonitors(std::move(monitors));
  return true;
}
