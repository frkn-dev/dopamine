/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "windowsdaemon.h"

#include <Windows.h>
#include <qassert.h>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QNetworkInterface>
#include <QTextStream>
#include <QtGlobal>

#include "daemon/daemonerrors.h"
#include "dnsutilswindows.h"
#include "leakdetector.h"
#include "logger.h"
#include "platforms/windows/daemon/windowsfirewall.h"
#include "platforms/windows/daemon/windowssplittunnel.h"
#include "windowsfirewall.h"

#include "core/networkUtilities.h"

namespace {
Logger logger("WindowsDaemon");
}

WindowsDaemon::WindowsDaemon() : Daemon(nullptr) {
  MZ_COUNT_CTOR(WindowsDaemon);
  m_firewallManager = WindowsFirewall::create(this);
  Q_ASSERT(m_firewallManager != nullptr);

  m_wgutils = WireguardUtilsWindows::create(m_firewallManager, this);
  m_dnsutils = new DnsUtilsWindows(this);
  m_splitTunnelManager = WindowsSplitTunnel::create(m_firewallManager);

  connect(m_wgutils.get(), &WireguardUtilsWindows::backendFailure, this,
          &WindowsDaemon::monitorBackendFailure);
  connect(this, &WindowsDaemon::activationFailure,
          [this]() { m_firewallManager->disableKillSwitch(); });
}

WindowsDaemon::~WindowsDaemon() {
  MZ_COUNT_DTOR(WindowsDaemon);
  logger.debug() << "Daemon released";
}

void WindowsDaemon::prepareActivation(const InterfaceConfig& config, int inetAdapterIndex) {
  // Before creating the interface we need to check which adapter
  // routes to the server endpoint
  if (inetAdapterIndex == 0) {
      auto serveraddr = QHostAddress(config.m_serverIpv4AddrIn);
      m_inetAdapterIndex = NetworkUtilities::AdapterIndexTo(serveraddr);
  } else {
      m_inetAdapterIndex = inetAdapterIndex;
  }
}

void WindowsDaemon::activateSplitTunnel(const InterfaceConfig& config, int vpnAdapterIndex) {
    if (m_splitTunnelManager == nullptr) {
        if (!config.m_vpnDisabledApps.isEmpty()) {
            logger.error() << "Split tunnel manager is not initialized";
            emit backendFailure(DaemonError::ERROR_SPLIT_TUNNEL_INIT_FAILURE);
        }
        return;
    }

  if (config.m_vpnDisabledApps.length() > 0) {
      if (!m_splitTunnelManager->start(m_inetAdapterIndex, vpnAdapterIndex,
                                       config.m_vpnDisabledApps)) {
          logger.error() << "Failed to start split tunnel for"
                         << config.m_vpnDisabledApps;
          // Never leave the driver half configured: an engaged driver with a
          // stale/wrong address blocks the excluded apps entirely instead of
          // letting them out. Back to passive, the apps keep working inside
          // the tunnel and the user sees a report instead of a dead game.
          m_splitTunnelManager->stop();
          emit backendFailure(DaemonError::ERROR_SPLIT_TUNNEL_START_FAILURE);
          return;
      }
      if (!m_splitTunnelManager->isRunning()) {
          logger.error() << "Split tunnel did not reach running state";
          emit backendFailure(DaemonError::ERROR_SPLIT_TUNNEL_START_FAILURE);
      }
  } else {
      m_splitTunnelManager->stop();
  }
}

bool WindowsDaemon::run(Op op, const InterfaceConfig& config) {
  if (!m_splitTunnelManager) {
    if (config.m_vpnDisabledApps.length() > 0) {
      // The Client has sent us a list of disabled apps, but we failed
      // to init the the split tunnel driver.
      // So let the client know this was not possible
      emit backendFailure(DaemonError::ERROR_SPLIT_TUNNEL_INIT_FAILURE);
    }
    return true;
  }

  if (op == Down) {
    m_splitTunnelManager->stop();
    return true;
  }
  if (config.m_vpnDisabledApps.length() > 0) {
    if (!m_splitTunnelManager->start(m_inetAdapterIndex, 0,
                                     config.m_vpnDisabledApps)) {
      // Same fail-safe as above: an engaged driver with a wrong address kills
      // the connectivity of the excluded apps, a passive one leaves them in the
      // tunnel. Keep the VPN up, report the failure.
      logger.error() << "Split tunnel start failed for" << config.m_vpnDisabledApps;
      m_splitTunnelManager->stop();
      emit backendFailure(DaemonError::ERROR_SPLIT_TUNNEL_START_FAILURE);
      return true;
    };
    // Now the driver should be running (State == 4)
    if (!m_splitTunnelManager->isRunning()) {
      logger.error() << "Split tunnel did not reach running state";
      emit backendFailure(DaemonError::ERROR_SPLIT_TUNNEL_START_FAILURE);
    }
    return true;
  }
  m_splitTunnelManager->stop();

  return true;
}

void WindowsDaemon::monitorBackendFailure() {
  logger.warning() << "Tunnel service is down";

  emit backendFailure();
  deactivate();
}
