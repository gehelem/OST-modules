#ifndef DARKASSIST_MODULE_h_
#define DARKASSIST_MODULE_h_
#include <indimodule.h>
#include <fileio.h>

#if defined(DARKASSIST_MODULE)
#  define MODULE_INIT Q_DECL_EXPORT
#else
#  define MODULE_INIT Q_DECL_IMPORT
#endif

class MODULE_INIT Darkassist: public IndiModule
{
        Q_OBJECT

    public:
        Darkassist(QString name, QString label, QString profile, QVariantMap availableModuleLibs);
        ~Darkassist();

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

    private:
        void newBLOB(INDI::PropertyBlob pblob);
        void newProperty(INDI::Property property) override;
        void updateProperty(INDI::Property property) override;
        void newExp(INDI::PropertyNumber exp);

        void Shoot();
        void SMAlert();
        //void SMLoadblob(IBLOB *bp);
        void SMLoadblob();
        void SMAbort();

        void AppendSequence();
        void StartSequence();
        void StartLine();


        bool    _newblob;

        QPointer<fileio> _image;
        FITSImage::Statistic stats;

        int    _iterations = 3;
        int    _steps = 3000;
        int    _loopIterations = 2;
        int    _loopIteration;
        double _loopHFRavg;

        int currentLine = 0;
        int currentCount = 0;
        double currentExposure = 0;
        QString currentExposureAlpha = "";
        int currentGain = 0;
        QString currentGainAlpha = "";
        int currentOffset = 0;
        QString currentOffsetAlpha = "";
        double currentTemperature = 0;
        QString currentTemperatureAlpha = "";
        QString currentStatus = "";

        QVariantMap mActiveSeq;
        bool isSequenceRunning = false;
        QString mFolder;
        QString mSubFolder;



};

extern "C" MODULE_INIT Darkassist *initialize(QString name, QString label, QString profile,
        QVariantMap availableModuleLibs);

#endif
