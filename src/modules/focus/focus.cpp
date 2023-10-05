#include "focus.h"
#include "polynomialfit.h"

FocusModule *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    FocusModule *basemodule = new FocusModule(name, label, profile, availableModuleLibs);
    return basemodule;
}

FocusModule::FocusModule(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
    : IndiModule(name, label, profile, availableModuleLibs)

{
    Q_INIT_RESOURCE(focus);

    loadOstPropertiesFromFile(":focus.json");
    setClassName(QString(metaObject()->className()).toLower());

    //setModuleLabel(label);
    setModuleDescription("Focus module with statemachines");
    setModuleVersion("0.1");
    //setModuleType("focus");

    _startpos =          getValueInt("parameters", "startpos")->value();
    _steps =             getValueInt("parameters", "steps")->value();
    _iterations =        getValueInt("parameters", "iterations")->value();
    _loopIterations =    getValueInt("parameters", "loopIterations")->value();
    _backlash =          getValueInt("parameters", "backlash")->value();

    defineMeAsFocuser();
    giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
    giveMeADevice("focuser", "Focuser", INDI::BaseDevice::FOCUSER_INTERFACE);
    giveMeADevice("filter", "Filter wheel", INDI::BaseDevice::FILTER_INTERFACE);

}

FocusModule::~FocusModule()
{

}
void FocusModule::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
                                    const QVariantMap &eventData)
{
    Q_UNUSED(eventType);
    Q_UNUSED(eventKey);

    if (getModuleName() == eventModule)
    {
        foreach(const QString &keyprop, eventData.keys())
        {
            foreach(const QString &keyelt, eventData[keyprop].toMap()["elements"].toMap().keys())
            {
                QVariant val = eventData[keyprop].toMap()["elements"].toMap()[keyelt].toMap()["value"];
                if (keyprop == "actions")
                {
                    if (keyelt == "autofocus")
                    {
                        if (setOstElementValue(keyprop, keyelt, false, false))
                        {
                            getProperty(keyprop)->setState(OST::Busy);
                            startCoarse();
                        }
                    }
                    if (keyelt == "abortfocus")
                    {
                        if (setOstElementValue(keyprop, keyelt, false, false))
                        {
                            getProperty(keyprop)->setState(OST::Ok);
                            emit abort();
                        }
                    }
                    if (keyelt == "loop")
                    {
                        if (setOstElementValue(keyprop, keyelt, false, false))
                        {
                            getProperty(keyprop)->setState(OST::Ok);
                        }
                    }

                }
            }
        }
    }
}

void FocusModule::updateProperty(INDI::Property p)
{
    if (
        (QString(p.getDeviceName()) == getString("devices", "camera") )
        &&  (p.getState() == IPS_ALERT)
    )
    {
        //sendMessage("cameraAlert");
        emit cameraAlert();
    }
    if (
        (QString(p.getDeviceName()) == getString("devices", "focuser"))
        &&  (QString(p.getName())   == "ABS_FOCUS_POSITION")
    )
    {
        INDI::PropertyNumber n = p;
        setOstElementValue("values", "focpos", n[0].value, true);

        if (n.getState() == IPS_OK)
        {
            //sendMessage("focuserReachedPosition");
            emit GotoBestDone();
            emit BacklashBestDone();
            emit BacklashDone();
            emit GotoNextDone();
            emit GotoStartDone();
        }
    }
    if (
        (QString(p.getDeviceName()) == getString("devices", "camera"))
        &&  (QString(p.getName())   == "CCD_FRAME_RESET")
        &&  (p.getState() == IPS_OK)
    )
    {
        INDI::PropertySwitch n = p;
        //sendMessage("FrameResetDone");
        if (_machine.isRunning()) emit FrameResetDone();
    }
    if (QString(p.getName()) == "CCD1")
    {
        newBLOB(p);
    }



}

