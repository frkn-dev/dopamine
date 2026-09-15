#ifndef WIREGUARDPROTOCOL_H
#define WIREGUARDPROTOCOL_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryFile>
#include <QTimer>

#include "vpnprotocol.h"

#include "mozilla/controllerimpl.h"

class WireguardProtocol : public VpnProtocol
{
    Q_OBJECT

public:
    explicit WireguardProtocol(const QJsonObject& configuration, QObject* parent = nullptr);
    virtual ~WireguardProtocol() override;

    ErrorCode start() override;
    void stop() override;

    ErrorCode startMzImpl();
    ErrorCode stopMzImpl();

private:
    QTimer m_statsTimer;

    // the desktop daemon reports CUMULATIVE rx/tx (uapi counters) — keep the
    // previous sample to emit per-interval deltas, matching the iOS/Android
    // bytesChanged contract (and the live speed meter that divides by elapsed)
    quint64 m_lastRxBytes = 0;
    quint64 m_lastTxBytes = 0;

    QScopedPointer<ControllerImpl> m_impl;
};

#endif // WIREGUARDPROTOCOL_H
