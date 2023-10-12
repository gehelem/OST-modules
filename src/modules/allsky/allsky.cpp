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
    defineMeAsSequencer();


    OST::ValueBool* b = new OST::ValueBool("Loop", "0", "");
    getProperty("actions")->addValue("loop", b);
    b = new OST::ValueBool("Abort", "2", "");
    getProperty("actions")->addValue("abort", b);
    b = new OST::ValueBool("Timelapse", "1", "");
    getProperty("actions")->addValue("timelapse", b);

    getProperty("actions")->deleteValue("startsequence");
    getProperty("actions")->deleteValue("abortsequence");


    _process = new QProcess(this);
    connect(_process, &QProcess::readyReadStandardOutput, this, &Allsky::processOutput);
    connect(_process, &QProcess::readyReadStandardError, this, &Allsky::processError);
    connect(_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            &Allsky::processFinished);

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
        foreach(const QString &keyprop, eventData.keys())
        {
            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                setOstElementValue(keyprop, keyelt, eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"], true);
                if (keyprop == "actions")
                {
                    if (keyelt == "timelapse")
                    {
                        if (setOstElementValue(keyprop, keyelt, true, true))
                        {
                            startBatch();
                        }
                    }
                    if (keyelt == "loop")
                    {
                        if (setOstElementValue(keyprop, keyelt, true, false))
                        {
                            getProperty("actions")->setState(OST::Busy);
                            startLoop();
                        }
                    }
                    if (keyelt == "abort")
                    {
                        if (setOstElementValue(keyprop, keyelt, false, false))
                        {
                            _isLooping = false;
                            setOstElementValue(keyprop, "loop", false, false);
                            getProperty("actions")->setState(OST::Ok);

                        }
                    }
                }
            }
        }
    }
}
void Allsky::startLoop()
{
    _isLooping = true;
    _index = 0;
    mKheog = QImage();

    getProperty("log")->clearGrid();

    QDir dir0(getWebroot() + "/" + getModuleName() + "/batch/", {"*"});
    for(const QString &filename : dir0.entryList())
    {
        dir0.remove(filename);
    }

    QDir dir;
    dir.mkdir(getWebroot() + "/" + getModuleName());
    dir.mkdir(getWebroot() + "/" + getModuleName() + "/batch/");
    connectIndi();
    connectDevice(getString("devices", "camera"));
    setBLOBMode(B_ALSO, getString("devices", "camera").toStdString().c_str(), nullptr);
    enableDirectBlobAccess(getString("devices", "camera").toStdString().c_str(), nullptr);
    if (!requestCapture(getString("devices", "camera"), getFloat("parms", "exposure"), getInt("parms", "gain"), getInt("parms",
                        "offset")))
    {
        getProperty("actions")->setState(OST::Error);
    }
}
void Allsky::startBatch()
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
        arguments << "-i" << getWebroot() + "/" + getModuleName() + "/batch/*.jpeg";
        arguments << "-c:v" << "libx264";
        arguments << "-pix_fmt" << "yuv420p";
        arguments << "-framerate" << "30";
        arguments << "-y";
        arguments << getWebroot() + "/" + getModuleName() + "/batch/" + getModuleName() + ".mp4";
        qDebug() << "PROCESS ARGS " << arguments;
        _process->start(program, arguments);

    }

}
void Allsky::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);
    getValueVideo("timelapse", "video1")->setValue(getModuleName() + "/batch/" + getModuleName() + ".mp4", true);
    sendMessage("PROCESS FINISHED (" + QString::number(exitCode) + ")");
}
void Allsky::processOutput()
{
    QString output = _process->readAllStandardOutput();
    qDebug() << "PROCESS LOG   " << output;
}
void Allsky::processError()
{
    QString output = _process->readAllStandardError();
    sendMessage("Process log : " + output);
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

        mKheog.save(getWebroot() + "/" + getModuleName() + "-KHEOGRAM" + ".jpeg", "JPG", 100);
        OST::ImgData kh;
        kh.mUrlJpeg = getModuleName() + "-KHEOGRAM" + ".jpeg";
        getValueImg("kheogram", "image1")->setValue(kh, true);

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
        getValueImg("image", "image")->setValue(dta, true);

        QString _n = QStringLiteral("%1").arg(_index, 10, 10, QLatin1Char('0'));
        im.save(getWebroot() + "/" + getModuleName() + "/batch/" + _n + ".jpeg", "JPG", 100);

        getProperty("actions")->setState(OST::Busy);
        if (_isLooping)
        {
            if (!sendModNewNumber(getString("devices", "camera"), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", getFloat("parms",
                                  "exposure")))
            {
                getProperty("actions")->setState(OST::Error);
                _isLooping = false;
            }
        }
        double tt = QDateTime::currentDateTime().toMSecsSinceEpoch();
        setOstElementValue("log", "time", tt, false);
        setOstElementValue("log", "snr", _image->getStats().SNR, true);
        getProperty("log")->push();

    }


}
void Allsky::updateProperty(INDI::Property property)
{
    if (strcmp(property.getName(), "CCD1") == 0)
    {
        newBLOB(property);
    }
}
