#include "indipanel.h"

IndiPanel *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    IndiPanel *basemodule = new IndiPanel(name, label, profile, availableModuleLibs);
    return basemodule;
}

IndiPanel::IndiPanel(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)
{
    setClassName(QString(metaObject()->className()).toLower());
    setOstPropertyValue("moduleDescription", "Full indi control panel", true);
    setOstPropertyValue("moduleName", "indipanel", true);
    setOstPropertyValue("moduleVersion", 0.11, true);

    connectIndi();

    //setModuleLabel(label);
    setModuleDescription("Full indi control panel");
    setModuleVersion("0.1");


}

IndiPanel::~IndiPanel()
{

}
void IndiPanel::newDevice(INDI::BaseDevice dp)
{
    auto props = dp.getProperties();
    //BOOST_LOG_TRIVIAL(debug) << "Indipanel new device" << dp->getDeviceName();

    for (auto pProperty : props)
    {
        QString dev = pProperty.getDeviceName();
        QString pro = pProperty.getName();
        QString devpro = dev + pro;
        //BOOST_LOG_TRIVIAL(debug) << "Indipanel new property " << devpro.toStdString();
        QString mess;
        if (!createOstProperty(devpro, pProperty.getLabel(), pProperty.getPermission(), pProperty.getDeviceName(),
                               pProperty.getGroupName()))
        {
            sendMessage("Indipanel can't create property ");
        }
    }
}
void IndiPanel::removeDevice(INDI::BaseDevice dp)
{
    QString dev = dp.getDeviceName();
    QVariantMap props = getProperties();
    for(QVariantMap::const_iterator prop = props.begin(); prop != props.end(); ++prop)
    {
        if (prop.value().toMap()["devcat"] == dev)
        {
            //BOOST_LOG_TRIVIAL(debug) << "indi remove property " << prop.key().toStdString();
            deleteOstProperty(prop.key());
        }
    }
}
void IndiPanel::newProperty(INDI::Property pProperty)
{
    QString dev = pProperty.getDeviceName();
    QString pro = pProperty.getName();
    QString devpro = dev + pro;
    //sendMessage("Indipanel new property " + devpro);
    QString mess;
    if (!createOstProperty(devpro, pProperty.getLabel(), pProperty.getPermission(), pProperty.getDeviceName(),
                           pProperty.getGroupName()))
    {
        sendMessage("Indipanel can't create property ");
    }
    setOstPropertyAttribute(devpro, "indi", pProperty.getType(), false);

    switch (pProperty.getType())
    {

        case INDI_NUMBER:
        {
            INDI::PropertyNumber n = pProperty;

            if ( (strcmp(pProperty.getName(), "CCD_EXPOSURE") == 0) ||
                    (strcmp(pProperty.getName(), "GUIDER_EXPOSURE") == 0) )
            {
                INDI::BaseDevice wdp = getDevice(pProperty.getDeviceName());
                if (wdp.getDriverInterface() & INDI::BaseDevice::CCD_INTERFACE)
                {
                    //BOOST_LOG_TRIVIAL(debug) << "Setting blob mode for " << _wdp->getDeviceName();
                    setBLOBMode(B_ALSO, wdp.getDeviceName(), nullptr);
                }

            }
            for (unsigned int i = 0; i < n.count(); i++)
            {
                createOstElement(devpro, n[i].name, n[i].label, false);
                setOstElementValue(devpro, n[i].name, n[i].getValue(), false);
                setOstElementAttribute(devpro, n[i].name, "min", n[i].min, false);
                setOstElementAttribute(devpro, n[i].name, "max", n[i].max, false);
                setOstElementAttribute(devpro, n[i].name, "step", n[i].step, false);
                setOstElementAttribute(devpro, n[i].name, "format", n[i].format, i == n.count() - 1);
                //setOstElementAttribute(devpro, n[i].name, "aux0"  , n[i].aux0,false);
                //setOstElementAttribute(devpro, n[i].name, "aux1"  , n[i].aux1,false);
            }
            break;
        }
        case INDI_SWITCH:
        {
            INDI::PropertySwitch s = pProperty;
            for (unsigned int i = 0; i < s.count(); i++)
            {
                createOstElement(devpro, s[i].name, s[i].label, false);
                if (s[i].s == 0) setOstElementValue(devpro, s[i].name, false, i == s.count() - 1);
                if (s[i].s == 1) setOstElementValue(devpro, s[i].name, true, i == s.count() - 1);
                //setOstElementAttribute(devpro,vp->sp[i].name,"aux0",vp->sp[i].aux,false);
            }
            setOstPropertyAttribute(devpro, "rule", s.getRule(), false);
            break;
        }
        case INDI_TEXT:
        {
            INDI::PropertyText t = pProperty;
            for (unsigned int i = 0; i < t.count(); i++)
            {
                createOstElement(devpro, t[i].name, t[i].label,  i == t.count() - 1);
                setOstElementValue(devpro, t[i].name, t[i].text, i == t.count() - 1);
                //setOstElementAttribute(devpro,vp->tp[i].name,"aux0",vp->tp[i].aux0,false);
                //setOstElementAttribute(devpro,vp->tp[i].name,"aux1",vp->tp[i].aux1,false);
            }
            break;
        }
        case INDI_LIGHT:
        {
            INDI::PropertyLight l = pProperty;
            for (unsigned int i = 0; i < l.count(); i++)
            {
                createOstElement(devpro, l[i].name, l[i].label, i == l.count() - 1);
                setOstElementValue(devpro, l[i].name, l[i].s, i == l.count() - 1);
                //setOstElementAttribute(devpro,vp->lp[i].name,"aux0",vp->lp[i].aux,false);
            }
            break;
        }
        case INDI_BLOB:
        {

            break;
        }
        case INDI_UNKNOWN:
        {
            break;
        }
    }
    setOstPropertyAttribute(devpro, "status", pProperty.getState(), true);
    //emitPropertyCreation(devpro);


}
void IndiPanel::updateProperty (INDI::Property property)
{
    QString dev = property.getDeviceName();
    QString pro = property.getName();
    QString devpro = dev + pro;
    switch (property.getType())
    {

        case INDI_NUMBER:
        {
            INDI::PropertyNumber n = property;

            for (unsigned int i = 0; i < n.count(); i++)
            {
                setOstElementValue(devpro, n[i].name, n[i].value, false);
                setOstElementAttribute(devpro, n[i].name, "min", n[i].min, false);
                setOstElementAttribute(devpro, n[i].name, "max", n[i].max, false);
                setOstElementAttribute(devpro, n[i].name, "step", n[i].step, false);
                setOstElementAttribute(devpro, n[i].name, "format", n[i].format, false);
            }
            break;
        }
        case INDI_SWITCH:
        {
            INDI::PropertySwitch s = property;
            for (unsigned int i = 0; i < s.count(); i++)
            {
                if (s[i].s == 0) setOstElementValue(devpro, s[i].name, false, i == s.count() - 1);
                if (s[i].s == 1) setOstElementValue(devpro, s[i].name, true, i == s.count() - 1);
            }
            break;
        }
        case INDI_TEXT:
        {
            INDI::PropertyText t = property;
            for (unsigned int i = 0; i < t.count(); i++)
            {
                setOstElementValue(devpro, t[i].name, t[i].text, i == t.count() - 1);
            }
            break;
        }
        case INDI_LIGHT:
        {
            INDI::PropertyLight l = property;
            for (unsigned int i = 0; i < l.count(); i++)
            {
                setOstElementValue(devpro, l[i].name, l[i].s, i == l.count() - 1);
            }
            break;
        }
        case INDI_BLOB:
        {

            break;
        }
        case INDI_UNKNOWN:
        {
            break;
        }
    }
    setOstPropertyAttribute(devpro, "status", property.getState(), true);
}


