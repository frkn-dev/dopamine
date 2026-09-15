#ifndef KEYACTIVATIONCONTROLLER_H
#define KEYACTIVATIONCONTROLLER_H

#include <QObject>

class KeyActivationController : public QObject
{
    Q_OBJECT
public:
    explicit KeyActivationController(QObject *parent = nullptr);

public slots:
    void validateKey(const QString &code);
    void activateKey(const QString &code, const QString &email = QString());

signals:
    // key already activated and linked to a subscription (e.g. app reinstall) —
    // proceed to import that subscription right away
    void keyAlreadyLinked(const QString &subscriptionId);
    // key is valid but not activated yet — ask the user to confirm activation
    void keyValidationPassed(const QString &code, int days, int trafficGib, bool isLite);
    void keyActivated(const QString &subscriptionId);
    // the server asked for an email on activation (lite keys) — show the email input and retry
    void emailRequired();
    void keyErrorOccurred(const QString &message);
};

#endif // KEYACTIVATIONCONTROLLER_H
