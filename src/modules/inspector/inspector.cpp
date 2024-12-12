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
    b->setValue(false, false);
    getProperty("actions")->addElt("shoot", b);

    b = new OST::ElementBool("Loop", "1", "");
    b->setValue(false, false);
    getProperty("actions")->addElt("loop", b);

    b = new OST::ElementBool("Reload", "2", "");
    b->setValue(false, false);
    getProperty("actions")->addElt("reload", b);

    b = new OST::ElementBool("Abort", "3", "");
    b->setValue(false, false);
    getProperty("actions")->addElt("abort", b);

    getProperty("actions")->deleteElt("startsequence");
    getProperty("actions")->deleteElt("abortsequence");

    auto* i = new OST::ElementInt("Corner size (pixels)", "50", "");
    i->setAutoUpdate(true);
    i->setDirectEdit(true);
    i->setMinMax(0, 2000);
    i->setValue(200);
    i->setSlider(OST::SliderAndValue);

    getProperty("parms")->addElt("cornersize", i);
    connect(this, &Inspector::newImage, this, &Inspector::OnNewImage);

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
                    if (keyelt == "reload")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(false, true))
                        {
                            emit newImage();
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
                if (keyprop == "fileselect")
                {
                    if (keyelt == "name")
                    {
                        if (eventType == "Fposticon")
                        {
                            getProperty("actions")->setState(OST::Ok);
                            delete _image;
                            _image = new fileio();
                            _image->loadFits(getString("fileselect", "name"));
                            _image->generateQImage();
                            stats = _image->getStats();
                            emit newImage();
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
        // testing : load fits, comment previous and uncomment **2** lines below
        //_image->loadFits("/pathoftheimage/Light_LLL_008.fits");
        //_image->generateQImage();
        stats = _image->getStats();
        emit newImage();
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
    QImage imHFR = rawImage.convertToFormat(QImage::Format_ARGB32);
    //imHFR.setColorTable(rawImage.colorTable());
    QImage imShape = rawImage.convertToFormat(QImage::Format_RGB32);
    imShape.setColorTable(rawImage.colorTable());
    int imgWidth = _image->getStats().width;
    int imgHeight = _image->getStats().height;

    double ech = getSampling();

    /****************************************************************** HFR */
    /****************************************************************** HFR */
    /****************************************************************** HFR */
    /****************************************************************** HFR */

    /* smoothing avg 8 surround cell */
    QList<float> smoothedHFR = _solver.HFRavgZone;
    for (int line = 1; line < _solver.HFRZones - 1 ; line++)
    {
        for (int column = 1; column < _solver.HFRZones - 1; column++)
        {
            int i = _solver.HFRZones * line + column;
            smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column - 1];
            smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column - 0];
            smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column + 1];
            smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 0) + column - 1];
            smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 0) + column + 1];
            smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column - 1];
            smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column - 0];
            smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column + 1];
            smoothedHFR[i] = smoothedHFR[i] / 9;
        }
    }
    /* first row */
    for (int column = 1; column < _solver.HFRZones - 1; column++)
    {
        int line = 0;
        int i = _solver.HFRZones * line + column;
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 0) + column - 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 0) + column + 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column - 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column - 0];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column + 1];
        smoothedHFR[i] = smoothedHFR[i] / 6;
    }
    /* last row */
    for (int column = 1; column < _solver.HFRZones - 1; column++)
    {
        int line = _solver.HFRZones - 1;
        int i = _solver.HFRZones * line + column;
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column - 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column - 0];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column + 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 0) + column - 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 0) + column + 1];
        smoothedHFR[i] = smoothedHFR[i] / 6;
    }
    /* first column */
    for (int line = 1; line < _solver.HFRZones - 1 ; line++)
    {
        int column = 0;
        int i = _solver.HFRZones * line + column;
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column - 0];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column + 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 0) + column + 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column - 0];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column + 1];
        smoothedHFR[i] = smoothedHFR[i] / 6;
    }
    /* last column */
    for (int line = 1; line < _solver.HFRZones - 1 ; line++)
    {
        int column = _solver.HFRZones - 1;
        int i = _solver.HFRZones * line + column;
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column - 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column - 0];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 0) + column - 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column - 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column - 0];
        smoothedHFR[i] = smoothedHFR[i] / 6;
    }
    /*corners*/
    {
        /* upper left */
        int line = 0;
        int column = 0;
        int i = _solver.HFRZones * line + column;
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 0) + column + 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column + 0];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column + 1];
        smoothedHFR[i] = smoothedHFR[i] / 4;
        /* upper right */
        line = 0;
        column = _solver.HFRZones - 1;
        i = _solver.HFRZones * line + column;
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 0) + column - 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column + 0];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 1) + column - 1];
        smoothedHFR[i] = smoothedHFR[i] / 4;
        /* lower left */
        line = _solver.HFRZones - 1;
        column = 0;
        i = _solver.HFRZones * line + column;
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column + 0];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column + 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 0) + column + 1];
        smoothedHFR[i] = smoothedHFR[i] / 4;
        /* lower right */
        line = _solver.HFRZones - 1;
        column = _solver.HFRZones - 1;
        i = _solver.HFRZones * line + column;
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column + 0];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line - 1) + column - 1];
        smoothedHFR[i] = smoothedHFR[i] + _solver.HFRavgZone[_solver.HFRZones * (line + 0) + column - 1];
        smoothedHFR[i] = smoothedHFR[i] / 4;
    }
    /* calculate min & max HFR */
    float minHFR = 99;
    float maxHFR = 0;
    for (int i = 0; i < _solver.HFRZones * _solver.HFRZones ; i++)
    {
        if (smoothedHFR[i] < minHFR) minHFR = smoothedHFR[i];
        if ((smoothedHFR[i] > maxHFR) && (smoothedHFR[i] < 99)) maxHFR = smoothedHFR[i];
    }
    /*zoning*/
    QPainter p2;
    p2.begin(&imHFR);
    p2.setOpacity(0.5);
    for (int line = 0; line < _solver.HFRZones ; line++)
    {
        for (int column = 0; column < _solver.HFRZones; column++)
        {
            int zone = _solver.HFRZones * line + column;

            int x = (imgWidth / _solver.HFRZones) * (column) ;
            int y = (imgHeight / _solver.HFRZones) * (line) ;
            int dx = imgWidth / _solver.HFRZones;
            int dy = imgHeight / _solver.HFRZones;

            qreal alpha = 1 * (smoothedHFR[zone] - minHFR) / (maxHFR - minHFR);
            unsigned int r = 255 * (smoothedHFR[zone] - minHFR) / (maxHFR - minHFR);
            unsigned int g = 255 * (maxHFR - smoothedHFR[zone]) / (maxHFR - minHFR);
            p2.fillRect(QRect(x / 2, y / 2, dx / 2, dy / 2), qRgb(r, 0, 0));


        }
    }
    p2.end();

    /*surround stars*/
    p2.begin(&imHFR);
    p2.setPen(QPen(Qt::blue, 10));
    foreach( FITSImage::Star s, _solver.stars )
    {
        QPainter p2;
        int x = s.x;
        int y = s.y;
        int hfr = s.HFR;
        p2.drawEllipse(QPoint(x / 2, y / 2), hfr, hfr);
    }
    p2.end();




    /* HFR rectangle*/
    QPainter p;
    p.begin(&imHFR);
    p.setPen(QPen(Qt::red));
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
    hexPoints << QPointF(1 * im.width() / 4 - mul * (upperLeftHFR - _solver.HFRavg * ech),
                         1 * im.height() / 4 - mul * (upperLeftHFR - _solver.HFRavg * ech));
    hexPoints << QPointF(3 * im.width() / 4 + mul * (upperRightHFR - _solver.HFRavg * ech),
                         1 * im.height() / 4 - mul * (upperRightHFR - _solver.HFRavg * ech));
    hexPoints << QPointF(3 * im.width() / 4 - mul * (lowerRightHFR - _solver.HFRavg * ech),
                         3 * im.height() / 4 + mul * (lowerRightHFR - _solver.HFRavg * ech));
    hexPoints << QPointF(1 * im.width() / 4 + mul * (lowerLeftHFR - _solver.HFRavg * ech),
                         3 * im.height() / 4 + mul * (lowerLeftHFR - _solver.HFRavg * ech));
    p.drawPolygon(hexPoints);
    p.setFont(QFont("Courrier", im.width() / 50, QFont::Normal));
    p.drawText(  QRect(0, 0, im.width(), im.height()), Qt::AlignCenter, QString::number(_solver.HFRavg * ech, 'f', 3) + "''");
    p.drawText(1 * im.width() / 4, 1 * im.height() / 4, QString::number(upperLeftHFR, 'f', 3) + "''");
    p.drawText(3 * im.width() / 4, 1 * im.height() / 4, QString::number(upperRightHFR, 'f', 3) + "''");
    p.drawText(1 * im.width() / 4, 3 * im.height() / 4, QString::number(lowerLeftHFR, 'f', 3) + "''");
    p.drawText(3 * im.width() / 4, 3 * im.height() / 4, QString::number(lowerRightHFR, 'f', 3) + "''");

    p.end();

    imHFR.save(getWebroot() + "/" + getModuleName() + "HFR.jpeg", "JPG", 100);

    /****************************************************************** corners */
    /****************************************************************** corners */
    /****************************************************************** corners */
    /****************************************************************** corners */

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
    im.save(getWebroot()  + "/" + getModuleName() + ".jpeg", "JPG", 100);

    /****************************************************************** aberations */
    /****************************************************************** aberations */
    /****************************************************************** aberations */
    /****************************************************************** aberations */

    int ellipseSize = 0.20 * imgHeight / _solver.HFRZones;

    /* min and max aberations */
    float eMin = 99;
    float eMax = 0;
    for (int line = 0; line < _solver.HFRZones ; line++)
    {
        for (int column = 0; column < _solver.HFRZones; column++)
        {
            int zone = _solver.HFRZones * line + column;
            float e = _solver.aAxeAvgZone[zone] / _solver.bAxeAvgZone[zone] - 1;
            if (e < eMin) eMin = e;
            if (e > eMax) eMax = e;
        }
    }

    /* draw aberations */
    p2.begin(&imShape);
    p2.setOpacity(0.5);
    p2.setPen(QPen(Qt::red, 10));
    for (int line = 0; line < _solver.HFRZones ; line++)
    {
        for (int column = 0; column < _solver.HFRZones; column++)
        {
            int zone = _solver.HFRZones * line + column;

            int x = (imgWidth / _solver.HFRZones) * (column + 0.5) ;
            int y = (imgHeight / _solver.HFRZones) * (line + 0.5) ;
            float e = _solver.aAxeAvgZone[zone] / _solver.bAxeAvgZone[zone] - 1;
            float dx = (0.5 * imgWidth / _solver.HFRZones) * ((e - eMin) / (eMax - eMin)) * cos(_solver.thetaAvgZone[zone] * 3.14159 /
                       360);
            float dy = (0.5 * imgWidth / _solver.HFRZones) * ((e - eMin) / (eMax - eMin)) * sin(_solver.thetaAvgZone[zone] * 3.14159 /
                       360);

            p2.drawLine(x / 2 - dx, y / 2 - dy, x / 2 + dx, y / 2 + dy);
        }
    }
    p2.end();
    imShape.save(getWebroot() + "/" + getModuleName() + "shape.jpeg", "JPG", 100);


    OST::ImgData dta = _image->ImgStats();
    dta.mUrlJpeg = getModuleName() + ".jpeg";
    dta.HFRavg = ech * _solver.HFRavg;
    dta.starsCount = _solver.stars.size();
    dta.mAlternates.clear();
    dta.mAlternates.push_front(getModuleName() + "corners.jpeg");
    dta.mAlternates.push_front(getModuleName() + "HFR.jpeg");
    dta.mAlternates.push_front(getModuleName() + "shape.jpeg");
    getEltImg("image", "image")->setValue(dta, true);

    dta = _image->ImgStats();
    dta.mUrlJpeg = getModuleName() + "corners.jpeg";
    getEltImg("corners", "image")->setValue(dta, true);
    dta.mUrlJpeg = getModuleName() + "HFR.jpeg";
    getEltImg("hfr", "image")->setValue(dta, true);
    dta.mUrlJpeg = getModuleName() + "shape.jpeg";
    getEltImg("shape", "image")->setValue(dta, true);



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
void Inspector::OnNewImage()
{
    _solver.ResetSolver(stats, _image->getImageBuffer(), getInt("parameters", "zoning"));
    connect(&_solver, &Solver::successSEP, this, &Inspector::OnSucessSEP);
    _solver.FindStars(_solver.stellarSolverProfiles[0]);
}
