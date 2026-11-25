#ifndef DEVICEIDENTITY_H
#define DEVICEIDENTITY_H

#include <QString>

// Identity of this installation.
//
// `deviceUid` is what the api knows a device by: a random uuid kept in the
// Keychain, so a reinstall on iOS keeps the same device instead of burning a new
// device slot of the subscription. It is never derived from a hardware id.
class DeviceIdentity
{
public:
    static QString deviceUid();
    static QString deviceName();
    static void setDeviceName(const QString &name);

    // WireGuard identity of this installation, for the peers plane: the api
    // assigns an address to this key, and every peer of the device is tied to
    // it. Kept in the Keychain-backed store, because a regenerated key makes
    // every existing peer unusable while still counting against the limits.
    // The private half never leaves the device: only the public key is sent.
    static QString privateKey();
    static QString publicKey();
};

#endif // DEVICEIDENTITY_H
