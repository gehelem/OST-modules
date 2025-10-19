#include "sequencer.h"
#include <QPainter>
#include <QTimer>
#include "versionModule.cc"

Sequencer *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    Sequencer *basemodule = new Sequencer(name, label, profile, availableModuleLibs);
    return basemodule;
}

Sequencer::Sequencer(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)

{
    setClassName(QString(metaObject()->className()).toLower());
    loadOstPropertiesFromFile(":sequencer.json");
    setModuleDescription("Sequencer module");
    setModuleVersion("0.2");
    getEltString("thisGit", "hash")->setValue(QString::fromStdString(VersionModule::GIT_SHA1), true);
    getEltString("thisGit", "date")->setValue(QString::fromStdString(VersionModule::GIT_DATE), true);
    getEltString("thisGit", "message")->setValue(QString::fromStdString(VersionModule::GIT_COMMIT_SUBJECT), true);

    giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
    giveMeADevice("filter", "Filter wheel", INDI::BaseDevice::FILTER_INTERFACE);
    defineMeAsSequencer();
    refreshFilterLov();

    // Initialize guiding settle timer
    mGuidingSettleTimer = new QTimer(this);
    mGuidingSettleTimer->setSingleShot(true);
    connect(mGuidingSettleTimer, &QTimer::timeout, this, &Sequencer::OnGuidingSettleTimeout);

}

Sequencer::~Sequencer()
{

}
void Sequencer::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                                  const QVariantMap &eventData)
{
    //BOOST_LOG_TRIVIAL(debug) << "OnMyExternalEvent - recv : " << getName().toStdString() << "-" << eventType.toStdString() << "-" << eventKey.toStdString();

    // Check if this is a focus completion event
    if (eventType == "focusdone" && mWaitingForFocus)
    {
        OnFocusDone(eventType, eventModule, eventKey, eventData);
        return;
    }

    if (getModuleName() == eventModule)
    {
        foreach(const QString &keyprop, eventData.keys())
        {
            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                if (keyprop == "actions")
                {
                    if (keyelt == "startsequence")
                    {
                        StartSequence();
                    }
                    if (keyelt == "abortsequence")
                    {
                        emit Abort();
                        isSequenceRunning = false;
                        mWaitingForFocus = false;
                        mWaitingForGuidingSettle = false;
                        if (mGuidingSettleTimer->isActive())
                        {
                            mGuidingSettleTimer->stop();
                        }
                    }
                }
                if (keyprop == "devices")
                {
                    if (keyelt == "filter")
                    {
                        refreshFilterLov();
                    }
                }
            }
            if (eventType == "Fldelete")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                getProperty(keyprop)->deleteLine(line);
            }
            if (eventType == "Flcreate")
            {
                QVariantMap m = eventData[keyprop].toMap()["elements"].toMap();
                getProperty(keyprop)->newLine(m);
                getEltPrg(keyprop, "progress")->setPrgValue(0, false);
                //getEltPrg(keyprop, "progress")->setDynLabel("Added", false);
                getProperty(keyprop)->updateLine(getProperty(keyprop)->getGrid().count() - 1);
            }
            if (eventType == "Flupdate")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                QVariantMap m = eventData[keyprop].toMap()["elements"].toMap();
                getProperty(keyprop)->updateLine(line, m);
                getEltPrg(keyprop, "progress")->setPrgValue(0, false);
                //getEltPrg(keyprop, "progress")->setDynLabel("Updated", false);
                getProperty(keyprop)->updateLine(line);

            }
        }
    }
}

