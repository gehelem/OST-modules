#include "focus.h"
#include "polynomialfit.h"

Focus *initialize(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
{
    Focus *basemodule = new Focus(name, label, profile, availableModuleLibs);
    return basemodule;
}

Focus::Focus(QString name, QString label, QString profile, QVariantMap availableModuleLibs)
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

Focus::~Focus()
{

}
void Focus::OnMyExternalEvent(const QString &eventType, const QString  &eventModule, const QString  &eventKey,
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

void Focus::updateProperty(INDI::Property p)
{
    if (
        (QString(p.getDeviceName()) == getString("devices", "camera") )
        &&  (p.getState() == IPS_ALERT)
    )
    {
        //sendMessage("cameraAlert");
        emit cameraAlert();
        pMachine->submitEvent("cameraAlert");
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
            pMachine->submitEvent("GotoBestDone");
            pMachine->submitEvent("BacklashBestDone");
            pMachine->submitEvent("BacklashDone");
            pMachine->submitEvent("GotoNextDone");
            pMachine->submitEvent("GotoStartDone");
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
        if (pMachine->isRunning())
        {
            emit FrameResetDone();
            pMachine->submitEvent("FrameResetDone");

        }
    }
    if (QString(p.getName()) == "CCD1")
    {
        newBLOB(p);
    }



}

void Focus::newBLOB(INDI::PropertyBlob b)
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

        if (pMachine->isRunning())
        {
            emit ExposureDone();
            emit ExposureBestDone();
            pMachine->submitEvent("ExposureDone");
            pMachine->submitEvent("ExposureBestDone");


        }

    }
}

void Focus::SMAbort()
{

    pMachine->stop();
    //sendMessage("machine stopped");
}

