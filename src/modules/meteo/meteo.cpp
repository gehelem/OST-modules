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

    connect(&mTimer, &QTimer::timeout, this, &Meteo::OnTimer);
    mTimer.start(getOstPropertyValue("timer").toDouble() * 1000);

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
                mTimer.stop();
                mTimer.start(val.toDouble() * 1000);
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
                declareNewGraph(id);
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
            QString propname = QString(n.getDeviceName()) +  "-" + n.getName() + "-" + n[i].getName();
            if (!mAvailableMeasures.contains(propname))
            {
                QString lab = QString(n.getDeviceName()) + "-" +  n.getLabel() + "-" + n[i].getLabel();
                mAvailableMeasures[propname] = lab;
                addOstElementLov("selection", "dpv", propname, lab);
                //sendMessage(lab);
            }
            if ( getOstElementGrid("selection", "dpv").contains(propname))
            {
                double tt = QDateTime::currentDateTime().toMSecsSinceEpoch();
                setOstElementValue(propname, "time", tt, false);
                setOstElementValue(propname, propname,  n[i].getValue(), false);
            }
        }
    }

}
void Meteo::initIndi()
{
    connectIndi();
}
void Meteo::OnTimer()
{
    QVariantList propnames = getOstElementGrid("selection", "dpv");
    for (int i = 0; i < propnames.count(); i++)
    {
        QString propname = propnames[i].toString();
        declareNewGraph(propname);
        if (getProperties().contains(propname))
        {
            double tt = QDateTime::currentDateTime().toMSecsSinceEpoch();
            setOstElementValue(propname, "time", tt, false);
            pushOstElements(propname);
        }

    }


}
void Meteo::declareNewGraph(const QString  &pName)
{
    if (getProperties().contains(pName))
    {
        return;
    }
    if (!mAvailableMeasures.contains(pName))
    {
        sendMessage("Can't follow " + pName + " at the moment");
        return;
    }
    QString lab = mAvailableMeasures[pName];
    createOstProperty(pName, lab, 0, "Measures", "");
    createOstElement(pName, "time", "Time", false);
    setOstElementValue(pName, "time", QDateTime::currentDateTime().toMSecsSinceEpoch(), false);
    createOstElement(pName, pName, lab, false);
    setOstElementValue(pName, pName, 0, false);
    QVariantMap gdy;
    gdy["D"] = "time";
    gdy["Y"] = pName;
    setOstPropertyAttribute(pName, "gridlimit", 1000, false);
    setOstPropertyAttribute(pName, "grid", QVariantList(), false);
    setOstPropertyAttribute(pName, "GDY", gdy, true);

}
