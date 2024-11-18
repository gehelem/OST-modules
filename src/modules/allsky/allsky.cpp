#include "allsky.h"
#include <QPainter>

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
    defineMeAsSequencer();


    OST::ElementBool* b = new OST::ElementBool("Loop", "0", "");
    getProperty("actions")->addElt("loop", b);
    b = new OST::ElementBool("Abort", "2", "");
    getProperty("actions")->addElt("abort", b);
    b = new OST::ElementBool("Timelapse", "1", "");
    getProperty("actions")->addElt("timelapse", b);

    getProperty("actions")->deleteElt("startsequence");
    getProperty("actions")->deleteElt("abortsequence");

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

    mScheduleTimer.setInterval(10000); // every 10s check
    connect(&mScheduleTimer, &QTimer::timeout, this, &Allsky::OnScheduleTimer);
    mScheduleTimer.start();


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
            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                if (keyprop == "actions")
                {
                    if (keyelt == "timelapse")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(false))
                        {
                            startTimelapseBatch();
                        }
                    }
                    if (keyelt == "loop")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(true))
                        {
                            getProperty("actions")->setState(OST::Busy);
                            startLoop();
                        }
                    }
                    if (keyelt == "abort")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(false))
                        {
                            stopLoop();
                        }
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

    _index = 0;
    mKheog = QImage();
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
    if (!mIsLooping)
    {
        sendWarning("Not looping");
        return;
    }
    mTimer.stop();
    mIsLooping = false;
    startTimelapseBatch();
    getEltBool("actions", "loop")->setValue(false);
    getProperty("actions")->setState(OST::Ok);
}
void Allsky::startTimelapseBatch()
{
    qDebug() << "PROCESS Start Batch";
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


        QImage image1 = mKheog;
        QImage image2 = im.copy(r);
        QImage result(mKheog.width() + 1, rawImage.height(), QImage::Format_RGB32);
        QPainter painter(&result);
        painter.drawImage(0, 0, image1);
        painter.drawImage(mKheog.width(), 0, image2);
        mKheog = result;

        mKheog.save(getWebroot() +  "/" + getModuleName() + "/" + mFolder + "/kheogram" + ".jpeg", "JPG", 100);
        OST::ImgData kh;
        kh.mUrlJpeg = getModuleName() + "/" + mFolder + "/kheogram" + ".jpeg";
        getEltImg("kheogram", "image1")->setValue(kh, true);

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
        dta.mAlternates.push_front(getModuleName() + "/" + mFolder + "/kheogram" + ".jpeg");
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
}

void Allsky::OnTimer()
{
    if (!requestCapture(getString("devices", "camera"), getFloat("parms", "exposure"), getInt("parms", "gain"), getInt("parms",
                        "offset")))
    {
        getProperty("actions")->setState(OST::Error);
    }
    else
    {
        mTimer.setInterval(getInt("parms", "delay") * 1000);
        getProperty("actions")->setState(OST::Busy);
    }

}
void Allsky::OnScheduleTimer()
{
    if (getBool("daily", "enable"))
    {
        QTime now = QDateTime::currentDateTime().time();
        QTime start = getTime("daily", "begin");
        QTime stop = getTime("daily", "end");
        if (start == stop)
        {
            sendError("Can't do anything when start time equals stop time, daily schedule disabled.");
            getEltBool("daily", "enable")->setValue(false, true);
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
        OST::ImgData i = getEltImg("archives", "kheogram")->value();
        i.mUrlJpeg = getModuleName() + "/archives" + dd + "/kheogram.jpeg";
        getEltImg("archives", "kheogram")->setValue(i);
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
    checkArchives();
}
