/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SYSTEMTRAY_NOTIFICATIONHANDLER_H
#define SYSTEMTRAY_NOTIFICATIONHANDLER_H

#include "notificationhandler.h"

#include <QMenu>
#include <QSystemTrayIcon>
#include <QTimer>

class SystemTrayNotificationHandler : public NotificationHandler {
    Q_OBJECT

public:
    explicit SystemTrayNotificationHandler(QObject* parent);
    ~SystemTrayNotificationHandler();

    void setConnectionState(Vpn::ConnectionState state) override;

    void onTranslationsUpdated() override;

public slots:
    void updateWebsiteUrl(const QString &newWebsiteUrl);
    void setServerName(const QString &serverName);

protected:
    virtual void notify(Message type, const QString& title,
                        const QString& message, int timerMsec) override;

private:
    void showHideWindow();

    void setTrayState(Vpn::ConnectionState state);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

    // templateIcon matters on macOS only: template renders monochrome, non-template keeps colors
    void setTrayIcon(const QString &iconPath, bool templateIcon = true);

private:
    QMenu m_menu;
    QSystemTrayIcon m_systemTrayIcon;

    QAction* m_trayActionShow = nullptr;
    QAction* m_trayActionConnect = nullptr;
    QAction* m_trayActionDisconnect = nullptr;
    QAction* m_trayActionVisitWebSite = nullptr;
    QAction* m_trayActionQuit = nullptr;
    QAction* m_statusLabel = nullptr;
    QAction* m_separator = nullptr;

    QString m_serverName;

    // tray icon states: on Windows colors show as-is; on macOS a template
    // (mask) icon renders monochrome in the system theme, so "connected" is
    // shown as the full-color blue icon instead (mask off), "disconnected"
    // stays a monochrome template
    const QString ConnectedTrayIconName = "active.png";
    const QString DisconnectedTrayIconName = "default.png";
    const QString ErrorTrayIconName = "error.png";

    // blink animation for transitional states (connecting/reconnecting/…)
    QTimer m_blinkTimer;
    bool m_blinkToggle = false;
    void startBlink();
    void stopBlink();

    QString  websiteUrl = "https://frkn.org";
};

#endif  // SYSTEMTRAY_NOTIFICATIONHANDLER_H
