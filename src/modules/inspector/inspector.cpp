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

    giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
    defineMeAsSequencer();

    OST::ElementBool* b = new OST::ElementBool("Shoot", "0", "");
    getProperty("actions")->addElt("shoot", b);
    b = new OST::ElementBool("Loop", "2", "");
    b->setValue(false, false);
    getProperty("actions")->addElt("loop", b);
    b = new OST::ElementBool("Abort", "2", "");
    getProperty("actions")->addElt("abort", b);
    b->setValue(false, false);

    getProperty("actions")->deleteElt("startsequence");
    getProperty("actions")->deleteElt("abortsequence");

    OST::ElementImg* im = new OST::ElementImg("Image map", "2", "");
    getProperty("image")->addElt("imagemap", im);
    im = new OST::ElementImg("Corners", "3", "");
    getProperty("image")->addElt("corners", im);

    auto* i = new OST::ElementInt("Corner size (pixels)", "50", "");
    i->setAutoUpdate(true);
    i->setDirectEdit(true);
    i->setMinMax(0, 2000);
    i->setValue(200);
    i->setSlider(OST::SliderAndValue);

    getProperty("parms")->addElt("cornersize", i);

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
                if (keyprop == "actions")
                {
                    if (keyelt == "shoot")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(false))
                        {
                            mState = "single";
                            initIndi();
                            Shoot();
                        }
                    }
                    if (keyelt == "loop")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(true, true))
                        {
                            mState = "loop";
                            initIndi();
                            Shoot();
                        }
                    }
                    if (keyelt == "abort")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(false, true))
                        {
                            getEltBool("actions", "loop")->setValue(false, true);
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
        (QString(pblob.getDeviceName()) == getString("devices", "camera")) && (mState != "idle")
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
}
void Inspector::Shoot()
{
    if (connectDevice(getString("devices", "camera")))
    {
        frameReset(getString("devices", "camera"));
        requestCapture(getString("devices", "camera"), getFloat("parms", "exposure"), getInt("parms", "gain"), getInt("parms",
                       "offset"));
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
    connectDevice(getString("devices", "camera"));
    setBLOBMode(B_ALSO, getString("devices", "camera").toStdString().c_str(), nullptr);
    if (getString("devices", "camera") == "CCD Simulator")
    {
        sendModNewNumber(getString("devices", "camera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
    }
    enableDirectBlobAccess(getString("devices", "camera").toStdString().c_str(), nullptr);

}

void Inspector::OnSucessSEP()
{
    //qDebug() << "OnSucessSEP";

    getProperty("actions")->setState(OST::Ok);

    disconnect(&_solver, &Solver::successSEP, this, &Inspector::OnSucessSEP);


    //image->saveMapToJpeg(_webroot+"/"+_modulename+".jpeg",100,_solver.stars);
    QList<fileio::Record> rec = _image->getRecords();
    QImage rawImage = _image->getRawQImage();
    QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
    im.setColorTable(rawImage.colorTable());
    QImage immap = rawImage.convertToFormat(QImage::Format_RGB32);
    immap.setColorTable(rawImage.colorTable());

    double ech = getSampling();
    im.save(getWebroot()  + "/" + getModuleName() + ".jpeg", "JPG", 100);
    OST::ImgData dta = _image->ImgStats();
    dta.mUrlJpeg = getModuleName() + ".jpeg";
    dta.HFRavg = ech * _solver.HFRavg;
    dta.starsCount = _solver.stars.size();
    getEltImg("image", "image")->setValue(dta, true);

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
            upperLeftHFR = ( upperLeftI * upperLeftHFR + s.HFR * ech) / (upperLeftI + 1);
            upperLeftI++;
        }
        if ( (s.x > (im.width() / 2)) && (s.y < (im.height() / 2) ))
        {
            upperRightHFR = (upperRightI * upperRightHFR + s.HFR * ech ) / (upperRightI + 1);
            upperRightI++;
        }
        if ( (s.x < (im.width() / 2)) && (s.y > (im.height() / 2) ))
        {
            lowerLeftHFR = (lowerLeftI * lowerLeftHFR + s.HFR * ech) / (lowerLeftI + 1);
            lowerLeftI++;
        }
        if ( (s.x > (im.width() / 2)) && (s.y > (im.height() / 2) ))
        {
            lowerRightHFR = (lowerRightI * lowerRightHFR + s.HFR * ech) / (lowerRightI + 1);
            lowerRightI++;
        }
    };

    p.setPen(QPen(Qt::white));
    int mul = 200;
    QVector<QPointF> hexPoints;
    hexPoints << QPointF(1 * im.width() / 4 - mul*(upperLeftHFR - _solver.HFRavg*ech),
                         1 * im.height() / 4 - mul*(upperLeftHFR - _solver.HFRavg*ech));
    hexPoints << QPointF(3 * im.width() / 4 + mul*(upperRightHFR - _solver.HFRavg*ech),
                         1 * im.height() / 4 - mul*(upperRightHFR - _solver.HFRavg*ech));
    hexPoints << QPointF(3 * im.width() / 4 - mul*(lowerRightHFR - _solver.HFRavg*ech),
                         3 * im.height() / 4 + mul*(lowerRightHFR - _solver.HFRavg*ech));
    hexPoints << QPointF(1 * im.width() / 4 + mul*(lowerLeftHFR - _solver.HFRavg*ech),
                         3 * im.height() / 4 + mul*(lowerLeftHFR - _solver.HFRavg*ech));
    p.drawPolygon(hexPoints);
    p.setFont(QFont("Courrier", im.width() / 50, QFont::Normal));
    p.drawText(  QRect(0, 0, im.width(), im.height()), Qt::AlignCenter, QString::number(_solver.HFRavg*ech, 'f', 3) + "''");
    p.drawText(1 * im.width() / 4, 1 * im.height() / 4, QString::number(upperLeftHFR, 'f', 3) + "''");
    p.drawText(3 * im.width() / 4, 1 * im.height() / 4, QString::number(upperRightHFR, 'f', 3) + "''");
    p.drawText(1 * im.width() / 4, 3 * im.height() / 4, QString::number(lowerLeftHFR, 'f', 3) + "''");
    p.drawText(3 * im.width() / 4, 3 * im.height() / 4, QString::number(lowerRightHFR, 'f', 3) + "''");

    p.end();


    immap.save(getWebroot() + "/" + getModuleName() + "map.jpeg", "JPG", 100);
    OST::ImgData dta2 = _image->ImgStats();
    dta2.mUrlJpeg = getModuleName() + "map.jpeg";
    getEltImg("image", "imagemap")->setValue(dta2, true);

    int s = getInt("parms", "cornersize");
    int h = rawImage.height();
    int w = rawImage.width();

    QImage corners = QImage(3 * s, 3 * s, QImage::Format_RGB32);
    corners.setColorTable(rawImage.colorTable());
    corners.fill(Qt::green);
    QPainter painter(&corners);

    painter.drawImage(QRect(0 * s, 0 * s, s, s), rawImage, QRect(0, 0, s, s)); //upper left
    painter.drawImage(QRect(1 * s, 0 * s, s, s), rawImage, QRect(w / 2 - s / 2, 0, s, s)); //upper middle
    painter.drawImage(QRect(2 * s, 0 * s, s, s), rawImage, QRect(w - s, 0, s, s)); //upper right

    painter.drawImage(QRect(0 * s, 1 * s, s, s), rawImage, QRect(0, h / 2 - s / 2, s, s)); //middle left
    painter.drawImage(QRect(1 * s, 1 * s, s, s), rawImage, QRect(w / 2 - s / 2, h / 2 - s / 2, s, s)); //middle middle
    painter.drawImage(QRect(2 * s, 1 * s, s, s), rawImage, QRect(w - s, h / 2 - s / 2, s, s)); //middle right

    painter.drawImage(QRect(0 * s, 2 * s, s, s), rawImage, QRect(0, h - s, s, s)); //lower left
    painter.drawImage(QRect(1 * s, 2 * s, s, s), rawImage, QRect(w / 2 - s / 2, h - s, s, s)); //lower middle
    painter.drawImage(QRect(2 * s, 2 * s, s, s), rawImage, QRect(w - s, h - s, s, s)); //lower right

    painter.setPen(QPen(Qt::red));
    painter.drawRect(QRect(s, 0, s, 3 * s - 1));
    painter.drawRect(QRect(0, s, 3 * s - 1, s));
    painter.drawRect(QRect(0, 0, 3 * s - 1, 3 * s - 1));

    painter.end();

    corners.save(getWebroot() + "/" + getModuleName() + "corners.jpeg", "JPG", 100);
    OST::ImgData dta3;
    dta3.height = 3 * s;
    dta3.width = 3 * s;
    dta3.mUrlJpeg = getModuleName() + "corners.jpeg";
    getEltImg("image", "corners")->setValue(dta3, true);

    emit FindStarsDone();

    if (mState == "single")
    {
        mState = "idle";
    }
    if (mState == "loop")
    {
        Shoot();
    }


}
