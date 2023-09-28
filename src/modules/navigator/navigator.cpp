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

    defineMeAsNavigator();

    connectIndi();
    connectAllDevices();

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
            foreach(const QString &keyelt, pEventData[keyprop].toMap()["elements"].toMap().keys())
            {
                if (pEventType == "Fposticon")
                {
                    if ((keyprop == "search") && (keyelt == "name"))
                    {
                        updateSearchList();
                    }
                }

                if (keyprop == "popcat")
                {
                    if (keyelt == "go")
                    {
                        if (setOstElementValue(keyprop, keyelt, true, true))
                        {
                            getProperty(keyprop)->setState(OST::Busy);
                            /* we should avoid this, and do this in a different thread. We'll see later */
                            populateCatalog(":" + getString("popcat", "catlg") + ".txt", getString("popcat", "catlg"));
                            getProperty(keyprop)->setState(OST::Ok);
                        }
                    }
                }

                if (keyprop == "actions")
                {
                    getProperty(keyprop)->setState(OST::Busy);
                    if (keyelt == "gototarget")
                    {
                        slewToSelection();
                    }
                }
            }
            if (pEventType == "Flselect")
            {
                double line = pEventData[keyprop].toMap()["line"].toDouble();
                QString code = getValueString("results", "code")->getGrid()[line];
                float ra = getValueFloat("results", "RA")->getGrid()[line];
                float dec = getValueFloat("results", "DEC")->getGrid()[line];
                QString ns = getValueString("results", "NS")->getGrid()[line];
                setOstElementValue("actions", "targetname", code, false);
                setOstElementValue("actions", "targetra", ra, false);
                setOstElementValue("actions", "targetde", dec, false);
                convertSelection();
            }
        }

    }
}

void Navigator::newBLOB(INDI::PropertyBlob pblob)
{

    if (
        (QString(pblob.getDeviceName()) == getString("devices", "navigatorcamera")) && (mState != "idle")
    )
    {
        getProperty("actions")->setState(OST::Ok);
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
    //if (mState == "idle") return;

    if (strcmp(property.getName(), "CCD1") == 0)
    {
        newBLOB(property);
    }
    if (
        (property.getDeviceName() == getString("devices", "navigatorcamera"))
        &&  (property.getState() == IPS_ALERT)
    )
    {
        sendWarning("cameraAlert");
        emit cameraAlert();
    }


    if (
        (property.getDeviceName() == getString("devices", "navigatorcamera"))
        &&  (property.getName()   == std::string("CCD_FRAME_RESET"))
        &&  (property.getState() == IPS_OK)
    )
    {
        //sendMessage("FrameResetDone");
        emit FrameResetDone();
    }
    if (
        (property.getDeviceName() == getString("devices", "navigatormount"))
        &&  (property.getName()   == std::string("EQUATORIAL_EOD_COORD"))
        &&  (property.getState() == IPS_OK)
    )
    {
        sendMessage("Slew finished");
        getProperty("actions")->setState(OST::Ok);
    }
}
void Navigator::Shoot()
{
    if (connectDevice(getString("devices", "navigatorcamera")))
    {
        frameReset(getString("devices", "navigatorcamera"));
        sendModNewNumber(getString("devices", "navigatorcamera"), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE",
                         getFloat("parameters", "exposure"));
        getProperty("actions")->setState(OST::Busy);
    }
    else
    {
        getProperty("actions")->setState(OST::Error);
    }
}
void Navigator::initIndi()
{
    connectIndi();
    connectDevice(getString("devices", "navigatorcamera"));
    connectDevice(getString("devices", "navigatormount"));
    setBLOBMode(B_ALSO, getString("devices", "navigatorcamera").toStdString().c_str(), nullptr);
    sendModNewNumber(getString("devices", "navigatorcamera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
    enableDirectBlobAccess(getString("devices", "navigatorcamera").toStdString().c_str(), nullptr);

}
void Navigator::OnSucessSEP()
{
    getProperty("actions")->setState(OST::Ok);
    setOstElementValue("imagevalues", "imgHFR", mSolver.HFRavg, false);
    setOstElementValue("imagevalues", "starscount", mSolver.stars.size(), true);

    QList<fileio::Record> rec = pImage->getRecords();
    QImage rawImage = pImage->getRawQImage();
    QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
    im.setColorTable(rawImage.colorTable());

    im.save(getWebroot()  + "/" + getModuleName() + ".jpeg", "JPG", 100);
    OST::ImgData dta;
    dta.mUrlJpeg = getModuleName() + ".jpeg";
    getValueImg("image", "image1")->setValue(dta, true);

    emit FindStarsDone();
}
void Navigator::updateSearchList(void)
{
    sendMessage("Searching " + getString("search", "name"));
    getProperty("results")->clearGrid();
    QList<catalogResult> results;
    searchCatalog(getString("search", "name"), results);
    if (results.count() == 0)
    {
        sendWarning("Searching " + getString("search", "name") + " gives no result");
        return;
    }

    int max = 20;
    if (max < results.count())
    {
        sendWarning("updateSearchList more than " + QString::number(max) + " objects found, limiting result to " + QString::number(
                        max));
    }
    else
    {
        max = results.count();
    }

    for (int i = 0; i < max; i++)
    {
        setOstElementValue("results", "catalog", results[i].catalog, false);
        setOstElementValue("results", "code", results[i].code, false);
        setOstElementValue("results", "RA", results[i].RA, false);
        setOstElementValue("results", "NS", results[i].NS, false);
        setOstElementValue("results", "DEC", results[i].DEC, false);
        setOstElementValue("results", "diam", results[i].diam, false);
        setOstElementValue("results", "mag", results[i].mag, false);
        setOstElementValue("results", "name", results[i].name, false);
        setOstElementValue("results", "alias", results[i].alias, false);
        getProperty("results")->push();
    }

}
void Navigator::slewToSelection(void)
{
    QString mount = getString("devices", "navigatormount");
    QString cam  = getString("devices", "navigatorcamera");
    double ra  = getFloat("selectnow", "RA");
    double dec  = getFloat("selectnow", "DEC");
    INDI::BaseDevice dp = getDevice(mount.toStdString().c_str());
    if (!dp.isValid())
    {
        sendError("Error - unable to find " + mount + " device. Aborting.");
        return;
    }
    INDI::PropertyNumber prop = dp.getProperty("EQUATORIAL_EOD_COORD");
    if (!prop.isValid())
    {
        sendError("Error - unable to find " + mount + "/" + prop + " property. Aborting.");
        return;
    }
    prop.findWidgetByName("RA")->value = ra;
    prop.findWidgetByName("DEC")->value = dec;
    sendNewNumber(prop);
    sendMessage("Slewing to " + getString("actions", "targetname"));

}
void Navigator::convertSelection(void)
{
    QString code = getString("actions", "targetname");
    float ra = getFloat("actions", "targetra");
    float dec = getFloat("actions", "targetde");
    double jd = ln_get_julian_from_sys();
    INDI::IEquatorialCoordinates j2000pos;
    INDI::IEquatorialCoordinates observed;
    j2000pos.declination = dec;
    j2000pos.rightascension = ra;

    INDI::J2000toObserved(&j2000pos, jd, &observed);
    setOstElementValue("selectnow", "jd", "", false); // we'll see that later
    setOstElementValue("selectnow", "code", code, false);
    setOstElementValue("selectnow", "RA", observed.rightascension, false);
    setOstElementValue("selectnow", "DEC", observed.declination, false);
}