void FocusModule::newBLOB(INDI::PropertyBlob b)
{
    if
    (
        (QString(b.getDeviceName()) == getString("devices", "camera"))
    )
    {
        delete _image;
        _image = new fileio();
        _image->loadBlob(b, 64);
        //setBLOBMode(B_NEVER, getString("devices","camera").toStdString().c_str(), nullptr);

        getProperty("image")->setState(OST::Ok);

        QImage rawImage = _image->getRawQImage();
        rawImage.save( getWebroot() + "/" + getModuleName() + QString(b.getDeviceName()) + ".jpeg", "JPG", 100);
        OST::ImgData dta = _image->ImgStats();
        dta.mUrlJpeg = getModuleName() + QString(b.getDeviceName()) + ".jpeg";
        dta.mUrlFits = getModuleName() + QString(b.getDeviceName()) + ".FITS";
        getValueImg("image", "image")->setValue(dta, true);

        if (_machine.isRunning())
        {
            emit ExposureDone();
            emit ExposureBestDone();
        }

    }
}

void FocusModule::SMAbort()
{

    _machine.stop();
    //sendMessage("machine stopped");
}

void FocusModule::startCoarse()
{
    getStore()["values"]->clearGrid();
    connectIndi();
    connectDevice(getString("devices", "camera"));
    setBLOBMode(B_ALSO, getString("devices", "camera").toStdString().c_str(), nullptr);
    if (getString("devices", "camera") == "CCD Simulator")
    {
        sendModNewNumber(getString("devices", "camera"), "SIMULATOR_SETTINGS", "SIM_TIME_FACTOR", 0.01 );
    }
    enableDirectBlobAccess(getString("devices", "camera").toStdString().c_str(), nullptr);

    _posvector.clear();
    _hfdvector.clear();
    _coefficients.clear();
    _iteration = 0;
    _besthfr = 99;
    _bestposfit = 99;

    _startpos =          getValueInt("parameters", "startpos")->value();
    _steps =             getValueInt("parameters", "steps")->value();
    _iterations =        getValueInt("parameters", "iterations")->value();
    _loopIterations =    getValueInt("parameters", "loopIterations")->value();
    _backlash =          getValueInt("parameters", "backlash")->value();

    /*_grid->clear();
    _propertyStore.update(_grid);
    emit propertyUpdated(_grid,&_modulename);*/


    //_machine = new QStateMachine();

    /* states definitions */
    auto *CoarseFocus = new QState();
    auto *Abort       = new QState();
    auto *Error       = new QState();
    auto *Init        = new QState(CoarseFocus);
    auto *Loop        = new QState(CoarseFocus);
    auto *Finish      = new QState(CoarseFocus);
    auto *Final       = new QFinalState();

    auto *RequestFrameReset   = new QState(Init);
    auto *WaitFrameReset      = new QState(Init);
    auto *RequestBacklash     = new QState(Init);
    auto *WaitBacklash        = new QState(Init);
    auto *RequestGotoStart    = new QState(Init);
    auto *WaitGotoStart       = new QState(Init);

    auto *Compute             = new QState(Loop);
    auto *RequestGotoNext     = new QState(Loop);
    auto *WaitGotoNext        = new QState(Loop);
    auto *LoopFrame           = new QState(Loop);

    auto *InitLoopFrame     = new QState(LoopFrame);
    auto *RequestExposure   = new QState(LoopFrame);
    auto *WaitExposure      = new QState(LoopFrame);
    auto *FindStars         = new QState(LoopFrame);
    auto *ComputeLoopFrame  = new QState(LoopFrame);



    auto *RequestBacklashBest   = new QState(Finish);
    auto *WaitBacklashBest      = new QState(Finish);
    auto *RequestGotoBest       = new QState(Finish);
    auto *WaitGotoBest          = new QState(Finish);
    auto *RequestExposureBest   = new QState(Finish);
    auto *WaitExposureBest      = new QState(Finish);
    auto *ComputeResult         = new QState(Finish);

    _machine.addState(CoarseFocus);
    _machine.addState(Abort);
    _machine.addState(Error);
    _machine.addState(Final);

    /* Set initial states */
    Init->setInitialState(RequestFrameReset);
    Loop->setInitialState(LoopFrame);
    LoopFrame->setInitialState(InitLoopFrame);
    Finish->setInitialState(RequestBacklashBest);
    CoarseFocus->setInitialState(Init);
    _machine.setInitialState(CoarseFocus);

    /* actions to take when entering into state */
    connect(Abort,              &QState::entered, this, &FocusModule::SMAbort);
    connect(RequestFrameReset,  &QState::entered, this, &FocusModule::SMRequestFrameReset);
    connect(RequestBacklash,    &QState::entered, this, &FocusModule::SMRequestBacklash);
    connect(RequestGotoStart,   &QState::entered, this, &FocusModule::SMRequestGotoStart);
    connect(RequestExposure,    &QState::entered, this, &FocusModule::SMRequestExposure);
    connect(FindStars,          &QState::entered, this, &FocusModule::SMFindStars);
    connect(Compute,            &QState::entered, this, &FocusModule::SMCompute);
    connect(RequestGotoNext,    &QState::entered, this, &FocusModule::SMRequestGotoNext);
    connect(RequestBacklashBest, &QState::entered, this, &FocusModule::SMRequestBacklashBest);
    connect(RequestGotoBest,    &QState::entered, this, &FocusModule::SMRequestGotoBest);
    connect(RequestExposureBest, &QState::entered, this, &FocusModule::SMRequestExposureBest);
    connect(ComputeResult,      &QState::entered, this, &FocusModule::SMComputeResult);
    connect(ComputeLoopFrame,   &QState::entered, this, &FocusModule::SMComputeLoopFrame);
    connect(InitLoopFrame,      &QState::entered, this, &FocusModule::SMInitLoopFrame);
    connect(Final,      &QState::entered, this, &FocusModule::SMFocusDone);

    /* mapping signals to state transitions */
    CoarseFocus->       addTransition(this, &FocusModule::abort,                Abort);
    RequestFrameReset-> addTransition(this, &FocusModule::RequestFrameResetDone, WaitFrameReset);
    WaitFrameReset->    addTransition(this, &FocusModule::FrameResetDone,       RequestBacklash);
    RequestBacklash->   addTransition(this, &FocusModule::RequestBacklashDone,  WaitBacklash);
    WaitBacklash->      addTransition(this, &FocusModule::BacklashDone,         RequestGotoStart);
    RequestGotoStart->  addTransition(this, &FocusModule::RequestGotoStartDone, WaitGotoStart);
    WaitGotoStart->     addTransition(this, &FocusModule::GotoStartDone,        Loop);

    RequestExposure->   addTransition(this, &FocusModule::RequestExposureDone,   WaitExposure);
    WaitExposure->      addTransition(this, &FocusModule::ExposureDone,          FindStars);
    FindStars->         addTransition(this, &FocusModule::FindStarsDone,         ComputeLoopFrame);
    Compute->           addTransition(this, &FocusModule::LoopFinished,          Finish);

    Compute->           addTransition(this, &FocusModule::NextLoop,              RequestGotoNext);
    RequestGotoNext->   addTransition(this, &FocusModule::RequestGotoNextDone,   WaitGotoNext);
    WaitGotoNext->      addTransition(this, &FocusModule::GotoNextDone,          LoopFrame);

    RequestBacklashBest->   addTransition(this, &FocusModule::RequestBacklashBestDone,   WaitBacklashBest);
    WaitBacklashBest->      addTransition(this, &FocusModule::BacklashBestDone,          RequestGotoBest);
    RequestGotoBest->       addTransition(this, &FocusModule::RequestGotoBestDone,       WaitGotoBest);
    WaitGotoBest->          addTransition(this, &FocusModule::GotoBestDone,              RequestExposureBest);
    RequestExposureBest->   addTransition(this, &FocusModule::RequestExposureBestDone,   WaitExposureBest);
    WaitExposureBest->      addTransition(this, &FocusModule::ExposureBestDone,          ComputeResult);
    ComputeResult->         addTransition(this, &FocusModule::ComputeResultDone,         Final);

    InitLoopFrame-> addTransition(this, &FocusModule::InitLoopFrameDone, RequestExposure );
    ComputeLoopFrame->addTransition(this, &FocusModule::NextFrame, RequestExposure);
    ComputeLoopFrame->addTransition(this, &FocusModule::LoopFrameDone, Compute);





    _machine.start();
    //sendMessage("machine started");
    qDebug() << "Start coarse focus";
}

