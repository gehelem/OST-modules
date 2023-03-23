#include "sequencer.h"
#include <QPainter>

SequencerModule *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    SequencerModule *basemodule = new SequencerModule(name, label, profile, availableModuleLibs);
    return basemodule;
}

SequencerModule::SequencerModule(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)

{
    setClassName(QString(metaObject()->className()).toLower());
    loadOstPropertiesFromFile(":sequencer.json");
    setModuleDescription("Sequencer module");
    setModuleVersion("0.1");


    createOstElement("devices", "camera", "Camera", true);
    createOstElement("devices", "fw", "Filter wheel", true);
    setOstElementValue("devices", "camera",   _camera, false);
    setOstElementValue("devices", "fw",   _fw, false);

    //saveAttributesToFile("inspector.json");
    _camera = getOstElementValue("devices", "camera").toString();
    _exposure = getOstElementValue("parameters", "exposure").toFloat();
}

SequencerModule::~SequencerModule()
{

}
void SequencerModule::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                                        const QVariantMap &eventData)
{
    //BOOST_LOG_TRIVIAL(debug) << "OnMyExternalEvent - recv : " << getName().toStdString() << "-" << eventType.toStdString() << "-" << eventKey.toStdString();
    if (getModuleName() == eventModule)
    {
        foreach(const QString &keyprop, eventData.keys())
        {
            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                setOstElementValue(keyprop, keyelt, eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"], true);
                if (keyprop == "devices")
                {
                    if (keyelt == "camera")
                    {
                        if (setOstElementValue(keyprop, keyelt, eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"], false))
                        {
                            setOstPropertyAttribute(keyprop, "status", IPS_OK, true);
                            _camera = getOstElementValue("devices", "camera").toString();
                        }
                    }
                }

                if (keyprop == "parameters")
                {
                    if (keyelt == "exposure")
                    {
                        if (setOstElementValue(keyprop, keyelt, eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"], false))
                        {
                            setOstPropertyAttribute(keyprop, "status", IPS_OK, true);
                            _exposure = getOstElementValue("parameters", "exposure").toFloat();
                        }
                    }
                }
                if (keyprop == "actions")
                {
                    if (keyelt == "run")
                    {
                        if (setOstElementValue(keyprop, keyelt, true, true))
                        {
                            StartSequence();
                        }
                    }
                    if (keyelt == "abort")
                    {
                        if (setOstElementValue(keyprop, keyelt, false, false))
                        {
                            emit Abort();
                            isSequenceRunning = false;

                        }
                    }
                }

            }
            if (eventType == "Fldelete")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                qDebug() << "dummy" << eventType << "-" << eventModule << "-" << eventKey << "-" << eventData << "line=" << line;
                deleteOstPropertyLine(keyprop, line);

            }
            if (eventType == "Flcreate")
            {
                qDebug() << "dummy" << eventType << "-" << eventModule << "-" << eventKey << "-" << eventData;
                newOstPropertyLine(keyprop, eventData);

            }
            if (eventType == "Flupdate")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                qDebug() << "dummy" << eventType << "-" << eventModule << "-" << eventKey << "-" << eventData;
                updateOstPropertyLine(keyprop, line, eventData);

            }


        }
    }
}

void SequencerModule::newBLOB(INDI::PropertyBlob pblob)
{

    if (
        (QString(pblob.getDeviceName()) == _camera)
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
        connect(&_solver, &Solver::successSEP, this, &SequencerModule::OnSucessSEP);
        _solver.FindStars(_solver.stellarSolverProfiles[0]);*/

        QImage rawImage = _image->getRawQImage();
        QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
        im.setColorTable(rawImage.colorTable());
        QImage immap = rawImage.convertToFormat(QImage::Format_RGB32);
        immap.setColorTable(rawImage.colorTable());

        im.save( getWebroot() + "/" + getModuleName() + ".jpeg", "JPG", 100);
        setOstPropertyAttribute("image", "URL", getModuleName() + ".jpeg", true);
        QString tt = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
        _image->saveAsFITSSimple(getWebroot() + "/" + getModuleName() + "-" + currentFilter + "-" + tt + ".FITS");

        currentCount--;
        //sendMessage("RVC frame " + QString::number(currentLine) + "/" + QString::number(currentCount));
        if(currentCount == 0)
        {
            //sendMessage("line finished" + QString::number(currentLine));
            setOstElementLineValue("sequence", "status", currentLine, "Finished");
            StartLine();
        }
        else
        {
            Shoot();
        }

    }
}
void SequencerModule::newProperty(INDI::Property property)
{
    if (
        (property.getDeviceName()  == _fw)
        &&  (QString(property.getName())   == "FILTER_NAME")
    )
    {
        clearOstElementLov("sequence", "filter");
        INDI::PropertyText txt = property;
        for (unsigned int i = 0; i < txt.count(); i++ )
        {
            addOstElementLov("sequence", "filter", QString::number(i + 1), txt[i].getText());
        }
    }
    if (
        (property.getDeviceName()  == _fw)
        &&  (QString(property.getName())   == "FILTER_SLOT")
        &&  (property.getState() == IPS_OK)
        && isSequenceRunning


    )
    {
        Shoot();
    }

}

void SequencerModule::updateProperty(INDI::Property property)
{

    if (strcmp(property.getName(), "CCD1") == 0)
    {
        newBLOB(property);
    }
    if (
        (property.getDeviceName() == _camera)
        &&  (property.getState() == IPS_ALERT)
    )
    {
        sendMessage("cameraAlert");
        emit cameraAlert();
    }


    if (
        (property.getDeviceName()  == _camera)
        &&  (QString(property.getName())   == "CCD_FRAME_RESET")
        &&  (property.getState()  == IPS_OK)
    )
    {
        sendMessage("FrameResetDone");
        emit FrameResetDone();
    }

    if (
        (property.getDeviceName()  == _fw)
        &&  (QString(property.getName())   == "FILTER_SLOT")
        &&  (property.getState() == IPS_OK)
        && isSequenceRunning
    )
    {
        //sendMessage("Filter OK");
        Shoot();
    }
}

void SequencerModule::Shoot()
{
    sendModNewNumber(_camera, "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE",
                     //getOstElementGrid("sequence", "exposure")[0].toDouble());
                     getOstElementLineValue("sequence", "exposure", 0).toDouble());
    double i = getOstElementLineValue("sequence", "count", currentLine).toInt();
    setOstElementLineValue("sequence", "status", currentLine,
                           "Running "  + QString::number(i - currentCount) + "/" + QString::number(i));
}

void SequencerModule::OnSucessSEP()
{
    setOstPropertyAttribute("actions", "status", IPS_OK, true);
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
    setOstPropertyAttribute("image", "URL", getModuleName() + ".jpeg", true);

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


void SequencerModule::StartSequence()
{
    currentLine = -1;
    isSequenceRunning = true;

    connectIndi();
    if (connectDevice(_camera))
    {
        setBLOBMode(B_ALSO, _camera.toStdString().c_str(), nullptr);
        frameReset(_camera);
        sendModNewNumber(_camera, "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
        setOstPropertyAttribute("actions", "status", IPS_BUSY, true);
        for (int i = 0; i < getOstElementGrid("sequence", "status").count(); i++)
        {
            setOstElementLineValue("sequence", "status", i, "Queued");
        }

        StartLine();
    }
    else
    {
        setOstPropertyAttribute("actions", "status", IPS_ALERT, true);
    }

}
void SequencerModule::StartLine()
{
    currentLine++;
    if (currentLine >= getOstElementGrid("sequence", "count").count())
    {
        sendMessage("Sequence completed");
        setOstPropertyAttribute("actions", "status", IPS_OK, true);
        isSequenceRunning = false;

    }
    else
    {

        currentCount = getOstElementLineValue("sequence", "count", currentLine).toInt();
        currentExposure  = getOstElementLineValue("sequence", "exposure", currentLine).toDouble();
        setOstElementLineValue("sequence", "status", currentLine, "Running " + QString::number(currentCount));
        int i = getOstElementLineValue("sequence", "filter", currentLine).toInt();
        currentFilter = getOstElementLov("sequence", "filter", QString::number(i)).toString();
        sendModNewNumber(_fw, "FILTER_SLOT", "FILTER_SLOT_VALUE", i );
    }
}
