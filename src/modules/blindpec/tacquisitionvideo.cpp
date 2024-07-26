#include "tacquisitionvideo.h"

TAcquisitionVideo::TAcquisitionVideo(int numeroCamera) : numeroCamera(numeroCamera), brightness(0.5), contrast(0.5),
    saturation(0.5), change(false)
{
    qDebug() << Q_FUNC_INFO;
}

TAcquisitionVideo::~TAcquisitionVideo()
{
    qDebug() << Q_FUNC_INFO;
}
void TAcquisitionVideo::stop()
{
    delete this;
}
void TAcquisitionVideo::run()
{
    qDebug() << Q_FUNC_INFO << "start";
    this->setPriority(QThread::NormalPriority);

    try
    {
        qDebug() << "nbCameras" << compterCameras();
        VideoCapture camera(numeroCamera);
        Mat frame;

        qDebug() << "numeroCamera" << numeroCamera << resolution << camera.isOpened();
        setProprietes(camera);

        while(camera.isOpened() && !isInterruptionRequested())
        {
            camera >> frame;
            //bool bSuccess = camera.read(frame);
            if(frame.empty())
                //if (!bSuccess)
                continue;
            emit nouvelleFrame(frame);

            //QImage image(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
            //emit nouvelleImage(image);

            //QPixmap pixmap = QPixmap::fromImage(QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888).rgbSwapped());
            //emit nouvelleImage(pixmap);

            if(change)
                setProprietes(camera);;
        }
        camera.release();
    }
    catch (...)
    {
        // faire quelque chose avec les exceptions
    }

    emit finAcquisition();
    qDebug() << Q_FUNC_INFO << "stop";
}

void TAcquisitionVideo::attendrePeriode(int periode)
{
    mutexChangement.lock();
    wcChangement.wait(&mutexChangement, periode); //QThread::msleep(periode);
    mutexChangement.unlock();
}

#if CV_VERSION_MAJOR == 3
void TAcquisitionVideo::getProprietes(const VideoCapture &camera)
#else
void TAcquisitionVideo::getProprietes(VideoCapture &camera)
#endif
{
    /*
    CV_CAP_PROP_POS_MSEC Current position of the video file in milliseconds or video capture timestamp.
    CV_CAP_PROP_POS_FRAMES 0-based index of the frame to be decoded/captured next.
    CV_CAP_PROP_POS_AVI_RATIO Relative position of the video file: 0 - start of the film, 1 - end of the film.
    CV_CAP_PROP_FOURCC 4-character code of codec.
    CV_CAP_PROP_FRAME_COUNT Number of frames in the video file.
    CV_CAP_PROP_FORMAT Format of the Mat objects returned by retrieve() .
    CV_CAP_PROP_MODE Backend-specific value indicating the current capture mode.
    CV_CAP_PROP_HUE Hue of the image (only for cameras).
    CV_CAP_PROP_GAIN Gain of the image (only for cameras).
    CV_CAP_PROP_EXPOSURE Exposure (only for cameras).
    CV_CAP_PROP_CONVERT_RGB Boolean flags indicating whether images should be converted to RGB.
    CV_CAP_PROP_WHITE_BALANCE_U The U value of the whitebalance setting (note: only supported by DC1394 v 2.x backend currently)
    CV_CAP_PROP_WHITE_BALANCE_V The V value of the whitebalance setting (note: only supported by DC1394 v 2.x backend currently)
    CV_CAP_PROP_RECTIFICATION Rectification flag for stereo cameras (note: only supported by DC1394 v 2.x backend currently)
    CV_CAP_PROP_ISO_SPEED The ISO speed of the camera (note: only supported by DC1394 v 2.x backend currently)
    CV_CAP_PROP_BUFFERSIZE Amount of frames stored in internal buffer memory (note: only supported by DC1394 v 2.x backend currently)
    */
    QString message = "FRAME_WIDTH = " + QString::number(camera.get(CAP_PROP_FRAME_WIDTH));
    nouveauMessage(message);
    message = "FRAME_WIDTH = " + QString::number(camera.get(CAP_PROP_FRAME_HEIGHT));
    nouveauMessage(message);
    message = "FPS = " + QString::number(camera.get(CAP_PROP_FPS));
    nouveauMessage(message);
    message = "BRIGHTNESS = " + QString::number(camera.get(CAP_PROP_BRIGHTNESS));
    nouveauMessage(message);
    message = "CONTRAST = " + QString::number(camera.get(CAP_PROP_CONTRAST));
    nouveauMessage(message);
    message = "SATURATION = " + QString::number(camera.get(CAP_PROP_SATURATION));
    nouveauMessage(message);

    //qDebug() << "CV_CAP_PROP_HUE" << camera.get(CV_CAP_PROP_HUE);
    //qDebug() << "CV_CAP_PROP_GAIN" << camera.get(CV_CAP_PROP_GAIN);
}

void TAcquisitionVideo::setProprietes(VideoCapture &camera)
{
    //camera.set(CAP_PROP_FRAME_WIDTH, resolution.width());
    //camera.set(CAP_PROP_FRAME_HEIGHT, resolution.height());
    //camera.set(CAP_PROP_FRAME_WIDTH, 640);
    //camera.set(CAP_PROP_FRAME_HEIGHT, 480);
    //camera.set(CAP_PROP_BRIGHTNESS, brightness);
    //camera.set(CAP_PROP_CONTRAST, contrast);
    //camera.set(CAP_PROP_SATURATION, saturation);
    change = false;
    getProprietes(camera);
}

void TAcquisitionVideo::setResolution(const QSize &resolution)
{
    this->resolution = resolution;
    change = true;
}

void TAcquisitionVideo::setBrightness(double brightness)
{
    this->brightness = brightness;
    change = true;
}

void TAcquisitionVideo::setContrast(double contrast)
{
    this->contrast = contrast;
    change = true;
}

void TAcquisitionVideo::setSaturation(double saturation)
{
    this->saturation = saturation;
    change = true;
}

int TAcquisitionVideo::compterCameras()
{
    int max = 10;

    for (int i = 0; i < max; i++)
    {
        VideoCapture camera(i);
        bool res = (!camera.isOpened());
        camera.release();
        if (res)
        {
            return i;
        }
    }
    return max;
}
