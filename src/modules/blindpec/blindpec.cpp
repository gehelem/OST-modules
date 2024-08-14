#include "blindpec.h"
#include "CV_SubPix.h"
#include <QtWidgets>
#include <QImage>

#define PI 3.14159265

BlindPec *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    BlindPec *basemodule = new BlindPec(name, label, profile, availableModuleLibs);
    return basemodule;
}
BlindPec::BlindPec(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)
{

    loadOstPropertiesFromFile(":blindpec.json");
    setClassName(QString(metaObject()->className()).toLower());
    setModuleDescription("Blind PEC - experimental");
    setModuleVersion("0.1");

    giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
    giveMeADevice("guider", "Guide via", INDI::BaseDevice::GUIDER_INTERFACE);

    defineMeAsGuider();
    connect(&mytimer, &QTimer::timeout, this, &BlindPec::onTimer);

    if (QCameraInfo::availableCameras().count() > 0)
    {
        qDebug() << "Caméra(s) disponible(s)" << QCameraInfo::availableCameras().count();
        QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
        foreach (const QCameraInfo &cameraInfo, cameras)
        {
            qDebug() << "Device" << cameraInfo.deviceName();
            qDebug() << "Description" << cameraInfo.description();
            if (cameraInfo.position() == QCamera::FrontFace)
                qDebug() << "Position : FrontFace";
            else if (cameraInfo.position() == QCamera::BackFace)
                qDebug() << "Position : BackFace";
            qDebug() << "Orientation" << cameraInfo.orientation() << "degrés";
        }
    }
    else
    {
        qDebug() << "Aucune caméra disponible !";
    }
    qRegisterMetaType< cv::Mat >("cv::Mat");

    Mat test(200, 100, CV_8UC3, Scalar(0, 0, 0));
    QImage image(test.data, test.cols, test.rows, test.step, QImage::Format_RGB888);
    image.save("test.jpg", "JPG", 100);




}

BlindPec::~BlindPec()
{

}
void BlindPec::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                                 const QVariantMap &eventData)
{
    Q_UNUSED(eventType);
    Q_UNUSED(eventKey);

    //BOOST_LOG_TRIVIAL(debug) << "OnMyExternalEvent - recv : " << getName().toStdString() << "-" << eventType.toStdString() << "-" << eventKey.toStdString();
    if (getModuleName() == eventModule)
    {
        foreach(const QString &keyprop, eventData.keys())
        {
            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                QVariant val = eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"];
                if (keyprop == "guideParams"  && keyelt == "graphsize" )
                {
                    if (val.toInt() <= 0 )
                    {
                        sendWarning("Invalid graph size value(" + val.toString() + ")" );
                    }
                    else
                    {
                        getEltInt("guideParams", "graphsize")->setValue(val.toInt(), true);
                        getProperty("guiding")->setGridLimit(val.toInt());

                    }
                }
                if (keyprop == "disCorrections"  && (keyelt == "disRA+" || keyelt == "disRA-" || keyelt == "disDE+" || keyelt == "disDE-"))
                {
                }
                if (keyprop == "actions")
                {
                    if (keyelt == "calguide")
                    {
                    }
                    if (keyelt == "abortguider")
                    {
                        //getProperty("guiding")->clearGrid();
                        //mytimer.stop();
                        disconnect(acquisition, &TAcquisitionVideo::nouvelleFrame, this, &BlindPec::traiterFrame);
                        acquisition->requestInterruption();
                        acquisition->wait();
                        delete acquisition;
                        acquisition = NULL;
                    }
                    if (keyelt == "calibrate")
                    {
                    }
                    if (keyelt == "guide")
                    {
                        qDebug() << "guide";
                        getProperty("guiding")->clearGrid();
                        QString strdt = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
                        strdt.replace("T", " ");
                        outfile.setFileName(QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + ".csv");
                        guidelog.setFileName("PHD2_GuideLog_" + QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + ".txt");
                        if (guidelog.open(QIODevice::WriteOnly | QIODevice::Append))
                        {
                            QTextStream in(&guidelog);
                            in << "BlindPEC\n";
                            in << "\n";
                            in << "Guiding Begins at " << strdt << "\n";
                            //in << "Pixel scale = 2.75 arc-sec/px, Binning = 1, Focal length = 180 mm\n";
                            in << "Pixel scale = " << QString::number(15 / getFloat("guideParams", "pixsec")).replace(',',
                                    '.') << " arc-sec/px, Binning = 1, Focal length = 800 mm\n";
                            in << "RA = 03.48 hr, Dec = 0.0 deg, Hour angle = N/A hr, Pier side = East, Rotator pos = N/A, Alt = 52.1 deg, Az = 0.01 deg\n";
                            in << "Mount = mount, xAngle = 263.6, xRate = 2.370, yAngle = 40.7, yRate = 6.464\n";
                            in << "Frame,Time,mount,dx,dy,RARawDistance,DECRawDistance,RAGuideDistance,DECGuideDistance,RADuration,RADirection,DECDuration,DECDirection,XStep,YStep,StarMass,SNR,ErrorCode\n";
                        }
                        guidelog.close();
                        numframe = 0;
                        offset = 0;
                        offsety = 0;
                        driftprev = 0;
                        drifts.clear();
                        avgdrifts.clear();
                        dtStart = QDateTime::currentDateTime();
                        //mytimer.start(getInt("guideParams", "interval") );
                        acquisition = new TAcquisitionVideo(QCameraInfo::availableCameras().count() - 1);
                        connect(acquisition, &TAcquisitionVideo::nouvelleFrame, this, &BlindPec::traiterFrame);
                        acquisition->start();
                    }
                }
            }
        }
    }
}

