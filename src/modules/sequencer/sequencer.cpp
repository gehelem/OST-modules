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
        (QString(pblob.getDeviceName()) == getString("devices", "sequencercamera"))
        && isSequenceRunning
    )
    {
        delete _image;
        _image = new fileio();
        _image->loadBlob(pblob);
        stats = _image->getStats();
        setOstElementValue("imagevalues", "width", _image->getStats().width, false);
        setOstElementValue("imagevalues", "height", _image->getStats().height, false);
        setOstElementValue("imagevalues", "min", _image->getStats().min[0], false);
        setOstElementValue("imagevalues", "max", _image->getStats().max[0], false);
        setOstElementValue("imagevalues", "mean", _image->getStats().mean[0], false);
        setOstElementValue("imagevalues", "median", _image->getStats().median[0], false);
        setOstElementValue("imagevalues", "stddev", _image->getStats().stddev[0], false);
        setOstElementValue("imagevalues", "snr", _image->getStats().SNR, true);
        /*sendMessage("SMFindStars");
        _solver.ResetSolver(stats, _image->getImageBuffer());
        connect(&_solver, &Solver::successSEP, this, &Sequencer::OnSucessSEP);
        _solver.FindStars(_solver.stellarSolverProfiles[0]);*/

        QImage rawImage = _image->getRawQImage();
        QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
        im.setColorTable(rawImage.colorTable());
        QImage immap = rawImage.convertToFormat(QImage::Format_RGB32);
        immap.setColorTable(rawImage.colorTable());
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
        (property.getDeviceName()  == getString("devices", "sequencerfilter"))
        &&  (QString(property.getName())   == "FILTER_NAME")
    )
    {
        refreshFilterLov();
    }
    if (
        (property.getDeviceName()  == getString("devices", "sequencerfilter"))
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
        (property.getDeviceName() == getString("devices", "sequencercamera"))
        &&  (property.getState() == IPS_ALERT)
    )
    {
        sendMessage("cameraAlert");
        emit cameraAlert();
    }


    if (
        (property.getDeviceName()  == getString("devices", "sequencercamera"))
        &&  (QString(property.getName())   == "CCD_FRAME_RESET")
        &&  (property.getState()  == IPS_OK)
    )
    {
        //sendMessage("FrameResetDone");
        emit FrameResetDone();
    }

    if (
        (property.getDeviceName()  == getString("devices", "sequencerfilter"))
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
    double exp = getValueFloat("sequence", "exposure")->getGrid()[0];
    sendModNewNumber(getString("devices", "sequencercamera"), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE",
                     //getOstElementGrid("sequence", "exposure")[0].toDouble());
                     exp);
    double i = getValueInt("sequence", "count")->getGrid()[currentLine];
    getValueString("sequence", "status")->gridUpdate("Running "  + QString::number(
                i - currentCount) + "/" + QString::number(i), currentLine, true);
}

void Sequencer::OnSucessSEP()
{
    getProperty("actions")->setState(OST::Ok);
    setOstElementValue("imagevalues", "imgHFR", _solver.HFRavg, false);
    setOstElementValue("imagevalues", "starscount", _solver.stars.size(), true);



    //image->saveMapToJpeg(_webroot+"/"+_modulename+".jpeg",100,_solver.stars);
    QList<fileio::Record> rec = _image->getRecords();
    QImage rawImage = _image->getRawQImage();
    QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
    im.setColorTable(rawImage.colorTable());
    QImage immap = rawImage.convertToFormat(QImage::Format_RGB32);
    immap.setColorTable(rawImage.colorTable());

    im.save( getWebroot() + "/" + getModuleName() + ".jpeg", "JPG", 100);
    OST::ImgData dta;
    dta.mUrlJpeg = getModuleName() + ".jpeg";
    getValueImg("image", "image1")->setValue(dta, true);

    //QRect r;
    //r.setRect(0,0,im.width(),im.height());

    /*QPainter p;
    p.begin(&immap);
    p.setPen(QPen(Qt::red));
    //p.setFont(QFont("Times", 15, QFont::Normal));
    //p.drawText(r, Qt::AlignLeft, QDateTime::currentDateTime().toString("dd/MM/yyyy hh:mm:ss zzz") );
    p.setPen(QPen(Qt::green));
    foreach( FITSImage::Star s, _solver.stars )
    {
        qDebug() << "draw " << s.x << "/" << s.y;
        int x = s.x;
        int y = s.y;
        int a = s.a;
        int b = s.b;
        qDebug() << "draw " << x << "/" << y;
        p.drawEllipse(QPoint(x, y), a * 5, b * 5);
    }
    p.end();

    resetOstElements("histogram");
    QVector<double> his = _image->getHistogramFrequency(0);
    for( int i = 1; i < his.size(); i++)
    {
        //qDebug() << "HIS " << i << "-"  << _image->getCumulativeFrequency(0)[i] << "-"  << _image->getHistogramIntensity(0)[i] << "-"  << _image->getHistogramFrequency(0)[i];
        setOstElementValue("histogram", "i", i, false);
        setOstElementValue("histogram", "n", his[i], false);
        pushOstElements("histogram");
    }

    immap.save(getWebroot() + "/" + getModuleName() + "map.jpeg", "JPG", 100);
    setOstPropertyAttribute("imagemap", "URL", getModuleName() + "map.jpeg", true);

    emit FindStarsDone();*/
}


void Sequencer::StartSequence()
{
    currentLine = -1;
    isSequenceRunning = true;

    connectIndi();
    if (connectDevice(getString("devices", "sequencercamera")))
    {
        setBLOBMode(B_ALSO, getString("devices", "sequencercamera").toStdString().c_str(), nullptr);
        frameReset(getString("devices", "sequencercamera"));
        if (getString("devices", "sequencercamera") == "CCD Simulator")
        {
            sendModNewNumber(getString("devices", "sequencercamera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
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
        sendModNewNumber(getString("devices", "sequencerfilter"), "FILTER_SLOT", "FILTER_SLOT_VALUE", i);
    }
}
void Sequencer::refreshFilterLov()
{

    INDI::BaseDevice dp = getDevice(getString("devices", "sequencerfilter").toStdString().c_str());

    if (!dp.isValid())
    {
        sendError("Unable to find " + getString("devices", "sequencerfilter") + " device.");
        return;
    }
    INDI::PropertyText txt = dp.getText("FILTER_NAME");
    if (!txt.isValid())
    {
        sendError("Unable to find " + getString("devices", "sequencerfilter")  + "/" + "FILTER_NAME" + " property.");
        return;
    }

    getValueInt("sequence", "filter")->lovClear();
    for (unsigned int i = 0; i < txt.count(); i++ )
    {
        txt[i].getText();
        getValueInt("sequence", "filter")->lovAdd(i + 1, txt[i].getText());
        qDebug() << QString::number(i + 1) << "/" << txt[i].getText();
    }

}
