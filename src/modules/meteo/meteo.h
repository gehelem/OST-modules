#ifndef METEO_MODULE_h_
#define METEO_MODULE_h_
#include <indimodule.h>

#if defined(METEO_MODULE)
#  define MODULE_INIT Q_DECL_EXPORT
#else
#  define MODULE_INIT Q_DECL_IMPORT
#endif

class MODULE_INIT Meteo : public IndiModule
{
        Q_OBJECT

    public:
        Meteo(QString name, QString label, QString profile, QVariantMap availableModuleLibs);
        ~Meteo();

    signals:

    public slots:
        void OnMyExternalEvent(const QString &pEventType, const QString  &pEventModule, const QString  &pEventKey,
                               const QVariantMap &pEventData) override;

    private:
        void updateProperty(INDI::Property property) override;

        void initIndi(void);

        QString mDevice  = "OpenWeatherMap";
        QString mState = "idle";



};

extern "C" MODULE_INIT Meteo *initialize(QString name, QString label, QString profile,
        QVariantMap availableModuleLibs);

#endif
