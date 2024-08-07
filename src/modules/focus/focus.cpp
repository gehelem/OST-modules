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


    _startpos =          getEltInt("parameters", "startpos")->value();
    _steps =             getEltInt("parameters", "steps")->value();
    _iterations =        getEltInt("parameters", "iterations")->value();
    _loopIterations =    getEltInt("parameters", "loopIterations")->value();
    _backlash =          getEltInt("parameters", "backlash")->value();

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
                        if (getEltBool(keyprop, keyelt)->setValue(false))
                        {
                            getProperty(keyprop)->setState(OST::Busy);
                            startCoarse();
                        }
                    }
                    if (keyelt == "abortfocus")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(false))
                        {
                            getProperty(keyprop)->setState(OST::Ok);
                            getEltPrg("progress", "global")->setPrgValue(0, true);
                            getEltPrg("progress", "global")->setDynLabel("Aborted", true);
                            getProperty("parms")->enable();
                            getProperty("devices")->enable();
                            getProperty("parameters")->enable();

                            pMachine->submitEvent("abort");
                        }
                    }
                    if (keyelt == "loop")
                    {
                        if (getEltBool(keyprop, keyelt)->setValue(false))
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
        getEltInt("values", "focpos")->setValue(n[0].value, true);

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
        getEltImg("image", "image")->setValue(dta, true);

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
    getProperty("values")->clearGrid();
    getProperty("parms")->disable();
    getProperty("devices")->disable();
    getProperty("parameters")->disable();
    if (!isServerConnected()) connectIndi();
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

    _zonePosvector.clear();
    _zoneHfdvector.clear();
    _zoneCoefficients.clear();


    _iteration = 0;
    _besthfr = 99;
    _bestposfit = 0;
    _zoneBestposfit.clear();

    mZoning =  getInt("parameters", "zoning");
    getProperty("zones")->clearGrid();


    for (int i = 0; i < mZoning * mZoning; i++)
    {
        _zoneBestposfit.append(0);
    }

    _steps =             getEltInt("parameters", "steps")->value();
    _iterations =        getEltInt("parameters", "iterations")->value();
    if (getBool("parameters", "aroundinitial"))
    {
        double p = 0;
        if (!getModNumber(getString("devices", "focuser"), "ABS_FOCUS_POSITION", "FOCUS_ABSOLUTE_POSITION", p))
        {
            pMachine->submitEvent("abort");
            return;
        }
        _startpos = p -  _steps * _iterations / 2;
    }
    else
    {
        _startpos =          getEltInt("parameters", "startpos")->value();
    }
    _loopIterations =    getEltInt("parameters", "loopIterations")->value();
    _backlash =          getEltInt("parameters", "backlash")->value();

    //pMachine = QScxmlStateMachine::fromFile(":focus.scxml");

    pMachine->init();
    pMachine->start();
    getEltPrg("progress", "global")->setPrgValue(0, true);
    getEltPrg("progress", "global")->setDynLabel("0/" + QString::number(_iterations), true);
}

void Focus::SMRequestFrameReset()
{
    //sendMessage("SMRequestFrameReset");

    if (!frameReset(getString("devices", "camera")))
    {
        usleep(1000);
        pMachine->submitEvent("FrameResetDone");
        return;
    }

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
    _solver.ResetSolver(stats, _image->getImageBuffer(), getInt("parameters", "zoning"));
    connect(&_solver, &Solver::successSEP, this, &Focus::OnSucessSEP);
    _solver.FindStars(_solver.stellarSolverProfiles[0]);
}

void Focus::OnSucessSEP()
{
    disconnect(&_solver, &Solver::successSEP, this, &Focus::OnSucessSEP);
    OST::ImgData dta = getEltImg("image", "image")->value();
    double ech = getSampling();
    dta.HFRavg = _solver.HFRavg * ech;
    dta.starsCount = _solver.stars.size();
    getEltImg("image", "image")->setValue(dta, true);
    pMachine->submitEvent("FindStarsDone");
}

