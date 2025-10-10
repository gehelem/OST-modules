#include "allsky.h"
#include <QPainter>
#include <libnova/solar.h>
#include <libnova/julian_day.h>
#include <libnova/rise_set.h>
#include <libnova/transform.h>
#include <algorithm>

Allsky *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    Allsky *basemodule = new Allsky(name, label, profile, availableModuleLibs);
    return basemodule;
}

Allsky::Allsky(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)
{

    //Q_INIT_RESOURCE(dummy);
    setClassName(QString(metaObject()->className()).toLower());

    loadOstPropertiesFromFile(":allsky.json");

    setModuleDescription("Simple allsky camera module");
    setModuleVersion("0.1");

    giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
    giveMeADevice("gps", "GPS", INDI::BaseDevice::GPS_INTERFACE);
    giveMeADevice("meteo", "Météo", INDI::BaseDevice::GENERAL_INTERFACE);
    defineMeAsSequencer();


    if (getString("devices", "camera") != "") connectDevice(getString("devices", "camera"));
    if (getString("devices", "gps") != "") connectDevice(getString("devices", "gps"));
    if (getString("devices", "meteo") != "") connectDevice(getString("devices", "meteo"));


    OST::ElementBool* b = new OST::ElementBool("Play", "0", "");
    getProperty("actions")->addElt("loop", b);
    b->setAutoUpdate(true);
    b = new OST::ElementBool("Stop", "2", "");
    getProperty("actions")->addElt("abort", b);
    b->setAutoUpdate(true);
    b = new OST::ElementBool("Pause", "1", "");
    b->setAutoUpdate(true);
    getProperty("actions")->addElt("pause", b);
    getProperty("actions")->deleteElt("startsequence");
    getProperty("actions")->deleteElt("abortsequence");
    getProperty("actions")->setElt("abort", true);
    getProperty("actions")->setRule(OST::SwitchsRule::OneOfMany);


    OST::ElementInt* i = new OST::ElementInt("Delay (s)", "0", "");
    i->setAutoUpdate(false);
    i->setDirectEdit(true);
    i->setValue(5);
    i->setSlider(OST::SliderAndValue);
    i->setMinMax(1, 120);
    i->setStep(1);
    getProperty("parms")->addElt("delay", i);
    getEltFloat("parms", "exposure")->setAutoUpdate(false);

    _process = new QProcess(this);
    connect(_process, &QProcess::readyReadStandardOutput, this, &Allsky::processOutput);
    connect(_process, &QProcess::readyReadStandardError, this, &Allsky::processError);
    connect(_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            &Allsky::processFinished);

    mScheduleTimer.setInterval(5000); // every 5s check
    connect(&mScheduleTimer, &QTimer::timeout, this, &Allsky::OnScheduleTimer);
    mScheduleTimer.start();

    getProperty("gps")->disable();
    getProperty("geogps")->disable();

    connectIndi();
    if (getString("devices", "camera") != "") connectDevice(getString("devices", "camera"));
    if (getString("devices", "gps") != "") connectDevice(getString("devices", "gps"));
    if (getString("devices", "meteo") != "") connectDevice(getString("devices", "meteo"));

    calculateSunset();


}

Allsky::~Allsky()
{
    //Q_CLEANUP_RESOURCE(dummy);
}

