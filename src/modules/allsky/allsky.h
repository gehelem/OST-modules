#ifndef ALLSKY_MODULE_h_
#define ALLSKY_MODULE_h_
#include <indimodule.h>

#if defined(ALLSKY_MODULE)
#  define MODULE_INIT Q_DECL_EXPORT
#else
#  define MODULE_INIT Q_DECL_IMPORT
#endif

class MODULE_INIT Allsky : public IndiModule
{
    Q_OBJECT

    public:
        Allsky(QString name,QString label,QString profile,QVariantMap availableModuleLibs);
        ~Allsky();

    public slots:
        void OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey, const QVariantMap &eventData) override;
        void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
    private:
        void newBLOB(IBLOB *bp) override;
        void startLoop();
        void startBatch();
        void processOutput();
        void processError();
        QString _camera  = "CCD Simulator";
        QPointer<fileio> _image;
        Solver _solver;
        FITSImage::Statistic stats;
        bool _isLooping = false;
        long _index;
        QProcess *_process;



};

extern "C" MODULE_INIT Allsky *initialize(QString name,QString label,QString profile,QVariantMap availableModuleLibs);

#endif
