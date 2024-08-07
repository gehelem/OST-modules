#ifndef GUIDER_MODULE_h_
#define GUIDER_MODULE_h_
#include <indimodule.h>
#include <fileio.h>
#include <solver.h>

#if defined(GUIDER_MODULE)
#  define MODULE_INIT Q_DECL_EXPORT
#else
#  define MODULE_INIT Q_DECL_IMPORT
#endif

#include <QtCore>
#include <QtConcurrent>
#include <QStateMachine>


struct Trig
{
    double x1; // first star position
    double y1;
    double x2; // second star position
    double y2;
    double x3; // third star position
    double y3;
    double d12; // 1-2 distance
    double d13; // 1-3 distance
    double d23; // 2-3 distance
    double p; // perimeter
    double s; // surface
    double ratio; // surface/perimeter ratio
};

struct MatchedPair
{
    double xr; // ref star position
    double yr;
    double xc; // current star position
    double yc;
    double dx; // x drift
    double dy; // y drift
};

class MODULE_INIT Guider  : public IndiModule
{
        Q_OBJECT

    public:
        Guider (QString name, QString label, QString profile, QVariantMap availableModuleLibs);
        ~Guider();

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
        void FindStarsDone();
        void RequestPulsesDone();
        void PulsesDone();
        void ComputeFirstDone();
        void ComputeCalDone();
        void ComputeGuideDone();
        void CalibrationDone();


    public slots:
        void OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                               const QVariantMap &eventData) override;
        void OnSucessSEP();
    private:
        void updateProperty(INDI::Property p) override;
        void newBLOB(INDI::PropertyBlob pblob);

        QPointer<fileio> _image;
        Solver _solver;
        FITSImage::Statistic stats;

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

        QStateMachine *_machine;
        QStateMachine _SMInit;
        QStateMachine _SMCalibration;
        QStateMachine _SMGuide;
        QVector<Trig> _trigFirst;
        QVector<Trig> _trigPrev;
        QVector<Trig> _trigCurrent;
        QVector<MatchedPair> _matchedCurPrev;
        QVector<MatchedPair> _matchedCurFirst;


        std::vector<double> _dxvector;
        std::vector<double> _dyvector;
        std::vector<double> _coefficients;
        std::vector<double> _dRAvector;
        std::vector<double> _dDEvector;


        void buildIndexes(Solver &solver, QVector<Trig> &trig);
        void matchIndexes(QVector<Trig> ref, QVector<Trig> act, QVector<MatchedPair> &pairs, double &dx, double &dy);
        double square(double value)
        {
            return value * value;
        }

        void buildInitStateMachines(void);
        void buildCalStateMachines(void);
        void buildGuideStateMachines(void);
        void SMInitInit();
        void SMInitCal();
        void SMInitGuide();
        void SMRequestExposure();
        void SMRequestPulses();
        void SMFindStars();
        void SMComputeFirst();
        void SMComputeCal();
        void SMComputeGuide();
        void SMRequestFrameReset();
        void SMAbort();
};

extern "C" MODULE_INIT Guider *initialize(QString name, QString label, QString profile,
        QVariantMap availableModuleLibs);

#endif
