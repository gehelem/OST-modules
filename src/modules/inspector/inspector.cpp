#include "inspector.h"
#include <QPainter>

Inspector *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    Inspector *basemodule = new Inspector(name, label, profile, availableModuleLibs);
    return basemodule;
}

Inspector::Inspector(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)

{

    loadOstPropertiesFromFile(":inspector.json");
    setClassName(QString(metaObject()->className()).toLower());

    setModuleDescription("Inspector module - work in progress");
    setModuleVersion("0.1");

    defineMeAsSequencer();

    OST::ValueBool* b = new OST::ValueBool("Shoot", "0", "");
    getProperty("actions")->addValue("shoot", b);
    b = new OST::ValueBool("Loop", "2", "");
    b->setValue(false, false);
    getProperty("actions")->addValue("loop", b);
    b = new OST::ValueBool("Abort", "2", "");
    getProperty("actions")->addValue("abort", b);
    b->setValue(false, false);

    getProperty("actions")->deleteValue("startsequence");
    getProperty("actions")->deleteValue("abortsequence");

    OST::ValueImg* im = new OST::ValueImg("Image map", "2", "");
    getProperty("image")->addValue("imagemap", im);

}

Inspector::~Inspector()
{

}
void Inspector::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
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

void Inspector::newBLOB(INDI::PropertyBlob pblob)
{

    if (
        (QString(pblob.getDeviceName()) == getString("devices", "sequencercamera")) && (mState != "idle")
    )
    {
        getProperty("actions")->setState(OST::Ok);
        delete _image;
        _image = new fileio();
        _image->loadBlob(pblob, 64);
        stats = _image->getStats();
        _solver.ResetSolver(stats, _image->getImageBuffer());
        connect(&_solver, &Solver::successSEP, this, &Inspector::OnSucessSEP);
        _solver.FindStars(_solver.stellarSolverProfiles[0]);
    }



}

