#include "navigator.h"
#include <QPainter>
#include "versionModule.cc"

Navigator *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    Navigator *basemodule = new Navigator(name, label, profile, availableModuleLibs);
    return basemodule;
}

Navigator::Navigator(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs),
      mMaxIterations(0),
      mCurrentIteration(0),
      mToleranceArcsec(0.0),
      mTargetRA(0.0),
      mTargetDEC(0.0),
      mTargetRAnow(0.0),
      mTargetDECnow(0.0),
      mWaitingSlew(false)
{

    loadOstPropertiesFromFile(":navigator.json");
    setClassName(QString(metaObject()->className()).toLower());

    setModuleDescription("Navigator module");
    setModuleVersion("0.2");
    getEltString("thisGit", "hash")->setValue(QString::fromStdString(VersionModule::GIT_SHA1), true);
    getEltString("thisGit", "date")->setValue(QString::fromStdString(VersionModule::GIT_DATE), true);
    getEltString("thisGit", "message")->setValue(QString::fromStdString(VersionModule::GIT_COMMIT_SUBJECT), true);

    giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
    giveMeADevice("mount", "Mount", INDI::BaseDevice::TELESCOPE_INTERFACE);

    defineMeAsNavigator();

    connectIndi();
    connectAllDevices();

    connect(this, &Navigator::newImage, this, &Navigator::OnNewImage);

    connect(&stellarSolver, &StellarSolver::logOutput, this, &Navigator::OnSolverLog);
    connect(&stellarSolver, &StellarSolver::ready, this, &Navigator::OnSucessSolve);

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
                    if (keyelt == "catlg")
                    {
                        QString s = pEventData[keyprop].toMap()["elements"].toMap()[keyelt].toString();
                        qDebug() << "update catlg " << s;
                        getEltString("popcat", "catlg")->setValue(s, true);
                        getProperty("popcat")->enable();
                    }

                }

                if (keyprop == "actions")
                {
                    getProperty(keyprop)->setState(OST::Busy);
                    if (keyelt == "gototarget")
                    {
                        mState = "running";
                        mCurrentIteration = 0;
                        // Load centering parameters from properties
                        mMaxIterations = getInt("centeringparams", "maxiterations");
                        mToleranceArcsec = getFloat("centeringparams", "tolerance");
                        // Store target coordinates for centering loop
                        mTargetRAnow = getFloat("selectnow", "RA");
                        mTargetDECnow = getFloat("selectnow", "DEC");
                        sendMessage(QString("Starting goto with centering: max %1 iterations, tolerance %2\"")
                                    .arg(mMaxIterations).arg(mToleranceArcsec, 0, 'f', 1));
                        initIndi();
                        slewToSelection();
                    }
                    if (keyelt == "abortnavigator")
                    {
                        stellarSolver.abort();
                        mState = "idle";
                        mCurrentIteration = 0;
                        getProperty(keyprop)->setState(OST::Ok);
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
        getProperty("actions")->setState(OST::Idle);
        delete pImage;
        pImage = new fileio();
        pImage->loadBlob(pblob, 64);
        QImage rawImage = pImage->getRawQImage();
        QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
        im.setColorTable(rawImage.colorTable());
        im.save(getWebroot()  + "/" + getModuleName() + ".jpeg", "JPG", 100);
        OST::ImgData dta = pImage->ImgStats();
        dta.mUrlJpeg = getModuleName() + ".jpeg";
        dta.isSolved = false;
        getEltImg("image", "image")->setValue(dta, true);

        emit newImage();
    }
}
void Navigator::OnNewImage()
{
    sendMessage("Solve new image");
    double ra, dec, pix, ech;
    if (
        !getModNumber(getString("devices", "camera"), "CCD_INFO", "CCD_PIXEL_SIZE", pix)
    )
    {
        sendMessage("Can't find camera device " + getString("devices", "camera") + " solve aborted");
    }
    ech = 206 * pix / getFloat("optic", "fl");

    if (
        !getModNumber(getString("devices", "mount"), "EQUATORIAL_EOD_COORD", "DEC", dec)
        || !getModNumber(getString("devices", "mount"), "EQUATORIAL_EOD_COORD", "RA", ra)
    )
    {
        sendMessage("Can't find mount device " + getString("devices", "mount") + " solve aborted");
    }
    else
    {
        double wra = ra * 360 / 24;
        double wde = dec;
        stellarSolver.loadNewImageBuffer(pImage->getStats(), pImage->getImageBuffer());
        QStringList folders;
        folders.append("/usr/share/astrometry/");
        stellarSolver.setIndexFolderPaths(folders);
        stellarSolver.setProperty("UseScale", true);
        stellarSolver.setSearchScale(ech * 0.9, ech * 1.1, ScaleUnits::ARCSEC_PER_PIX);
        stellarSolver.setProperty("UsePosition", true);
        stellarSolver.setSearchPositionInDegrees(wra, wde);
        stellarSolver.setParameters(StellarSolver::getBuiltInProfiles()[SSolver::Parameters::DEFAULT]);
        stellarSolver.setProperty("ProcessType", SOLVE);
        stellarSolver.setProperty("ExtractorType", EXTRACTOR_INTERNAL);
        stellarSolver.setProperty("SolverType", SOLVER_STELLARSOLVER);
        stellarSolver.setLogLevel(LOG_ALL);
        stellarSolver.setSSLogLevel(LOG_VERBOSE);
        stellarSolver.setProperty("LogToFile", true);
        stellarSolver.setProperty("LogFileName", getWebroot() + "/navigator_solver.log");
        stellarSolver.start();
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
        (property.getDeviceName() == getString("devices", "mount"))
        &&  (property.getName()   == std::string("EQUATORIAL_EOD_COORD"))
        &&  (property.getState() == IPS_OK)
        && mState == "running"
    )
    {

        if (mWaitingSlew)
        {
            mWaitingSlew = false;
            sendMessage("Slew finished");
            Shoot();
        }
    }
}
void Navigator::Shoot()
{
    sendMessage("Shoot new image");
    setFocalLengthAndDiameter();
    if (!requestCapture(getString("devices", "camera"), getFloat("parms", "exposure"), getInt("parms", "gain"), getInt("parms",
                        "offset")))
    {
        emit abort();
        return;
    }
    getProperty("actions")->setState(OST::Busy);
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
    enableDirectBlobAccess(getString("devices", "camera").toStdString().c_str(), nullptr);

}
void Navigator::OnSolverLog(QString text)
{
    //sendMessage(text);
    qDebug() << text;
}
void Navigator::OnSucessSolve()
{
    if (stellarSolver.failed())
    {
        sendError("Image NOT solved - centering aborted");
        stellarSolver.abort();
        getProperty("actions")->setState(OST::Error);
        mState = "idle";
        mCurrentIteration = 0;
        return;
    }

    // Get solved coordinates
    double solvedRA = stellarSolver.getSolution().ra * 24 / 360;
    double solvedDEC = stellarSolver.getSolution().dec;
    // Update image with solve data
    OST::ImgData dta = pImage->ImgStats();
    dta.mUrlJpeg = getModuleName() + ".jpeg";
    dta.solverRA = solvedRA;
    dta.solverDE = solvedDEC;
    dta.isSolved = true;
    getEltImg("image", "image")->setValue(dta, true);

    mCurrentIteration++;

    // Check if centering is satisfactory
    if (checkCentering(solvedRA, solvedDEC, mTargetRA, mTargetDEC))
    {
        sendMessage(QString("Centering successful after %1 iteration(s) - within tolerance")
                    .arg(mCurrentIteration));
        getProperty("actions")->setState(OST::Ok);
        mState = "idle";
        mCurrentIteration = 0;
        return;
    }

    // Check max iterations
    if (mCurrentIteration >= mMaxIterations)
    {
        sendError(QString("Centering failed after %1 iterations - tolerance not reached")
                  .arg(mMaxIterations));
        getProperty("actions")->setState(OST::Error);
        mState = "idle";
        mCurrentIteration = 0;
        return;
    }

    // Need correction - continue centering loop
    sendMessage(QString("Centering iteration %1/%2 - applying correction")
                .arg(mCurrentIteration).arg(mMaxIterations));
    correctOffset(solvedRA, solvedDEC);
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
        //qDebug() << results[i].name << results[i].RA;
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
    prop.findWidgetByName("RA")->value = mTargetRAnow;
    prop.findWidgetByName("DEC")->value = mTargetDECnow;

    mWaitingSlew = true;
    sendNewNumber(prop);
    sendMessage("Slewing to " + getString("actions", "targetname"));

}
void Navigator::convertSelection(void)
{
    QString code = getString("actions", "targetname");
    mTargetRA = getFloat("actions", "targetra");
    mTargetDEC = getFloat("actions", "targetde");
    double jd = ln_get_julian_from_sys();
    INDI::IEquatorialCoordinates j2000pos;
    INDI::IEquatorialCoordinates observed;
    j2000pos.declination = mTargetDEC;
    j2000pos.rightascension = mTargetRA;

    INDI::J2000toObserved(&j2000pos, jd, &observed);
    mTargetRAnow = observed.rightascension;
    mTargetDECnow = observed.declination;
    getEltString("selectnow", "jd")->setValue(""); // we'll see that later
    getEltString("selectnow", "code")->setValue(code);
    getEltFloat("selectnow", "RA")->setValue(observed.rightascension);
    getEltFloat("selectnow", "DEC")->setValue(observed.declination, true);

}

