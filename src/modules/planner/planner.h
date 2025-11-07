#ifndef PLANNER_H
#define PLANNER_H

#include <indimodule.h>

/**
 * @brief PLanner module -
 *
 * Scheduling list :
 * - A target
 * - A sequencer profile
 * - Constraints
 */
class Planner : public IndiModule
{
        Q_OBJECT

    public:
        Planner(QString name, QString label, QString profile, QVariantMap availableModuleLibs);
        ~Planner();

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
         * @brief Initialize module-specific properties and devices
         */
        void initProperties();

        /**
         * @brief Start the main operation (adapt to your needs)
         */
        void startPlanner();

        /**
         * @brief Stop/abort the current operation
         */
        void abortPlanner();

        // Example internal state variables
        bool mIsRunning;

};

// Required export function for module loading
extern "C" Planner* initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs);

#endif // Planner_H
