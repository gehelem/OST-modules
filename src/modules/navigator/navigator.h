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

        void cameraAlert();
        void newImage();
    public slots:
        void OnMyExternalEvent(const QString &pEventType, const QString  &pEventModule, const QString  &pEventKey,
                               const QVariantMap &pEventData) override;
        void OnSucessSolve();
        void OnSolverLog(QString text);
        void OnNewImage();

    private:
        void updateProperty(INDI::Property property) override;
        void newBLOB(INDI::PropertyBlob pblob);

        void initIndi(void);

        void Shoot();
        void SMAlert();
        void updateSearchList(void);
        void slewToSelection(void);
        void convertSelection(void);


        QString mState = "idle";

        QPointer<fileio> pImage;

        StellarSolver stellarSolver;
        QList<FITSImage::Star> stars;


};

extern "C" MODULE_INIT Navigator *initialize(QString name, QString label, QString profile,
        QVariantMap availableModuleLibs);

#endif
