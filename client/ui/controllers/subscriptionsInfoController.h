#ifndef SUBSCRIPTIONSINFOCONTROLLER_H
#define SUBSCRIPTIONSINFOCONTROLLER_H

#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QVariantList>

#include <memory>

class ServersModel;
class Settings;

// Settings → Subscriptions: every subscription this device is linked to.
//
// The ids come from ServersModel (the peers `peerSubscriptionId` and the v1
// `api_config.auth_data.api_key`), the details come from the api: the peers
// plane (`POST /v1/locations`) for shard subscriptions, `POST /v1/account_info`
// for the old v1 ones, and the plain `GET /short?subscription_id=...` for the
// share code.
//
// Nothing blocks the UI thread: the ids are collected on the GUI thread, the
// network work runs in QtConcurrent (with its own network manager, created
// inside the worker) and only the finished rows are applied back here.
class SubscriptionsInfoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList subscriptions READ subscriptions NOTIFY subscriptionsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit SubscriptionsInfoController(QSharedPointer<ServersModel> serversModel,
                                         std::shared_ptr<Settings> settings, QObject *parent = nullptr);

    QVariantList subscriptions() const { return m_subscriptions; }
    bool busy() const { return m_busy; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void copyToClipboard(const QString &text);

signals:
    void subscriptionsChanged();
    void busyChanged();

private:
    void setBusy(bool busy);
    void applyRows(const QVariantList &rows);

    QSharedPointer<ServersModel> m_serversModel;
    std::shared_ptr<Settings> m_settings;

    QVariantList m_subscriptions;
    bool m_busy = false;
};

#endif // SUBSCRIPTIONSINFOCONTROLLER_H
