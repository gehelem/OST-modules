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
    setModuleDescription("Full indi control panel");
    setModuleVersion("0.11");
    connectIndi();
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

    foreach(const QString &key, getStore().keys())
    {
        delete getStore()[key]; // this is wrong, we should test level1/devcat before deletion
    }

}
void IndiPanel::newProperty(INDI::Property pProperty)
{
    QString dev = pProperty.getDeviceName();
    QString pro = pProperty.getName();
    QString devpro = dev + pro;
    //sendMessage("Indipanel new property " + devpro);
    QString mess;

    OST::PropertyMulti* p = new OST::PropertyMulti(devpro, pProperty.getLabel(),
            OST::IntToPermission(pProperty.getPermission()),
            pProperty.getDeviceName(),
            pProperty.getGroupName(), "", false, false);


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
                OST::ValueFloat* v = new OST::ValueFloat(n[i].label, QString(i), n[i].label);
                v->setValue(n[i].getValue(), false);
                v->setMin(n[i].min);
                v->setMax(n[i].max);
                v->setStep(n[i].step);
                v->setFormat(n[i].format);
                p->addValue(n[i].getName(), v);
            }
            break;
        }
        case INDI_SWITCH:
        {
            INDI::PropertySwitch s = pProperty;
            for (unsigned int i = 0; i < s.count(); i++)
            {
                OST::ValueBool* v = new OST::ValueBool(s[i].label, QString(i), s[i].label);
                if (s[i].s == 0) v->setValue(false, false);
                if (s[i].s == 1) v->setValue(false, true);
                p->addValue(s[i].getName(), v);
            }
            p->setRule(OST::IntToRule(s.getRule()));

            break;
        }
        case INDI_TEXT:
        {
            INDI::PropertyText t = pProperty;
            for (unsigned int i = 0; i < t.count(); i++)
            {
                OST::ValueString* v = new OST::ValueString(t[i].label, QString(i), t[i].label);
                v->setValue(t[i].text, false);
                p->addValue(t[i].getName(), v);
            }
            break;
        }
        case INDI_LIGHT:
        {
            INDI::PropertyLight l = pProperty;
            for (unsigned int i = 0; i < l.count(); i++)
            {
                OST::ValueLight* v = new OST::ValueLight(l[i].label, QString(i), l[i].label);
                v->setValue(OST::IntToState(l[i].getState()), true);
                p->addValue(l[i].getName(), v);
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
    p->setState(OST::IntToState(pProperty.getState()));
    createProperty(devpro, p);


}
void IndiPanel::updateProperty (INDI::Property property)
{
    QString dev = property.getDeviceName();
    QString pro = property.getName();
    QString devpro = dev + pro;
    OST::PropertyMulti* p = getProperty(devpro);
    switch (property.getType())
    {

        case INDI_NUMBER:
        {
            INDI::PropertyNumber n = property;

            for (unsigned int i = 0; i < n.count(); i++)
            {
                getValueFloat(devpro, n[i].name)->setMin(n[i].min);
                getValueFloat(devpro, n[i].name)->setMax(n[i].max);
                getValueFloat(devpro, n[i].name)->setStep(n[i].step);
                getValueFloat(devpro, n[i].name)->setFormat(n[i].format);
                getValueFloat(devpro, n[i].name)->setValue(n[i].value, i == n.count() - 1);
            }
            break;
        }
        case INDI_SWITCH:
        {
            INDI::PropertySwitch s = property;
            for (unsigned int i = 0; i < s.count(); i++)
            {
                if (s[i].s == 0) getValueBool(devpro, s[i].name)->setValue(false, i == s.count() - 1);
                if (s[i].s == 1) getValueBool(devpro, s[i].name)->setValue(true, i == s.count() - 1);
            }
            break;
        }
        case INDI_TEXT:
        {
            INDI::PropertyText t = property;
            for (unsigned int i = 0; i < t.count(); i++)
            {
                getValueString(devpro, t[i].name)->setValue(t[i].text, i == t.count() - 1);
            }
            break;
        }
        case INDI_LIGHT:
        {
            INDI::PropertyLight l = property;
            for (unsigned int i = 0; i < l.count(); i++)
            {
                getValueLight(devpro, l[i].name)->setValue(OST::IntToState(l[i].getState()), true);
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
    p->setState(OST::IntToState(property.getState()));
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
    QVariantMap m = getPropertiesDump().toVariantMap();
    foreach(const QString &keyprop, eventData.keys())
    {
        if (!m.contains(keyprop))
        {
            sendError ("OnMyExternalEvent - property " + keyprop + " does not exist");
            return;
        }
        QString prop = keyprop;
        QVariantMap ostprop = m[keyprop].toMap();
        QString devcat = ostprop["level1"].toString();
        //BOOST_LOG_TRIVIAL(debug) << "DEVCAT - recv : "  << devcat.toStdString();
        prop.replace(devcat, "");
        if (!(devcat == "Indi"))
        {
            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                if (!m[keyprop].toMap()["elements"].toMap().contains(keyelt))
                {
                    sendError ("OnMyExternalEvent - property " + keyprop + ", element " + keyelt + " does not exist");
                    return;
                }
                if (getStore()[keyprop]->getValue(keyelt)->getType() == "string")
                {
                    sendModNewText(devcat, prop, keyelt, eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"].toString());
                }
                if (getStore()[keyprop]->getValue(keyelt)->getType() == "int"
                        || getStore()[keyprop]->getValue(keyelt)->getType() == "float")
                {
                    sendModNewNumber(devcat, prop, keyelt, eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"].toFloat());
                }
                if (getStore()[keyprop]->getValue(keyelt)->getType() == "bool")
                {
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


