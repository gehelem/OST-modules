#ifndef BLINDPEC_MODULE_h_
#define BLINDPEC_MODULE_h_
#include <indimodule.h>
#include <fileio.h>
#include <solver.h>
#include <opencv2/opencv.hpp>
#include "tacquisitionvideo.h"
Q_DECLARE_METATYPE(Mat);
#if defined(BLINDPEC_MODULE)
#  define MODULE_INIT Q_DECL_EXPORT
#else
#  define MODULE_INIT Q_DECL_IMPORT
#endif

#include <QtCore>
#include <QtConcurrent>
#include <QStateMachine>
#include <QtMultimedia/QCamera>
#include <QtMultimedia/QCameraInfo>

using namespace cv;

class MODULE_INIT BlindPec  : public IndiModule
{
        Q_OBJECT

    public:
        BlindPec (QString name, QString label, QString profile, QVariantMap availableModuleLibs);
        ~BlindPec();

    signals:
        void InitDone();
        void InitCalDone();
        void InitGuideDone();
        void AbortDone();
        void Abort();
        void RequestFrameResetDone();
        void FrameResetDone();
        void RequestExposureDone();
        void ExposureDone();
        void PulsesDone();


    public slots:
        void OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                               const QVariantMap &eventData) override;
        void traiterFrame(Mat frameIn);
    private:
        void updateProperty(INDI::Property p) override;
        void newBLOB(INDI::PropertyBlob pblob);
        void onTimer();
        bool boolloadBlobToCV(INDI::PropertyBlob pblob, int histoSize);
        QPointer<fileio> _image;
        FITSImage::Statistic stats;

        int    _pulse  = 1000;
        int    _pulseMax  = 2000;
        int    _pulseMin  = 20;
        double _raAgr = 0.8;
        double _deAgr = 0.8;
        int    _pulseN = 0;
        int    _pulseS = 0;
        int    _pulseE = 0;
        int    _pulseW = 0;
        int    _calPulseN = 300;
        int    _calPulseS = 300;
        int    _calPulseE = 300;
        int    _calPulseW = 300;
        int    _calPulseRA = 0;
        int    _calPulseDEC = 0;
        int    _calState = 0;
        int    _calStep = 0;
        int    _calSteps = 3;
        bool   _pulseRAfinished = true;
        bool   _pulseDECfinished = true;
        double _dxFirst = 0;
        double _dyFirst = 0;
        double _dxPrev = 0;
        double _dyPrev = 0;
        double _mountDEC;
        double _mountRA;
        bool   _mountPointingWest = false;
        bool   _calMountPointingWest = false;
        double _ccdOrientation;
        double _calCcdOrientation;
        double _ccdSampling = 206 * 5.2 / 800;
        int _itt = 0;
        QTimer mytimer;
        bool firstFrame = true;
        Mat previousFrame;
        Mat currentFrame;
        Point2d subPix;
        Point2d subPixTempl;
        Point2d subPixPrev;
        Point2d subPixDev;
        QDateTime dtPrev;
        QDateTime dt;
        int roidx;
        int roidy;
        int roix;
        int roiy;
        double offset = 0;
        double offsety = 0;
        //double pixsec = 2.16;

        Point minLoc, maxLoc;
        QDateTime       dtStart;
        int framecountavg = 0;
        double driftx, driftprev, drifty, driftt, ddrift, movingavgdrift, avgdrift;
        QVector<double>      drifts;
        QVector<double>      avgdrifts;
        Point matchLoc = Point(0, 0);
        Point matchLocPrev = Point(0, 0);
        int match_method = TM_CCOEFF;//TM_SQDIFF_NORMED;
        Mat result_mat;
        Mat frame, gray;
        Mat templ;
        QString _camera  = "CCD Simulator";
        QString _mount  = "Telescope Simulator";
        double minVal;
        double maxVal;
        QFile outfile;
        QFile guidelog;
        long numframe = 0;
        int countpulse = 0;
        TAcquisitionVideo   *acquisition;
        QVector<QSize>      resolutions;
        /* pid stuff */
        double setpoint; // desired output
        double processVariable; // current output
        double error; // difference between setpoint and processVariable
        double previousError; // error in previous iteration
        QVector<double>  integral; // integral of error
        double derivative; // derivative of error
        double output; // output of the controller
        double calculateOutput(double setpoint, double processVariable);

        double square(double value)
        {
            return value * value;
        }

        void SMRequestExposure();
        void SMRequestPulses();
        void SMRequestFrameReset();
        void SMAbort();
        void defineTemplate();
        void findTemplate();

};

extern "C" MODULE_INIT BlindPec *initialize(QString name, QString label, QString profile,
        QVariantMap availableModuleLibs);

#endif
