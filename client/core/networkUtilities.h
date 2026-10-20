#ifndef NETWORKUTILITIES_H
#define NETWORKUTILITIES_H

#include <QRegularExpression>
#include <QRegExp>
#include <QString>
#include <QHostAddress>
#include <QNetworkReply>
#include <QtNetwork/qnetworkinterface.h>

class NetworkUtilities : public QObject
{
    Q_OBJECT
public:
    static QString getIPAddress(const QString &host);
    static QString getStringBetween(const QString &s, const QString &a, const QString &b);
    static bool checkIPv4Format(const QString &ip);
    static bool checkIpSubnetFormat(const QString &ip);
    /// Normalizes a user-supplied list of local subnet exclusions.
    ///
    /// Accepts `a.b.c.d`, `a.b.c.d/nn`, `ipv6` and `ipv6/nn` (any case):
    /// - a bare address becomes /32 (IPv4) or /128 (IPv6);
    /// - malformed entries and `/0` (the whole internet is never a local
    ///   subnet) are rejected;
    /// - duplicates collapse (IPv6 compares its canonical form, so
    ///   `FD00::/8` and `fd00::/8` are the same entry);
    /// - the input order is kept.
    ///
    /// When `rejected` is not null, every dropped entry is appended to it (so
    /// a UI can tell the user what it ignored).
    static QStringList normalizeSubnetExclusions(const QStringList &raw, QStringList *rejected = nullptr);
    /// True when the entry is a subnet/address `normalizeSubnetExclusions`
    /// would keep.
    static bool isValidSubnetExclusion(const QString &entry);
    static bool checkIpv6Enabled();
    static QPair<QString, QNetworkInterface> getGatewayAndIface();
    // Returns the Interface Index that could Route to dst
    static int AdapterIndexTo(const QHostAddress& dst);

    static QRegularExpression ipAddressRegExp();
    static QRegularExpression ipAddressPortRegExp();
    static QRegExp ipAddressWithSubnetRegExp();
    static QRegExp ipNetwork24RegExp();
    static QRegExp ipPortRegExp();
    static QRegExp domainRegExp();

    static QString netMaskFromIpWithSubnet(const QString ip);
    static QString ipAddressFromIpWithSubnet(const QString ip);
    static QStringList summarizeRoutes(const QStringList &ips, const QString cidr);
};

#endif // NETWORKUTILITIES_H