void Inspector::updateProperty(INDI::Property property)
{
    if (mState == "idle") return;

    if (strcmp(property.getName(), "CCD1") == 0)
    {
        newBLOB(property);
    }
    if (
        (property.getDeviceName() == getString("devices", "sequencercamera"))
        &&  (property.getState() == IPS_ALERT)
    )
    {
        sendWarning("cameraAlert");
        emit cameraAlert();
    }


    if (
        (property.getDeviceName() == getString("devices", "sequencercamera"))
        &&  (property.getName()   == std::string("CCD_FRAME_RESET"))
        &&  (property.getState() == IPS_OK)
    )
    {
        //sendMessage("FrameResetDone");
        emit FrameResetDone();
    }
}
void Inspector::Shoot()
{
    if (connectDevice(getString("devices", "sequencercamera")))
    {
        frameReset(getString("devices", "sequencercamera"));
        sendModNewNumber(getString("devices", "sequencercamera"), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", getFloat("parms",
                         "exposure"));
        getProperty("actions")->setState(OST::Busy);
    }
    else
    {
        getProperty("actions")->setState(OST::Error);
    }
}
void Inspector::initIndi()
{
    connectIndi();
    connectDevice(getString("devices", "sequencercamera"));
    setBLOBMode(B_ALSO, getString("devices", "sequencercamera").toStdString().c_str(), nullptr);
    if (getString("devices", "sequencercamera") == "CCD Simulator")
    {
        sendModNewNumber(getString("devices", "sequencercamera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
    }
    enableDirectBlobAccess(getString("devices", "sequencercamera").toStdString().c_str(), nullptr);

}

void Inspector::OnSucessSEP()
{
    qDebug() << "OnSucessSEP";

    getProperty("actions")->setState(OST::Ok);

    disconnect(&_solver, &Solver::successSEP, this, &Inspector::OnSucessSEP);


    //image->saveMapToJpeg(_webroot+"/"+_modulename+".jpeg",100,_solver.stars);
    QList<fileio::Record> rec = _image->getRecords();
    QImage rawImage = _image->getRawQImage();
    QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
    im.setColorTable(rawImage.colorTable());
    QImage immap = rawImage.convertToFormat(QImage::Format_RGB32);
    immap.setColorTable(rawImage.colorTable());

    im.save(getWebroot()  + "/" + getModuleName() + ".jpeg", "JPG", 100);
    OST::ImgData dta = _image->ImgStats();
    dta.mUrlJpeg = getModuleName() + ".jpeg";
    dta.HFRavg = _solver.HFRavg;
    dta.starsCount = _solver.stars.size();
    getValueImg("image", "image")->setValue(dta, true);

    //QRect r;
    //r.setRect(0,0,im.width(),im.height());

    /* Drw HFR ellipses around found stars */
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

    /* determine 4 corner HFR */
    int upperLeftI = 0;
    int lowerLeftI = 0;
    int upperRightI = 0;
    int lowerRightI = 0;
    upperLeftHFR = 0;
    lowerLeftHFR = 0;
    upperRightHFR = 0;
    lowerRightHFR = 0;
    foreach( FITSImage::Star s, _solver.stars )
    {
        if ( (s.x < (im.width() / 2)) && (s.y < (im.height() / 2) ))
        {
            upperLeftHFR = ( upperLeftI * upperLeftHFR + s.HFR) / (upperLeftI + 1);
            upperLeftI++;
        }
        if ( (s.x > (im.width() / 2)) && (s.y < (im.height() / 2) ))
        {
            upperRightHFR = (upperRightI * upperRightHFR + s.HFR ) / (upperRightI + 1);
            upperRightI++;
        }
        if ( (s.x < (im.width() / 2)) && (s.y > (im.height() / 2) ))
        {
            lowerLeftHFR = (lowerLeftI * lowerLeftHFR + s.HFR) / (lowerLeftI + 1);
            lowerLeftI++;
        }
        if ( (s.x > (im.width() / 2)) && (s.y > (im.height() / 2) ))
        {
            lowerRightHFR = (lowerRightI * lowerRightHFR + s.HFR ) / (lowerRightI + 1);
            lowerRightI++;
        }
    };

    p.setPen(QPen(Qt::white));
    int mul = 200;
    QVector<QPointF> hexPoints;
    hexPoints << QPointF(1 * im.width() / 4 - mul*(upperLeftHFR - _solver.HFRavg),
                         1 * im.height() / 4 - mul*(upperLeftHFR - _solver.HFRavg));
    hexPoints << QPointF(3 * im.width() / 4 + mul*(upperRightHFR - _solver.HFRavg),
                         1 * im.height() / 4 - mul*(upperRightHFR - _solver.HFRavg));
    hexPoints << QPointF(3 * im.width() / 4 - mul*(lowerRightHFR - _solver.HFRavg),
                         3 * im.height() / 4 + mul*(lowerRightHFR - _solver.HFRavg));
    hexPoints << QPointF(1 * im.width() / 4 + mul*(lowerLeftHFR - _solver.HFRavg),
                         3 * im.height() / 4 + mul*(lowerLeftHFR - _solver.HFRavg));
    p.drawPolygon(hexPoints);
    p.setFont(QFont("Courrier", im.width() / 50, QFont::Normal));
    p.drawText(  QRect(0, 0, im.width(), im.height()), Qt::AlignCenter, QString::number(_solver.HFRavg, 'f', 3));
    p.drawText(1 * im.width() / 4, 1 * im.height() / 4, QString::number(upperLeftHFR, 'f', 3));
    p.drawText(3 * im.width() / 4, 1 * im.height() / 4, QString::number(upperRightHFR, 'f', 3));
    p.drawText(1 * im.width() / 4, 3 * im.height() / 4, QString::number(lowerLeftHFR, 'f', 3));
    p.drawText(3 * im.width() / 4, 3 * im.height() / 4, QString::number(lowerRightHFR, 'f', 3));

    p.end();


    immap.save(getWebroot() + "/" + getModuleName() + "map.jpeg", "JPG", 100);
    OST::ImgData dta2 = _image->ImgStats();
    dta2.mUrlJpeg = getModuleName() + "map.jpeg";
    getValueImg("image", "imagemap")->setValue(dta2, true);

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
