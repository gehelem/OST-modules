#include "inspector.h"
#include <QPainter>

InspectorModule *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    InspectorModule *basemodule = new InspectorModule(name, label, profile, availableModuleLibs);
    return basemodule;
}

InspectorModule::InspectorModule(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)

{

    loadOstPropertiesFromFile(":inspector.json");
    setClassName(QString(metaObject()->className()).toLower());

    setModuleDescription("Inspector module - work in progress");
    setModuleVersion("0.1");

    createOstElementText("devices", "camera", "Camera", true);
    setOstElementValue("devices", "camera",   _camera, false);

    //saveAttributesToFile("inspector.json");
    _camera = getString("devices", "camera");
    _exposure = getFloat("parameters", "exposure");
}

InspectorModule::~InspectorModule()
{

}
void InspectorModule::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                                        const QVariantMap &eventData)
{
    //sendMessage("OnMyExternalEvent - recv : " + getModuleName() + "-" + eventType + "-" + eventKey);
    Q_UNUSED(eventType);
    Q_UNUSED(eventKey);

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
                            getProperty(keyprop)->setState(OST::Ok);
                            _camera = getString("devices", "camera");
                        }
                    }
                }

                if (keyprop == "parameters")
                {
                    if (keyelt == "exposure")
                    {
                        if (setOstElementValue(keyprop, keyelt, eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"], false))
                        {
                            getProperty(keyprop)->setState(OST::Ok);
                            _exposure = getFloat("parameters", "exposure");
                        }
                    }
                }
                if (keyprop == "actions")
                {
                    if (keyelt == "shoot")
                    {
                        if (setOstElementValue(keyprop, keyelt, true, true))
                        {
                            mState = "single";
                            initIndi();
                            Shoot();
                        }
                    }
                    if (keyelt == "loop")
                    {
                        if (setOstElementValue(keyprop, keyelt, true, true))
                        {
                            mState = "loop";
                            initIndi();
                            Shoot();
                        }
                    }
                    if (keyelt == "abort")
                    {
                        if (setOstElementValue(keyprop, keyelt, false, false))
                        {
                            emit Abort();
                            mState = "idle";
                            getProperty("actions")->setState(OST::Ok);
                        }
                    }
                }
            }
        }
    }
}

void InspectorModule::newBLOB(INDI::PropertyBlob pblob)
{

    if (
        (QString(pblob.getDeviceName()) == _camera) && (mState != "idle")
    )
    {
        getProperty("actions")->setState(OST::Ok);
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
        //sendMessage("SMFindStars");
        _solver.ResetSolver(stats, _image->getImageBuffer());
        connect(&_solver, &Solver::successSEP, this, &InspectorModule::OnSucessSEP);
        _solver.FindStars(_solver.stellarSolverProfiles[0]);
    }



}

void InspectorModule::updateProperty(INDI::Property property)
{
    if (mState == "idle") return;

    if (strcmp(property.getName(), "CCD1") == 0)
    {
        newBLOB(property);
    }
    if (
        (property.getDeviceName() == _camera)
        &&  (property.getState() == IPS_ALERT)
    )
    {
        sendWarning("cameraAlert");
        emit cameraAlert();
    }


    if (
        (property.getDeviceName() == _camera)
        &&  (property.getName()   == std::string("CCD_FRAME_RESET"))
        &&  (property.getState() == IPS_OK)
    )
    {
        //sendMessage("FrameResetDone");
        emit FrameResetDone();
    }
}
void InspectorModule::Shoot()
{
    if (connectDevice(_camera))
    {
        frameReset(_camera);
        sendModNewNumber(_camera, "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", _exposure);
        getProperty("actions")->setState(OST::Busy);
    }
    else
    {
        getProperty("actions")->setState(OST::Error);
    }
}
void InspectorModule::initIndi()
{
    connectIndi();
    connectDevice(_camera);
    setBLOBMode(B_ALSO, _camera.toStdString().c_str(), nullptr);
    //sendModNewNumber(_camera,"SIMULATOR_SETTINGS","SIM_TIME_FACTOR",0.01 );
    enableDirectBlobAccess(_camera.toStdString().c_str(), nullptr);

}

void InspectorModule::OnSucessSEP()
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

    im.save(getWebroot()  + "/" + getModuleName() + ".jpeg", "JPG", 100);
    OST::ImgData dta;
    dta.mUrlJpeg = getModuleName() + ".jpeg";
    getValueImg("image", "image1")->setValue(dta, true);

    //QRect r;
    //r.setRect(0,0,im.width(),im.height());

    QPainter p;
    p.begin(&immap);
    p.setPen(QPen(Qt::red));
    //p.setFont(QFont("Times", 15, QFont::Normal));
    //p.drawText(r, Qt::AlignLeft, QDateTime::currentDateTime().toString("dd/MM/yyyy hh:mm:ss zzz") );
    p.setPen(QPen(Qt::green));
    foreach( FITSImage::Star s, _solver.stars )
    {
        //qDebug() << "draw " << s.x << "/" << s.y;
        int x = s.x;
        int y = s.y;
        int a = s.a;
        int b = s.b;
        //qDebug() << "draw " << x << "/" << y;
        p.drawEllipse(QPoint(x / 2, y / 2), a * 5, b * 5);
    }
    p.end();

    getProperty("histogram")->clearGrid();
    QVector<double> his = _image->getHistogramFrequency(0);
    for( int i = 1; i < his.size(); i++)
    {
        //qDebug() << "HIS " << i << "-"  << _image->getCumulativeFrequency(0)[i] << "-"  << _image->getHistogramIntensity(0)[i] << "-"  << _image->getHistogramFrequency(0)[i];
        setOstElementValue("histogram", "i", i, false);
        setOstElementValue("histogram", "n", his[i], false);
        getProperty("histogram")->push();
    }

    immap.save(getWebroot() + "/" + getModuleName() + "map.jpeg", "JPG", 100);
    OST::ImgData dta2;
    dta2.mUrlJpeg = getModuleName() + "map.jpeg";
    getValueImg("imagemap", "image1")->setValue(dta2, true);

    if (mState == "single")
    {
        mState = "idle";
    }
    if (mState == "loop")
    {
        Shoot();
    }

    emit FindStarsDone();
}
