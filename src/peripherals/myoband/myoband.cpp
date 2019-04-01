#include "myoband.h"
#include <QMutexLocker>

Myoband::Myoband()
    : _client(myolinux::Serial("/dev/myoband", 115200))
{
    _client.onEmg([this](myolinux::myo::EmgSample sample) {
        QMutexLocker lock(&_mutex);
        for (unsigned i = 0; i < _NB_EMGS; i++)
            this->_emgs[i] = sample[i];
    });

    _client.onImu([this](myolinux::myo::OrientationSample ori, myolinux::myo::AccelerometerSample acc, myolinux::myo::GyroscopeSample gyr) {
        QMutexLocker lock(&_mutex);
        for (unsigned i = 0; i < 4; i++)
            this->_imus[i] = (float)ori[i] / myolinux::myo::OrientationScale;
        for (unsigned i = 0; i < 3; i++)
            this->_imus[4 + i] = (float)acc[i] / myolinux::myo::AccelerometerScale;
        for (unsigned i = 0; i < 3; i++)
            this->_imus[7 + i] = (float)gyr[i] / myolinux::myo::GyroscopeScale;
    });
}

Myoband::~Myoband()
{
}

bool Myoband::setup()
{
    qInfo("MYOBAND : Trying to connect... Try to plug in/unplug the USB port\n");
    _client.connect();
    _client.setSleepMode(myolinux::myo::SleepMode::NeverSleep);
    _client.setMode(myolinux::myo::EmgMode::SendEmg, myolinux::myo::ImuMode::SendData, myolinux::myo::ClassifierMode::Disabled);
    return true;
}

void Myoband::loop(double, double)
{
    _client.listen();
}

void Myoband::cleanup()
{
    _client.disconnect();
}