void Allsky::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                               const QVariantMap &eventData)
{
    Q_UNUSED(eventType);
    Q_UNUSED(eventKey);
    //sendMessage("OnMyExternalEvent - recv : " + getModuleName() + "-" + eventType + "-" + eventKey);
    if (getModuleName() == eventModule)
    {
        if (eventType == "afterinit")
        {
            checkArchives();
        }
        foreach(const QString &keyprop, eventData.keys())
        {
            if (eventType == "Flcreate" && keyprop == "geo")
            {
                getStore()[keyprop]->newLine(eventData[keyprop].toMap()["elements"].toMap());
            }
            if (eventType == "Fldelete" && keyprop == "geo")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                getStore()[keyprop]->deleteLine(line);
            }
            if (eventType == "Flupdate"  && keyprop == "geo")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                getStore()[keyprop]->updateLine(line, eventData[keyprop].toMap()["elements"].toMap());
            }
            if (eventType == "Flselect" && keyprop == "geo")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                QString id = getString("geo", "id", line);
                float lat = getFloat("geo", "lat", line);
                float lng = getFloat("geo", "long", line);
                getEltString("geo", "id")->setValue(id);
                getEltFloat("geo", "lat")->setValue(lat);
                getEltFloat("geo", "long")->setValue(lng);
            }

            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                if (keyprop == "actions")
                {
                    if (keyelt == "pause")
                    {
                        if (getBool(keyprop, keyelt)) enableParms(false);
                        getProperty("actions")->setState(OST::Busy);
                        //startTimelapseBatch();
                    }
                    if (keyelt == "loop")
                    {
                        if (getBool(keyprop, keyelt)) enableParms(false);
                        getProperty("actions")->setState(OST::Busy);
                        //startLoop();
                    }
                    if (keyelt == "abort")
                    {
                        if (getBool(keyprop, keyelt)) enableParms(true);
                        getProperty("actions")->setState(OST::Ok);
                        stopLoop();
                    }
                }
                if (keyprop == "daily")
                {
                    if (keyelt == "enable")
                    {
                        //bool b = eventData[keyprop].toMap()["elements"].toMap()[keyelt].toBool();
                        //should i disable here other prog ?
                    }
                }
                if (keyprop == "gps")
                {
                    if (keyelt == "add")
                    {
                        addGPSLocalization();
                    }
                }
                if (keyprop == "parms")
                {
                    if (keyelt == "exposure")
                    {
                        double e = eventData["parms"].toMap()["elements"].toMap()["exposure"].toFloat();
                        int  d = getInt("parms", "delay");
                        if ( e > d )
                        {
                            sendWarning("Delay must be greater than exposure, increasing delay");
                            int step = getEltInt("parms", "delay")->step();
                            int dStepped = step * (int)(e / step) + step;
                            getEltInt("parms", "delay")->setValue(dStepped, true);
                        }
                        getEltFloat("parms", "exposure")->setValue(e, true);
                    }
                    if (keyelt == "delay")
                    {
                        int d = eventData["parms"].toMap()["elements"].toMap()["delay"].toInt();
                        double  e = getFloat("parms", "exposure");
                        if ( e > d )
                        {
                            sendWarning("Delay must be greater than exposure, decreasing exposure value - 5%");
                            getEltFloat("parms", "exposure")->setValue(d * 0.95, true);
                        }
                        getEltInt("parms", "delay")->setValue(d, true);
                    }
                }
            }
        }

    }
}
void Allsky::startLoop()
{
    if (mIsLooping)
    {
        sendWarning("Already looping");
        return;
    }

    if (getBool("type", "manual")) sendMessage("Start manual schedule");
    if (getBool("type", "fixed")) sendMessage("Start fixed schedule");
    if (getBool("type", "sunset")) sendMessage("Start sunset schedule");


    _index = 0;
    mKeog = QImage();
    mFolder = QDateTime::currentDateTime().toString("yyyyMMdd-hh-mm-ss");

    getProperty("log")->clearGrid();

    QDir dir0(getWebroot() + "/" + getModuleName() + "/" + mFolder + "/", {"*"});
    for(const QString &filename : dir0.entryList())
    {
        dir0.remove(filename);
    }

    QDir dir;
    dir.mkdir(getWebroot() + "/" + getModuleName());
    dir.mkdir(getWebroot() + "/" + getModuleName() + "/" + mFolder);
    dir.mkdir(getWebroot() + "/" + getModuleName() + "/" + mFolder + "/images");
    connectIndi();
    connectDevice(getString("devices", "camera"));
    setBLOBMode(B_ALSO, getString("devices", "camera").toStdString().c_str(), nullptr);
    enableDirectBlobAccess(getString("devices", "camera").toStdString().c_str(), nullptr);

    if (!requestCapture(getString("devices", "camera"), getFloat("parms", "exposure"), getInt("parms", "gain"), getInt("parms",
                        "offset")))
    {
        getProperty("actions")->setState(OST::Error);
        mIsLooping = false;
    }
    else
    {
        mTimer.setInterval(getInt("parms", "delay") * 1000);
        connect(&mTimer, &QTimer::timeout, this, &Allsky::OnTimer);
        mTimer.start();
        getProperty("actions")->setState(OST::Busy);
        mIsLooping = true;
    }

}
void Allsky::stopLoop()
{
    if (!mIsLooping) return;

    if (getBool("type", "manual")) sendMessage("Stop manual schedule");
    if (getBool("type", "fixed")) sendMessage("Stop fixed schedule");
    if (getBool("type", "sunset")) sendMessage("Stop sunset schedule");

    mTimer.stop();
    disconnect(&mTimer, &QTimer::timeout, this, &Allsky::OnTimer);

    mIsLooping = false;
    firstStack = true;
    startTimelapseBatch();
    getProperty("actions")->setState(OST::Ok);
}
void Allsky::startTimelapseBatch()
{
    sendMessage("Generating timelapse");
    if (_process->state() != 0)
    {
        qDebug() << "can't start process";
    }
    else
    {
        QString program = "ffmpeg";
        QStringList arguments;
        arguments << "-framerate" << "30";
        arguments << "-pattern_type" << "glob";
        arguments << "-i" << getWebroot() + "/" + getModuleName() + "/" + mFolder +  "/images/*.jpeg";
        arguments << "-c:v" << "libx264";
        arguments << "-pix_fmt" << "yuv420p";
        arguments << "-framerate" << "30";
        arguments << "-y";
        arguments << getWebroot() + "/" + getModuleName() + "/" + mFolder + "/timelapse.mp4";
        qDebug() << "PROCESS ARGS " << arguments;
        _process->start(program, arguments);

    }

}
void Allsky::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);
    OST::VideoData v;
    v.url = getModuleName() + "/" + mFolder + "/timelapse.mp4";
    getEltVideo("timelapse", "video1")->setValue(v, true);
    sendMessage("Timelapse ready (" + QString::number(exitCode) + ")");
    if (!mIsLooping) moveCurrentToArchives();
}
void Allsky::processOutput()
{
    QString output = _process->readAllStandardOutput();
    //qDebug() << "PROCESS LOG   " << output;
}
void Allsky::processError()
{
    QString output = _process->readAllStandardError();
    qDebug() << "Process log : " + output;
}
void Allsky::newBLOB(INDI::PropertyBlob pblob)
{
    if
    (
        (QString(pblob.getDeviceName()) == getString("devices", "camera"))
    )
    {
        getProperty("actions")->setState(OST::Ok);
        delete _image;
        _image = new fileio();
        _image->loadBlob(pblob, 64);

        QList<fileio::Record> rec = _image->getRecords();
        stats = _image->getStats();
        _image->saveAsFITS(getWebroot() + "/" + getModuleName() + QString(pblob.getDeviceName()) + ".FITS", stats,
                           _image->getImageBuffer(),
                           FITSImage::Solution(), rec, false);
        _index++;
        QImage rawImage = _image->getRawQImage();
        QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
        im.setColorTable(rawImage.colorTable());
        QRect r;
        r.setRect(rawImage.width() / 2, 1, 1, rawImage.height());
        if (firstStack)
        {
            imageStacked = im;
            firstStack = false;
        }
        else
        {
            for (int y = 0; y < im.height(); ++y)
            {
                QRgb *line_current = reinterpret_cast<QRgb*>(im.scanLine(y));
                QRgb *line_stacked = reinterpret_cast<QRgb*>(imageStacked.scanLine(y));
                for (int x = 0; x < im.width(); ++x)
                {
                    QRgb &rgb_current = line_current[x];
                    QRgb &rgb_stacked = line_stacked[x];
                    rgb_stacked = qRgb(
                                      std::max(qRed(rgb_current), qRed(rgb_stacked)),
                                      std::max(qGreen(rgb_current), qGreen(rgb_stacked)),
                                      std::max(qBlue(rgb_current), qBlue(rgb_stacked))
                                  );
                }
            }
        }
        imageStacked.save(getWebroot() +  "/" + getModuleName() + "/" + mFolder + "/stacked" + ".jpeg", "JPG", 100);


        QImage image1 = mKeog;
        QImage image2 = im.copy(r);
        QImage result(mKeog.width() + 1, rawImage.height(), QImage::Format_RGB32);
        QPainter painter(&result);
        painter.drawImage(0, 0, image1);
        painter.drawImage(mKeog.width(), 0, image2);
        mKeog = result;

        mKeog.save(getWebroot() +  "/" + getModuleName() + "/" + mFolder + "/keogram" + ".jpeg", "JPG", 100);
        OST::ImgData keo;
        keo.mUrlJpeg = getModuleName() + "/" + mFolder + "/keogram" + ".jpeg";
        getEltImg("keogram", "image1")->setValue(keo, true);

        r.setRect(0, 0, im.width(), im.height() / 10);
        QPainter p;
        p.begin(&im);
        p.setPen(QPen(Qt::red));
        p.setFont(QFont("Times", 15, QFont::Bold));
        p.drawText(r, Qt::AlignLeft, QDateTime::currentDateTime().toString("dd/MM/yyyy hh:mm:ss zzz") );
        p.end();



        im.save(getWebroot() + "/" + getModuleName() + QString(pblob.getDeviceName()) + ".jpeg", "JPG", 100);
        OST::ImgData dta = _image->ImgStats();
        dta.mUrlJpeg = getModuleName() + QString(pblob.getDeviceName()) + ".jpeg";
        dta.mAlternates.clear();
        dta.mAlternates.push_front(getModuleName() + "/" + mFolder + "/keogram" + ".jpeg");
        dta.mAlternates.push_front(getModuleName() + "/" + mFolder + "/stacked" + ".jpeg");
        getEltImg("image", "image")->setValue(dta, true);

        QString _n = QStringLiteral("%1").arg(_index, 10, 10, QLatin1Char('0'));
        im.save(getWebroot() + "/" + getModuleName() + "/" + mFolder + + "/images/" + _n + ".jpeg", "JPG", 100);

        double tt = QDateTime::currentDateTime().toMSecsSinceEpoch();
        getEltFloat("log", "time")->setValue(tt, false);
        getEltFloat("log", "snr")->setValue(_image->getStats().SNR, true);
        getProperty("log")->push();

        if (getBool("autoparms", "enabled"))
        {
            if (getString("autoparms", "measure") == "mean") computeExposureOrGain(_image->getStats().mean[0]);
            if (getString("autoparms", "measure") == "median") computeExposureOrGain(_image->getStats().median[0]);
        }

    }


}
void Allsky::updateProperty(INDI::Property property)
{
    if (strcmp(property.getName(), "CCD1") == 0)
    {
        newBLOB(property);
    }
    if (
        (property.getDeviceName() == getString("devices", "gps"))
        &&  (!getProperty("gps")->isEnabled())
    )
    {
        getProperty("gps")->enable();
        getProperty("geogps")->enable();
    }

    if (
        (property.getDeviceName() == getString("devices", "gps"))
        &&  (property.getName()   == std::string("GEOGRAPHIC_COORD"))
        &&  (property.getState() == IPS_OK)
    )
    {
        INDI::PropertyNumber n = property;
        getEltFloat("geogps", "lat")->setValue(n[0].value, true);
        getEltFloat("geogps", "long")->setValue(n[1].value, true);
        getEltFloat("geogps", "elev")->setValue(n[2].value, true);
    }
    if (
        (property.getDeviceName() == getString("devices", "meteo"))
        &&  (property.getName()   == std::string("WEATHER_PARAMETERS"))
    )
    {
        double dd = QDateTime::currentDateTime().toMSecsSinceEpoch();
        getEltFloat("history", "D")->setValue(dd, true);
        INDI::PropertyNumber n = property;
        for (int i = 0; i < n.size(); i++)
        {
            if (n[i].getName() == std::string("WEATHER_TEMPERATURE"))
            {
                getEltFloat("measures", "temp")->setValue(n[i].value, true);
                getEltString("history", "S")->setValue("temp", false);
                getEltFloat("history", "Y")->setValue(n[i].value, false);
                getProperty("history")->push();
            }
            if (n[i].getName() == std::string("WEATHER_HUMIDITY"))
            {
                getEltFloat("measures", "hum")->setValue(n[i].value, true);
                getEltString("history", "S")->setValue("hum", false);
                getEltFloat("history", "Y")->setValue(n[i].value, false);
                getProperty("history")->push();
            }
        }
    }
    if (
        (property.getDeviceName() == getString("devices", "gps"))
        &&  (property.getName()   == std::string("TIME_UTC"))
        &&  (property.getState() == IPS_OK)
    )
    {
        INDI::PropertyText t = property;
        QString time_format = "yyyy-MM-ddTHH:mm:ss";
        QString s = t[0].text;
        QDateTime d = QDateTime::fromString(s, time_format);
        getEltDate("geogps", "date")->setValue(d.date(), false);
        getEltTime("geogps", "time")->setValue(d.time(), false);
        getEltString("geogps", "offset")->setValue(t[1].text, true);
    }

}