void BlindPec::updateProperty(INDI::Property property)
{
    //qDebug() << property.getDeviceName() <<  property.getName();

    if (strcmp(property.getName(), "CCD1") == 0)
    {
        newBLOB(property);
    }
    if (
        (property.getDeviceName() == getString("devices", "camera"))
        &&  (QString(property.getName()) == "CCD_FRAME_RESET")
        &&  (property.getState() == IPS_OK)
    )
    {
        //sendMessage("FrameResetDone");
        emit FrameResetDone();
    }
    if (
        (property.getDeviceName() == getString("devices", "guider")) &&
        (QString(property.getName())   == "TELESCOPE_TIMED_GUIDE_NS") &&
        (property.getState()  == IPS_OK)

    )
    {
        _pulseDECfinished = true;
    }

    if (
        (property.getDeviceName() == getString("devices", "guider")) &&
        (QString(property.getName())  == "TELESCOPE_TIMED_GUIDE_WE") &&
        (property.getState()  == IPS_OK)

    )
    {
        //sendMessage("_pulseRAfinished");
        _pulseRAfinished = true;
    }

    if (
        (property.getDeviceName() == getString("devices", "guider")) &&
        ( (QString(property.getName())   == "TELESCOPE_TIMED_GUIDE_WE") ||
          (QString(property.getName())  == "TELESCOPE_TIMED_GUIDE_NS") ) &&
        (property.getState()  == IPS_OK)

    )
    {
        if (_pulseRAfinished && _pulseDECfinished)
        {
            //SMRequestExposure();
        }
    }


}

