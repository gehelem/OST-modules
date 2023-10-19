#ifndef NAVIGATOR_MODULE_h_
#define NAVIGATOR_MODULE_h_
#include <indimodule.h>
#include <fileio.h>
#include <solver.h>

#include <libastro.h>
#include <libnova/julian_day.h>

#define PI 3.14159265

#if defined(NAVIGATOR_MODULE)
#  define MODULE_INIT Q_DECL_EXPORT
#else
#  define MODULE_INIT Q_DECL_IMPORT
#endif

class MODULE_INIT Navigator : public IndiModule
{
        Q_OBJECT

    public:
        Navigator(QString name, QString label, QString profile, QVariantMap availableModuleLibs);
        ~Navigator();

    signals:

        void RequestFrameResetDone();
        void FrameResetDone();
        void RequestExposureDone();
        void ExposureDone();
        void FindStarsDone();
        void NextLoop();
        void LoopFinished();
        void ComputeDone();
        void InitDone();

        void RequestExposureBestDone();
        void ExposureBestDone();
        void ComputeResultDone();
        void InitLoopFrameDone();
        void LoopFrameDone();
        void NextFrame();

        void blobloaded();
        void cameraAlert();
        void AbortDone();
        void Abort();
    public slots:
        void OnMyExternalEvent(const QString &pEventType, const QString  &pEventModule, const QString  &pEventKey,
                               const QVariantMap &pEventData) override;
        void OnSucessSolve();
        void OnSolverLog(QString &text);

    private:
        void updateProperty(INDI::Property property) override;
        void newBLOB(INDI::PropertyBlob pblob);

        void initIndi(void);

        void Shoot();
        void SMAlert();
        //void SMLoadblob(IBLOB *bp);
        void SMLoadblob();
        void SMAbort();
        void updateSearchList(void);
        void slewToSelection(void);
        void convertSelection(void);


        QString mCamera  = "CCD Simulator";
        QString mMount  = "Telescope Simulator";
        QString mState = "idle";

        QPointer<fileio> pImage;
        Solver mSolver;
        FITSImage::Statistic mStats;


};

extern "C" MODULE_INIT Navigator *initialize(QString name, QString label, QString profile,
        QVariantMap availableModuleLibs);

#endif