void FocusModule::SMRequestFrameReset()
{
    //sendMessage("SMRequestFrameReset");


    //setBLOBMode(B_ALSO, getString("devices","camera").toStdString().c_str(), nullptr);

    /*qDebug() << "conf count" << _machine.configuration().count();
    QSet<QAbstractState *>::iterator i;
    for (i=_machine.configuration().begin();i !=_machine.configuration().end();i++)
    {
        qDebug() << (*i)->objectName();
    }*/

    if (!frameReset(getString("devices", "camera")))
    {
        RequestFrameResetDone();
        usleep(1000);
        emit FrameResetDone();
        return;
    }
    emit RequestFrameResetDone();
}

void FocusModule::SMRequestBacklash()
{
    //sendMessage("SMRequestBacklash");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION",
                          _startpos - _backlash))
    {
        emit abort();
        return;
    }
    emit RequestBacklashDone();
}

void FocusModule::SMRequestGotoStart()
{
    //sendMessage("SMRequestGotoStart");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION", _startpos))
    {
        emit abort();
        return;
    }
    emit RequestGotoStartDone();
}

void FocusModule::SMRequestExposure()
{
    //sendMessage("SMRequestExposure");
    if (!sendModNewNumber(getString("devices", "camera"), "CCD_GAIN", "GAIN", getValueInt("parms",
                          "gain")->value()))
    {
        emit abort();
        return;
    }
    if (!sendModNewNumber(getString("devices", "camera"), "CCD_OFFSET", "OFFSET", getValueInt("parms",
                          "offset")->value()))
    {
        emit abort();
        return;
    }

    if (!sendModNewNumber(getString("devices", "camera"), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", getValueFloat("parms",
                          "exposure")->value()))
    {
        emit abort();
        return;
    }
    //setBLOBMode(B_ALSO, getString("devices","camera").toStdString().c_str(), nullptr);
    emit RequestExposureDone();

}

