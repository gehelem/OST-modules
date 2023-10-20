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

    setModuleDescription("Focus (scxml)");
    setModuleVersion("0.1");

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
                            pMachine->submitEvent("abort");
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
        getProperty("image")->setState(OST::Ok);

        QImage rawImage = _image->getRawQImage();
        rawImage.save( getWebroot() + "/" + getModuleName() + QString(b.getDeviceName()) + ".jpeg", "JPG", 100);
        OST::ImgData dta = _image->ImgStats();
        dta.mUrlJpeg = getModuleName() + QString(b.getDeviceName()) + ".jpeg";
        dta.mUrlFits = getModuleName() + QString(b.getDeviceName()) + ".FITS";
        getValueImg("image", "image")->setValue(dta, true);

        if (pMachine->isRunning())
        {
            pMachine->submitEvent("ExposureDone");
            pMachine->submitEvent("ExposureBestDone");
        }

    }
}

void Focus::SMAbort()
{
    pMachine->stop();
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
    pMachine->connectToState("ComputeResult", QScxmlStateMachine::onExit(this, &Focus::SMFocusDone));
    pMachine->connectToState("ComputeLoopFrame", QScxmlStateMachine::onEntry(this, &Focus::SMComputeLoopFrame));
    pMachine->connectToState("InitLoopFrame", QScxmlStateMachine::onEntry(this, &Focus::SMInitLoopFrame));
    pMachine->connectToState("FindStarsFinal", QScxmlStateMachine::onEntry(this, &Focus::SMFindStars));


    pMachine->start();
}

void Focus::SMRequestFrameReset()
{
    sendMessage("SMRequestFrameReset");

    if (!frameReset(getString("devices", "camera")))
    {
        pMachine->submitEvent("RequestFrameResetDone");
        usleep(1000);
        pMachine->submitEvent("FrameResetDone");
        return;
    }
    pMachine->submitEvent("RequestFrameResetDone");

}

void Focus::SMRequestBacklash()
{
    //sendMessage("SMRequestBacklash");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION",
                          _startpos - _backlash))
    {
        pMachine->submitEvent("abort");
        return;
    }
    pMachine->submitEvent("RequestBacklashDone");

}

void Focus::SMRequestGotoStart()
{
    //sendMessage("SMRequestGotoStart");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION", _startpos))
    {
        pMachine->submitEvent("abort");
        return;
    }
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
        _bestpos = _startpos + _iteration * _steps;
    }

    setOstElementValue("values", "loopHFRavg", _loopHFRavg, false);
    setOstElementValue("values", "bestpos",   _bestpos, false);
    setOstElementValue("values", "bestposfit", _bestposfit, false);
    setOstElementValue("values", "focpos",    _startpos + _iteration * _steps, false);
    setOstElementValue("values", "iteration", _iteration, true);

    getStore()["values"]->push();

    if (_iteration < _iterations )
    {
        _iteration++;
        pMachine->submitEvent("NextLoop");
    }
    else
    {
        pMachine->submitEvent("LoopFinished");
    }
}

void Focus::SMRequestGotoNext()
{
    //sendMessage("SMRequestGotoNext");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION",
                          _startpos + _iteration * _steps))
    {
        pMachine->submitEvent("abort");
        return;
    }
    pMachine->submitEvent("RequestGotoNextDone");
}

void Focus::SMRequestBacklashBest()
{
    //sendMessage("SMRequestBacklashBest");
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION",
                          _bestpos - _backlash))
    {
        pMachine->submitEvent("abort");
        return;
    }
    pMachine->submitEvent("RequestBacklashBestDone");
}

void Focus::SMRequestGotoBest()
{
    //sendMessage("SMRequestGotoBest");
    if (_bestposfit == 99 ) _bestposfit = _bestpos;
    if (!sendModNewNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION", _bestposfit))
    {
        pMachine->submitEvent("abort");
        return;
    }
    pMachine->submitEvent("RequestGotoBestDone");
}

void Focus::SMRequestExposureBest()
{
    //sendMessage("SMRequestExposureBest");
    if (!sendModNewNumber(getString("devices", "camera"), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", getValueFloat("parms",
                          "exposure")->value()))
    {
        pMachine->submitEvent("abort");
        return;
    }
    double mFinalPos = 0;
    if (!getModNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION", mFinalPos))
    {
        pMachine->submitEvent("abort");
        return;
    }
    setOstElementValue("results", "pos", mFinalPos, true);
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
    pMachine->submitEvent("ComputeResultDone");
}




void Focus::SMInitLoopFrame()
{
    //sendMessage("SMInitLoopFrame");
    _loopIteration = 0;
    _loopHFRavg = 99;
    setOstElementValue("values", "loopHFRavg", _loopHFRavg, true);
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
        pMachine->submitEvent("NextFrame");
    }
    else
    {
        pMachine->submitEvent("LoopFrameDone");
    }
}

void Focus::SMAlert()
{
    sendMessage("SMAlert");
    pMachine->submitEvent("abort");
}

void Focus::SMFocusDone()
{
    sendMessage("Focus done");
    setOstElementValue("results", "hfr", _solver.HFRavg, false);
    getProperty("actions")->setState(OST::Ok);
}
