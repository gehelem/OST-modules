#include "template.h"

/**
 * @brief Required export function for dynamic module loading
 * This function is called by the controller when loading the module
 */
Template* initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    Template* module = new Template(name, label, profile, availableModuleLibs);
    return module;
}

Template::Template(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs),
      mIsRunning(false),
      mCurrentStep(0),
      mTotalSteps(10),
      mImage(nullptr)
{
    // Load Qt resources (properties definition)
    Q_INIT_RESOURCE(template);

    // Load properties from JSON file
    loadOstPropertiesFromFile(":template.json");

    // Set module class name (for profile management)
    setClassName(QString(metaObject()->className()).toLower());

    // Set module metadata
    setModuleDescription("Template module - adapt to your needs");
    setModuleVersion("1.0.0");

    // Initialize properties and request INDI devices
    initProperties();

    sendMessage("Template module initialized");
}

Template::~Template()
{
    if (mImage != nullptr)
    {
        delete mImage;
        mImage = nullptr;
    }
}

void Template::initProperties()
{
    // Define module role - choose one or more:
    // defineMeAsFocuser();    // For autofocus modules
    // defineMeAsGuider();     // For guiding modules
    // defineMeAsSequencer();  // For imaging sequence modules
    // defineMeAsNavigator();  // For telescope control modules
    defineMeAsImager();        // Generic imager (adds camera, gain, offset, exposure)

    // Request INDI devices - examples:
    giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
    // giveMeADevice("telescope", "Telescope", INDI::BaseDevice::TELESCOPE_INTERFACE);
    // giveMeADevice("focuser", "Focuser", INDI::BaseDevice::FOCUSER_INTERFACE);
    // giveMeADevice("filter", "Filter Wheel", INDI::BaseDevice::FILTER_INTERFACE);

    // Example: Create custom property at runtime
    OST::PropertyMulti* customProp = new OST::PropertyMulti(
        "customProperty",           // Internal name
        "Custom Property",          // Display label
        OST::ReadWrite,            // Permission
        "Control",                 // Level 1 (tab/category)
        "Settings",                // Level 2 (sub-category)
        "100",                     // Order (string for sorting)
        true,                      // Has profile (save in profile)
        false                      // Has grid (table view)
    );

    // Add elements to custom property
    OST::ElementInt* intElt = new OST::ElementInt("Integer value", "01", "");
    intElt->setValue(42, false);
    intElt->setDirectEdit(true);
    intElt->setAutoUpdate(true);
    intElt->setMinMax(0, 100);
    customProp->addElt("intValue", intElt);

    OST::ElementString* strElt = new OST::ElementString("Text value", "02", "");
    strElt->setValue("example", false);
    strElt->setDirectEdit(true);
    strElt->setAutoUpdate(true);
    customProp->addElt("textValue", strElt);

    // Register the property
    createProperty("customProperty", customProp);
}

void Template::OnMyExternalEvent(const QString &eventType, const QString &eventModule,
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

                if (keyelt == "autofocus" || keyelt == "guide" || keyelt == "startsequence" || keyelt == "gototarget")
                {
                    getProperty(keyprop)->setState(OST::Busy);
                    startOperation();
                }

                if (keyelt == "abortfocus" || keyelt == "abortguider" || keyelt == "abortsequence" || keyelt == "abortnavigator")
                {
                    getProperty(keyprop)->setState(OST::Ok);
                    abortOperation();
                }
            }

            // Example: Handle custom property changes
            if (keyprop == "customProperty")
            {
                if (keyelt == "intValue")
                {
                    int value = val.toInt();
                    sendMessage(QString("Integer value changed to: %1").arg(value));
                    // Do something with the value...
                }
            }
        }
    }
}

