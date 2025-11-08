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

    // Check if this is a sequence completion event
    if (eventType == "sequencedone" && mWaitingSequence && getString("parms", "sequencemodule") == eventModule )
    {
        sequenceComplete();
        return;
    }

    // Report sequence progress
    if (eventType == "se" && mWaitingSequence && getString("parms", "sequencemodule") == eventModule
            && eventKey == "progress" )
    {
        int i = eventData["elements"].toMap()["global"].toMap()["value"].toInt();
        QString s = eventData["elements"].toMap()["global"].toMap()["dynlabel"].toString();
        getEltPrg("planning", "progress")->setPrgValue(i, false);
        getEltPrg("planning", "progress")->setDynLabel("Seq " + s, false);
        getProperty("planning")->updateLine(mCurrentLine);
    }

    // Check if this is a navigator completion event
    if (eventType == "navigatordone" && mWaitingNavigator && getString("parms", "navigatormodule") == eventModule )
    {
        navigatorComplete();
        return;
    }

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
                getEltPrg(keyprop, "progress")->setPrgValue(0, false);
                getEltPrg(keyprop, "progress")->setDynLabel("Queued", false);
                getProperty(keyprop)->updateLine(getProperty(keyprop)->getGrid().count() - 1);
            }
            if (eventType == "Flupdate")
            {
                double line = eventData[keyprop].toMap()["line"].toDouble();
                QVariantMap m = eventData[keyprop].toMap()["elements"].toMap();
                getProperty(keyprop)->updateLine(line, m);
                getEltPrg(keyprop, "progress")->setPrgValue(0, false);
                getEltPrg(keyprop, "progress")->setDynLabel("Updated", false);
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
    int lineCount = getProperty("planning")->getGrid().count();

    if (lineCount == 0)
    {
        sendError("Planning grid is empty");
        getProperty("actions")->setState(OST::Error);
        abortPlanner();
        return;
    }


    mIsRunning = true;

    getProperty("actions")->setState(OST::Busy);
    sendMessage("Starting planner...");

    // Disable properties during operation (optional)
    getProperty("parms")->disable();
    getProperty("devices")->disable();

    // Update progress
    getEltPrg("progress", "global")->setPrgValue(0, true);
    getEltPrg("progress", "global")->setDynLabel("Step 0/" + QString::number(lineCount), true);
    for (int i = 0; i < getProperty("planning")->getGrid().count(); i++)
    {
        getProperty("planning")->fetchLine(i);
        getEltPrg("planning", "progress")->setDynLabel("Queued", false);
        getEltPrg("planning", "progress")->setPrgValue(0, false);
        getProperty("planning")->updateLine(i);
    }

    mCurrentLine = 0;
    startLine();

}

void Planner::abortPlanner()
{
    getEltBool("actions", "start")->setValue(false, false);
    getEltBool("actions", "stop")->setValue(true, true);

    if (!mIsRunning)
        return;

    mIsRunning = false;
    mWaitingSequence = false;
    mWaitingNavigator = false;

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
void Planner::sequenceComplete()
{
    mWaitingSequence = false;
    mWaitingNavigator = false;


    getProperty("planning")->fetchLine(mCurrentLine);
    getEltPrg("planning", "progress")->setDynLabel("Finished", false);
    getEltPrg("planning", "progress")->setPrgValue(100, false);
    getProperty("planning")->updateLine(mCurrentLine);


    mCurrentLine++;
    if (getProperty("planning")->getGrid().count() <= mCurrentLine)
    {
        sendMessage("Planning complete");
        mIsRunning = false;
        return;
    }

    startLine();

}
void Planner::navigatorComplete()
{
    mWaitingNavigator = false;

    sendMessage("Navigator went to target " + getString("planning",
                "object") + ", starting sequence with profile " + getString("planning",
                        "profile") );

    // Ask sequence to start
    QVariantMap eltData;
    eltData["startsequence"] = true;
    QVariantMap propData;
    propData["elements"] = eltData;
    QVariantMap eventData;
    eventData["actions"] = propData;
    emit moduleEvent("Fsetproperty", getString("parms", "sequencemodule"), "", eventData);

    mWaitingSequence = true;

}
void Planner::startLine()
{
    // update line status
    getProperty("planning")->fetchLine(mCurrentLine);
    getEltPrg("planning", "progress")->setDynLabel("Slewing", false);
    getEltPrg("planning", "progress")->setPrgValue(0, false);
    getProperty("planning")->updateLine(mCurrentLine);

    sendMessage("Navigator is slewing to target " + getString("planning", "object"));

    mWaitingSequence = false;
    mWaitingNavigator = true;

    // Set navigator's target
    QVariantMap eltData;
    QVariantMap propData;
    QVariantMap eventData;
    eltData["targetname"] = getString("planning", "object");
    propData["elements"] = eltData;
    eventData["actions"] = propData;
    emit moduleEvent("Fsetproperty", getString("parms", "navigatormodule"), "", eventData);
    eltData = QVariantMap();
    eltData["targetra"] = getFloat("planning", "ra");
    propData["elements"] = eltData;
    eventData["actions"] = propData;
    emit moduleEvent("Fsetproperty", getString("parms", "navigatormodule"), "", eventData);
    eltData = QVariantMap();
    eltData["targetde"] = getFloat("planning", "dec");
    propData["elements"] = eltData;
    eventData["actions"] = propData;
    emit moduleEvent("Fsetproperty", getString("parms", "navigatormodule"), "", eventData);

    // Set sequencer's profile
    eltData = QVariantMap();
    eltData["name"] = getString("planning", "profile");
    propData = QVariantMap();
    propData["elements"] = eltData;
    eventData = QVariantMap();
    eventData["loadprofile"] = propData;
    emit moduleEvent("Fsetproperty", getString("parms", "sequencemodule"), "", eventData); // set profile to load
    emit moduleEvent("Fposticon", getString("parms", "sequencemodule"), "", eventData); // load it

    // Set sequencer's object data
    eltData = QVariantMap();
    eltData["label"] = getString("planning", "object");
    propData = QVariantMap();
    propData["elements"] = eltData;
    eventData = QVariantMap();
    eventData["object"] = propData;
    emit moduleEvent("Fsetproperty", getString("parms", "sequencemodule"), "", eventData);
    eventData = QVariantMap();
    eltData["ra"] = getFloat("planning", "ra");
    propData["elements"] = eltData;
    eventData["object"] = propData;
    emit moduleEvent("Fsetproperty", getString("parms", "sequencemodule"), "", eventData);
    eventData = QVariantMap();
    eltData["de"] = getFloat("planning", "dec");
    propData["elements"] = eltData;
    eventData["object"] = propData;
    emit moduleEvent("Fsetproperty", getString("parms", "sequencemodule"), "", eventData);

    // Ask navigator to slew to target
    eltData = QVariantMap();
    eltData["gototarget"] = true;
    propData = QVariantMap();
    propData["elements"] = eltData;
    eventData = QVariantMap();
    eventData["actions"] = propData;
    emit moduleEvent("Fsetproperty", getString("parms", "navigatormodule"), "", eventData);

}
