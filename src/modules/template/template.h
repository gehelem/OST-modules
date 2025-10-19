#ifndef TEMPLATE_H
#define TEMPLATE_H

#include <indimodule.h>
#include <fileio.h>
#include <solver.h>

/**
 * @brief Template module - skeleton for creating new OST modules
 *
 * This is a complete example showing all common patterns:
 * - INDI device management
 * - Property creation and updates
 * - State machine integration (optional)
 * - Profile management
 * - Image handling
 */
class Template : public IndiModule
{
    Q_OBJECT

public:
    Template(QString name, QString label, QString profile, QVariantMap availableModuleLibs);
    ~Template();

protected:
    /**
     * @brief Handle external events from controller/websocket
     * Called when user interacts with properties from frontend
     */
    void OnMyExternalEvent(const QString &eventType, const QString &eventModule,
                          const QString &eventKey, const QVariantMap &eventData) override;

    /**
     * @brief Handle INDI property updates
     * Called when INDI devices send updates (exposure done, position changed, etc.)
     */
    void updateProperty(INDI::Property property) override;

    /**
     * @brief Handle new INDI device connection
     * Called when a device connects to INDI server
     */
    void newDevice(INDI::BaseDevice device) override;

    /**
     * @brief Handle INDI device removal
     * Called when a device disconnects from INDI server
     */
    void removeDevice(INDI::BaseDevice device) override;

private slots:
    /**
     * @brief Example slot for handling async operations
     */
    void onOperationComplete();

private:
    /**
     * @brief Handle INDI BLOB (images/large data)
     * Called when camera sends image data
     */
    void newBLOB(INDI::PropertyBlob blob);
    /**
     * @brief Initialize module-specific properties and devices
     */
    void initProperties();

    /**
     * @brief Start the main operation (adapt to your needs)
     */
    void startOperation();

    /**
     * @brief Stop/abort the current operation
     */
    void abortOperation();

    /**
     * @brief Process received image from camera
     */
    void processImage(INDI::PropertyBlob blob);

    // Example internal state variables
    bool mIsRunning;
    int mCurrentStep;
    int mTotalSteps;

    // Image handling (if needed)
    fileio* mImage;

    // Example: store device references
    QString mCameraDevice;
    QString mTelescopeDevice;
};

// Required export function for module loading
extern "C" Template* initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs);

#endif // TEMPLATE_H
