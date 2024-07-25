#ifndef TACQUISITION_H
#define TACQUISITION_H

#include <opencv2/opencv.hpp>
#include <QtCore>
#include <QThread>
#include <QMutexLocker>
#include <QMutex>
#include <QWaitCondition>
#include <QPixmap>

using namespace cv;

class TAcquisitionVideo : public QThread
{
        Q_OBJECT
    public:
        TAcquisitionVideo(int numeroCamera = 0);
        ~TAcquisitionVideo();

        void              run();
        void              stop();
#if CV_VERSION_MAJOR == 3
        void              getProprietes(const VideoCapture &camera);
#else
        void              getProprietes(VideoCapture &camera);
#endif
        void              setProprietes(VideoCapture &camera);
        void              setResolution(const QSize &resolution);
        void              setBrightness(double brightness);
        void              setContrast(double contrast);
        void              setSaturation(double saturation);
        static int        compterCameras();

    private:
        int               numeroCamera;
        QSize             resolution;
        double            brightness;
        double            contrast;
        double            saturation;
        bool              change;
        QWaitCondition    wcChangement;
        QMutex            mutexChangement;

        void              attendrePeriode(int periode);

    signals:
        void              nouvelleFrame(Mat frame);
        void              nouvelleImage(QImage image);
        void              nouvelleImage(QPixmap pixmap);
        void              nouveauMessage(QString message);
        void              finAcquisition();

    public slots:

};

#endif // TACQUISITION
