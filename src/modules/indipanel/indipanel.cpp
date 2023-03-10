#include "indipanel.h"

IndiPanel *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    IndiPanel *basemodule = new IndiPanel(name, label, profile, availableModuleLibs);
    return basemodule;
}

IndiPanel::IndiPanel(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)
{
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
void IndiPanel::newDevice(INDI::BaseDevice *dp)
{
    auto props = dp->getProperties();
    BOOST_LOG_TRIVIAL(debug) << "Indipanel new device" << dp->getDeviceName();

    for (auto pProperty : props)
    {
        QString dev = pProperty->getDeviceName();
        QString pro = pProperty->getName();
        QString devpro = dev + pro;
        BOOST_LOG_TRIVIAL(debug) << "Indipanel new property " << devpro.toStdString();
        QString mess;
        if (!createOstProperty(devpro, pProperty->getLabel(), pProperty->getPermission(), pProperty->getDeviceName(),
                               pProperty->getGroupName()))
        {
            BOOST_LOG_TRIVIAL(debug) << "Indipanel can't create property" << mess.toStdString();
        }
    }
}
void IndiPanel::removeDevice(INDI::BaseDevice *dp)
{
    QString dev = dp->getDeviceName();
    QVariantMap props = getProperties();
    for(QVariantMap::const_iterator prop = props.begin(); prop != props.end(); ++prop)
    {
        if (prop.value().toMap()["devcat"] == dev)
        {
            BOOST_LOG_TRIVIAL(debug) << "indi remove property " << prop.key().toStdString();
            deleteOstProperty(prop.key());
        }
    }
}
void IndiPanel::newProperty(INDI::Property *pProperty)
{
    QString dev = pProperty->getDeviceName();
    QString pro = pProperty->getName();
    QString devpro = dev + pro;
    //BOOST_LOG_TRIVIAL(debug) << "Indipanel new property " << devpro.toStdString();
    QString mess;
    if (!createOstProperty(devpro, pProperty->getLabel(), pProperty->getPermission(), pProperty->getDeviceName(),
                           pProperty->getGroupName()))
    {
        BOOST_LOG_TRIVIAL(debug) << "Indipanel can't create property" << mess.toStdString();
    }
    setOstPropertyAttribute(devpro, "indi", pProperty->getType(), false);

    switch (pProperty->getType())
    {

        case INDI_NUMBER:
        {
            if ( (strcmp(pProperty->getName(), "CCD_EXPOSURE") == 0) ||
                    (strcmp(pProperty->getName(), "GUIDER_EXPOSURE") == 0) )
            {
                INDI::BaseDevice *_wdp = getDevice(pProperty->getDeviceName());
                if (_wdp->getDriverInterface() & INDI::BaseDevice::CCD_INTERFACE)
                {
                    BOOST_LOG_TRIVIAL(debug) << "Setting blob mode for " << _wdp->getDeviceName();
                    setBLOBMode(B_ALSO, _wdp->getDeviceName(), nullptr);
                }

            }
            INumberVectorProperty *vp = pProperty->getNumber();
            for (int i = 0; i < vp->nnp; i++)
            {
                createOstElement(devpro, vp->np[i].name, vp->np[i].label, false);
                setOstElementValue(devpro, vp->np[i].name, vp->np[i].value, false);
                setOstElementAttribute(devpro, vp->np[i].name, "min", vp->np[i].min, false);
                setOstElementAttribute(devpro, vp->np[i].name, "max", vp->np[i].max, false);
                setOstElementAttribute(devpro, vp->np[i].name, "step", vp->np[i].step, false);
                setOstElementAttribute(devpro, vp->np[i].name, "format", vp->np[i].format, i == vp->nnp - 1);
                //setOstElementAttribute(devpro,vp->np[i].name,"aux0",vp->np[i].aux0,false);
                //setOstElementAttribute(devpro,vp->np[i].name,"aux1",vp->np[i].aux1,false);
            }
            break;
        }
        case INDI_SWITCH:
        {
            ISwitchVectorProperty *vp = pProperty->getSwitch();
            for (int i = 0; i < vp->nsp; i++)
            {
                createOstElement(devpro, vp->sp[i].name, vp->sp[i].label, false);
                if (vp->sp[i].s == 0) setOstElementValue(devpro, vp->sp[i].name, false, i == vp->nsp - 1);
                if (vp->sp[i].s == 1) setOstElementValue(devpro, vp->sp[i].name, true, i == vp->nsp - 1);
                //setOstElementAttribute(devpro,vp->sp[i].name,"aux0",vp->sp[i].aux,false);
            }
            setOstPropertyAttribute(devpro, "rule", vp->r, false);
            break;
        }
        case INDI_TEXT:
        {
            ITextVectorProperty *vp = pProperty->getText();
            for (int i = 0; i < vp->ntp; i++)
            {
                createOstElement(devpro, vp->tp[i].name, vp->tp[i].label,  i == vp->ntp - 1);
                setOstElementValue(devpro, vp->tp[i].name, vp->tp[i].text, i == vp->ntp - 1);
                //setOstElementAttribute(devpro,vp->tp[i].name,"aux0",vp->tp[i].aux0,false);
                //setOstElementAttribute(devpro,vp->tp[i].name,"aux1",vp->tp[i].aux1,false);
            }
            break;
        }
        case INDI_LIGHT:
        {
            ILightVectorProperty *vp = pProperty->getLight();
            for (int i = 0; i < vp->nlp; i++)
            {
                createOstElement(devpro, vp->lp[i].name, vp->lp[i].label, i == vp->nlp - 1);
                setOstElementValue(devpro, vp->lp[i].name, vp->lp[i].s, i == vp->nlp - 1);
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
    setOstPropertyAttribute(devpro, "status", pProperty->getState(), true);
    //emitPropertyCreation(devpro);


}

void IndiPanel::removeProperty(INDI::Property *property)
{
    QString dev = property->getDeviceName();
    QString pro = property->getName();
    QString devpro = dev + pro;
    BOOST_LOG_TRIVIAL(debug) << "indi remove property " << devpro.toStdString();
    deleteOstProperty(devpro);

    switch (property->getType())
    {

        case INDI_NUMBER:
        {
            break;
        }
        case INDI_SWITCH:
        {
            break;
        }
        case INDI_TEXT:
        {
            break;
        }
        case INDI_LIGHT:
        {
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
}
void IndiPanel::newNumber(INumberVectorProperty *nvp)
{
    QString dev = nvp->device;
    QString pro = nvp->name;
    QString devpro = dev + pro;
    for (int i = 0; i < nvp->nnp; i++)
    {
        setOstElementValue(devpro, nvp->np[i].name, nvp->np[i].value, false);
        setOstElementAttribute(devpro, nvp->np[i].name, "min", nvp->np[i].min, false);
        setOstElementAttribute(devpro, nvp->np[i].name, "max", nvp->np[i].max, false);
        setOstElementAttribute(devpro, nvp->np[i].name, "step", nvp->np[i].step, false);
        setOstElementAttribute(devpro, nvp->np[i].name, "format", nvp->np[i].format, false);
    }
    setOstPropertyAttribute(devpro, "status", nvp->s, true);
}
void IndiPanel::newText(ITextVectorProperty *tvp)
{
    QString dev = tvp->device;
    QString pro = tvp->name;
    QString devpro = dev + pro;
    for (int i = 0; i < tvp->ntp; i++)
    {
        setOstElementValue(devpro, tvp->tp[i].name, tvp->tp[i].text, false);
    }
    setOstPropertyAttribute(devpro, "status", tvp->s, true);
}
void IndiPanel::newLight(ILightVectorProperty *lvp)
{
    QString dev = lvp->device;
    QString pro = lvp->name;
    QString devpro = dev + pro;
    for (int i = 0; i < lvp->nlp; i++)
    {
        setOstElementValue(devpro, lvp->lp[i].name, lvp->lp[i].s, i == lvp->nlp - 1);
    }
    setOstPropertyAttribute(devpro, "status", lvp->s, true);
}
void IndiPanel::newSwitch(ISwitchVectorProperty *svp)
{
    QString dev = svp->device;
    QString pro = svp->name;
    QString devpro = dev + pro;
    for (int i = 0; i < svp->nsp; i++)
    {
        if (svp->sp[i].s == 0) setOstElementValue(devpro, svp->sp[i].name, false, i == svp->nsp - 1);
        if (svp->sp[i].s == 1) setOstElementValue(devpro, svp->sp[i].name, true, i == svp->nsp - 1);
    }
    setOstPropertyAttribute(devpro, "status", svp->s, true);

}
void IndiPanel::newBLOB(IBLOB *bp)
{
    Q_UNUSED(bp)
}
void IndiPanel::newMessage     (INDI::BaseDevice *dp, int messageID)
{
    sendMessage(dp->getDeviceName() + QString::fromStdString(dp->messageQueue(messageID)));
}

void IndiPanel::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                                  const QVariantMap &eventData)
{
    //BOOST_LOG_TRIVIAL(debug) << "OnMyExternalEvent - recv : " << getName().toStdString() << "-" << eventType.toStdString() << "-" << eventKey.toStdString();
    foreach(const QString &keyprop, eventData.keys())
    {
        QString prop = keyprop;
        QVariantMap ostprop = getProperties()[keyprop].toMap();
        QString devcat = ostprop["devcat"].toString();
        BOOST_LOG_TRIVIAL(debug) << "DEVCAT - recv : "  << devcat.toStdString();
        prop.replace(devcat, "");
        if (!(devcat == "Indi"))
        {
            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                //BOOST_LOG_TRIVIAL(debug) << "OnMyExternalEvent - recv : " << getName().toStdString() << "-" << eventType.toStdString() << "-" << prop.toStdString() << "-" << keyelt.toStdString() << "-" << eventData[keyprop].toMap()["indi"].toInt();
                //setOstElementValue(keyprop,keyelt,eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"],true);
                if (eventData[keyprop].toMap()["indi"].toInt() == INDI_TEXT)
                {
                    BOOST_LOG_TRIVIAL(debug) << "INDI_TEXT";
                    sendModNewText(devcat, prop, keyelt, eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"].toString());
                }
                if (eventData[keyprop].toMap()["indi"].toInt() == INDI_NUMBER)
                {
                    BOOST_LOG_TRIVIAL(debug) << "INDI_NUMBER";
                    sendModNewNumber(devcat, prop, keyelt, eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"].toFloat());
                }
                if (eventData[keyprop].toMap()["indi"].toInt() == INDI_SWITCH)
                {
                    BOOST_LOG_TRIVIAL(debug) << "INDI_SWITCH" << devcat.toStdString() << "-" << prop.toStdString() << "-" <<
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


