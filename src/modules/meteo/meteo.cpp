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
    OST::ValueInt* i = new OST::ValueInt("Refresh interval (s)", "0", "");
    i->setValue(5, false);
    i->setDirectEdit(true);
    i->setAutoUpdate(true);
    getProperty("parms")->addValue("interval", i);
    i = new OST::ValueInt("Keep x value", "0", "");
    i->setValue(10, false);
    i->setDirectEdit(true);
    i->setAutoUpdate(true);
    getProperty("parms")->addValue("histo", i);
    connect(&mTimer, &QTimer::timeout, this, &Meteo::OnTimer);
    mTimer.start(getInt("parms", "interval") * 1000);

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
            foreach(const QString &keyelt, pEventData[keyprop].toMap()["elements"].toMap().keys())
            {
                if (keyprop == "parms")
                {
                    if (keyelt == "interval")
                    {
                        QVariant val = pEventData[keyprop].toMap()[keyelt];
                        mTimer.stop();
                        mTimer.start(val.toDouble() * 1000);
                        getValueInt("parms", "interval")->setValue(val.toInt(), true);
                    }
                }


            }
            if (pEventType == "Fldelete" && keyprop == "selection")
            {
                double line = pEventData[keyprop].toMap()["line"].toDouble();
                QString id = pEventData["selection"].toMap()["dpv"].toString();
                getStore()[keyprop]->deleteLine(line);
                deleteOstProperty(id);
            }
            if (pEventType == "Flcreate" && keyprop == "selection")
            {
                getStore()[keyprop]->newLine(pEventData[keyprop].toMap()["elements"].toMap());
                QString id = getString("selection", "dpv", getOstElementGrid(keyprop, "dpv").size() - 1);
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
                getValueString("selection", "dpv")->lovAdd(propname, lab);
                //sendMessage(lab);
            }

            if ( getOstElementGrid("selection", "dpv").contains(propname))
            {
                declareNewGraph(propname);
                getValueFloat(propname, "time")->setValue(QDateTime::currentDateTime().toMSecsSinceEpoch(), false);
                getValueFloat(propname, propname)->setValue(n[i].getValue(), false);
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
        if (getStore().contains(propname))
        {
            getValueFloat(propname, "time")->setValue(QDateTime::currentDateTime().toMSecsSinceEpoch(), true);
            getProperty(propname)->push();
        }

    }


}
void Meteo::declareNewGraph(const QString  &pName)
{
    if (getStore().contains(pName))
    {
        return;
    }
    if (!mAvailableMeasures.contains(pName))
    {
        sendMessage("Can't follow " + pName + " at the moment");
        return;
    }
    QString lab = mAvailableMeasures[pName];
    OST::PropertyMulti* pm = new OST::PropertyMulti(pName, lab, OST::ReadOnly, "Measures", "", 0, false, true);
    pm->setShowArray(false);
    OST::ValueFloat* t = new OST::ValueFloat("Time", "", "");
    pm->addValue("time", t);
    t = new OST::ValueFloat(pName, "", "");
    pm->addValue(pName, t);
    OST::ValueGraph* g = new OST::ValueGraph("", "", "");
    OST::GraphDefs def;
    def.type = OST::GraphType::DY;
    def.params["D"] = "time";
    def.params["Y"] = pName;
    g->setGraphDefs(def);
    pm->addValue("graph" + pName, g);

    createProperty(pName, pm);


    //setOstPropertyAttribute(pName, "gridlimit", 1000, false);
    //setOstPropertyAttribute(pName, "grid", QVariantList(), false);
    //setOstPropertyAttribute(pName, "GDY", gdy, true);

}
