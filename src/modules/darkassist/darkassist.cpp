#include "darkassist.h"

Darkassist *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    Darkassist *basemodule = new Darkassist(name, label, profile, availableModuleLibs);
    return basemodule;
}

Darkassist::Darkassist(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)

{
    setClassName(QString(metaObject()->className()).toLower());
    loadOstPropertiesFromFile(":darkassist.json");
    setModuleDescription("Dark assistant module");
    setModuleVersion("0.1");

    giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
    defineMeAsSequencer();

}

Darkassist::~Darkassist()
{

}
void Darkassist::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                                   const QVariantMap &eventData)
{
    qDebug() << "OnMyExternalEvent";
    //BOOST_LOG_TRIVIAL(debug) << "OnMyExternalEvent - recv : " << getName().toStdString() << "-" << eventType.toStdString() << "-" << eventKey.toStdString();
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
                m["status"] = "Added";
                getProperty(keyprop)->newLine(m);
            }
            if (eventType == "Flupdate")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                QVariantMap m = eventData[keyprop].toMap()["elements"].toMap();
                m["status"] = "Updated";
                getProperty(keyprop)->updateLine(line, m);
            }
        }
    }
}

void Darkassist::newBLOB(INDI::PropertyBlob pblob)
{
    if (
        (QString(pblob.getDeviceName()) == getString("devices", "camera"))
        && isSequenceRunning
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
        _image->saveAsFITSSimple(getWebroot() + "/" + getModuleName() + "-" + currentFilter + "-" + tt + ".FITS");
        OST::ImgData dta = _image->ImgStats();
        dta.mUrlJpeg = getModuleName() + ".jpeg";
        dta.mUrlFits = getModuleName() + "-" + currentFilter + "-" + tt + ".FITS";
        getEltImg("image", "image")->setValue(dta, true);

        currentCount--;
        //sendMessage("RVC frame " + QString::number(currentLine) + "/" + QString::number(currentCount));
        if(currentCount == 0)
        {
            getEltString("sequence", "status")->setValue("Finished");
            getProperty("sequence")->updateLine(currentLine);
            StartLine();
        }
        else
        {
            Shoot();
        }

    }
}
void Darkassist::newProperty(INDI::Property property)
{

    //if (
    //    (property.getDeviceName()  == getString("devices", "filter"))
    //    &&  (QString(property.getName())   == "FILTER_SLOT")
    //    &&  (property.getState() == IPS_OK)
    //    && isSequenceRunning
    //)
    //{
    //    Shoot();
    //}

}

void Darkassist::updateProperty(INDI::Property property)
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
        (property.getDeviceName()  == getString("devices", "camera"))
        &&  (QString(property.getName())   == "CCD_EXPOSURE")
        //&&  (property.getState() == IPS_OK)
        && isSequenceRunning
    )
    {
        newExp(property);
    }
}

void Darkassist::newExp(INDI::PropertyNumber exp)
{
    double etot = getFloat("sequence", "exposure");
    double ex = exp.findWidgetByName("CCD_EXPOSURE_VALUE")->value;
    getEltPrg("progress", "exposure")->setPrgValue(100 * (etot - ex) / etot, true);
}
void Darkassist::Shoot()
{
    double exp = getFloat("sequence", "exposure");
    double gain = getInt("sequence", "gain");
    double offset = getInt("sequence", "offset");
    requestCapture(getString("devices", "camera"), exp, gain, offset);

    double i = getInt("sequence", "count");
    getEltString("sequence", "status")->setValue("Running "  + QString::number(
                i - currentCount) + "/" + QString::number(i), true);
    getProperty("sequence")->updateLine(currentLine);


    getEltPrg("progress", "current")->setPrgValue(100 * (i - currentCount + 1) / i, false);
    getEltPrg("progress", "current")->setDynLabel(QString::number(i - currentCount + 1) + "/" + QString::number(i), false);

    int tot = getProperty("sequence")->getGrid().size();
    getEltPrg("progress", "global")->setPrgValue(100 * (currentLine + 1) / (tot), false);
    getEltPrg("progress", "global")->setDynLabel(QString::number(currentLine + 1) + "/" + QString::number(tot), true);
}

void Darkassist::StartSequence()
{

    currentLine = -1;
    isSequenceRunning = true;

    connectIndi();
    if (connectDevice(getString("devices", "camera")))
    {
        setBLOBMode(B_ALSO, getString("devices", "camera").toStdString().c_str(), nullptr);
        frameReset(getString("devices", "camera"));
        if (getString("devices", "camera") == "CCD Simulator")
        {
            sendModNewNumber(getString("devices", "camera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 1 );
        }
        getProperty("actions")->setState(OST::Busy);

        for (int i = 0; i < getProperty("sequence")->getGrid().count(); i++)
        {
            getProperty("sequence")->fetchLine(i);
            getEltString("sequence", "status")->setValue("Queued");
            getProperty("sequence")->updateLine(i);
        }

        StartLine();
    }
    else
    {
        getProperty("actions")->setState(OST::Error);
    }

}
void Darkassist::StartLine()
{

    currentLine++;
    if (currentLine >= getProperty("sequence")->getGrid().count())
    {
        sendMessage("Sequence completed");
        getProperty("actions")->setState(OST::Ok);
        isSequenceRunning = false;
    }
    else
    {
        getProperty("sequence")->fetchLine(currentLine);
        currentCount = getInt("sequence", "count");
        currentExposure = getFloat("sequence", "exposure");
        getEltString("sequence", "status")->setValue("Running " + QString::number(currentCount), true);
        getProperty("sequence")->updateLine(currentLine);
        int i = getInt("sequence", "filter");
        currentFilter = getEltInt("sequence", "filter")->getLov()[i];
        sendModNewNumber(getString("devices", "filter"), "FILTER_SLOT", "FILTER_SLOT_VALUE", i);
    }
}