void IndiPanel::removeProperty(INDI::Property property)
{
    QString dev = property.getDeviceName();
    QString pro = property.getName();
    QString devpro = dev + pro;
    //BOOST_LOG_TRIVIAL(debug) << "indi remove property " << devpro.toStdString();
    deleteOstProperty(devpro);
}

void IndiPanel::newBLOB(IBLOB bp)
{
    Q_UNUSED(bp)
}
void IndiPanel::newMessage     (INDI::BaseDevice dp, int messageID)
{
    sendMessage(dp.getDeviceName() + QString::fromStdString(dp.messageQueue(messageID)));
}

void IndiPanel::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                                  const QVariantMap &eventData)
{
    Q_UNUSED(eventType);
    Q_UNUSED(eventModule);
    Q_UNUSED(eventKey);
    //BOOST_LOG_TRIVIAL(debug) << "OnMyExternalEvent - recv : " << getName().toStdString() << "-" << eventType.toStdString() << "-" << eventKey.toStdString();
    foreach(const QString &keyprop, eventData.keys())
    {
        QString prop = keyprop;
        QVariantMap ostprop = getProperties()[keyprop].toMap();
        QString devcat = ostprop["devcat"].toString();
        //BOOST_LOG_TRIVIAL(debug) << "DEVCAT - recv : "  << devcat.toStdString();
        prop.replace(devcat, "");
        if (!(devcat == "Indi"))
        {
            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                //BOOST_LOG_TRIVIAL(debug) << "OnMyExternalEvent - recv : " << getName().toStdString() << "-" << eventType.toStdString() << "-" << prop.toStdString() << "-" << keyelt.toStdString() << "-" << eventData[keyprop].toMap()["indi"].toInt();
                //setOstElementValue(keyprop,keyelt,eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"],true);
                if (eventData[keyprop].toMap()["indi"].toInt() == INDI_TEXT)
                {
                    sendMessage("INDI_TEXT");
                    sendModNewText(devcat, prop, keyelt, eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"].toString());
                }
                if (eventData[keyprop].toMap()["indi"].toInt() == INDI_NUMBER)
                {
                    sendMessage("INDI_NUMBER");
                    sendModNewNumber(devcat, prop, keyelt, eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"].toFloat());
                }
                if (eventData[keyprop].toMap()["indi"].toInt() == INDI_SWITCH)
                {
                    sendMessage("INDI_SWITCH");
                    keyelt.toStdString();
                    if ( eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"].toBool()) sendModNewSwitch(devcat, prop,
                                keyelt, ISS_ON);
                    if (!eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"].toBool()) sendModNewSwitch(devcat, prop,
                                keyelt, ISS_OFF);
                }
            }

        }

    }
}


