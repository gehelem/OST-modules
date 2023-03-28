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

    connectIndi();
    connectAllDevices();
    qDebug() << "starting with " <<  getOstElementGrid("selection", "dpv");

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
            if (pEventType == "Fldelete" && keyprop == "selection")
            {
                double line = pEventData[keyprop].toMap()["line"].toDouble();
                QString id = pEventData["selection"].toMap()["dpv"].toString();
                deleteOstPropertyLine(keyprop, line);
                deleteOstProperty(id);
            }
            if (pEventType == "Flcreate" && keyprop == "selection")
            {
                newOstPropertyLine(keyprop, pEventData);
                QString id = getOstElementLineValue(keyprop, "dpv", getOstElementGrid(keyprop, "dpv").size() - 1).toString();
                QString lab = getOstElementLov("selection", "dpv", id).toString();
                createOstProperty(id, lab, 0, "Measures", "");
                createOstElement(id, "time", "Time", false);
                createOstElement(id, id, lab, false);
                QVariantMap gdy;
                gdy["D"] = "time";
                gdy["Y"] = id;
                setOstPropertyAttribute(id, "grid", QVariantList(), false);
                setOstPropertyAttribute(id, "GDY", gdy, true);
            }
        }

    }
}

void Meteo::updateProperty(INDI::Property property)
{
    if (property.getType() == INDI_NUMBER)
    {
        INDI::PropertyNumber n = property;
        for (unsigned int i = 0; i < n.count(); i++)
        {
            QString elt = QString(n.getDeviceName()) +  "-" + n.getName() + "-" + n[i].getName();
            if (!mAvailableMeasures.contains(elt))
            {
                QString lab = QString(n.getDeviceName()) + "-" +  n.getLabel() + "-" + n[i].getLabel();
                mAvailableMeasures[elt] = lab;
                addOstElementLov("selection", "dpv", elt, lab);
                sendMessage(lab);
            }
            if ( getOstElementGrid("selection", "dpv").contains(elt))
            {
                double tt = QDateTime::currentDateTime().toMSecsSinceEpoch();
                setOstElementValue(elt, "time", tt, false);
                setOstElementValue(elt, elt,  n[i].getValue(), false);
                pushOstElements(elt);
            }
        }
    }

}
void Meteo::initIndi()
{
    connectIndi();
}