void Template::startOperation()
{
    if (mIsRunning)
    {
        sendWarning("Operation already running");
        return;
    }

    // Check INDI server connection
    if (!isServerConnected())
    {
        sendError("INDI server not connected");
        getProperty("actions")->setState(OST::Error);
        return;
    }

    // Check and connect devices
    mCameraDevice = getString("devices", "camera");
    if (mCameraDevice.isEmpty() || mCameraDevice == "--")
    {
        sendError("No camera selected");
        getProperty("actions")->setState(OST::Error);
        return;
    }

    // Connect device if not connected
    connectDevice(mCameraDevice);

    // Enable BLOB mode for camera
    setBLOBMode(B_ALSO, mCameraDevice.toStdString().c_str(), nullptr);

    // Disable properties during operation (optional)
    getProperty("parms")->disable();
    getProperty("devices")->disable();

    mIsRunning = true;
    mCurrentStep = 0;

    sendMessage("Starting operation...");

    // Update progress
    getEltPrg("progress", "global")->setPrgValue(0, true);
    getEltPrg("progress", "global")->setDynLabel("Step 0/" + QString::number(mTotalSteps), true);

    // Example: Request a camera exposure
    double exposure = getFloat("parms", "exposure");
    int gain = getInt("parms", "gain");
    int offset = getInt("parms", "offset");

    if (!requestCapture(mCameraDevice, exposure, gain, offset))
    {
        sendError("Failed to request capture");
        abortOperation();
        return;
    }
}

void Template::abortOperation()
{
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

void Template::updateProperty(INDI::Property property)
{
    QString deviceName = QString(property.getDeviceName());
    QString propertyName = QString(property.getName());

    // Example: Monitor camera exposure state
    if (deviceName == mCameraDevice && propertyName == "CCD_EXPOSURE")
    {
        if (property.getState() == IPS_BUSY)
        {
            sendMessage("Camera exposing...");
        }
    }

    // Example: Handle frame reset completion
    if (deviceName == mCameraDevice && propertyName == "CCD_FRAME_RESET")
    {
        if (property.getState() == IPS_OK)
        {
            sendMessage("Frame reset completed");
        }
    }

    // Handle BLOB (image) data
    if (propertyName == "CCD1")
    {
        newBLOB(property);
    }
}

void Template::newBLOB(INDI::PropertyBlob blob)
{
    QString deviceName = QString(blob.getDeviceName());

    if (deviceName != mCameraDevice)
        return;

    processImage(blob);
}

void Template::processImage(INDI::PropertyBlob blob)
{
    sendMessage("Processing image...");

    // Clean up previous image
    if (mImage != nullptr)
    {
        delete mImage;
        mImage = nullptr;
    }

    // Load image from BLOB
    mImage = new fileio();
    mImage->loadBlob(blob, 64); // 64 = debayer quality

    // Save JPEG preview
    QImage rawImage = mImage->getRawQImage();
    QString jpegPath = getWebroot() + "/" + getModuleName() + "_latest.jpeg";
    rawImage.save(jpegPath, "JPG", 90);

    // Update image element with stats
    OST::ImgData imgData = mImage->ImgStats();
    imgData.mUrlJpeg = getModuleName() + "_latest.jpeg";
    imgData.mUrlFits = getModuleName() + "_latest.fits";
    getEltImg("image", "image")->setValue(imgData, true);

    sendMessage(QString("Image received: %1x%2, %3 stars, HFR: %4")
        .arg(imgData.width)
        .arg(imgData.height)
        .arg(imgData.starsCount)
        .arg(imgData.HFRavg, 0, 'f', 2));

    // Continue operation or finish
    if (mIsRunning)
    {
        mCurrentStep++;

        if (mCurrentStep < mTotalSteps)
        {
            // Update progress
            int progress = (100 * mCurrentStep) / mTotalSteps;
            getEltPrg("progress", "global")->setPrgValue(progress, true);
            getEltPrg("progress", "global")->setDynLabel(
                QString("Step %1/%2").arg(mCurrentStep).arg(mTotalSteps), true);

            // Request next capture
            requestCapture(mCameraDevice,
                          getFloat("parms", "exposure"),
                          getInt("parms", "gain"),
                          getInt("parms", "offset"));
        }
        else
        {
            // Operation complete
            onOperationComplete();
        }
    }
}

void Template::onOperationComplete()
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

void Template::newDevice(INDI::BaseDevice device)
{
    QString deviceName = QString(device.getDeviceName());
    sendMessage("New INDI device detected: " + deviceName);

    // Refresh device lists for user selection
    if (device.getDriverInterface() & INDI::BaseDevice::CCD_INTERFACE)
    {
        refreshDeviceslovs(deviceName);
    }
}

void Template::removeDevice(INDI::BaseDevice device)
{
    QString deviceName = QString(device.getDeviceName());
    sendMessage("INDI device removed: " + deviceName);
}
