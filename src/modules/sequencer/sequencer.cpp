#include "sequencer.h"
#include <QPainter>

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
    setModuleVersion("0.1");

    giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
    giveMeADevice("filter", "Filter wheel", INDI::BaseDevice::FILTER_INTERFACE);
    defineMeAsSequencer();
    refreshFilterLov();

}

Sequencer::~Sequencer()
{

}
void Sequencer::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                                  const QVariantMap &eventData)
{
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
                getProperty(keyprop)->newLine(eventData[keyprop].toMap()["elements"].toMap());
            }
            if (eventType == "Flupdate")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                getProperty(keyprop)->updateLine(line, eventData[keyprop].toMap()["elements"].toMap());

            }


        }
    }
}

void Sequencer::newBLOB(INDI::PropertyBlob pblob)
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
        sendMessage("Couting stars ...");
        _solver.ResetSolver(stats, _image->getImageBuffer());
        connect(&_solver, &Solver::successSEP, this, &Sequencer::OnSucessSEP);
        _solver.FindStars(_solver.stellarSolverProfiles[0]);

        QImage rawImage = _image->getRawQImage();
        QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
        im.setColorTable(rawImage.colorTable());
        im.save( getWebroot() + "/" + getModuleName() + ".jpeg", "JPG", 100);

        QString tt = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
        _image->saveAsFITSSimple(getWebroot() + "/" + getModuleName() + "-" + currentFilter + "-" + tt + ".FITS");

        OST::ImgData dta = _image->ImgStats();
        dta.mUrlJpeg = getModuleName() + ".jpeg";
        dta.mUrlFits = getModuleName() + "-" + currentFilter + "-" + tt + ".FITS";
        getValueImg("image", "image")->setValue(dta, true);

        currentCount--;
        //sendMessage("RVC frame " + QString::number(currentLine) + "/" + QString::number(currentCount));
        if(currentCount == 0)
        {
            getValueString("sequence", "status")->gridUpdate("Finished", currentLine, true);
            StartLine();
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
    )
    {
        //sendMessage("Filter OK");
        Shoot();
    }
}

void Sequencer::Shoot()
{
    double exp = getValueFloat("sequence", "exposure")->getGrid()[currentLine];
    double gain = getValueInt("sequence", "gain")->getGrid()[currentLine];
    double offset = getValueInt("sequence", "offset")->getGrid()[currentLine];
    sendModNewNumber(getString("devices", "camera"), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE",
                     //getOstElementGrid("sequence", "exposure")[0].toDouble());
                     exp);
    requestCapture(getString("devices", "camera"), exp, gain, offset);

    double i = getValueInt("sequence", "count")->getGrid()[currentLine];
    getValueString("sequence", "status")->gridUpdate("Running "  + QString::number(
                i - currentCount) + "/" + QString::number(i), currentLine, true);
}

void Sequencer::OnSucessSEP()
{
    OST::ImgData dta = _image->ImgStats();
    dta.HFRavg = _solver.HFRavg;
    dta.starsCount = _solver.stars.size();
    getValueImg("image", "image")->setValue(dta, true);
}


void Sequencer::StartSequence()
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
            sendModNewNumber(getString("devices", "camera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
        }
        getProperty("actions")->setState(OST::Busy);

        for (int i = 0; i < getOstElementGrid("sequence", "status").count(); i++)
        {
            getValueString("sequence", "status")->gridUpdate("Queued", i, true);
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
    if (currentLine >= getOstElementGrid("sequence", "count").count())
    {
        sendMessage("Sequence completed");
        getProperty("actions")->setState(OST::Ok);

        isSequenceRunning = false;

    }
    else
    {

        currentCount = getValueInt("sequence", "count")->getGrid()[currentLine];
        currentExposure = getValueFloat("sequence", "exposure")->getGrid()[currentLine];
        getValueString("sequence", "status")->gridUpdate("Running" + QString::number(currentCount), currentLine, true);

        int i = getValueInt("sequence", "filter")->getGrid()[currentLine];
        currentFilter = getValueInt("sequence", "filter")->getLov()[i];
        sendModNewNumber(getString("devices", "filter"), "FILTER_SLOT", "FILTER_SLOT_VALUE", i);
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

    getValueInt("sequence", "filter")->lovClear();
    for (unsigned int i = 0; i < txt.count(); i++ )
    {
        txt[i].getText();
        getValueInt("sequence", "filter")->lovAdd(i + 1, txt[i].getText());
    }

}