void BlindPec::newBLOB(INDI::PropertyBlob pblob)
{
    if (
        (QString(pblob.getDeviceName()) == getString("devices", "camera"))
    )
    {
        dt = QDateTime::currentDateTime();
        delete _image;
        _image = new fileio();
        _image->loadBlob(pblob);
        stats = _image->getStats();
        QImage rawImage = _image->getRawQImage();
        QPainter qPainter(&rawImage);
        qPainter.setBrush(Qt::NoBrush);
        qPainter.setPen(Qt::green);
        qPainter.drawRect(roix / 2, roiy / 2, roidx / 2, roidy / 2);

        qPainter.setBrush(Qt::NoBrush);
        qPainter.setPen(Qt::red);
        qPainter.drawRect(matchLoc.x / 2, matchLoc.y / 2, roidx / 2, roidy / 2);
        qPainter.drawRect((matchLoc.x + 1) / 2, (matchLoc.y + 1) / 2, (roidx - 2) / 2, (roidy - 2) / 2);
        qPainter.end();
        QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
        im.setColorTable(rawImage.colorTable());

        im.save(getWebroot() + "/" + getModuleName() + ".jpeg", "JPG", 100);
        OST::ImgData dta = _image->ImgStats();
        dta.mUrlJpeg = getModuleName() + ".jpeg";
        getEltImg("image", "image")->setValue(dta, true);

        /**************************************************************************************/
        uint8_t *m_ImageBuffer { nullptr };
        /// Above buffer size in bytes
        uint32_t m_ImageBufferSize { 0 };
        fitsfile *fptr { nullptr };
        int status = 0, anynullptr = 0;
        m_ImageBufferSize = stats.samples_per_channel * stats.channels * static_cast<uint16_t>(stats.bytesPerPixel);
        m_ImageBuffer = new uint8_t[m_ImageBufferSize];
        size_t bsize = static_cast<size_t>(pblob[0].getBlobLen());

        if (fits_open_memfile(&fptr, "", READONLY, &pblob[0].cast()->blob, &bsize, 0, NULL, &status) )

        {
            sendMessage("IMG Unsupported type or read error loading FITS blob");
            return;
        }
        if (m_ImageBuffer == nullptr)
        {
            sendError("FITSData: Not enough memory for image_buffer channel. Requested: " + QString::number(
                          m_ImageBufferSize)  + " bytes ");
            fits_close_file(fptr, &status);
            return ;
        }

        long nelements = stats.samples_per_channel * stats.channels;

        if (fits_read_img(fptr, static_cast<uint16_t>(stats.dataType), 1, nelements, nullptr, m_ImageBuffer, &anynullptr, &status))
        {
            sendError("Error reading image.");
            fits_close_file(fptr, &status);
            return ;
        }


        fits_close_file(fptr, &status);
        /**************************************************************************************/

        frame = cv::Mat(_image->ImgStats().height, _image->ImgStats().width, CV_8UC1, m_ImageBuffer);




        //BOOST_LOG_TRIVIAL(debug) << "Emit Exposure done";
        emit ExposureDone();
        //SMRequestExposure();
    }

}
void BlindPec::SMRequestFrameReset()
{
    //BOOST_LOG_TRIVIAL(debug) << "SMRequestFrameReset";
    //sendMessage("SMRequestFrameReset");
    if (!frameReset(getString("devices", "camera")))
    {
        emit Abort();
        return;
    }
    //BOOST_LOG_TRIVIAL(debug) << "SMRequestFrameResetDone";
    emit RequestFrameResetDone();
}


void BlindPec::SMRequestExposure()
{
    //BOOST_LOG_TRIVIAL(debug) << "SMRequestExposure";
    //sendMessage("SMRequestExposure");
    if (!requestCapture(getString("devices", "camera"), getFloat("parms", "exposure"), getInt("parms", "gain"), getInt("parms",
                        "offset")))
    {
        emit Abort();
        return;
    }
    emit RequestExposureDone();
}
void BlindPec::SMRequestPulses()
{

    //    sendMessage("SMRequestPulses");


    if (_pulseN > 0)
    {
        //qDebug() << "********* Pulse  N " << _pulseN;
        _pulseDECfinished = false;
        if (!sendModNewNumber(getString("devices", "guider"), "TELESCOPE_TIMED_GUIDE_NS", "TIMED_GUIDE_N",
                              _pulseN))
        {
            emit abort();
            return;
        }
    }

    if (_pulseS > 0)
    {
        _pulseDECfinished = false;
        //qDebug() << "********* Pulse  S " << _pulseS;
        if (!sendModNewNumber(getString("devices", "guider"), "TELESCOPE_TIMED_GUIDE_NS", "TIMED_GUIDE_S",
                              _pulseS))
        {
            emit abort();
            return;
        }
    }

    if (_pulseE > 0)
    {
        _pulseRAfinished = false;
        //qDebug() << "********* Pulse  E " << _pulseE;
        sendModNewNumber(getString("devices", "guider"), "TELESCOPE_TIMED_GUIDE_WE", "TIMED_GUIDE_W", 0);
        if (!sendModNewNumber(getString("devices", "guider"), "TELESCOPE_TIMED_GUIDE_WE", "TIMED_GUIDE_E",
                              _pulseE))
        {
            emit abort();
            return;
        }
    }

    if (_pulseW > 0)
    {
        _pulseRAfinished = false;
        //qDebug()  << "********* Pulse  W " << _pulseW;
        sendModNewNumber(getString("devices", "guider"), "TELESCOPE_TIMED_GUIDE_WE", "TIMED_GUIDE_E", 0);
        if (!sendModNewNumber(getString("devices", "guider"), "TELESCOPE_TIMED_GUIDE_WE", "TIMED_GUIDE_W",
                              _pulseW))
        {
            emit abort();
            return;
        }
    }

    //BOOST_LOG_TRIVIAL(debug) << "SMRequestPulses before";
    //BOOST_LOG_TRIVIAL(debug) << "SMRequestPulses after";

    if ((_pulseN == 0) && (_pulseS == 0) && (_pulseE == 0) && (_pulseW == 0))
    {
        //qDebug()  << "SMRequestPulses zéro";
        emit PulsesDone();
        //SMRequestExposure();
    }

}


