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

    setModuleDescription("Navigator module");
    setModuleVersion("0.1");

    giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
    giveMeADevice("mount", "Mount", INDI::BaseDevice::TELESCOPE_INTERFACE);

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
                        if (getEltBool(keyprop, keyelt)->setValue(true, true))
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
                        mState = "running";
                        initIndi();
                        slewToSelection();
                    }
                }
            }
            if (pEventType == "Flselect")
            {
                double line = pEventData[keyprop].toMap()["line"].toDouble();
                getProperty("results")->fetchLine(line);
                QString code = getString("results", "code");
                float ra = getFloat("results", "RA");
                float dec = getFloat("results", "DEC");
                QString ns = getString("results", "NS");
                getEltString("actions", "targetname")->setValue(code);
                getEltFloat("actions", "targetra")->setValue(ra);
                getEltFloat("actions", "targetde")->setValue(dec, true);
                convertSelection();
            }
        }

    }
}

void Navigator::newBLOB(INDI::PropertyBlob pblob)
{

    if (
        (QString(pblob.getDeviceName()) == getString("devices", "camera")) && (mState != "idle")
    )
    {
        double ra, dec;
        if (
            !getModNumber(getString("devices", "mount"), "EQUATORIAL_EOD_COORD", "DEC", dec)
            || !getModNumber(getString("devices", "mount"), "EQUATORIAL_EOD_COORD", "RA", ra)
        )
        {
            sendMessage("Can't find mount device " + getString("devices", "mount") + " solve aborted");
        }
        else
        {
            getProperty("actions")->setState(OST::Idle);
            delete pImage;
            pImage = new fileio();
            pImage->loadBlob(pblob, 64);
            mStats = pImage->getStats();
            QStringList folders;
            folders.append("/usr/share/astrometry");
            mSolver.stellarSolver.setIndexFolderPaths(folders);
            mSolver.ResetSolver(mStats, pImage->getImageBuffer());
            mSolver.stellarSolver.setSearchPositionInDegrees(ra * 360 / 24, dec);
            connect(&mSolver, &Solver::successSolve, this, &Navigator::OnSucessSolve);
            connect(&mSolver, &Solver::solverLog, this, &Navigator::OnSolverLog);
            mSolver.SolveStars(mSolver.stellarSolverProfiles[0]);
        }



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
        (property.getDeviceName() == getString("devices", "camera"))
        &&  (property.getState() == IPS_ALERT)
    )
    {
        sendWarning("cameraAlert");
        emit cameraAlert();
    }


    if (
        (property.getDeviceName() == getString("devices", "camera"))
        &&  (property.getName()   == std::string("CCD_FRAME_RESET"))
        &&  (property.getState() == IPS_OK)
    )
    {
        //sendMessage("FrameResetDone");
        emit FrameResetDone();
    }
    if (
        (property.getDeviceName() == getString("devices", "mount"))
        &&  (property.getName()   == std::string("EQUATORIAL_EOD_COORD"))
        &&  (property.getState() == IPS_OK)
        && mState == "running"
    )
    {

        sendMessage("Slew finished");
        Shoot();
    }
}
void Navigator::Shoot()
{
    //sendMessage("SMRequestExposure");
    if (!requestCapture(getString("devices", "camera"), getFloat("parms", "exposure"), getInt("parms", "gain"), getInt("parms",
                        "offset")))
    {
        emit abort();
        return;
    }
    getProperty("actions")->setState(OST::Error);
}
void Navigator::initIndi()
{
    connectIndi();
    connectDevice(getString("devices", "camera"));
    connectDevice(getString("devices", "mount"));
    setBLOBMode(B_ALSO, getString("devices", "camera").toStdString().c_str(), nullptr);
    if (getString("devices", "camera") == "CCD Simulator")
    {
        sendModNewNumber(getString("devices", "camera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
    }
    sendModNewNumber(getString("devices", "camera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
    enableDirectBlobAccess(getString("devices", "camera").toStdString().c_str(), nullptr);

}
void Navigator::OnSolverLog(QString &text)
{
    sendMessage(text);
}
void Navigator::OnSucessSolve()
{
    getProperty("actions")->setState(OST::Ok);

    QImage rawImage = pImage->getRawQImage();
    QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
    im.setColorTable(rawImage.colorTable());
    im.save(getWebroot()  + "/" + getModuleName() + ".jpeg", "JPG", 100);
    OST::ImgData dta = pImage->ImgStats();
    dta.mUrlJpeg = getModuleName() + ".jpeg";
    dta.solverRA = mSolver.stellarSolver.getSolution().ra;
    dta.solverDE = mSolver.stellarSolver.getSolution().dec;
    dta.isSolved = true;
    qDebug() << "RA=" << dta.solverRA << " DE=" << dta.solverDE;
    getEltImg("image", "image")->setValue(dta, true);

    mState = "idle";
    disconnect(&mSolver, &Solver::successSolve, this, &Navigator::OnSucessSolve);
    disconnect(&mSolver, &Solver::solverLog, this, &Navigator::OnSolverLog);


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
    sendMessage("Searching " + getString("search", "name") + " gives " + QString::number(results.count()) + " results");

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
        qDebug() << results[i].name << results[i].RA;
        getEltString("results", "catalog")->setValue(results[i].catalog);
        getEltString("results", "code")->setValue(results[i].code);
        getEltString("results", "NS")->setValue(results[i].NS);
        getEltString("results", "name")->setValue(results[i].name);
        getEltString("results", "alias")->setValue(results[i].alias);
        getEltFloat("results", "RA")->setValue(results[i].RA);
        getEltFloat("results", "DEC")->setValue(results[i].DEC);
        getEltFloat("results", "diam")->setValue(results[i].diam);
        getEltFloat("results", "mag")->setValue(results[i].mag);
        getProperty("results")->push();
    }

}
void Navigator::slewToSelection(void)
{
    QString mount = getString("devices", "mount");
    QString cam  = getString("devices", "camera");
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
    getEltString("selectnow", "jd")->setValue(""); // we'll see that later
    getEltString("selectnow", "code")->setValue(code);
    getEltFloat("selectnow", "RA")->setValue(observed.rightascension);
    getEltFloat("selectnow", "DEC")->setValue(observed.declination, true);
}
