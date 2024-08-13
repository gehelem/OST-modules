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

    auto* i = new OST::ElementInt("Refresh interval (s)", "0", "");
    i->setValue(5, false);
    i->setDirectEdit(true);
    i->setAutoUpdate(true);
    getProperty("parms")->addElt("interval", i);
    i = new OST::ElementInt("Keep x values", "0", "");
    i->setValue(10, false);
    i->setDirectEdit(true);
    i->setAutoUpdate(true);
    getProperty("parms")->addElt("histo", i);
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
                        mTimer.stop();
                        mTimer.start(getInt("parms", "interval") * 1000);
                    }
                }
            }

            if (pEventType == "Fldelete" && keyprop == "selection")
            {
                double line = pEventData[keyprop].toMap()["line"].toDouble();
                QString id = getString("selection", "dpv", line);
                getStore()[keyprop]->deleteLine(line);
                deleteOstProperty(id);
            }

            if (pEventType == "Flcreate" && keyprop == "selection")
            {
                getStore()[keyprop]->newLine(pEventData[keyprop].toMap()["elements"].toMap());
                QString id = getString("selection", "dpv", getProperty("selection")->getGrid().size() - 1);
                declareNewGraph(id);
            }

        }

    }
}
void Meteo::newProperty(INDI::Property property)
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
                getEltString("selection", "dpv")->lovAdd(propname, lab);
                //sendMessage(lab);
            }
            QStringList propnames;
            for (int i = 0; i < getProperty("selection")->getGrid().size(); i++)
            {
                propnames.append(getString("selection", "dpv", i));
            }
            if ( propnames.contains(propname))
            {
                declareNewGraph(propname);
                getEltFloat(propname, "time")->setValue(QDateTime::currentDateTime().toMSecsSinceEpoch(), false);
                getEltFloat(propname, propname)->setValue(n[i].getValue(), false);
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
            QStringList propnames;
            for (int i = 0; i < getProperty("selection")->getGrid().size(); i++)
            {
                propnames.append(getString("selection", "dpv", i));
            }
            if ( propnames.contains(propname))
            {
                declareNewGraph(propname);
                getProperty(propname)->setGridLimit(getInt("parms", "histo"));
                getEltFloat(propname, "time")->setValue(QDateTime::currentDateTime().toMSecsSinceEpoch(), false);
                getEltFloat(propname, propname)->setValue(n[i].getValue(), false);
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
    //QVariantList propnames = getOstElementGrid("selection", "dpv");
    QStringList propnames;

    for (int i = 0; i < getProperty("selection")->getGrid().size(); i++)
    {
        propnames.append(getString("selection", "dpv", i));
    }
    for (int i = 0; i < propnames.count(); i++)
    {
        QString propname = propnames[i];
        declareNewGraph(propname);
        if (getStore().contains(propname))
        {
            getProperty(propname)->setGridLimit(getInt("parms", "histo"));
            getEltFloat(propname, "time")->setValue(QDateTime::currentDateTime().toMSecsSinceEpoch(), true);
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
    pm->setShowGrid(false);
    pm->setGridLimit(getInt("parms", "histo"));
    pm->setShowElts(false);
    auto* t = new OST::ElementFloat("Time", "", "");
    pm->addElt("time", t);
    t = new OST::ElementFloat(pName, "", "");
    pm->addElt(pName, t);

    OST::GraphDefs def;
    def.type = OST::GraphType::DY;
    def.params["D"] = "time";
    def.params["Y"] = pName;
    pm->setGraphDefs(def);

    createProperty(pName, pm);

}