void FocusModule::SMFindStars()
{
    //sendMessage("SMFindStars");
    stats = _image->getStats();
    _solver.ResetSolver(stats, _image->getImageBuffer());
    connect(&_solver, &Solver::successSEP, this, &FocusModule::OnSucessSEP);
    _solver.FindStars(_solver.stellarSolverProfiles[0]);
}

void FocusModule::OnSucessSEP()
{
    OST::ImgData dta = getValueImg("image", "image")->value();
    dta.HFRavg = _solver.HFRavg;
    dta.starsCount = _solver.stars.size();
    getValueImg("image", "image")->setValue(dta, true);

    emit FindStarsDone();
}

void FocusModule::SMCompute()
{
    //sendMessage("SMCompute");

    _posvector.push_back(_startpos + _iteration * _steps);
    _hfdvector.push_back(_loopHFRavg);

    if (_posvector.size() > 2)
    {
        double coeff[3];
        polynomialfit(_posvector.size(), 3, _posvector.data(), _hfdvector.data(), coeff);
        _bestposfit = -coeff[1] / (2 * coeff[2]);
    }

    if ( _loopHFRavg < _besthfr )
    {
        _besthfr = _loopHFRavg;
        //emit valueChanged(_loopHFRavg);
        _bestpos = _startpos + _iteration * _steps;
    }
    //qDebug() << "Compute " << _iteration << "/" << _iterations << "=" << _loopHFRavg << "bestpos/pos" << _bestpos << "/" <<
    //_startpos + _iteration*_steps << "polfit=" << _bestposfit;

    setOstElementValue("values", "loopHFRavg", _loopHFRavg, false);
    setOstElementValue("values", "bestpos",   _bestpos, false);
    setOstElementValue("values", "bestposfit", _bestposfit, false);
    setOstElementValue("values", "focpos",    _startpos + _iteration * _steps, false);
    setOstElementValue("values", "iteration", _iteration, true);

    getStore()["values"]->push();

    /*_grid->append(_startpos + _iteration*_steps,_loopHFRavg);
    _propertyStore.update(_grid);
    emit propertyAppended(_grid,&_modulename,0,_startpos + _iteration*_steps,_loopHFRavg,0,0);*/

    if (_iteration < _iterations )
    {
        _iteration++;
        emit NextLoop();
    }
    else
    {
        emit LoopFinished();
    }
}