void BlindPec::SMAbort()
{


}

void BlindPec::onTimer()
{
    //qDebug() << "Timer hit";
    SMRequestExposure();
}
void BlindPec::defineTemplate()
{
    roidx = 620;
    roidy = 440;
    roix = frame.size().width / 2 - roidx / 2;
    //roix = 600;
    roiy = frame.size().height / 2 - roidy / 2;
    Rect roi(roix, roiy, roidx, roidy);
    templ = frame(roi).clone();
}
void BlindPec::findTemplate()
{
    matchTemplate(frame, templ, result_mat, match_method);
    minMaxLoc(result_mat, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat() );
    if( match_method  == TM_SQDIFF || match_method == TM_SQDIFF_NORMED )  matchLoc = minLoc;
    else matchLoc = maxLoc;
    if (matchLoc != matchLocPrev)
    {
        qDebug() <<  QDateTime::currentDateTime();
        matchLocPrev = matchLoc;
    }

    minMaxLocSubPix(&subPix, result_mat, &matchLoc, 1);

    double dd = dtStart.msecsTo(dt);
    dd = dd / 1000;

    //drift = ddtempl * (-1.7073467429668) - subPix.x + result_mat.size().width / 2; // OK !!
    driftx = subPix.x - offset - roix;
    drifty = subPix.y - offsety - roiy;

    driftt = sqrt(driftx * driftx + drifty * drifty) - dd * (getFloat("guideParams", "pixsec"));

    //drift = dd * (-1.7238) - subPix.x + offset + roix;

    //drifts.append(driftt);
    //if (drifts.size() > 5) drifts.removeFirst();
    //double ss = 0;
    //for (int i = 0; i < drifts.size(); i++ )
    //{
    //    ss = ss + drifts.at(i);
    //}
    //movingavgdrift = ss / drifts.size();
    movingavgdrift = driftt;
    //qDebug() << driftt;

}
void BlindPec::traiterFrame(Mat frameIn)
{
    dt = QDateTime::currentDateTime();
    if (numframe == 0) dtStart = dt;
    //qDebug() << frameIn.cols << frameIn.rows;
    //cv::cvtColor(frameIn, frame, cv::COLOR_BGR2GRAY);
    frame = frameIn;
    if (numframe == 1)
    {
        defineTemplate();
        //SMRequestExposure();
        avgdrift = 0;
    }

    if (numframe > 1)
    {
        findTemplate();

        if (matchLoc.x < 5 ||  matchLoc.x > frame.size().width - roidx - 5)
        {
            offset = offset + roix - subPix.x;
            offsety = offsety + roiy - subPix.y;
            defineTemplate();
        }

        avgdrifts.append(movingavgdrift);

        if (avgdrifts.size() > getInt("guideParams", "avgover"))
        {
            driftprev = avgdrift;

            double ss = 0;
            for (int i = 0; i < avgdrifts.size(); i++ )
            {
                ss = ss + avgdrifts.at(i);
            }

            avgdrift = ss / avgdrifts.size();
            ddrift = avgdrift - driftprev;

            double ech = 15 / getFloat("guideParams", "pixsec");
            //qDebug() << "pixsec=" << getFloat("guideParams", "pixsec") << "ech=" << ech << "drift=" << avgdrift;

            float k = getFloat("guideParams", "raAgr");
            float d = getFloat("guideParams", "deAgr");
            //double ms = k * avgdrift + d * ddrift;
            double ms = calculateOutput(0, avgdrift);
            //qDebug() << ms;
            if (ms > 0) _pulseE = ms;
            else _pulseE = 0;
            if (ms < 0) _pulseW = -ms;
            else _pulseW = 0;
            if (_pulseW < getInt("guideParams", "pulsemin")) _pulseW = 0;
            if (_pulseE < getInt("guideParams", "pulsemin")) _pulseE = 0;
            if (_pulseW > getInt("guideParams", "pulsemax")) _pulseW = getInt("guideParams", "pulsemax");
            if (_pulseE > getInt("guideParams", "pulsemax")) _pulseE = getInt("guideParams", "pulsemax");
            if (getBool("disCorrections", "disRA+"))  _pulseW = 0;
            if (getBool("disCorrections", "disRA-"))  _pulseE = 0;

            //qDebug() << "drift request " << avgdrift << ddrift << "->" << avgdrift << _pulseE << _pulseW;

            double tt = QDateTime::currentDateTime().toMSecsSinceEpoch();
            getEltFloat("guiding", "time")->setValue(tt, false);
            getEltFloat("guiding", "RA")->setValue(avgdrift * ech, false);
            getEltFloat("guiding", "DE")->setValue(0, false);
            getEltFloat("guiding", "pDE")->setValue(0, false);
            getEltFloat("guiding", "pRA")->setValue(_pulseE - _pulseW, false);
            getProperty("guiding")->push();


            _pulseN = 0;
            _pulseS = 0;
            _pulseDECfinished = true;
            double dd = dtStart.msecsTo(dt);
            dd = dd / 1000;

            if (guidelog.open(QIODevice::WriteOnly | QIODevice::Append))
            {
                //Frame	Time	mount	dx	dy	RARawDistance	DECRawDistance	RAGuideDistance	DECGuideDistance	RADuration	RADirection	DECDuration	DECDirection	XStep	YStep	StarMass	SNR	ErrorCode
                QTextStream in(&guidelog);
                in << numframe << ',' ;
                in << QString::number(dd).replace(',', '.') << ',' ;
                in << "mount," ;
                in << QString::number(avgdrift).replace(',', '.') << ',' ;
                in << "0," ;
                in << QString::number(avgdrift).replace(',', '.') << ',' ;
                in << "0," ;
                in << QString::number(avgdrift).replace(',', '.') << ',' ;
                in << "0," ;
                in << QString::number(_pulseE + _pulseW).replace(',', '.') << ',' ;
                if (_pulseW > 0 ) in << "W," ;
                if (_pulseE > 0 ) in << "E," ;
                if ((_pulseW == 0 ) && (_pulseE == 0 )) in << "," ;
                in << "0," ;
                in << "," ;
                in << "," ;
                in << "," ;
                in << "1," ;
                in << "1," ;
                in << "0" ;
                in << "\n";
                QString messageWithDateTime =

                    QString::number(dd) + ";" +
                    "avgdrift" + ";" +
                    QString::number(avgdrift) + ";" +
                    "";
                messageWithDateTime.replace(".", ",");
                if (outfile.open(QIODevice::WriteOnly | QIODevice::Append))
                {
                    QTextStream in(&outfile);
                    in << messageWithDateTime << '\n';
                }
                outfile.close();


            }
            guidelog.close();
            SMRequestPulses();

            QImage image(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
            //QPixmap scaled = QPixmap::fromImage(image.rgbSwapped()).scaled(ui->img->width(), ui->img->height());
            //QPixmap scaled = QPixmap::fromImage(image);
            QPainter qPainter(&image);
            qPainter.setBrush(Qt::NoBrush);
            qPainter.setPen(Qt::green);
            qPainter.drawRect(roix, roiy, roidx, roidy);

            qPainter.setBrush(Qt::NoBrush);
            qPainter.setPen(Qt::red);
            qPainter.drawRect(matchLoc.x, matchLoc.y, roidx, roidy);
            qPainter.drawRect(matchLoc.x + 1, matchLoc.y + 1, roidx - 2, roidy - 2);
            qPainter.end();

            image.save(getWebroot() + "/" + getModuleName() + ".jpeg", "JPG", 100);
            OST::ImgData dta;
            dta.mUrlJpeg = getModuleName() + ".jpeg";
            getEltImg("image", "image")->setValue(dta, true);








            avgdrifts.clear();
        }




    }
    numframe++;

}
double BlindPec::calculateOutput(double setpoint, double processVariable)
{
    error = setpoint - processVariable;
    integral.append(error);
    if (integral.size() > 100 ) integral.removeFirst();
    double is = 0;
    for (int i = 0; i < integral.size(); i++)
    {
        is += integral.at(i);
    }
    is = is / integral.size();
    derivative = error - previousError;
    output = getFloat("pid", "kp") * error + getFloat("pid", "ki") * is + getFloat("pid", "kd") * derivative;
    previousError = error;
    return output;
}
