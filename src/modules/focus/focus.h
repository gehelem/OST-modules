#ifndef FOCUS_MODULE_h_
#define FOCUS_MODULE_h_
#include <indimodule.h>

#if defined(FOCUS_MODULE)
#  define MODULE_INIT Q_DECL_EXPORT
#else
#  define MODULE_INIT Q_DECL_IMPORT
#endif

#include <QStateMachine>

class MODULE_INIT FocusModule : public IndiModule
{
    Q_OBJECT

    public:
        FocusModule(QString name,QString label,QString profile,QVariantMap availableModuleLibs);
        ~FocusModule();

    signals:
        void focuserPositionChanged(const double &newFocuserPosition);
        //void expdone(IBLOB *bp);
        void focuserReachedPosition();
        void valueChanged(const double &newValue);
        //void valueChanged(const QString qnewValue);

        void RequestFrameResetDone();
        void FrameResetDone();
        void RequestBacklashDone();
        void BacklashDone();
        void RequestGotoStartDone();
        void GotoStartDone();

        void RequestExposureDone();
        void ExposureDone();
        void FindStarsDone();
        void NextLoop();
        void RequestGotoNextDone();
        void GotoNextDone();
        void LoopFinished();

        void RequestBacklashBestDone();
        void BacklashBestDone();
        void RequestGotoBestDone();
        void GotoBestDone();
        void RequestExposureBestDone();
        void ExposureBestDone();
        void ComputeResultDone();
        void InitLoopFrameDone();
        void LoopFrameDone();
        void NextFrame();

        void blobloaded();
        void cameraAlert();
        void abort();
    public slots:
        void OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey, const QVariantMap &eventData) override;
        void OnSucessSEP();

    private:
        void newNumber(INumberVectorProperty *nvp) override;
        void newBLOB(IBLOB *bp) override;
        void newSwitch(ISwitchVectorProperty *svp) override;


        void SMRequestFrameReset();
        void SMRequestBacklash();
        void SMRequestGotoStart();
        void SMRequestExposure();
        void SMFindStars();
        void SMCompute();
        void SMRequestGotoNext();

        void SMRequestBacklashBest();
        void SMRequestGotoBest();
        void SMRequestExposureBest();
        void SMComputeResult();
        void SMInitLoopFrame();
        void SMComputeLoopFrame();


        void SMAlert();
        //void SMLoadblob(IBLOB *bp);
        void SMLoadblob();
        void SMAbort();
        void startCoarse();


        QString _camera  = "CCD Simulator";
        QString _focuser = "Focuser Simulator";
        QString _mount = "Telescope Simulator";
        bool    _newblob;

        int    _startpos = 30000;
        int    _backlash = 100;
        int    _iterations = 3;
        int    _steps = 3000;
        int    _exposure = 2;
        int    _loopIterations = 2;
        int    _loopIteration;
        double _loopHFRavg;

        int    _iteration;
        double _bestpos;
        double _bestposfit;
        double _besthfr;
        double  _focuserPosition;
        QStateMachine _machine;

        std::vector<double> _posvector;
        std::vector<double> _hfdvector;
        std::vector<double> _coefficients;

        QPointer<fileio> _image;
        Solver _solver;
        FITSImage::Statistic stats;


};

extern "C" MODULE_INIT FocusModule *initialize(QString name,QString label,QString profile,QVariantMap availableModuleLibs);

#endif
