#include "guider.h"
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

    createOstElementText("devices", "camera", "Camera", true);
    createOstElementText("devices", "mount", "Mount", true);
    setOstElementValue("devices", "camera",   _camera, false);
    setOstElementValue("devices", "mount",    _mount, true);


    //    _grid = new GridProperty(_modulename,"Control","root","grid","Grid property label",0,0,"PXY","Set","DX","DY","","");
    //    emit propertyCreated(_grid,&_modulename);
    //    _propertyStore.add(_grid);
    //
    //    _states = new LightProperty(_modulename,"Control","root","states","State",0,0);
    //    _states->addLight(new LightValue("idle"  ,"Idle","hint",1));
    //    _states->addLight(new LightValue("cal"   ,"Calibrating","hint",0));
    //    _states->addLight(new LightValue("guide" ,"Guiding","hint",0));
    //    _states->addLight(new LightValue("error" ,"Error","hint",0));
    //    emit propertyCreated(_states,&_modulename);
    //    _propertyStore.add(_states);

    buildInitStateMachines();
    buildCalStateMachines();
    buildGuideStateMachines();



}

Guider::~Guider()
{

}
void Guider::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                               const QVariantMap &eventData)
{
    Q_UNUSED(eventType);
    Q_UNUSED(eventKey);

    //BOOST_LOG_TRIVIAL(debug) << "OnMyExternalEvent - recv : " << getName().toStdString() << "-" << eventType.toStdString() << "-" << eventKey.toStdString();
    if (getModuleName() == eventModule)
    {
        foreach(const QString &keyprop, eventData.keys())
        {
            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                QVariant val = eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"];
                if (keyprop == "commonParams")
                {
                    if (keyelt == "exposure")
                    {
                        if (setOstElementValue(keyprop, keyelt, val, true))
                        {
                            _exposure = val.toDouble();
                        }
                    }
                }
                if (keyprop == "calParams")
                {
                    if (keyelt == "pulse")
                    {
                        if (setOstElementValue(keyprop, keyelt, val, true))
                        {
                            _pulse = val.toInt();
                        }
                    }
                    if (keyelt == "calsteps")
                    {
                        if (setOstElementValue(keyprop, keyelt, val, true))
                        {
                            _calSteps = val.toInt();
                        }
                    }
                }
                if (keyprop == "guideParams")
                {
                    if (keyelt == "pulsemax")
                    {
                        if (setOstElementValue(keyprop, keyelt, val, true))
                        {
                            _pulseMax = val.toInt();
                        }
                    }
                    if (keyelt == "pulsemin")
                    {
                        if (setOstElementValue(keyprop, keyelt, val, true))
                        {
                            _pulseMin = val.toInt();
                        }
                    }
                    if (keyelt == "raAgr")
                    {
                        if (setOstElementValue(keyprop, keyelt, val, true))
                        {
                            _raAgr = val.toDouble();
                        }
                    }
                    if (keyelt == "deAgr")
                    {
                        if (setOstElementValue(keyprop, keyelt, val, true))
                        {
                            _deAgr = val.toDouble();
                        }
                    }
                }
                if (keyprop == "revCorrections"  && (keyelt == "revRA" || keyelt == "revDE" ))
                {
                    setOstElementValue(keyprop, keyelt, val, true);
                }
                if (keyprop == "disCorrections"  && (keyelt == "disRA+" || keyelt == "disRA-" || keyelt == "disDE+" || keyelt == "disDE-"))
                {
                    setOstElementValue(keyprop, keyelt, val, true);
                }
                if (keyprop == "actions")
                {
                    if (keyelt == "calguide")
                    {
                        if (setOstElementValue(keyprop, keyelt, false, false))
                        {
                            getProperty(keyprop)->setState(OST::Busy);

                            disconnect(&_SMInit,        &QStateMachine::finished, nullptr, nullptr);
                            disconnect(&_SMCalibration, &QStateMachine::finished, nullptr, nullptr);
                            connect(&_SMInit,           &QStateMachine::finished, &_SMCalibration, &QStateMachine::start) ;
                            connect(&_SMCalibration,    &QStateMachine::finished, &_SMGuide,      &QStateMachine::start) ;
                            _SMInit.start();
                        }
                    }
                    if (keyelt == "abort")
                    {
                        if (setOstElementValue(keyprop, keyelt, false, false))
                        {
                            getProperty(keyprop)->setState(OST::Ok);

                            emit Abort();
                        }
                    }
                    if (keyelt == "calibration")
                    {
                        if (setOstElementValue(keyprop, keyelt, false, false))
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
                        if (setOstElementValue(keyprop, keyelt, false, false))
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
        (property.getDeviceName() == getString("devices", "mount")) &&
        (QString(property.getName())   == "TELESCOPE_TIMED_GUIDE_NS") &&
        (property.getState()  == IPS_IDLE)

    )
    {
        _pulseDECfinished = true;
    }

    if (
        (property.getDeviceName() == getString("devices", "mount")) &&
        (QString(property.getName())  == "TELESCOPE_TIMED_GUIDE_WE") &&
        (property.getState()  == IPS_IDLE)

    )
    {
        _pulseRAfinished = true;
    }

    if (
        (property.getDeviceName() == getString("devices", "mount")) &&
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
        _image->loadBlob(pblob);
        stats = _image->getStats();
        QImage rawImage = _image->getRawQImage();
        QImage im = rawImage.convertToFormat(QImage::Format_RGB32);
        im.setColorTable(rawImage.colorTable());

        im.save(getWebroot() + "/" + getModuleName() + ".jpeg", "JPG", 100);
        OST::ImgData dta;
        dta.mUrlJpeg = getModuleName() + ".jpeg";
        getValueImg("image", "image1")->setValue(dta, true);


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
        connectDevice(getString("devices", "mount"));
        setBLOBMode(B_ALSO, getString("devices", "camera").toStdString().c_str(), nullptr);
        enableDirectBlobAccess(getString("devices", "camera").toStdString().c_str(), nullptr);
        frameReset(getString("devices", "camera"));
        sendModNewNumber(getString("devices", "camera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 1 );
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
    if (!getModNumber(getString("devices", "mount"), "EQUATORIAL_EOD_COORD", "DEC",
                      _mountDEC))
    {
        emit Abort();
        return;
    }
    /* get mount RA */
    if (!getModNumber(getString("devices", "mount"), "EQUATORIAL_EOD_COORD", "RA",
                      _mountRA))
    {
        emit Abort();
        return;
    }
    /* get mount Pier position  */
    if (!getModSwitch(getString("devices", "mount"), "TELESCOPE_PIER_SIDE", "PIER_WEST",
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
    setOstElementValue("values", "calPulseN", _calPulseN, false);
    setOstElementValue("values", "calPulseS", _calPulseS, false);
    setOstElementValue("values", "calPulseE", _calPulseE, false);
    setOstElementValue("values", "calPulseW", _calPulseW, true);

    _pulseN = 0;
    _pulseS = 0;
    _pulseE = 0;
    _pulseW = _pulse;
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
    if (!sendModNewNumber(getString("devices", "camera"), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", _exposure))
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
    //BOOST_LOG_TRIVIAL(debug) << "SMComputeCal";
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
        //BOOST_LOG_TRIVIAL(debug) << "houston, we have a problem";
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
    if (_calStep >= _calSteps)
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
            _calPulseW = _pulse / sqrt(square(ddy) + square(ddy));
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
            _calPulseE = _pulse / sqrt(square(ddy) + square(ddy));
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " Drift orientation =  " << a * 180 / PI;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " E drift (px) " <<  sqrt(square(ddy) + square(
            //                             ddy));
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " E drift ms/px " << _calPulseE;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " E drift ms/'' " << (_pulse) / (sqrt(square(
            //ddy) + square(ddy))*_ccdSampling);
        }
        if (_calState == 2)
        {
            _calPulseN = _pulse / sqrt(square(ddy) + square(ddy));
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " Drift orientation =  " << a * 180 / PI;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " N drift (px) " <<  sqrt(square(ddy) + square(
            //                             ddy));
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " N drift ms/px " << _calPulseN;
            //BOOST_LOG_TRIVIAL(debug) << "*********************** step " << _calState << " N drift ms/'' " << (_pulse) / (sqrt(square(
            //ddy) + square(ddy))*_ccdSampling);
        }
        if (_calState == 3)
        {
            _calPulseS = _pulse / sqrt(square(ddy) + square(ddy));
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
            setOstElementValue("values", "calPulseN", _calPulseN, false);
            setOstElementValue("values", "calPulseS", _calPulseS, false);
            setOstElementValue("values", "calPulseE", _calPulseE, false);
            setOstElementValue("values", "calPulseW", _calPulseW, true);
            emit CalibrationDone();
            _trigFirst = _trigCurrent;
            return;
        }
    }
    if (_calState == 0)
    {
        _pulseW = _pulse;
    }
    if (_calState == 1)
    {
        _pulseE = _pulse;
    }
    if (_calState == 2)
    {
        _pulseN = _pulse;
    }
    if (_calState == 3)
    {
        _pulseS = _pulse;
    }
    double _driftRA =  _dxFirst * cos(_calCcdOrientation) + _dyFirst * sin(_calCcdOrientation);
    double _driftDE = -_dxFirst * sin(_calCcdOrientation) + _dyFirst * cos(_calCcdOrientation);
    setOstElementValue("drift", "RA", _driftRA, false);
    setOstElementValue("drift", "DEC", _driftDE, false);
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
        _pulseW = _raAgr * revRA * _driftRA * _calPulseW;
        if (_pulseW > _pulseMax) _pulseW = _pulseMax;
        if (_pulseW < _pulseMin) _pulseW = 0;
    }
    else _pulseW = 0;
    //if (_pulseW > 0) sendMessage("*********************** guide  W pulse " + QString::number(_pulseW));

    if (revRA * _driftRA < 0 && !disRAE)
    {
        _pulseE = -_raAgr * revRA * _driftRA * _calPulseE;
        if (_pulseE > _pulseMax) _pulseE = _pulseMax;
        if (_pulseE < _pulseMin) _pulseE = 0;
    }
    else _pulseE = 0;
    //if (_pulseE > 0) sendMessage("*********************** guide  E pulse " + QString::number(_pulseE));

    if (revDE * _driftDE > 0 && !disDEN)
    {
        _pulseS = _deAgr * revDE * _driftDE * _calPulseS;
        if (_pulseS > _pulseMax) _pulseS = _pulseMax;
        if (_pulseS < _pulseMin) _pulseS = 0;
    }
    else _pulseS = 0;
    //if (_pulseS > 0) sendMessage("*********************** guide  S pulse " + QString::number(_pulseS));

    if (revDE * _driftDE < 0 && !disDES)
    {
        _pulseN = -_deAgr * revDE * _driftDE * _calPulseN;
        if (_pulseN > _pulseMax) _pulseN = _pulseMax;
        if (_pulseN < _pulseMin) _pulseN = 0;
    }
    else _pulseN = 0;
    //if (_pulseN > 0) sendMessage("*********************** guide  N pulse " + QString::number(_pulseN));

    _itt++;

    setOstElementValue("values", "pulseN", _pulseN, false);
    setOstElementValue("values", "pulseS", _pulseS, false);
    setOstElementValue("values", "pulseE", _pulseE, false);
    setOstElementValue("values", "pulseW", _pulseW, true);
    setOstElementValue("drift", "RA", _driftRA, false);
    setOstElementValue("drift", "DEC", _driftDE, false);
    getProperty("drift")->push();

    //setOstElementValue("guiding", "time", QDateTime::currentDateTime().toString("dd/MM/yyyy hh:mm:ss zzz"), false);
    double tt = QDateTime::currentDateTime().toMSecsSinceEpoch();
    setOstElementValue("guiding", "time", tt, false);
    setOstElementValue("guiding", "RA", _driftRA, false);
    setOstElementValue("guiding", "DE", _driftDE, false);
    setOstElementValue("guiding", "pDE", _pulseN - _pulseS, false);
    setOstElementValue("guiding", "pRA", _pulseE - _pulseW, false);
    getProperty("guiding")->push();

    //setOstElementValue("snr", "time", QDateTime::currentDateTime().toString("dd/MM/yyyy hh:mm:ss zzz"), false);
    setOstElementValue("snr", "time", tt, false);
    setOstElementValue("snr", "snr", _image->getStats().SNR, false);
    getProperty("snr")->push();

    emit ComputeGuideDone();
}
void Guider::SMRequestPulses()
{

    //sendMessage("SMRequestPulses");

    if (_pulseN > 0)
    {
        //BOOST_LOG_TRIVIAL(debug) << "********* Pulse  N " << _pulseN;
        _pulseDECfinished = false;
        if (!sendModNewNumber(getString("devices", "mount"), "TELESCOPE_TIMED_GUIDE_NS", "TIMED_GUIDE_N",
                              _pulseN))
        {
            emit abort();
            return;
        }
    }

    if (_pulseS > 0)
    {
        _pulseDECfinished = false;
        //BOOST_LOG_TRIVIAL(debug) << "********* Pulse  S " << _pulseS;
        if (!sendModNewNumber(getString("devices", "mount"), "TELESCOPE_TIMED_GUIDE_NS", "TIMED_GUIDE_S",
                              _pulseS))
        {
            emit abort();
            return;
        }
    }

    if (_pulseE > 0)
    {
        _pulseRAfinished = false;
        //BOOST_LOG_TRIVIAL(debug) << "********* Pulse  E " << _pulseE;
        if (!sendModNewNumber(getString("devices", "mount"), "TELESCOPE_TIMED_GUIDE_WE", "TIMED_GUIDE_E",
                              _pulseE))
        {
            emit abort();
            return;
        }
    }

    if (_pulseW > 0)
    {
        _pulseRAfinished = false;
        //BOOST_LOG_TRIVIAL(debug) << "********* Pulse  W " << _pulseW;
        if (!sendModNewNumber(getString("devices", "mount"), "TELESCOPE_TIMED_GUIDE_WE", "TIMED_GUIDE_W",
                              _pulseW))
        {
            emit abort();
            return;
        }
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