void Sequencer::newBLOB(INDI::PropertyBlob pblob)
{
    if (
        (QString(pblob.getDeviceName()) == getString("devices", "camera"))
        && isSequenceRunning
        && !mWaitingForFocus  // Don't process images while focus is running
    )
    {
        delete _image;
        _image = new fileio();
        _image->loadBlob(pblob, 64);
        stats = _image->getStats();
        //qDebug() << "1";
        //_solver.ResetSolver(stats, _image->getImageBuffer());
        //qDebug() << "2";
        //connect(&_solver, &Solver::successSEP, this, &Sequencer::OnSucessSEP);
        //qDebug() << "3";
        //_solver.FindStars(_solver.stellarSolverProfiles[0]);
        //qDebug() << "4";
        QImage rawImage = _image->getRawQImage();
        QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
        im.setColorTable(rawImage.colorTable());
        im.save( getWebroot() + "/" + getModuleName() + ".jpeg", "JPG", 100);

        QString tt = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
        _image->saveAsFITSSimple(currentFolder + "/" + mObjectName + "-" + currentFrameType + "-"  + currentFilter + "-" + tt +
                                 ".FITS");
        OST::ImgData dta = _image->ImgStats();
        dta.mUrlJpeg = getModuleName() + ".jpeg";
        //dta.mUrlFits = getModuleName() + "-" + currentFilter + "-" + tt + ".FITS";
        getEltImg("image", "image")->setValue(dta, true);

        currentCount--;
        //sendMessage("RVC frame " + QString::number(currentLine) + "/" + QString::number(currentCount));
        if(currentCount == 0)
        {
            //getEltString("sequence", "status")->setValue("Finished");
            //getProperty("sequence")->updateLine(currentLine);
            // Don't start next line if waiting for focus to complete
            if (!mWaitingForFocus)
            {
                StartLine();
            }
        }
        else
        {
            Shoot();
        }

    }
}
void Sequencer::newProperty(INDI::Property property)
{

    if (
        (property.getDeviceName()  == getString("devices", "filter"))
        &&  (QString(property.getName())   == "FILTER_NAME")
    )
    {
        refreshFilterLov();
    }
    if (
        (property.getDeviceName()  == getString("devices", "filter"))
        &&  (QString(property.getName())   == "FILTER_SLOT")
        &&  (property.getState() == IPS_OK)
        && isSequenceRunning
        && !mWaitingForFocus  // Don't shoot if waiting for focus
    )
    {
        Shoot();
    }

}

void Sequencer::updateProperty(INDI::Property property)
{

    if (strcmp(property.getName(), "CCD1") == 0)
    {
        newBLOB(property);
    }
    if (
        (property.getDeviceName() == getString("devices", "camera"))
        &&  (property.getState() == IPS_ALERT)
    )
    {
        sendMessage("cameraAlert");
        emit cameraAlert();
    }


    if (
        (property.getDeviceName()  == getString("devices", "camera"))
        &&  (QString(property.getName())   == "CCD_FRAME_RESET")
        &&  (property.getState()  == IPS_OK)
    )
    {
        //sendMessage("FrameResetDone");
        emit FrameResetDone();
    }

    if (
        (property.getDeviceName()  == getString("devices", "filter"))
        &&  (QString(property.getName())   == "FILTER_SLOT")
        &&  (property.getState() == IPS_OK)
        && isSequenceRunning
        && !mWaitingForFocus  // Don't shoot if waiting for focus
    )
    {
        //sendMessage("Filter OK");
        Shoot();
    }
    if (
        (property.getDeviceName()  == getString("devices", "camera"))
        &&  (QString(property.getName())   == "CCD_EXPOSURE")
        //&&  (property.getState() == IPS_OK)
        && isSequenceRunning
        && !mWaitingForFocus  // Don't update exposure progress while focus is running
    )
    {
        newExp(property);
    }
}

void Sequencer::newExp(INDI::PropertyNumber exp)
{
    double etot = getFloat("sequence", "exposure");
    double ex = exp.findWidgetByName("CCD_EXPOSURE_VALUE")->value;
    getEltPrg("progress", "exposure")->setPrgValue(100 * (etot - ex) / etot, true);
}
void Sequencer::Shoot()
{
    double exp = getFloat("sequence", "exposure");
    double gain = getInt("sequence", "gain");
    double offset = getInt("sequence", "offset");
    requestCapture(getString("devices", "camera"), exp, gain, offset);

    double i = getInt("sequence", "count");
    getEltPrg("sequence", "progress")->setPrgValue(100 * (i - currentCount + 1) / i, false);
    getEltPrg("sequence", "progress")->setDynLabel(QString::number(i - currentCount + 1) + "/" + QString::number(i), false);


    getProperty("sequence")->updateLine(currentLine);

    int tot = getProperty("sequence")->getGrid().size();
    getEltPrg("progress", "global")->setPrgValue(100 * (currentLine + 1) / (tot), false);
    getEltPrg("progress", "global")->setDynLabel(QString::number(currentLine + 1) + "/" + QString::number(tot), true);
}

void Sequencer::OnSucessSEP()
{
    disconnect(&_solver, &Solver::successSEP, this, &Sequencer::OnSucessSEP);
    OST::ImgData dta = _image->ImgStats();
    dta.HFRavg = _solver.HFRavg;
    dta.starsCount = _solver.stars.size();
    getEltImg("image", "image")->setValue(dta, true);
}


