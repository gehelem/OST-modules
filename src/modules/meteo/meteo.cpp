#include "meteo.h"

Meteo *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    Meteo *basemodule = new Meteo(name, label, profile, availableModuleLibs);
    return basemodule;
}

Meteo::Meteo(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)

{

    loadOstPropertiesFromFile(":meteo.json");
    setClassName(QString(metaObject()->className()).toLower());

    setModuleDescription("Meteo module - work in progress");
    setModuleVersion("0.1");

    createOstElement("devices", "device", "Meteo device", false);
    setOstElementValue("devices", "device",   mDevice, false);
    connectIndi();
    connectAllDevices();

}

Meteo::~Meteo()
{

}
void Meteo::OnMyExternalEvent(const QString &pEventType, const QString  &pEventModule, const QString  &pEventKey,
                              const QVariantMap &pEventData)
{
    //sendMessage("OnMyExternalEvent - recv : " + getModuleName() + "-" + eventType + "-" + eventKey);
    Q_UNUSED(pEventType);
    Q_UNUSED(pEventKey);

    if (getModuleName() == pEventModule)
    {
        foreach(const QString &keyprop, pEventData.keys())
        {
            if (pEventData[keyprop].toMap().contains("value"))
            {
                QVariant val = pEventData[keyprop].toMap()["value"];
                setOstPropertyValue(keyprop, val, true);
            }

            foreach(const QString &keyelt, pEventData[keyprop].toMap()["elements"].toMap().keys())
            {
                if (keyprop == "devices")
                {
                    if (keyelt == "device")
                    {
                        if (setOstElementValue(keyprop, keyelt, pEventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"], false))
                        {
                            setOstPropertyAttribute(keyprop, "status", IPS_OK, true);
                        }
                    }
                }


            }
        }

    }
}

void Meteo::updateProperty(INDI::Property property)
{
    //if (mState == "idle") return;
    if (getOstElementValue("devices", "device").toString() == property.getDeviceName())
    {
        sendMessage(QString(property.getDeviceName()) + "-" + property.getName());

    }

}
void Meteo::initIndi()
{
    connectIndi();
    connectDevice(getOstElementValue("devices", "device").toString());

}