bool Navigator::checkCentering(double solvedRA, double solvedDEC, double targetRA, double targetDEC)
{
    // Calculate angular distance in arcseconds
    double deltaRA = (solvedRA * 360 / 24 - targetRA * 360 / 24) * 3600 ;
    double deltaDEC = (solvedDEC - targetDEC) * 3600 ;

    // Total angular distance in arcseconds
    double distance = sqrt(deltaRA * deltaRA + deltaDEC * deltaDEC);

    sendMessage(QString("Offset: RA=%1\" DEC=%2\" Total=%3\"")
                .arg(deltaRA, 0, 'f', 3)
                .arg(deltaDEC, 0, 'f', 3)
                .arg(distance, 0, 'f', 3));

    // Check tolerance (convert to arcsec from JSON parameter if needed)
    return (distance <= mToleranceArcsec);
}

void Navigator::correctOffset(double solvedRA, double solvedDEC)
{
    // Calculate correction offset
    double deltaRA = mTargetRA - solvedRA;  // Hours
    double deltaDEC = mTargetDEC - solvedDEC; // Degrees

    // Get current mount position
    QString mount = getString("devices", "mount");
    INDI::BaseDevice dp = getDevice(mount.toStdString().c_str());
    if (!dp.isValid())
    {
        sendError("Error - unable to find " + mount + " device for correction.");
        mState = "idle";
        return;
    }

    INDI::PropertyNumber prop = dp.getProperty("EQUATORIAL_EOD_COORD");
    if (!prop.isValid())
    {
        sendError("Error - unable to find mount coordinates for correction.");
        mState = "idle";
        return;
    }

    // Read current position
    double currentRA = prop.findWidgetByName("RA")->value;
    double currentDEC = prop.findWidgetByName("DEC")->value;

    // Apply correction
    double newRA = currentRA + deltaRA;
    double newDEC = currentDEC + deltaDEC;
    sendMessage("Slewing to " + getString("actions",
                                          "targetname") + "-" + QString::number(newRA) + "-" + QString::number(newDEC));

    sendMessage(QString("Applying correction: RA %1h → %2h, DEC %3° → %4°")
                .arg(currentRA, 0, 'f', 4)
                .arg(newRA, 0, 'f', 4)
                .arg(currentDEC, 0, 'f', 2)
                .arg(newDEC, 0, 'f', 2));

    // Send corrected position to mount
    qDebug() << prop.findWidgetByName("RA")->value << "->" << newRA;
    qDebug() << prop.findWidgetByName("DEC")->value << "->" << newDEC;
    prop.findWidgetByName("RA")->value = newRA;
    prop.findWidgetByName("DEC")->value = newDEC;
    mWaitingSlew = true;
    sendNewNumber(prop);

    // Mount will trigger updateProperty when slew is complete
    // which will call Shoot() again for next iteration
}