void Sequencer::StartSequence()
{

    currentLine = -1;
    isSequenceRunning = true;
    mObjectName = getString("object", "label");
    mDate = QDateTime::currentDateTime().toString("yyyyMMdd-hh-mm-ss");


    QDir dir;
    dir.mkdir(getWebroot() + "/" + getModuleName());
    dir.mkdir(getWebroot() + "/" + getModuleName() + "/" + mObjectName);

    connectIndi();
    if (connectDevice(getString("devices", "camera")))
    {
        setBLOBMode(B_ALSO, getString("devices", "camera").toStdString().c_str(), nullptr);
        frameReset(getString("devices", "camera"));
        if (getString("devices", "camera") == "CCD Simulator")
        {
            sendModNewNumber(getString("devices", "camera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
        }
        getProperty("actions")->setState(OST::Busy);

        for (int i = 0; i < getProperty("sequence")->getGrid().count(); i++)
        {
            getProperty("sequence")->fetchLine(i);
            getEltPrg("sequence", "progress")->setDynLabel("Queued", false);
            getEltPrg("sequence", "progress")->setPrgValue(0, false);
            getProperty("sequence")->updateLine(i);
        }

        // Check if autofocus at sequence start is enabled
        bool autofocusAtStart = getBool("parameters", "autofocusatstart");

        if (autofocusAtStart && getProperty("sequence")->getGrid().count() > 0)
        {
            // Get first line filter information
            getProperty("sequence")->fetchLine(0);
            int filterIndex = getInt("sequence", "filter");
            QString firstFilter = getEltInt("sequence", "filter")->getLov()[filterIndex];
            QString firstFrameType = getString("sequence", "frametype");

            // Only do initial focus for light or flat frames
            if (firstFrameType == "L" || firstFrameType == "F")
            {
                sendMessage("Starting sequence with autofocus using filter: " + firstFilter);

                // Set the filter to the first line's filter
                sendModNewNumber(getString("devices", "filter"), "FILTER_SLOT", "FILTER_SLOT_VALUE", filterIndex);

                // Initialize previousFilter so first line doesn't trigger another focus
                previousFilter = firstFilter;

                // Request focus before starting sequence
                requestFocus();
                return;  // StartLine() will be called after focus completes
            }
        }

        StartLine();
    }
    else
    {
        getProperty("actions")->setState(OST::Error);
    }

}
void Sequencer::StartLine()
{

    currentLine++;
    if (currentLine >= getProperty("sequence")->getGrid().count())
    {
        sendMessage("Sequence completed");
        getProperty("actions")->setState(OST::Ok);
        isSequenceRunning = false;
        previousFilter = "";
    }
    else
    {
        getProperty("sequence")->fetchLine(currentLine);
        currentCount = getInt("sequence", "count");
        currentExposure = getFloat("sequence", "exposure");
        int i = getInt("sequence", "filter");
        currentFilter = getEltInt("sequence", "filter")->getLov()[i];
        currentFrameType = getString("sequence", "frametype");

        // Check if filter has changed and autofocus is enabled
        bool filterChanged = (!previousFilter.isEmpty() && previousFilter != currentFilter);
        bool autofocusEnabled = getBool("parameters", "autofocusonfilterchange");

        if (filterChanged && autofocusEnabled && (currentFrameType == "L" || currentFrameType == "F"))
        {
            sendMessage("Filter changed from " + previousFilter + " to " + currentFilter);
            previousFilter = currentFilter;

            // First change the filter
            sendModNewNumber(getString("devices", "filter"), "FILTER_SLOT", "FILTER_SLOT_VALUE", i);

            // Setup folders before requesting focus
            QDir dir;
            currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "";
            dir.mkdir(currentFolder);
            if (currentFrameType == "L")
            {
                sendModNewSwitch(getString("devices", "camera"), "CCD_FRAME_TYPE", "FRAME_LIGHT", ISS_ON);
                currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "/LIGHT";
                dir.mkdir(currentFolder);
                currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "/LIGHT/" + currentFilter;
            }
            if (currentFrameType == "F")
            {
                sendModNewSwitch(getString("devices", "camera"), "CCD_FRAME_TYPE", "FRAME_FLAT", ISS_ON);
                currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "/FLAT";
                dir.mkdir(currentFolder);
                currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "/FLAT/" + currentFilter;
            }
            dir.mkdir(currentFolder);

            // Request focus and return - Shoot() will be called after focus completes
            requestFocus();
            return;
        }

        // Update previous filter for next iteration
        previousFilter = currentFilter;

        sendModNewNumber(getString("devices", "filter"), "FILTER_SLOT", "FILTER_SLOT_VALUE", i);
        QDir dir;
        currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "";
        dir.mkdir(currentFolder);
        if (currentFrameType == "L")
        {
            sendModNewSwitch(getString("devices", "camera"), "CCD_FRAME_TYPE", "FRAME_LIGHT", ISS_ON);
            currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "/LIGHT";
            dir.mkdir(currentFolder);
            currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "/LIGHT/" + currentFilter;
        }
        if (currentFrameType == "F")
        {
            sendModNewSwitch(getString("devices", "camera"), "CCD_FRAME_TYPE", "FRAME_FLAT", ISS_ON);
            currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "/FLAT";
            dir.mkdir(currentFolder);
            currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "/FLAT/" + currentFilter;
        }

        if (currentFrameType == "B")
        {
            sendModNewSwitch(getString("devices", "camera"), "CCD_FRAME_TYPE", "FRAME_BIAS", ISS_ON);
            currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "/BIAS";
        }
        if (currentFrameType == "D")
        {
            sendModNewSwitch(getString("devices", "camera"), "CCD_FRAME_TYPE", "FRAME_DARK", ISS_ON);
            currentFolder = getWebroot() + "/" + getModuleName() + "/" + mObjectName + "/DARK";
        }
        dir.mkdir(currentFolder);
    }
}
void Sequencer::refreshFilterLov()
{
    INDI::BaseDevice dp = getDevice(getString("devices", "filter").toStdString().c_str());

    if (!dp.isValid())
    {
        sendError("Unable to find " + getString("devices", "filter") + " device.");
        return;
    }
    INDI::PropertyText txt = dp.getText("FILTER_NAME");
    if (!txt.isValid())
    {
        sendError("Unable to find " + getString("devices", "filter")  + "/" + "FILTER_NAME" + " property.");
        return;
    }

    getEltInt("sequence", "filter")->lovClear();
    for (unsigned int i = 0; i < txt.count(); i++ )
    {
        txt[i].getText();
        getEltInt("sequence", "filter")->lovAdd(i + 1, txt[i].getText());
    }

}

void Sequencer::requestFocus()
{
    QString focusModule = getString("parameters", "focusmodule");
    sendMessage("Filter changed - requesting autofocus from module: " + focusModule);

    mWaitingForFocus = true;

    // Suspend guiding if option is enabled
    bool suspendGuiding = getBool("parameters", "suspendguidingduringfocus");
    if (suspendGuiding)
    {
        QString guiderModule = getString("parameters", "guidermodule");
        sendMessage("Suspending guiding on module: " + guiderModule);
        emit moduleEvent("suspendguiding", guiderModule, "", QVariantMap());
    }

    // Emit custom event type "requestautofocus" that focus module will handle
    emit moduleEvent("requestautofocus", focusModule, "", QVariantMap());
}

void Sequencer::OnFocusDone(const QString &eventType, const QString &eventModule, const QString &eventKey,
                            const QVariantMap &eventData)
{
    Q_UNUSED(eventType);
    Q_UNUSED(eventModule);
    Q_UNUSED(eventKey);
    Q_UNUSED(eventData);

    sendMessage("Autofocus completed - resuming sequence");
    mWaitingForFocus = false;

    // Resume guiding if it was suspended
    bool suspendGuiding = getBool("parameters", "suspendguidingduringfocus");
    if (suspendGuiding)
    {
        QString guiderModule = getString("parameters", "guidermodule");
        sendMessage("Resuming guiding on module: " + guiderModule);
        emit moduleEvent("resumeguiding", guiderModule, "", QVariantMap());

        // Wait for guiding to settle before continuing
        int settleTime = getInt("parameters", "guidingsettletime");
        if (settleTime > 0)
        {
            sendMessage("Waiting " + QString::number(settleTime) + " seconds for guiding to settle...");
            mWaitingForGuidingSettle = true;
            mGuidingSettleTimer->start(settleTime * 1000); // Convert seconds to milliseconds
            return; // OnGuidingSettleTimeout() will continue the sequence
        }
    }

    // Continue sequence immediately if no settle time needed
    // If currentLine is -1, focus was done at sequence start, so start the first line
    if (currentLine == -1)
    {
        sendMessage("Initial autofocus completed - starting sequence");
        StartLine();
    }
    else
    {
        // After focus completes during sequence, shoot the first image of the current line
        Shoot();
    }
}

void Sequencer::OnGuidingSettleTimeout()
{
    sendMessage("Guiding settle time completed - continuing sequence");
    mWaitingForGuidingSettle = false;

    // Continue sequence after settle time
    if (currentLine == -1)
    {
        sendMessage("Starting sequence");
        StartLine();
    }
    else
    {
        // Shoot the first image of the current line
        Shoot();
    }
}
