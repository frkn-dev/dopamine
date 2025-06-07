#pragma once

#include "ui/controllers/frkn/configController.h"
#include "ui/controllers/frkn/frknApiController.h"
#include "ui/bip39_helper.h"
#include <qobject.h>
#include <qtmetamacros.h>

class AmneziaApplication;

namespace frkn {

class ApplicationDelegate : public QObject
{
    Q_OBJECT
public:
    ApplicationDelegate(AmneziaApplication *app, QObject *parent = nullptr);

    void registerTypes();
    void initControllers();
    void updateConfigs();

private:
    AmneziaApplication *m_app;
    QScopedPointer<Bip39Helper> m_bip39Helper;
    QScopedPointer<frkn::FrknApiController> m_frknApiController;
    QScopedPointer<frkn::ConfigController> m_frknConfigController;
};

}