void FocusModule::SMRequestGotoNext()
{
    //sendMessage("SMRequestGotoNext");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION",
                          _startpos + _iteration * _steps))
    {
        emit abort();
        return;
    }
    emit RequestGotoNextDone();
}

void FocusModule::SMRequestBacklashBest()
{
    //sendMessage("SMRequestBacklashBest");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION",
                          _bestpos - _backlash))
    {
        emit abort();
        return;
    }
    emit RequestBacklashBestDone();
}

void FocusModule::SMRequestGotoBest()
{
    //sendMessage("SMRequestGotoBest");
    if (_bestposfit == 99 ) _bestposfit = _bestpos;
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION", _bestposfit))
    {
        emit abort();
        return;
    }
    emit RequestGotoBestDone();
}

void FocusModule::SMRequestExposureBest()
{
    //sendMessage("SMRequestExposureBest");
    if (!sendModNewNumber(getString("devices", "camera"), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", getValueFloat("parms",
                          "exposure")->value()))
    {
        emit abort();
        return;
    }
    double mFinalPos = 0;
    if (!getModNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION", mFinalPos))
    {
        emit abort();
        return;
    }
    setOstElementValue("results", "pos", mFinalPos, true);
    emit RequestExposureBestDone();
}

void FocusModule::SMComputeResult()
{
    //sendMessage("SMComputeResult");
    setOstElementValue("values", "imgHFR", _solver.HFRavg, true);
    setOstElementValue("results", "hfr", _solver.HFRavg, true);
    OST::ImgData dta = getValueImg("image", "image")->value();
    dta.HFRavg = _solver.HFRavg;
    dta.starsCount = _solver.stars.size();
    getValueImg("image", "image")->setValue(dta, true);

    // what should i do here ?
    emit ComputeResultDone();
}




void FocusModule::SMInitLoopFrame()
{
    //sendMessage("SMInitLoopFrame");
    _loopIteration = 0;
    _loopHFRavg = 99;
    setOstElementValue("values", "loopHFRavg", _loopHFRavg, true);
    emit InitLoopFrameDone();
}

void FocusModule::SMComputeLoopFrame()
{
    //sendMessage("SMComputeLoopFrame");
    _loopIteration++;
    _loopHFRavg = ((_loopIteration - 1) * _loopHFRavg + _solver.HFRavg) / _loopIteration;
    setOstElementValue("values", "loopHFRavg", _loopHFRavg, false);
    setOstElementValue("values", "imgHFR", _solver.HFRavg, true);

    //qDebug() << "Loop    " << _loopIteration << "/" << _loopIterations << " = " <<  _solver.HFRavg;


    if (_loopIteration < _loopIterations )
    {
        emit NextFrame();
    }
    else
    {
        emit LoopFrameDone();
    }
}

void FocusModule::SMAlert()
{
    sendMessage("SMAlert");
    emit abort();
}

void FocusModule::SMFocusDone()
{
    sendMessage("Focus done");
    setOstElementValue("results", "hfr", _solver.HFRavg, false);
    getProperty("actions")->setState(OST::Ok);
}
