#include "planner.h"
#include "versionModule.cc"

/**
 * @brief Required export function for dynamic module loading
 * This function is called by the controller when loading the module
 */
Planner* initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    Planner* module = new Planner(name, label, profile, availableModuleLibs);
    return module;
}

Planner::Planner(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs),
      mIsRunning(false)
{
    Q_INIT_RESOURCE(planner);

    // Load properties from JSON file
    loadOstPropertiesFromFile(":planner.json");

    // Set module class name (for profile management)
    setClassName(QString(metaObject()->className()).toLower());

    // Set module metadata
    setModuleDescription("Planner module");
    setModuleVersion("1.0.0");
    getEltString("thisGit", "hash")->setValue(QString::fromStdString(VersionModule::GIT_SHA1), true);
    getEltString("thisGit", "date")->setValue(QString::fromStdString(VersionModule::GIT_DATE), true);
    getEltString("thisGit", "message")->setValue(QString::fromStdString(VersionModule::GIT_COMMIT_SUBJECT), true);

    // Initialize properties and request INDI devices
    initProperties();

    sendMessage("Planner module initialized");
}

Planner::~Planner()
{
}

void Planner::initProperties()
{

    // Request INDI devices
    giveMeADevice("gps", "GPS", INDI::BaseDevice::GPS_INTERFACE);

    // Add start and stop buttons
    OST::PropertyMulti* pm = getProperty("actions");
    OST::ElementBool* b = new OST::ElementBool("Start", "010", "");
    b->setValue(false, false);
    pm->addElt("start", b);
    b = new OST::ElementBool("Stop", "020", "");
    b->setValue(false, false);
    pm->addElt("stop", b);


}

void Planner::OnMyExternalEvent(const QString &eventType, const QString &eventModule,
                                const QString &eventKey, const QVariantMap &eventData)
{
    Q_UNUSED(eventType);
    Q_UNUSED(eventKey);

    // Only handle events for this module
    if (getModuleName() != eventModule)
        return;

    // Iterate through properties in the event
    foreach(const QString &keyprop, eventData.keys())
    {
        // Iterate through elements in each property
        foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
        {
            // Get the value sent from frontend
            QVariant val = eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"];

            // Example: Handle actions property (buttons)
            if (keyprop == "actions")
            {
                // Reset button state
                getEltBool(keyprop, keyelt)->setValue(false, false);

                if (keyelt == "start")
                {
                    getProperty(keyprop)->setState(OST::Busy);
                    startPlanner();
                }

                if (keyelt == "stop")
                {
                    getProperty(keyprop)->setState(OST::Ok);
                    abortPlanner();
                }
            }
        }

        if (keyprop == "planning")
        {
            if (eventType == "Fldelete")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                getProperty(keyprop)->deleteLine(line);
            }
            if (eventType == "Flcreate")
            {
                QVariantMap m = eventData[keyprop].toMap()["elements"].toMap();
                getProperty(keyprop)->newLine(m);
                //getEltPrg(keyprop, "progress")->setPrgValue(0, false);
                //getEltPrg(keyprop, "progress")->setDynLabel("Added", false);
                getProperty(keyprop)->updateLine(getProperty(keyprop)->getGrid().count() - 1);
            }
            if (eventType == "Flupdate")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                QVariantMap m = eventData[keyprop].toMap()["elements"].toMap();
                getProperty(keyprop)->updateLine(line, m);
                //getEltPrg(keyprop, "progress")->setPrgValue(0, false);
                //getEltPrg(keyprop, "progress")->setDynLabel("Updated", false);
                getProperty(keyprop)->updateLine(line);
            }
        }

    }
}

void Planner::startPlanner()
{
    if (mIsRunning)
    {
        sendWarning("Operation already running");
        return;
    }
    getEltBool("actions", "start")->setValue(true, false);
    getEltBool("actions", "stop")->setValue(false, true);
    // Check INDI server connection
    if (!isServerConnected())
    {
        sendError("INDI server not connected");
        getProperty("actions")->setState(OST::Error);
        abortPlanner();
        return;
    }

    // Check and connect devices
    QString g = getString("devices", "gps");
    if (g.isEmpty() || g == "--")
    {
        sendError("No GPS selected");
        getProperty("actions")->setState(OST::Error);
        abortPlanner();
        return;
    }

    // Connect device if not connected
    connectDevice(g);

    // Check slaves modules
    g = getString("parms", "sequencemodule");
    if (g.isEmpty() || g == "--")
    {
        sendError("No Sequence module selected");
        getProperty("actions")->setState(OST::Error);
        abortPlanner();
        return;
    }
    g = getString("parms", "navigatormodule");
    if (g.isEmpty() || g == "--")
    {
        sendError("No Sequence module selected");
        getProperty("actions")->setState(OST::Error);
        abortPlanner();
        return;
    }

    // Disable properties during operation (optional)
    getProperty("parms")->disable();
    getProperty("devices")->disable();

    mIsRunning = true;

    sendMessage("Starting planner...");

    // Update progress
    getEltPrg("progress", "global")->setPrgValue(0, true);
    //getEltPrg("progress", "global")->setDynLabel("Step 0/" + QString::number(mTotalSteps), true);

}

void Planner::abortPlanner()
{
    getEltBool("actions", "start")->setValue(false, false);
    getEltBool("actions", "stop")->setValue(true, true);

    if (!mIsRunning)
        return;

    mIsRunning = false;

    sendMessage("Operation aborted");

    // Re-enable properties
    getProperty("parms")->enable();
    getProperty("devices")->enable();

    // Reset progress
    getEltPrg("progress", "global")->setPrgValue(0, true);
    getEltPrg("progress", "global")->setDynLabel("Aborted", true);

    getProperty("actions")->setState(OST::Ok);
}

void Planner::updateProperty(INDI::Property property)
{
    QString deviceName = QString(property.getDeviceName());
    QString propertyName = QString(property.getName());

    // Example: Monitor camera exposure state
    //if (deviceName == mCameraDevice && propertyName == "CCD_EXPOSURE")
    //{
    //    if (property.getState() == IPS_BUSY)
    //    {
    //        sendMessage("Camera exposing...");
    //    }
    //}

    //// Example: Handle frame reset completion
    //if (deviceName == mCameraDevice && propertyName == "CCD_FRAME_RESET")
    //{
    //    if (property.getState() == IPS_OK)
    //    {
    //        sendMessage("Frame reset completed");
    //    }
    //}

    //// Handle BLOB (image) data
    //if (propertyName == "CCD1")
    //{
    //    newBLOB(property);
    //}
}

void Planner::onOperationComplete()
{
    sendMessage("Operation completed successfully");

    mIsRunning = false;

    // Re-enable properties
    getProperty("parms")->enable();
    getProperty("devices")->enable();

    // Update progress to 100%
    getEltPrg("progress", "global")->setPrgValue(100, true);
    getEltPrg("progress", "global")->setDynLabel("Completed", true);

    getProperty("actions")->setState(OST::Ok);
}

void Planner::newDevice(INDI::BaseDevice device)
{
    //QString deviceName = QString(device.getDeviceName());
    //sendMessage("New INDI device detected: " + deviceName);

    //// Refresh device lists for user selection
    //if (device.getDriverInterface() & INDI::BaseDevice::CCD_INTERFACE)
    //{
    //    refreshDeviceslovs(deviceName);
    //}
}

void Planner::removeDevice(INDI::BaseDevice device)
{
    //QString deviceName = QString(device.getDeviceName());
    //sendMessage("INDI device removed: " + deviceName);
}