void Focus::startCoarse()
{
    getStore()["values"]->clearGrid();
    connectIndi();
    connectDevice(getString("devices", "camera"));
    connectDevice(getString("devices", "focuser"));
    connectDevice(getString("devices", "filter"));
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
    pMachine = QScxmlStateMachine::fromFile(":focus.scxml");
    pMachine->connectToState("RequestFrameReset", QScxmlStateMachine::onEntry(this, &Focus::SMRequestFrameReset));
    pMachine->connectToState("RequestBacklash", QScxmlStateMachine::onEntry(this, &Focus::SMRequestBacklash));
    pMachine->connectToState("RequestGotoStart", QScxmlStateMachine::onEntry(this, &Focus::SMRequestGotoStart));
    pMachine->connectToState("RequestExposure", QScxmlStateMachine::onEntry(this, &Focus::SMRequestExposure));
    pMachine->connectToState("FindStars", QScxmlStateMachine::onEntry(this, &Focus::SMFindStars));
    pMachine->connectToState("Compute", QScxmlStateMachine::onEntry(this, &Focus::SMCompute));
    pMachine->connectToState("RequestGotoNext", QScxmlStateMachine::onEntry(this, &Focus::SMRequestGotoNext));
    pMachine->connectToState("RequestBacklashBest", QScxmlStateMachine::onEntry(this, &Focus::SMRequestBacklashBest));
    pMachine->connectToState("RequestGotoBest", QScxmlStateMachine::onEntry(this, &Focus::SMRequestGotoBest));
    pMachine->connectToState("RequestExposureBest", QScxmlStateMachine::onEntry(this, &Focus::SMRequestExposureBest));
    pMachine->connectToState("ComputeResult", QScxmlStateMachine::onEntry(this, &Focus::SMComputeResult));
    pMachine->connectToState("ComputeLoopFrame", QScxmlStateMachine::onEntry(this, &Focus::SMComputeLoopFrame));
    pMachine->connectToState("InitLoopFrame", QScxmlStateMachine::onEntry(this, &Focus::SMInitLoopFrame));

    //_machine = new QStateMachine();

    /* states definitions */
    /*auto *CoarseFocus = new QState();
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
    _machine.addState(Final);*/

    /* Set initial states */
    /*Init->setInitialState(RequestFrameReset);
    Loop->setInitialState(LoopFrame);
    LoopFrame->setInitialState(InitLoopFrame);
    Finish->setInitialState(RequestBacklashBest);
    CoarseFocus->setInitialState(Init);
    _machine.setInitialState(CoarseFocus);*/

    /* actions to take when entering into state */
    /*connect(Abort,              &QState::entered, this, &Focus::SMAbort);
    connect(RequestFrameReset,  &QState::entered, this, &Focus::SMRequestFrameReset);
    connect(RequestBacklash,    &QState::entered, this, &Focus::SMRequestBacklash);
    connect(RequestGotoStart,   &QState::entered, this, &Focus::SMRequestGotoStart);
    connect(RequestExposure,    &QState::entered, this, &Focus::SMRequestExposure);
    connect(FindStars,          &QState::entered, this, &Focus::SMFindStars);
    connect(Compute,            &QState::entered, this, &Focus::SMCompute);
    connect(RequestGotoNext,    &QState::entered, this, &Focus::SMRequestGotoNext);
    connect(RequestBacklashBest, &QState::entered, this, &Focus::SMRequestBacklashBest);
    connect(RequestGotoBest,    &QState::entered, this, &Focus::SMRequestGotoBest);
    connect(RequestExposureBest, &QState::entered, this, &Focus::SMRequestExposureBest);
    connect(ComputeResult,      &QState::entered, this, &Focus::SMComputeResult);
    connect(ComputeLoopFrame,   &QState::entered, this, &Focus::SMComputeLoopFrame);
    connect(InitLoopFrame,      &QState::entered, this, &Focus::SMInitLoopFrame);
    connect(Final,      &QState::entered, this, &Focus::SMFocusDone);*/

    /* mapping signals to state transitions */
    /*CoarseFocus->       addTransition(this, &Focus::abort,                Abort);
    RequestFrameReset-> addTransition(this, &Focus::RequestFrameResetDone, WaitFrameReset);
    WaitFrameReset->    addTransition(this, &Focus::FrameResetDone,       RequestBacklash);
    RequestBacklash->   addTransition(this, &Focus::RequestBacklashDone,  WaitBacklash);
    WaitBacklash->      addTransition(this, &Focus::BacklashDone,         RequestGotoStart);
    RequestGotoStart->  addTransition(this, &Focus::RequestGotoStartDone, WaitGotoStart);
    WaitGotoStart->     addTransition(this, &Focus::GotoStartDone,        Loop);

    RequestExposure->   addTransition(this, &Focus::RequestExposureDone,   WaitExposure);
    WaitExposure->      addTransition(this, &Focus::ExposureDone,          FindStars);
    FindStars->         addTransition(this, &Focus::FindStarsDone,         ComputeLoopFrame);
    Compute->           addTransition(this, &Focus::LoopFinished,          Finish);

    Compute->           addTransition(this, &Focus::NextLoop,              RequestGotoNext);
    RequestGotoNext->   addTransition(this, &Focus::RequestGotoNextDone,   WaitGotoNext);
    WaitGotoNext->      addTransition(this, &Focus::GotoNextDone,          LoopFrame);

    RequestBacklashBest->   addTransition(this, &Focus::RequestBacklashBestDone,   WaitBacklashBest);
    WaitBacklashBest->      addTransition(this, &Focus::BacklashBestDone,          RequestGotoBest);
    RequestGotoBest->       addTransition(this, &Focus::RequestGotoBestDone,       WaitGotoBest);
    WaitGotoBest->          addTransition(this, &Focus::GotoBestDone,              RequestExposureBest);
    RequestExposureBest->   addTransition(this, &Focus::RequestExposureBestDone,   WaitExposureBest);
    WaitExposureBest->      addTransition(this, &Focus::ExposureBestDone,          ComputeResult);
    ComputeResult->         addTransition(this, &Focus::ComputeResultDone,         Final);

    InitLoopFrame-> addTransition(this, &Focus::InitLoopFrameDone, RequestExposure );
    ComputeLoopFrame->addTransition(this, &Focus::NextFrame, RequestExposure);
    ComputeLoopFrame->addTransition(this, &Focus::LoopFrameDone, Compute);*/

    pMachine->start();
    //sendMessage("machine started");
    qDebug() << "Start coarse focus";
}

void Focus::SMRequestFrameReset()
{
    sendMessage("SMRequestFrameReset");


    //setBLOBMode(B_ALSO, getString("devices","camera").toStdString().c_str(), nullptr);

    /*qDebug() << "conf count" << _machine.configuration().count();
    QSet<QAbstractState *>::iterator i;
    for (i=_machine.configuration().begin();i !=_machine.configuration().end();i++)
    {
        qDebug() << (*i)->objectName();
    }*/

    if (!frameReset(getString("devices", "camera")))
    {
        pMachine->submitEvent("RequestFrameResetDone");
        usleep(1000);
        emit FrameResetDone();
        pMachine->submitEvent("FrameResetDone");
        return;
    }
    emit RequestFrameResetDone();
    pMachine->submitEvent("RequestFrameResetDone");

}

