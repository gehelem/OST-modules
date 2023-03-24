#include "navigator.h"
#include <QPainter>

Navigator *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    Navigator *basemodule = new Navigator(name, label, profile, availableModuleLibs);
    return basemodule;
}

Navigator::Navigator(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)

{

    loadOstPropertiesFromFile(":navigator.json");
    setClassName(QString(metaObject()->className()).toLower());

    setModuleDescription("Navigator module - work in progress");
    setModuleVersion("0.1");

    createOstElement("devices", "camera", "Camera", false);
    setOstElementValue("devices", "camera",   mCamera, false);
    createOstElement("devices", "mount", "Mount", false);
    setOstElementValue("devices", "mount",   mMount, true);


}

Navigator::~Navigator()
{

}
void Navigator::OnMyExternalEvent(const QString &pEventType, const QString  &pEventModule, const QString  &pEventKey,
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
                    if ((keyelt == "camera") || (keyelt == "mount"))
                    {
                        if (setOstElementValue(keyprop, keyelt, pEventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"], false))
                        {
                            setOstPropertyAttribute(keyprop, "status", IPS_OK, true);
                        }
                    }
                }

                if (keyprop == "search")
                {
                    if (keyelt == "searchbtn")
                    {
                        if (setOstElementValue(keyprop, keyelt, true, true))
                        {
                            sendMessage("Searching " + getOstPropertyValue("search").toString());
                        }
                    }
                }
            }
        }
    }
}

void Navigator::newBLOB(INDI::PropertyBlob pblob)
{

    if (
        (QString(pblob.getDeviceName()) == getOstElementValue("devices", "camera").toString()) && (mState != "idle")
    )
    {
        setOstPropertyAttribute("actions", "status", IPS_OK, true);
        delete pImage;
        pImage = new fileio();
        pImage->loadBlob(pblob);
        mStats = pImage->getStats();
        mSolver.ResetSolver(mStats, pImage->getImageBuffer());
        connect(&mSolver, &Solver::successSEP, this, &Navigator::OnSucessSEP);
        mSolver.FindStars(mSolver.stellarSolverProfiles[0]);
    }



}

void Navigator::updateProperty(INDI::Property property)
{
    if (mState == "idle") return;

    if (strcmp(property.getName(), "CCD1") == 0)
    {
        newBLOB(property);
    }
    if (
        (property.getDeviceName() == getOstElementValue("devices", "camera").toString())
        &&  (property.getState() == IPS_ALERT)
    )
    {
        sendWarning("cameraAlert");
        emit cameraAlert();
    }


    if (
        (property.getDeviceName() == getOstElementValue("devices", "camera").toString())
        &&  (property.getName()   == std::string("CCD_FRAME_RESET"))
        &&  (property.getState() == IPS_OK)
    )
    {
        //sendMessage("FrameResetDone");
        emit FrameResetDone();
    }
}
void Navigator::Shoot()
{
    if (connectDevice(getOstElementValue("devices", "camera").toString()))
    {
        frameReset(getOstElementValue("devices", "camera").toString());
        sendModNewNumber(getOstElementValue("devices", "camera").toString(), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE",
                         getOstElementValue("parameters", "exposure").toFloat());
        setOstPropertyAttribute("actions", "status", IPS_BUSY, true);
    }
    else
    {
        setOstPropertyAttribute("actions", "status", IPS_ALERT, true);
    }
}
void Navigator::initIndi()
{
    connectIndi();
    connectDevice(getOstElementValue("devices", "camera").toString());
    connectDevice(getOstElementValue("devices", "mount").toString());
    setBLOBMode(B_ALSO, getOstElementValue("devices", "camera").toString().toStdString().c_str(), nullptr);
    sendModNewNumber(getOstElementValue("devices", "camera").toString(), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
    enableDirectBlobAccess(getOstElementValue("devices", "camera").toString().toStdString().c_str(), nullptr);

}

void Navigator::OnSucessSEP()
{
    setOstPropertyAttribute("actions", "status", IPS_OK, true);
    setOstElementValue("imagevalues", "imgHFR", mSolver.HFRavg, false);
    setOstElementValue("imagevalues", "starscount", mSolver.stars.size(), true);

    QList<fileio::Record> rec = pImage->getRecords();
    QImage rawImage = pImage->getRawQImage();
    QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
    im.setColorTable(rawImage.colorTable());

    im.save(getWebroot()  + "/" + getModuleName() + ".jpeg", "JPG", 100);
    setOstPropertyAttribute("image", "URL", getModuleName() + ".jpeg", true);

    emit FindStarsDone();
}