void Focus::SMCompute()
{
    //sendMessage("SMCompute");

    _posvector.push_back(_startpos + _iteration * _steps);
    _hfdvector.push_back(_loopHFRavg);

    for (int i = 0; i < mZoning * mZoning; i++)
    {
        if (_zoneloopHFRavg[i] != 99)
        {
            _zonePosvector[i].push_back(_startpos + _iteration * _steps);
            _zoneHfdvector[i].push_back(_zoneloopHFRavg[i]);
        }
    }

    if (_posvector.size() > 2)
    {
        double coeff[3];
        polynomialfit(_posvector.size(), 3, _posvector.data(), _hfdvector.data(), coeff);
        _bestposfit = -coeff[1] / (2 * coeff[2]);
    }

    for (int i = 0; i < mZoning * mZoning; i++)
    {
        if (_zoneHfdvector[i].size() > 2)
        {
            double coeff[3];
            polynomialfit(_posvector.size(), 3, _posvector.data(), _zoneHfdvector[i].data(), coeff);
            _zoneBestposfit[i] = -coeff[1] / (2 * coeff[2]);
        }
    }

    if ( _loopHFRavg < _besthfr )
    {
        _besthfr = _loopHFRavg;
        _bestpos = _startpos + _iteration * _steps;
    }

    getEltFloat("values", "loopHFRavg")->setValue(_loopHFRavg);
    getEltInt("values", "bestpos")->setValue(_bestpos);
    getEltFloat("values", "bestposfit")->setValue(_bestposfit);
    getEltInt("values", "focpos")->setValue(_startpos + _iteration * _steps);
    getEltInt("values", "iteration")->setValue(_iteration, true);

    getStore()["values"]->push();
    getEltPrg("progress", "global")->setPrgValue(100 * _iteration / _iterations, true);
    getEltPrg("progress", "global")->setDynLabel(QString::number(_iteration + 1) + "/" + QString::number(_iterations), true);

    if (_iteration + 1 < _iterations )
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
    if (!sendModNewNumber(getString("devices", "camera"), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", getEltFloat("parms",
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
    getEltFloat("results", "pos")->setValue(mFinalPos, true);
    pMachine->submitEvent("RequestExposureBestDone");
}

void Focus::SMComputeResult()
{
    //sendMessage("SMComputeResult");
    double ech = getSampling();
    getEltFloat("values", "imgHFR")->setValue(_solver.HFRavg * ech, true);
    getEltFloat("results", "hfr")->setValue(_solver.HFRavg * ech, true);

    OST::ImgData dta = getEltImg("image", "image")->value();
    dta.HFRavg = _solver.HFRavg * ech;
    dta.starsCount = _solver.stars.size();
    getEltImg("image", "image")->setValue(dta, true);

    // what should i do here ?
    pMachine->submitEvent("ComputeResultDone");
    getEltPrg("progress", "global")->setPrgValue(100, true);
    getEltPrg("progress", "global")->setDynLabel("Finished", true);

    getProperty("zones")->clearGrid();

    for (int i = 0; i < mZoning * mZoning; i++)
    {
        if ((mZoning != 2) && (mZoning != 3))
        {
            getEltString("zones", "zone")->setValue(QString::number(i + 1), false);
        }
        if (mZoning == 2)
        {
            if (i == 0) getEltString("zones", "zone")->setValue("Upper left", false);
            if (i == 1) getEltString("zones", "zone")->setValue("Upper right", false);
            if (i == 2) getEltString("zones", "zone")->setValue("Lower left", false);
            if (i == 3) getEltString("zones", "zone")->setValue("Lower right", false);
        }
        if (mZoning == 3)
        {
            if (i == 0) getEltString("zones", "zone")->setValue("Upper left", false);
            if (i == 1) getEltString("zones", "zone")->setValue("Upper middle", false);
            if (i == 2) getEltString("zones", "zone")->setValue("Upper right", false);
            if (i == 3) getEltString("zones", "zone")->setValue("Midle left", false);
            if (i == 4) getEltString("zones", "zone")->setValue("Center", false);
            if (i == 5) getEltString("zones", "zone")->setValue("Middle right", false);
            if (i == 6) getEltString("zones", "zone")->setValue("Lower left", false);
            if (i == 7) getEltString("zones", "zone")->setValue("Lower middle", false);
            if (i == 8) getEltString("zones", "zone")->setValue("Lower right", false);
        }


        getEltFloat("zones", "bestpos")->setValue(_zoneBestposfit[i], false);
        getProperty("zones")->push();
    }

    getProperty("parms")->enable();
    getProperty("devices")->enable();
    getProperty("parameters")->enable();


}




void Focus::SMInitLoopFrame()
{
    //sendMessage("SMInitLoopFrame");
    _loopIteration = 0;
    _loopHFRavg = 99;
    _zoneloopHFRavg.clear();
    _zoneLoopIteration.clear();
    for (int i = 0; i < mZoning * mZoning; i++)
    {
        _zoneloopHFRavg.append(99);
        _zoneLoopIteration.append(0);
        _zonePosvector.append(std::vector<double>());
        _zoneHfdvector.append(std::vector<double>());
    }
    getEltFloat("values", "loopHFRavg")->setValue(_loopHFRavg, true);

    pMachine->submitEvent("InitLoopFrameDone");

}

void Focus::SMComputeLoopFrame()
{
    //sendMessage("SMComputeLoopFrame");
    double ech = getSampling();
    _loopIteration++;
    _loopHFRavg = ((_loopIteration - 1) * _loopHFRavg + _solver.HFRavg * ech) / _loopIteration;
    for (int i = 0; i < mZoning * mZoning; i++)
    {
        if (_solver.HFRavgZone[i] != 99)
        {
            _zoneloopHFRavg[i] = (_zoneLoopIteration[i] * _zoneloopHFRavg[i] + _solver.HFRavgZone[i] * ech) /
                                 (_zoneLoopIteration[i] + 1);
            _zoneLoopIteration[i]++;

        }
    }
    getEltFloat("values", "loopHFRavg")->setValue(_loopHFRavg, true);
    getEltFloat("values", "imgHFR")->setValue(_solver.HFRavg, true);

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
    double ech = getSampling();
    sendMessage("Focus done");
    getEltFloat("results", "hfr")->setValue(_solver.HFRavg * ech, true);

    getProperty("actions")->setState(OST::Ok);
    pMachine->stop();
}
