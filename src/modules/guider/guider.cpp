#include "guider.h"
#include "versionModule.cc"
//#include "polynomialfit.h"
#define PI 3.14159265

Guider *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    Guider *basemodule = new Guider(name, label, profile, availableModuleLibs);
    return basemodule;
}
Guider::Guider(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)
{

    loadOstPropertiesFromFile(":guider.json");
    setClassName(QString(metaObject()->className()).toLower());
    setModuleDescription("Guider module - work in progress");
    setModuleVersion("0.1");
    getEltString("thisGit", "hash")->setValue(QString::fromStdString(VersionModule::GIT_SHA1), true);
    getEltString("thisGit", "date")->setValue(QString::fromStdString(VersionModule::GIT_DATE), true);
    getEltString("thisGit", "message")->setValue(QString::fromStdString(VersionModule::GIT_COMMIT_SUBJECT), true);

    buildInitStateMachines();
    buildCalStateMachines();
    buildGuideStateMachines();

    giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
    giveMeADevice("guider", "Guide via", INDI::BaseDevice::GUIDER_INTERFACE);

    defineMeAsGuider();

}

Guider::~Guider()
{

}
void Guider::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                               const QVariantMap &eventData)
{
    Q_UNUSED(eventKey);

    //BOOST_LOG_TRIVIAL(debug) << "OnMyExternalEvent - recv : " << getName().toStdString() << "-" << eventType.toStdString() << "-" << eventKey.toStdString();

    // Handle suspend/resume guiding events from sequencer
    if (eventType == "suspendguiding" && getModuleName() == eventModule)
    {
        sendMessage("Guiding suspended by external request (focus in progress)");
        // Stop the guiding state machine
        _SMGuide.stop();
        return;
    }

    if (eventType == "resumeguiding" && getModuleName() == eventModule)
    {
        sendMessage("Resuming guiding after external suspension (focus completed)");
        // Restart the guiding state machine
        disconnect(&_SMInit,        &QStateMachine::finished, nullptr, nullptr);
        disconnect(&_SMCalibration, &QStateMachine::finished, nullptr, nullptr);
        connect(&_SMInit,           &QStateMachine::finished, &_SMGuide, &QStateMachine::start);
        _SMInit.start();
        return;
    }

    if (getModuleName() == eventModule)
    {
        foreach(const QString &keyprop, eventData.keys())
        {
            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                QVariant val = eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"];
                if (keyprop == "actions")
                {
                    if (keyelt == "calguide")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(false))
                        {
                            getProperty(keyprop)->setState(OST::Busy);

                            disconnect(&_SMInit,        &QStateMachine::finished, nullptr, nullptr);
                            disconnect(&_SMCalibration, &QStateMachine::finished, nullptr, nullptr);
                            connect(&_SMInit,           &QStateMachine::finished, &_SMCalibration, &QStateMachine::start) ;
                            connect(&_SMCalibration,    &QStateMachine::finished, &_SMGuide,      &QStateMachine::start) ;
                            _SMInit.start();
                        }
                    }
                    if (keyelt == "abortguider")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(false))
                        {
                            getProperty(keyprop)->setState(OST::Ok);

                            emit Abort();
                        }
                    }
                    if (keyelt == "calibrate")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(false))
                        {
                            getProperty(keyprop)->setState(OST::Ok);
                            disconnect(&_SMInit,        &QStateMachine::finished, nullptr, nullptr);
                            disconnect(&_SMCalibration, &QStateMachine::finished, nullptr, nullptr);
                            connect(&_SMInit,           &QStateMachine::finished, &_SMCalibration, &QStateMachine::start) ;
                            _SMInit.start();

                        }
                    }
                    if (keyelt == "guide")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(false))
                        {
                            getProperty(keyprop)->setState(OST::Ok);
                            disconnect(&_SMInit,        &QStateMachine::finished, nullptr, nullptr);
                            disconnect(&_SMCalibration, &QStateMachine::finished, nullptr, nullptr);
                            connect(&_SMInit,           &QStateMachine::finished, &_SMGuide, &QStateMachine::start) ;
                            _SMInit.start();

                        }
                    }

                }
            }
        }
    }
}

void Guider::updateProperty(INDI::Property property)
{
    if (strcmp(property.getName(), "CCD1") == 0)
    {
        newBLOB(property);
    }
    if (
        (property.getDeviceName() == getString("devices", "camera"))
        &&  (QString(property.getName()) == "CCD_FRAME_RESET")
        &&  (property.getState() == IPS_OK)
    )
    {
        //sendMessage("FrameResetDone");
        emit FrameResetDone();
    }
    if (
        (property.getDeviceName() == getString("devices", "guider")) &&
        (QString(property.getName())   == "TELESCOPE_TIMED_GUIDE_NS") &&
        (property.getState()  == IPS_IDLE)

    )
    {
        _pulseDECfinished = true;
    }

    if (
        (property.getDeviceName() == getString("devices", "guider")) &&
        (QString(property.getName())  == "TELESCOPE_TIMED_GUIDE_WE") &&
        (property.getState()  == IPS_IDLE)

    )
    {
        _pulseRAfinished = true;
    }

    if (
        (property.getDeviceName() == getString("devices", "guider")) &&
        ( (QString(property.getName())   == "TELESCOPE_TIMED_GUIDE_WE") ||
          (QString(property.getName())  == "TELESCOPE_TIMED_GUIDE_NS") ) &&
        (property.getState()  == IPS_IDLE)

    )
    {
        if (_pulseRAfinished && _pulseDECfinished) emit PulsesDone();
    }


}