void Allsky::OnTimer()
{
    if (getBool("actions", "abort") || getBool("actions", "pause")) return;
    if (!requestCapture(getString("devices", "camera"), getFloat("parms", "exposure"), getInt("parms", "gain"), getInt("parms",
                        "offset")))
    {
        getProperty("actions")->setState(OST::Error);
    }
    else
    {
        getProperty("actions")->setState(OST::Busy);
    }

}
void Allsky::OnScheduleTimer()
{
    calculateSunset();

    if (getBool("actions", "abort") || getBool("actions", "pause")) return;

    QTime now = QDateTime::currentDateTime().time();

    if (getBool("type", "sunset"))
    {
        QTime start = getTime("coming", "sunset"); // coucher
        QTime stop = getTime("coming", "sunrise"); // lever
        if (start < stop) // daytime requested
        {
            sendError("sunrise > sunset ???");
        }
        if (start > stop) // nighttime requested
        {
            if (((now > start ) || (now < stop)) && (!mIsLooping))
            {
                sendMessage("Start sunset schedule");
                startLoop();
            }
            if (((now < start ) && (now > stop)) && (mIsLooping))
            {
                sendMessage("Stop sunset schedule");
                stopLoop();
            }
        }
        return;
    }

    if (getBool("type", "fixed"))
    {
        QTime start = getTime("daily", "begin");
        QTime stop = getTime("daily", "end");
        if (start == stop)
        {
            sendError("Can't do anything when start time equals stop time, daily schedule disabled.");
            //getEltBool("daily", "enable")->setValue(false, true);
            return;
        }
        if (start < stop) // daytime requested
        {
            if ((now > start ) && (now < stop) && (!mIsLooping))
            {
                sendMessage("Start daily schedule");
                startLoop();
            }
            if (((now < start ) || (now > stop)) && (mIsLooping))
            {
                sendMessage("Stop daily schedule");
                stopLoop();
            }
        }
        if (start > stop) // nighttime requested
        {
            if (((now > start ) || (now < stop)) && (!mIsLooping))
            {
                sendMessage("Start nightly schedule");
                startLoop();
            }
            if (((now < start ) && (now > stop)) && (mIsLooping))
            {
                sendMessage("Stop nightly schedule");
                stopLoop();
            }
        }
        return;
    }

    if (getBool("type", "manual") && !mIsLooping)
    {
        startLoop();

    }
    if (!getBool("type", "manual") && mIsLooping)
    {
        stopLoop();
    }


}
void Allsky::computeExposureOrGain(double fromValue)
{
    QString elt = "";
    if (getString("autoparms", "expgain") == "exp") elt = "exposure";
    if (getString("autoparms", "expgain") == "gain") elt = "gain";
    double target = getFloat("autoparms", "target");
    double threshold = getFloat("autoparms", "threshold") / 100; // not used ATM, we'll see that later
    double coef = target / fromValue;
    double val = 0;
    if (elt == "exposure") val = getFloat("parms", elt);
    if (elt == "gain") val = getInt("parms", elt);

    int    delay = getInt("parms", "delay");
    double newval = val * coef;
    if (elt == "exposure" && newval > delay)
    {
        newval = 0.95 * delay;
    }
    if (newval < getFloat("autoparms", "min"))
    {
        newval = getFloat("autoparms", "min");
    }
    if (newval > getFloat("autoparms", "max"))
    {
        newval = getFloat("autoparms", "max");
    }
    if (elt == "exposure") getEltFloat("parms", elt)->setValue(newval, true);
    if (elt == "gain") getEltInt("parms", elt)->setValue(newval, true);



}
void Allsky::checkArchives(void)
{
    getProperty("archives")->clearGrid();
    QStringList folders;

    //check existing folders
    QDirIterator itFolders(getWebroot() + "/" + getModuleName() + "/archives", QDirIterator::Subdirectories);
    while (itFolders.hasNext())
    {
        QString d = itFolders.next();
        d.replace(getWebroot() + "/" + getModuleName() + "/archives", "");
        if (d.endsWith("/.") && !d.contains("images") && d != "/.")
        {
            QString dd = d;
            dd.replace("/.", "");
            if (!folders.contains(dd))
            {
                folders.append(dd);
            }
        }
    }

    folders.sort();
    for (const auto &f : folders)
    {
        QString dd = f;
        OST::ImgData i = getEltImg("archives", "keogram")->value();
        i.mUrlJpeg = getModuleName() + "/archives" + dd + "/keogram.jpeg";
        getEltImg("archives", "keogram")->setValue(i);
        i = getEltImg("archives", "stack")->value();
        i.mUrlJpeg = getModuleName() + "/archives" + dd + "/stacked.jpeg";
        getEltImg("archives", "stack")->setValue(i);
        OST::VideoData v = getEltVideo("archives", "timelapse")->value();
        v.url = getModuleName() + "/archives" + dd + "/timelapse.mp4";
        getEltVideo("archives", "timelapse")->setValue(v);
        getEltString("archives", "date")->setValue(dd.replace("/", ""));
        getProperty("archives")->push();
    }
}
void Allsky::moveCurrentToArchives(void)
{
    QDir dir;
    dir.mkdir(getWebroot() + "/" + getModuleName() + "/archives");
    dir.rename(getWebroot() + "/" + getModuleName() + "/" + mFolder,
               getWebroot() + "/" + getModuleName() + "/archives/" + mFolder);
    if (!getBool("keepimages", "enable"))
    {
        QDir dd(getWebroot() + "/" + getModuleName() + "/archives/" + mFolder + "/images");
        dd.removeRecursively();
    }

    checkArchives();
}
void Allsky::calculateSunset(void)
{
    double JD = ln_get_julian_from_sys();

    ln_rst_time rst;
    ln_zonedate rise, set, transit;
    ln_lnlat_posn observer;
    observer.lat = getFloat("geo", "lat");
    observer.lng = getFloat("geo", "long");
    if (ln_get_solar_rst(JD, &observer, &rst) != 0)
    {
        sendError("Sun is circumpolar");
        return;
    }
    else
    {
        ln_get_local_date(rst.rise, &rise);
        ln_get_local_date(rst.transit, &transit);
        ln_get_local_date(rst.set, &set);
        QTime t;
        t.setHMS(rise.hours, rise.minutes, rise.seconds);
        getEltTime("coming", "sunrise")->setValue(t, false);
        t.setHMS(set.hours, set.minutes, set.seconds);
        getEltTime("coming", "sunset")->setValue(t, true);
    }

}
void Allsky::addGPSLocalization(void)
{
    getEltFloat("geo", "lat")->setValue(getFloat("geogps", "lat"), false);
    getEltFloat("geo", "long")->setValue(getFloat("geogps", "long"), false);
    getEltString("geo", "id")->setValue("GPS", false);
    getProperty("geo")->push();
}
void Allsky::enableParms(bool enable)
{
    if (enable)
    {
        getProperty("daily")->enable();
        getProperty("type")->enable();
    }
    else
    {
        getProperty("daily")->disable();
        getProperty("type")->disable();
    }
}
