#ifndef WGJUNKPARAMS_H
#define WGJUNKPARAMS_H

#include <QJsonObject>
#include <QString>

// AWG junk parameters that must be sent around the handshake, otherwise the
// server stays silent. Collected from the stored `last_config` (long and short
// field names) and topped up from the wg-quick INI where fields are empty.
// Shared by the health probe and the server test so the two never disagree
// about which obfuscation a node is configured with.
QJsonObject wgJunkParamsFromConfig(const QJsonObject &lastConfig, const QString &ini);

#endif // WGJUNKPARAMS_H