void Guider::newBLOB(INDI::PropertyBlob pblob)
{
    if (
        (QString(pblob.getDeviceName()) == getString("devices", "camera"))
    )
    {
        delete _image;
        _image = new fileio();
        _image->loadBlob(pblob, 64);
        stats = _image->getStats();
        QImage rawImage = _image->getRawQImage();
        QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
        im.setColorTable(rawImage.colorTable());

        im.save(getWebroot() + "/" + getModuleName() + ".jpeg", "JPG", 100);
        OST::ImgData dta = _image->ImgStats();
        dta.mUrlJpeg = getModuleName() + ".jpeg";
        getEltImg("image", "image")->setValue(dta, true);


        //BOOST_LOG_TRIVIAL(debug) << "Emit Exposure done";
        emit ExposureDone();
    }

}

void Guider::buildInitStateMachines(void)
{
    /* Initialization statemachine = SMInit */

    auto *Abort = new QState();
    auto *Init  = new QState();
    auto *End   = new QFinalState();

    auto *InitInit             = new QState(Init);
    auto *RequestFrameReset    = new QState(Init);
    auto *WaitFrameReset       = new QState(Init);
    auto *RequestFirstExposure = new QState(Init);
    auto *WaitFirstExposure    = new QState(Init);
    auto *FindStarsFirst       = new QState(Init);
    auto *ComputeFirst         = new QState(Init);

    connect(InitInit, &QState::entered, this, &Guider::SMInitInit);
    connect(RequestFrameReset, &QState::entered, this, &Guider::SMRequestFrameReset);
    connect(RequestFirstExposure, &QState::entered, this, &Guider::SMRequestExposure);
    connect(FindStarsFirst, &QState::entered, this, &Guider::SMFindStars);
    connect(ComputeFirst, &QState::entered, this, &Guider::SMComputeFirst);
    connect(Abort,               &QState::entered, this, &Guider::SMAbort);

    Init->                addTransition(this, &Guider::Abort, Abort);
    Abort->               addTransition(this, &Guider::AbortDone, End);
    InitInit->            addTransition(this, &Guider::InitDone, RequestFrameReset);
    RequestFrameReset->   addTransition(this, &Guider::RequestFrameResetDone, WaitFrameReset);
    WaitFrameReset->      addTransition(this, &Guider::FrameResetDone, RequestFirstExposure);
    RequestFirstExposure->addTransition(this, &Guider::RequestExposureDone, WaitFirstExposure);
    WaitFirstExposure->   addTransition(this, &Guider::ExposureDone, FindStarsFirst);
    FindStarsFirst->      addTransition(this, &Guider::FindStarsDone, ComputeFirst);
    ComputeFirst->        addTransition(this, &Guider::ComputeFirstDone, End);

    Init->setInitialState(InitInit);

    _SMInit.addState(Init);
    _SMInit.addState(Abort);
    _SMInit.addState(End);
    _SMInit.setInitialState(Init);


}
void Guider::buildCalStateMachines(void)
{

    auto *Abort = new QState();
    auto *Cal  = new QState();
    auto *End   = new QFinalState();

    auto *InitCal             = new QState(Cal);
    auto *RequestCalPulses    = new QState(Cal);
    auto *WaitCalPulses       = new QState(Cal);
    auto *RequestCalExposure  = new QState(Cal);
    auto *WaitCalExposure     = new QState(Cal);
    auto *FindStarsCal        = new QState(Cal);
    auto *ComputeCal          = new QState(Cal);

    connect(InitCal, &QState::entered, this, &Guider::SMInitCal);
    connect(RequestCalExposure, &QState::entered, this, &Guider::SMRequestExposure);
    connect(FindStarsCal, &QState::entered, this, &Guider::SMFindStars);
    connect(ComputeCal, &QState::entered, this, &Guider::SMComputeCal);
    connect(RequestCalPulses,    &QState::entered, this, &Guider::SMRequestPulses);
    connect(Abort,               &QState::entered, this, &Guider::SMAbort);

    Cal->                 addTransition(this, &Guider::Abort, Abort);
    Abort->               addTransition(this, &Guider::AbortDone, End);
    InitCal->             addTransition(this, &Guider::InitCalDone, RequestCalPulses);

    RequestCalPulses->    addTransition(this, &Guider::RequestPulsesDone, WaitCalPulses);
    WaitCalPulses->       addTransition(this, &Guider::PulsesDone, RequestCalExposure);
    RequestCalExposure->  addTransition(this, &Guider::RequestExposureDone, WaitCalExposure);
    WaitCalExposure->     addTransition(this, &Guider::ExposureDone, FindStarsCal);
    FindStarsCal->        addTransition(this, &Guider::FindStarsDone, ComputeCal);
    ComputeCal->          addTransition(this, &Guider::ComputeCalDone, RequestCalPulses);
    ComputeCal->          addTransition(this, &Guider::CalibrationDone, End);


    Cal->setInitialState(InitCal);

    _SMCalibration.addState(Cal);
    _SMCalibration.addState(Abort);
    _SMCalibration.addState(End);
    _SMCalibration.setInitialState(Cal);


}
void Guider::buildGuideStateMachines(void)
{

    auto *Abort = new QState();
    auto *Guide  = new QState();
    auto *End   = new QFinalState();

    auto *InitGuide           = new QState(Guide);
    auto *RequestGuideExposure = new QState(Guide);
    auto *WaitGuideExposure   = new QState(Guide);
    auto *FindStarsGuide      = new QState(Guide);
    auto *ComputeGuide        = new QState(Guide);
    auto *RequestGuidePulses  = new QState(Guide);
    auto *WaitGuidePulses     = new QState(Guide);

    connect(InitGuide, &QState::entered, this, &Guider::SMInitGuide);
    connect(RequestGuideExposure, &QState::entered, this, &Guider::SMRequestExposure);
    connect(FindStarsGuide, &QState::entered, this, &Guider::SMFindStars);
    connect(ComputeGuide, &QState::entered, this, &Guider::SMComputeGuide);
    connect(RequestGuidePulses,  &QState::entered, this, &Guider::SMRequestPulses);
    connect(Abort,               &QState::entered, this, &Guider::SMAbort);

    Guide->               addTransition(this, &Guider::Abort, Abort);
    Abort->               addTransition(this, &Guider::AbortDone, End);
    InitGuide->           addTransition(this, &Guider::InitGuideDone, RequestGuideExposure);

    RequestGuideExposure->  addTransition(this, &Guider::RequestExposureDone, WaitGuideExposure);
    WaitGuideExposure->     addTransition(this, &Guider::ExposureDone, FindStarsGuide);
    FindStarsGuide->        addTransition(this, &Guider::FindStarsDone, ComputeGuide);
    ComputeGuide->          addTransition(this, &Guider::ComputeGuideDone, RequestGuidePulses);
    RequestGuidePulses->    addTransition(this, &Guider::RequestPulsesDone, WaitGuidePulses);
    RequestGuidePulses->    addTransition(this, &Guider::PulsesDone, RequestGuideExposure);
    WaitGuidePulses->       addTransition(this, &Guider::PulsesDone, RequestGuideExposure);
    //ComputeGuide->          addTransition(this,&Guider::GuideDone           ,End); // useless ??


    Guide->setInitialState(InitGuide);

    _SMGuide.addState(Guide);
    _SMGuide.addState(Abort);
    _SMGuide.addState(End);
    _SMGuide.setInitialState(Guide);


}
void Guider::SMInitInit()
{
    //sendMessage("SMInitInit");
    if (connectDevice(getString("devices", "camera")))
    {
        connectIndi();
        connectDevice(getString("devices", "camera"));
        connectDevice(getString("devices", "guider"));
        setBLOBMode(B_ALSO, getString("devices", "camera").toStdString().c_str(), nullptr);
        enableDirectBlobAccess(getString("devices", "camera").toStdString().c_str(), nullptr);
        frameReset(getString("devices", "camera"));
        if (getString("devices", "camera") == "CCD Simulator")
        {
            sendModNewNumber(getString("devices", "camera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
        }
        getProperty("actions")->setState(OST::Busy);
        getProperty("drift")->clearGrid();
        getProperty("guiding")->clearGrid();
        getProperty("snr")->clearGrid();
    }

    else
    {
        getProperty("actions")->setState(OST::Error);
        emit Abort();
        return;
    }

    /* get mount DEC */
    if (!getModNumber(getString("devices", "guider"), "EQUATORIAL_EOD_COORD", "DEC",
                      _mountDEC))
    {
        emit Abort();
        return;
    }
    /* get mount RA */
    if (!getModNumber(getString("devices", "guider"), "EQUATORIAL_EOD_COORD", "RA",
                      _mountRA))
    {
        emit Abort();
        return;
    }
    /* get mount Pier position  */
    if (!getModSwitch(getString("devices", "guider"), "TELESCOPE_PIER_SIDE", "PIER_WEST",
                      _mountPointingWest))
    {
        emit Abort();
        return;
    }
    //_grid->clear();
    //_propertyStore.update(_grid);
    //emit propertyUpdated(_grid,&_modulename);
    //BOOST_LOG_TRIVIAL(debug) << "SMInitInitDone";
    emit InitDone();
}
void Guider::SMInitCal()
{
    //sendMessage("SMInitCal");
    //_states->addLight(new LightValue("idle"  ,"Idle","hint",0));
    //_states->addLight(new LightValue("cal"   ,"Calibrating","hint",2));
    //_states->addLight(new LightValue("guide" ,"Guiding","hint",0));
    //_states->addLight(new LightValue("error" ,"Error","hint",0));
    //emit propertyUpdated(_states,&_modulename);
    //_propertyStore.update(_states);

    _calState = 0;
    _calStep = 0;
    _calPulseN = 0;
    _calPulseS = 0;
    _calPulseE = 0;
    _calPulseW = 0;
    getEltInt("calibrationvalues", "calPulseN")->setValue(_calPulseN);
    getEltInt("calibrationvalues", "calPulseS")->setValue(_calPulseS);
    getEltInt("calibrationvalues", "calPulseE")->setValue(_calPulseE);
    getEltInt("calibrationvalues", "calPulseW")->setValue(_calPulseW, true);
    _pulseN = 0;
    _pulseS = 0;
    _pulseE = 0;
    _pulseW = getInt("calParams", "pulse");
    _trigCurrent.clear();
    _trigPrev = _trigFirst;
    _dxvector.clear();
    _dyvector.clear();
    _coefficients.clear();
    _itt = 0;
    _pulseDECfinished = true;
    _pulseRAfinished = true;

    emit InitCalDone();
}
void Guider::SMInitGuide()
{
    //sendMessage("SMInitGuide");
    getProperty("drift")->clearGrid();;

    //BOOST_LOG_TRIVIAL(debug) << "************************************************************";
    //BOOST_LOG_TRIVIAL(debug) << "************************************************************";
    //BOOST_LOG_TRIVIAL(debug) << "Guider module - Start guide with fllowing calibration data : ";
    //BOOST_LOG_TRIVIAL(debug) << "*********************** cal CCD Orientation " << _calCcdOrientation * 180 / PI;
    //BOOST_LOG_TRIVIAL(debug) << "*********************** cal moutn pointing west  " << _calMountPointingWest;
    //BOOST_LOG_TRIVIAL(debug) << "*********************** cal W " << _calPulseW;
    //BOOST_LOG_TRIVIAL(debug) << "*********************** cal E " << _calPulseE;
    //BOOST_LOG_TRIVIAL(debug) << "*********************** cal N " << _calPulseN;
    //BOOST_LOG_TRIVIAL(debug) << "*********************** cal S " << _calPulseS;
    //BOOST_LOG_TRIVIAL(debug) << "************************************************************";
    //BOOST_LOG_TRIVIAL(debug) << "************************************************************";
    //_states->addLight(new LightValue("idle"  ,"Idle","hint",0));
    //_states->addLight(new LightValue("cal"   ,"Calibrating","hint",0));
    //_states->addLight(new LightValue("guide" ,"Guiding","hint",2));
    //_states->addLight(new LightValue("error" ,"Error","hint",0));
    //emit propertyUpdated(_states,&_modulename);
    //_propertyStore.update(_states);

    //_grid->clear();
    //_propertyStore.update(_grid);
    //emit propertyUpdated(_grid,&_modulename);

    //BOOST_LOG_TRIVIAL(debug) << "SMInitGuideDone";
    emit InitGuideDone();
}
void Guider::SMRequestFrameReset()
{
    //BOOST_LOG_TRIVIAL(debug) << "SMRequestFrameReset";
    //sendMessage("SMRequestFrameReset");
    if (!frameReset(getString("devices", "camera")))
    {
        emit Abort();
        return;
    }
    //BOOST_LOG_TRIVIAL(debug) << "SMRequestFrameResetDone";
    emit RequestFrameResetDone();
}


void Guider::SMRequestExposure()
{
    //BOOST_LOG_TRIVIAL(debug) << "SMRequestExposure";
    //sendMessage("SMRequestExposure");
    if (!requestCapture(getString("devices", "camera"), getFloat("parms", "exposure"), getInt("parms", "gain"), getInt("parms",
                        "offset")))
    {
        emit Abort();
        return;
    }
    emit RequestExposureDone();
}
void Guider::SMComputeFirst()
{
    //BOOST_LOG_TRIVIAL(debug) << "SMComputeFirst";
    _trigFirst.clear();
    buildIndexes(_solver, _trigFirst);
    //BOOST_LOG_TRIVIAL(debug) << "******************************************************************** ";
    //BOOST_LOG_TRIVIAL(debug) << "******************************************************************** ";
    //BOOST_LOG_TRIVIAL(debug) << "************ Guider module - initialization : ";
    //BOOST_LOG_TRIVIAL(debug) << "*********************** mount pointing west true/false  " << _mountPointingWest;
    //BOOST_LOG_TRIVIAL(debug) << "*********************** actual RA  " << _mountRA;
    //BOOST_LOG_TRIVIAL(debug) << "*********************** actual DEC " << _mountDEC;
    //BOOST_LOG_TRIVIAL(debug) << "******************************************************************** ";
    //BOOST_LOG_TRIVIAL(debug) << "******************************************************************** ";


    emit ComputeFirstDone();
}
void Guider::SMComputeCal()
{
    //qDebug()  << "SMComputeCal" << _calStep << _calState;
    buildIndexes(_solver, _trigCurrent);
    _ccdOrientation = 0;

    double coeff[2];
    Q_UNUSED(coeff);
    if (_trigCurrent.size() > 0)
    {
        matchIndexes(_trigPrev, _trigCurrent, _matchedCurPrev, _dxPrev, _dyPrev);
        matchIndexes(_trigFirst, _trigCurrent, _matchedCurFirst, _dxFirst, _dyFirst);
        //_grid->append(_dxFirst,_dyFirst);
        //_propertyStore.update(_grid);
        //emit propertyAppended(_grid,&_modulename,0,_dxFirst,_dyFirst,0,0);
        //BOOST_LOG_TRIVIAL(debug) << "DX DY // first =  " << _dxFirst << "-" << _dyFirst;
        _dxvector.push_back(_dxPrev);
        _dyvector.push_back(_dyPrev);
        /*if (_dxvector.size() > 1)
        {
            polynomialfit(_dxvector.size(), 2, _dxvector.data(), _dyvector.data(), coeff);
            BOOST_LOG_TRIVIAL(debug) << "Coeffs " << coeff[0] << "-" <<  coeff[1] << " CCD Orientation = " << atan(coeff[1])*180/PI;
        }*/


    }
    else
    {
        qDebug() << "houston, we have a problem";
    }
    //BOOST_LOG_TRIVIAL(debug) << "Drifts // prev " << sqrt(square(_dxPrev) + square(_dyPrev));
    _trigPrev = _trigCurrent;

    /*if (_calState==0) {
        BOOST_LOG_TRIVIAL(debug) << "RA drift " << sqrt(square(_avdx)+square(_avdy)) << " drift / ms = " << 1000*sqrt(square(_avdx)+square(_avdy))/_pulseWTot;
    }
    if (_calState==2) {
        BOOST_LOG_TRIVIAL(debug) << "DEC drift " << sqrt(square(_avdx)+square(_avdy)) << " drift / ms = " << 1000*sqrt(square(_avdx)+square(_avdy))/_pulseNTot;
    }*/
    _pulseN = 0;
    _pulseS = 0;
    _pulseE = 0;
    _pulseW = 0;
    _calStep++;
    if (_calStep >= getInt("calParams", "calsteps") )
    {
        double ddx = 0;
        double ddy = 0;
        for (unsigned int i = 1; i < _dxvector.size(); i++)
        {
            ddx = ddx + _dxvector[i];
            ddy = ddy + _dyvector[i];
        }
        ddx = ddx / (_dxvector.size());
        ddy = ddy / (_dyvector.size());
        double a = atan(ddy / ddx);
        //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " DX drift " <<  ddx;
        //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " DY drift " <<  ddy;
        //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " Total drift " <<  sqrt(square(ddy) + square(
        //ddy));
        if (_calState == 0)
        {
            _calPulseW = getInt("calParams", "pulse") / sqrt(square(ddx) + square(ddy));
            _ccdOrientation = a;
            _calMountPointingWest = _mountPointingWest;
            _calCcdOrientation = _ccdOrientation;

            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " Drift orientation =  " << a * 180 / PI;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " W drift (px) " <<  sqrt(square(ddy) + square(
            //                             ddy));
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " W drift ms/px " << _calPulseW;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " W drift ms/'' " << (_pulse) / (sqrt(square(
            //ddy) + square(ddy))*_ccdSampling);
        }
        if (_calState == 1)
        {
            _calPulseE = getInt("calParams", "pulse") / sqrt(square(ddx) + square(ddy));
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " Drift orientation =  " << a * 180 / PI;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " E drift (px) " <<  sqrt(square(ddy) + square(
            //                             ddy));
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " E drift ms/px " << _calPulseE;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " E drift ms/'' " << (_pulse) / (sqrt(square(
            //ddy) + square(ddy))*_ccdSampling);
        }
        if (_calState == 2)
        {
            _calPulseN = getInt("calParams", "pulse") / sqrt(square(ddx) + square(ddy));
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " Drift orientation =  " << a * 180 / PI;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " N drift (px) " <<  sqrt(square(ddy) + square(
            //                             ddy));
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " N drift ms/px " << _calPulseN;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " N drift ms/'' " << (_pulse) / (sqrt(square(
            //ddy) + square(ddy))*_ccdSampling);
        }
        if (_calState == 3)
        {
            _calPulseS = getInt("calParams", "pulse") / sqrt(square(ddx) + square(ddy));
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " Drift orientation =  " << a * 180 / PI;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " S drift (px) " <<  sqrt(square(ddy) + square(
            //                             ddy));
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " S drift ms/px " << _calPulseS;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " S drift ms/'' " << (_pulse) / (sqrt(square(
            //ddy) + square(ddy))*_ccdSampling);
        }

        _calStep = 0;
        _calState++;
        //if (_calState==2) {
        _dxvector.clear();
        _dyvector.clear();
        _coefficients.clear();
        //}
        if (_calState >= 4)
        {
            //BOOST_LOG_TRIVIAL(debug) << "*********************** cal W " << _calPulseW;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** cal E " << _calPulseE;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** cal N " << _calPulseN;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** cal S " << _calPulseS;
            getEltInt("calibrationvalues", "calPulseN")->setValue(_calPulseN);
            getEltInt("calibrationvalues", "calPulseS")->setValue(_calPulseS);
            getEltInt("calibrationvalues", "calPulseE")->setValue(_calPulseE);
            getEltInt("calibrationvalues", "calPulseW")->setValue(_calPulseW, true);
            emit CalibrationDone();
            _trigFirst = _trigCurrent;
            return;
        }
    }
    if (_calState == 0)
    {
        _pulseW = getInt("calParams", "pulse");
    }
    if (_calState == 1)
    {
        _pulseE = getInt("calParams", "pulse");
    }
    if (_calState == 2)
    {
        _pulseN = getInt("calParams", "pulse");
    }
    if (_calState == 3)
    {
        _pulseS = getInt("calParams", "pulse");
    }
    double _driftRA =  _dxFirst * cos(_calCcdOrientation) + _dyFirst * sin(_calCcdOrientation);
    double _driftDE =  _dxFirst * sin(_calCcdOrientation) + _dyFirst * cos(_calCcdOrientation);
    getEltFloat("drift", "RA")->setValue(_driftRA);
    getEltFloat("drift", "DEC")->setValue(_driftDE);
    getProperty("drift")->push();


    emit ComputeCalDone();
}
void Guider::SMComputeGuide()
{
    //_states->addLight(new LightValue("idle"  ,"Idle","hint",0));
    //_states->addLight(new LightValue("cal"   ,"Calibrating","hint",1));
    //_states->addLight(new LightValue("guide" ,"Guiding","hint",2));
    //_states->addLight(new LightValue("error" ,"Error","hint",0));
    //emit propertyUpdated(_states,&_modulename);
    //_propertyStore.update(_states);

    //BOOST_LOG_TRIVIAL(debug) << "SMComputeGuide " << _solver.stars.size();
    _pulseW = 0;
    _pulseE = 0;
    _pulseN = 0;
    _pulseS = 0;
    buildIndexes(_solver, _trigCurrent);

    //BOOST_LOG_TRIVIAL(debug) << "Trig current size " << _trigCurrent.size();
    if (_trigCurrent.size() > 0)
    {
        matchIndexes(_trigFirst, _trigCurrent, _matchedCurFirst, _dxFirst, _dyFirst);
        //_grid->append(_dxFirst,_dyFirst);
        //_propertyStore.update(_grid);
        //emit propertyAppended(_grid,&_modulename,0,_dxFirst,_dyFirst,0,0);
    }
    double _driftRA =  _dxFirst * cos(_calCcdOrientation) + _dyFirst * sin(_calCcdOrientation);
    double _driftDE = -_dxFirst * sin(_calCcdOrientation) + _dyFirst * cos(_calCcdOrientation);
    //BOOST_LOG_TRIVIAL(debug) << "*********************** guide  RA drift (px) " << _driftRA;
    //BOOST_LOG_TRIVIAL(debug) << "*********************** guide  DE drift (px) " << _driftDE;
    int  revRA = 1;
    if (getBool("revCorrections", "revRA")) revRA = -1;
    int  revDE = 1;
    if (getBool("revCorrections", "revDE")) revDE = -1;
    bool disRAO = getBool("disCorrections", "disRA+");
    bool disRAE = getBool("disCorrections", "disRA-");
    bool disDEN = getBool("disCorrections", "disDE+");
    bool disDES = getBool("disCorrections", "disDE-");

    if (revRA * _driftRA > 0 && !disRAO)
    {
        _pulseW = getFloat("guideParams", "raAgr") * revRA * _driftRA * _calPulseW;
        if (_pulseW > getInt("guideParams", "pulsemax")) _pulseW = getInt("guideParams", "pulsemax");
        if (_pulseW < getInt("guideParams", "pulsemin")) _pulseW = 0;
    }
    else _pulseW = 0;
    //if (_pulseW > 0) sendMessage("*********************** guide  W pulse " + QString::number(_pulseW));

    if (revRA * _driftRA < 0 && !disRAE)
    {
        _pulseE = - getFloat("guideParams", "raAgr")  * revRA * _driftRA * _calPulseE;
        if (_pulseE > getInt("guideParams", "pulsemax")) _pulseE = getInt("guideParams", "pulsemax");
        if (_pulseE < getInt("guideParams", "pulsemin")) _pulseE = 0;
    }
    else _pulseE = 0;
    //if (_pulseE > 0) sendMessage("*********************** guide  E pulse " + QString::number(_pulseE));

    if (revDE * _driftDE > 0 && !disDEN)
    {
        _pulseS = getFloat("guideParams", "deAgr")  * revDE * _driftDE * _calPulseS;
        if (_pulseS > getInt("guideParams", "pulsemax")) _pulseS = getInt("guideParams", "pulsemax");
        if (_pulseS < getInt("guideParams", "pulsemin")) _pulseS = 0;
    }
    else _pulseS = 0;
    //if (_pulseS > 0) sendMessage("*********************** guide  S pulse " + QString::number(_pulseS));

    if (revDE * _driftDE < 0 && !disDES)
    {
        _pulseN = -getFloat("guideParams", "deAgr") * revDE * _driftDE * _calPulseN;
        if (_pulseN > getInt("guideParams", "pulsemax")) _pulseN = getInt("guideParams", "pulsemax");
        if (_pulseN < getInt("guideParams", "pulsemin")) _pulseN = 0;
    }
    else _pulseN = 0;
    //if (_pulseN > 0) sendMessage("*********************** guide  N pulse " + QString::number(_pulseN));

    _itt++;

    getEltInt("values", "pulseN")->setValue(_pulseN);
    getEltInt("values", "pulseS")->setValue(_pulseS);
    getEltInt("values", "pulseE")->setValue(_pulseE);
    getEltInt("values", "pulseW")->setValue(_pulseW, true);
    getEltFloat("drift", "RA")->setValue(_driftRA);
    getEltFloat("drift", "DEC")->setValue(_driftDE, true);
    getProperty("drift")->push();

    //setOstElementValue("guiding", "time", QDateTime::currentDateTime().toString("dd/MM/yyyy hh:mm:ss zzz"), false);
    double tt = QDateTime::currentDateTime().toMSecsSinceEpoch();
    getEltFloat("guiding", "time")->setValue(tt);
    getEltFloat("guiding", "RA")->setValue(_driftRA);
    getEltFloat("guiding", "DE")->setValue(_driftDE);
    getEltFloat("guiding", "pDE")->setValue(_pulseN - _pulseS);
    getEltFloat("guiding", "pRA")->setValue( _pulseE - _pulseW);
    getProperty("guiding")->push();

    //setOstElementValue("snr", "time", QDateTime::currentDateTime().toString("dd/MM/yyyy hh:mm:ss zzz"), false);
    getEltFloat("snr", "time")->setValue(tt);
    getEltFloat("snr", "snr")->setValue(_image->getStats().SNR);
    getProperty("snr")->push();

    emit ComputeGuideDone();
}
void Guider::SMRequestPulses()
{

    //sendMessage("SMRequestPulses");
    INDI::BaseDevice dp = getDevice(getString("devices", "guider").toStdString().c_str());

    if (_pulseN > 0)
    {
        //qDebug() << "********* Pulse  N " << _pulseN;
        _pulseDECfinished = false;
        INDI::PropertyNumber prop = dp.getNumber("TELESCOPE_TIMED_GUIDE_NS");
        for (std::size_t i = 0; i < prop.size(); i++)
        {
            if (strcmp(prop[i].name, "TIMED_GUIDE_N") == 0)
            {
                prop[i].value = _pulseN;
            }
            else prop[i].value = 0;
        }
        sendNewNumber(prop);
    }

    if (_pulseS > 0)
    {
        _pulseDECfinished = false;
        //qDebug()  << "********* Pulse  S " << _pulseS;
        INDI::PropertyNumber prop = dp.getNumber("TELESCOPE_TIMED_GUIDE_NS");
        for (std::size_t i = 0; i < prop.size(); i++)
        {
            if (strcmp(prop[i].name, "TIMED_GUIDE_S") == 0)
            {
                prop[i].value = _pulseS;
            }
            else prop[i].value = 0;
        }
        sendNewNumber(prop);
    }

    if (_pulseE > 0)
    {
        _pulseRAfinished = false;
        //qDebug()  << "********* Pulse  E " << _pulseE;
        INDI::PropertyNumber prop = dp.getNumber("TELESCOPE_TIMED_GUIDE_WE");
        for (std::size_t i = 0; i < prop.size(); i++)
        {
            if (strcmp(prop[i].name, "TIMED_GUIDE_E") == 0)
            {
                prop[i].value = _pulseE;
            }
            else prop[i].value = 0;
        }
        sendNewNumber(prop);
    }

    if (_pulseW > 0)
    {
        _pulseRAfinished = false;
        //qDebug()  << "********* Pulse  W " << _pulseW;
        INDI::PropertyNumber prop = dp.getNumber("TELESCOPE_TIMED_GUIDE_WE");
        for (std::size_t i = 0; i < prop.size(); i++)
        {
            if (strcmp(prop[i].name, "TIMED_GUIDE_W") == 0)
            {
                prop[i].value = _pulseW;
            }
            else prop[i].value = 0;
        }
        sendNewNumber(prop);
    }

    //BOOST_LOG_TRIVIAL(debug) << "SMRequestPulses before";
    emit RequestPulsesDone();
    //BOOST_LOG_TRIVIAL(debug) << "SMRequestPulses after";

    if ((_pulseN == 0) && (_pulseS == 0) && (_pulseE == 0) && (_pulseW == 0))
    {
        //BOOST_LOG_TRIVIAL(debug) << "SMRequestPulses zéro";
        emit PulsesDone();
    }

}

void Guider::SMFindStars()
{
    //BOOST_LOG_TRIVIAL(debug) << "SMFindStars";

    //sendMessage("SMFindStars");
    stats = _image->getStats();
    _solver.ResetSolver(stats, _image->getImageBuffer());
    connect(&_solver, &Solver::successSEP, this, &Guider::OnSucessSEP);
    _solver.stars.clear();
    _solver.FindStars(_solver.stellarSolverProfiles[0]);
}

void Guider::OnSucessSEP()
{
    //BOOST_LOG_TRIVIAL(debug) << "OnSucessSEP";
    OST::ImgData dta = getEltImg("image", "image")->value();
    dta.HFRavg = _solver.HFRavg;
    dta.starsCount = _solver.stars.size();
    getEltImg("image", "image")->setValue(dta, true);

    //sendMessage("SEP finished");
    disconnect(&_solver, &Solver::successSEP, this, &Guider::OnSucessSEP);
    //BOOST_LOG_TRIVIAL(debug) << "********* SEP Finished";
    emit FindStarsDone();
}

void Guider::SMAbort()
{

    disconnect(&_SMInit,        &QStateMachine::finished, nullptr, nullptr);
    disconnect(&_SMCalibration, &QStateMachine::finished, nullptr, nullptr);
    _SMInit.stop();
    _SMCalibration.stop();
    _SMGuide.stop();

    emit AbortDone();

    //_states->addLight(new LightValue("idle"  ,"Idle","hint",1));
    //_states->addLight(new LightValue("cal"   ,"Calibrating","hint",0));
    //_states->addLight(new LightValue("guide" ,"Guiding","hint",0));
    //_states->addLight(new LightValue("error" ,"Error","hint",0));
    //emit propertyUpdated(_states,&_modulename);
    //_propertyStore.update(_states);


}

void Guider::matchIndexes(QVector<Trig> ref, QVector<Trig> act, QVector<MatchedPair> &pairs, double &dx, double &dy)
{
    pairs.clear();

    foreach (Trig r, ref)
    {
        foreach (Trig a, act)
        {
            if (
                (r.s < a.s * 1.001 ) && (r.s > a.s * 0.999 ) && (r.p < a.p * 1.001 ) && (r.p > a.p * 0.999 )
                && (r.d12 < a.d12 * 1.001) && (r.d12 > a.d12 * 0.999)
                && (r.d13 < a.d13 * 1.001) && (r.d13 > a.d13 * 0.999)
                && (r.d23 < a.d23 * 1.001) && (r.d23 > a.d23 * 0.999)
            )
            {
                //BOOST_LOG_TRIVIAL(debug) << "Matching " << r.ratio <<  " / " << a.ratio << " xr-yr=" << r.x1 << "-" << r.y1 << " xa-ya=" << a.x1 << "-" << a.y1 << " / dx1 =" << r.x1-a.x1 << " / dy1 =" << r.y1-a.y1 << " / dx2 =" << r.x2-a.x2 << " / dy2 =" << r.y2-a.y2 << " / dx3 =" << r.x3-a.x3 << " / dy3 =" << r.y3-a.y3;
                //BOOST_LOG_TRIVIAL(debug) << "Matching " << r.x1 << " - " << r.d12 << " / " << a.d12 << " - " << r.d13 << " / " << a.d13 << " - " << r.d23 << " / " << a.d23;
                //BOOST_LOG_TRIVIAL(debug) << "Matching dxy1= " << r.x1-a.x1 << "/" << r.y1-a.y1 << " - dxy2="  << r.x2-a.x2 << "/" << r.y2-a.y2 << " - dxy3="  << r.x3-a.x3 << "/" << r.y3-a.y3;
                bool found;
                found = false;
                foreach (MatchedPair pair, pairs)
                {
                    if ( (pair.xr == r.x1) && (pair.yr == r.y1) ) found = true;
                }
                if (!found) pairs.append({r.x1, r.y1, a.x1, a.y1, r.x1 - a.x1, r.y1 - a.y1});
                found = false;
                foreach (MatchedPair pair, pairs)
                {
                    if ( (pair.xr == r.x2) && (pair.yr == r.y2) ) found = true;
                }
                if (!found) pairs.append({r.x2, r.y2, a.x2, a.y2, r.x2 - a.x2, r.y2 - a.y2});
                found = false;
                foreach (MatchedPair pair, pairs)
                {
                    if ( (pair.xr == r.x3) && (pair.yr == r.y3) ) found = true;
                }
                if (!found) pairs.append({r.x3, r.y3, a.x3, a.y3, r.x3 - a.x3, r.y3 - a.y3});
            }
        }
    }
    dx = 0;
    dy = 0;
    for (int i = 0 ; i < pairs.size(); i++ )
    {
        dx = dx + pairs[i].dx;
        dy = dy + pairs[i].dy;
    }
    dx = dx / pairs.size();
    dy = dy / pairs.size();



    /*foreach (MatchedPair pair, _matchedPairs) {
            BOOST_LOG_TRIVIAL(debug) << "Matched pair =  " << pair.dx << "-" << pair.dy;
    }*/

}
void Guider::buildIndexes(Solver &solver, QVector<Trig> &trig)
{
    int nb = solver.stars.size();
    if (nb > 10) nb = 10;
    trig.clear();

    for (int i = 0; i < nb; i++)
    {
        for (int j = i + 1; j < nb; j++)
        {
            for (int k = j + 1; k < nb; k++)
            {
                double dij, dik, djk, p, s;
                dij = sqrt(square(solver.stars[i].x - solver.stars[j].x) + square(solver.stars[i].y - solver.stars[j].y));
                dik = sqrt(square(solver.stars[i].x - solver.stars[k].x) + square(solver.stars[i].y - solver.stars[k].y));
                djk = sqrt(square(solver.stars[j].x - solver.stars[k].x) + square(solver.stars[j].y - solver.stars[k].y));
                p = dij + dik + djk;
                s = sqrt(p * (p - dij) * (p - dik) * (p - djk));
                //BOOST_LOG_TRIVIAL(debug) << "Trig CURRENT" << " - " << i << " - " << j << " - " << k << " - p=  " << p << " - s=" << s << " - s/p=" << s/p;
                trig.append(
                {
                    solver.stars[i].x,
                    solver.stars[i].y,
                    solver.stars[j].x,
                    solver.stars[j].y,
                    solver.stars[k].x,
                    solver.stars[k].y,
                    dij, dik, djk,
                    p, s, s / p
                });

            }
        }

    }

}