void Focus::SMRequestBacklash()
{
    //sendMessage("SMRequestBacklash");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION",
                          _startpos - _backlash))
    {
        emit abort();
        pMachine->submitEvent("abort");
        return;
    }
    emit RequestBacklashDone();
    pMachine->submitEvent("RequestBacklashDone");

}

void Focus::SMRequestGotoStart()
{
    //sendMessage("SMRequestGotoStart");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION", _startpos))
    {
        emit abort();
        pMachine->submitEvent("abort");
        return;
    }
    emit RequestGotoStartDone();
    pMachine->submitEvent("RequestGotoStartDone");

}

void Focus::SMRequestExposure()
{
    if (!requestCapture(getString("devices", "camera"), getFloat("parms", "exposure"), getInt("parms", "gain"), getInt("parms",
                        "offset")))
    {
        emit abort();
        return;
    }
    //setBLOBMode(B_ALSO, getString("devices","camera").toStdString().c_str(), nullptr);
    emit RequestExposureDone();
    pMachine->submitEvent("RequestExposureDone");

}

void Focus::SMFindStars()
{
    //sendMessage("SMFindStars");
    stats = _image->getStats();
    _solver.ResetSolver(stats, _image->getImageBuffer());
    connect(&_solver, &Solver::successSEP, this, &Focus::OnSucessSEP);
    _solver.FindStars(_solver.stellarSolverProfiles[0]);
}

void Focus::OnSucessSEP()
{
    OST::ImgData dta = getValueImg("image", "image")->value();
    dta.HFRavg = _solver.HFRavg;
    dta.starsCount = _solver.stars.size();
    getValueImg("image", "image")->setValue(dta, true);
    pMachine->submitEvent("FindStarsDone");
    emit FindStarsDone();
}

void Focus::SMCompute()
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
        pMachine->submitEvent("NextLoop");
    }
    else
    {
        emit LoopFinished();
        pMachine->submitEvent("LoopFinished");
    }
}

void Focus::SMRequestGotoNext()
{
    //sendMessage("SMRequestGotoNext");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION",
                          _startpos + _iteration * _steps))
    {
        emit abort();
        pMachine->submitEvent("abort");
        return;
    }
    pMachine->submitEvent("RequestGotoNextDone");
    emit RequestGotoNextDone();
}

void Focus::SMRequestBacklashBest()
{
    //sendMessage("SMRequestBacklashBest");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION",
                          _bestpos - _backlash))
    {
        emit abort();
        pMachine->submitEvent("abort");
        return;
    }
    emit RequestBacklashBestDone();
    pMachine->submitEvent("RequestBacklashBestDone");
}

void Focus::SMRequestGotoBest()
{
    //sendMessage("SMRequestGotoBest");
    if (_bestposfit == 99 ) _bestposfit = _bestpos;
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION", _bestposfit))
    {
        emit abort();
        pMachine->submitEvent("abort");
        return;
    }
    emit RequestGotoBestDone();
    pMachine->submitEvent("RequestGotoBestDone");
}

void Focus::SMRequestExposureBest()
{
    //sendMessage("SMRequestExposureBest");
    if (!sendModNewNumber(getString("devices", "camera"), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", getValueFloat("parms",
                          "exposure")->value()))
    {
        emit abort();
        pMachine->submitEvent("abort");
        return;
    }
    double mFinalPos = 0;
    if (!getModNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION", mFinalPos))
    {
        pMachine->submitEvent("abort");
        emit abort();
        return;
    }
    setOstElementValue("results", "pos", mFinalPos, true);
    emit RequestExposureBestDone();
    pMachine->submitEvent("RequestExposureBestDone");
}

void Focus::SMComputeResult()
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
    pMachine->submitEvent("ComputeResultDone");
}




void Focus::SMInitLoopFrame()
{
    //sendMessage("SMInitLoopFrame");
    _loopIteration = 0;
    _loopHFRavg = 99;
    setOstElementValue("values", "loopHFRavg", _loopHFRavg, true);
    emit InitLoopFrameDone();
    pMachine->submitEvent("InitLoopFrameDone");

}

void Focus::SMComputeLoopFrame()
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
        pMachine->submitEvent("NextFrame");

    }
    else
    {
        emit LoopFrameDone();
        pMachine->submitEvent("LoopFrameDone");
    }
}

void Focus::SMAlert()
{
    sendMessage("SMAlert");
    emit abort();
    pMachine->submitEvent("abort");
}

void Focus::SMFocusDone()
{
    sendMessage("Focus done");
    setOstElementValue("results", "hfr", _solver.HFRavg, false);
    getProperty("actions")->setState(OST::Ok);
}
