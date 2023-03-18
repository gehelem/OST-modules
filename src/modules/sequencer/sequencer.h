#ifndef SEQUENCER_MODULE_h_
#define SEQUENCER_MODULE_h_
#include <indimodule.h>
#include <fileio.h>
#include <solver.h>

#if defined(SEQUENCER_MODULE)
#  define MODULE_INIT Q_DECL_EXPORT
#else
#  define MODULE_INIT Q_DECL_IMPORT
#endif

class MODULE_INIT SequencerModule : public IndiModule
{
        Q_OBJECT

    public:
        SequencerModule(QString name, QString label, QString profile, QVariantMap availableModuleLibs);
        ~SequencerModule();

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
        void OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                               const QVariantMap &eventData) override;
        void OnSucessSEP();

    private:
        void newBLOB(INDI::PropertyBlob pblob);
        void updateProperty(INDI::Property property) override;

        void Shoot();
        void SMAlert();
        //void SMLoadblob(IBLOB *bp);
        void SMLoadblob();
        void SMAbort();

        QString _camera  = "CCD Simulator";
        QString _fw  = "Filter Simulator";
        bool    _newblob;

        QPointer<fileio> _image;
        Solver _solver;
        FITSImage::Statistic stats;

        int    _iterations = 3;
        int    _steps = 3000;
        int    _loopIterations = 2;
        int    _loopIteration;
        double _loopHFRavg;
        double _exposure = 2;

        QVariantMap mActiveSeq;


};

extern "C" MODULE_INIT SequencerModule *initialize(QString name, QString label, QString profile,
        QVariantMap availableModuleLibs);

#endif
