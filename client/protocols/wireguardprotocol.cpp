#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QTcpSocket>
#include <QThread>

#include "wireguardprotocol.h"
#include "core/networkUtilities.h"

#include "mozilla/localsocketcontroller.h"

WireguardProtocol::WireguardProtocol(const QJsonObject &configuration, QObject *parent)
    : VpnProtocol(configuration, parent)
{
    m_impl.reset(new LocalSocketController());
    connect(m_impl.get(), &ControllerImpl::connected, this,
            [this](const QString &pubkey, const QDateTime &connectionTimestamp) {
                setConnectionState(Vpn::ConnectionState::Connected);
                m_statsTimer.start();
            });
    connect(m_impl.get(), &ControllerImpl::statusUpdated, this,
            [this](const QString& serverIpv4Gateway,
                   const QString& deviceIpv4Address, uint64_t txBytes,
                   uint64_t rxBytes) {
                const QString previousGateway = m_vpnGateway;
                const QString previousLocal = m_vpnLocalAddress;

                if (!serverIpv4Gateway.isEmpty()) {
                    m_vpnGateway = serverIpv4Gateway;
                }
                if (!deviceIpv4Address.isEmpty()) {
                    m_vpnLocalAddress = deviceIpv4Address;
                }

                if ((!m_vpnGateway.isEmpty() && m_vpnGateway != previousGateway) ||
                    (!m_vpnLocalAddress.isEmpty() && m_vpnLocalAddress != previousLocal)) {
                    emit tunnelAddressesUpdated(m_vpnGateway, m_vpnLocalAddress);
                }

                emit bytesChanged(rxBytes, txBytes);
            });

    connect(m_impl.get(), &ControllerImpl::disconnected, this,
            [this]() {
                setConnectionState(Vpn::ConnectionState::Disconnected);
                m_statsTimer.stop();
            });

    // desktop daemon reports cumulative rx/tx only on a status request —
    // poll it once a second so the live speed meter works
    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, [this]() { m_impl->checkStatus(); });

    // The stats timer is normally kicked by the daemon's connected() callback,
    // but after a daemon-socket reconnect (system sleep / App Nap wake) the
    // controller can report Connected without re-emitting connected() — the
    // timer then stays dead and the speed meter shows only the arrows.
    // Self-heal on any transition to Connected.
    connect(this, &VpnProtocol::connectionStateChanged, this, [this](Vpn::ConnectionState state) {
        if (state == Vpn::ConnectionState::Connected && !m_statsTimer.isActive()) {
            m_statsTimer.start();
        }
    });
    m_impl->initialize(nullptr, nullptr);
}

WireguardProtocol::~WireguardProtocol()
{
    WireguardProtocol::stop();
    QThread::msleep(200);
}

void WireguardProtocol::stop()
{
    stopMzImpl();
    return;
}

ErrorCode WireguardProtocol::startMzImpl()
{
    QString protocolName = m_rawConfig.value("protocol").toString();
    QJsonObject vpnConfigData = m_rawConfig.value(protocolName + "_config_data").toObject();
    vpnConfigData[config_key::hostName] = NetworkUtilities::getIPAddress(vpnConfigData.value(config_key::hostName).toString());
    m_rawConfig.insert(protocolName + "_config_data", vpnConfigData);
    m_rawConfig[config_key::hostName] = NetworkUtilities::getIPAddress(m_rawConfig[config_key::hostName].toString());

    m_impl->activate(m_rawConfig);
    return ErrorCode::NoError;
}

ErrorCode WireguardProtocol::stopMzImpl()
{
    m_impl->deactivate();
    return ErrorCode::NoError;
}


ErrorCode WireguardProtocol::start()
{
    return startMzImpl();
}
