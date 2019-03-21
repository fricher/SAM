#include "compensationoptitrack.h"
#include <QNetworkDatagram>
#include <QDir>
#include <qmath.h>
#include <iostream>
#include "wiringPi.h"

CompensationOptitrack::CompensationOptitrack(QObject *parent) : QObject(parent),
    _osmer(OsmerElbow::instance()),
    _pronosup(PronoSupination::instance()),
    _adc("/dev/i2c-1", 0x48),
    _imu_bras("/dev/ttyUSB0", XIMU::XIMU_LOGLEVEL_NONE, 115200),
    _imu_tronc("/dev/ttyUSB1", XIMU::XIMU_LOGLEVEL_NONE, 115200),
    _Lt(40),
    _Lua(0.),
    _Lfa(0.),
    _lsh(-35),
    _lambda(0),
    _threshold(0.)
{
    _settings.beginGroup("CompensationOptitrack");

    QObject::connect(&_receiver,&QUdpSocket::readyRead,this,&CompensationOptitrack::on_def);
    if(!_receiver.bind(QHostAddress::AnyIPv4,45454)) {
        qCritical() << _receiver.errorString();
    }

    _abs_time.start();

    _menu.set_title("Compensation control with optitrack");
    _menu.set_code("opti");
    _menu.addItem(ConsoleMenuItem("Start (+ filename)","start",[this](QString args){ this->start(args); }));
    _menu.addItem(ConsoleMenuItem("Stop","stop",[this](QString){ this->stop(); }));
    _menu.addItem(ConsoleMenuItem("Back to 0°","zero",[this](QString){ this->zero(); }));
    _menu.addItem(ConsoleMenuItem("Display law parameters","disp",[this](QString){this->display_parameters(); }));
    _menu.addItem(_osmer.menu());
    _menu.addItem(_pronosup.menu());

    QObject::connect(&_menu,&ConsoleMenu::finished,this,&CompensationOptitrack::stop);
    QObject::connect(&_menu,&ConsoleMenu::activated,this,&CompensationOptitrack::on_activated);

    _optitrack.begin(_settings.value("port", 1511).toInt());
}

CompensationOptitrack::~CompensationOptitrack()
{
    stop();
}

void CompensationOptitrack::zero(){
    _osmer.move_to_angle(0,10,true);
}

void CompensationOptitrack::display_parameters(){
    QObject::connect(&_receiver,&QUdpSocket::readyRead,this,&CompensationOptitrack::on_def);
    qDebug("lambda:%d",_lambda);
    qDebug("Lfa:%lf",_Lfa);
    qDebug("Lua:%lf",_Lua);
    qDebug("Threshold (in rad):%lf",_threshold);
}

void CompensationOptitrack::start(QString filename = QString()) {
    if(filename.isEmpty())
        filename = "compensation_optitrack";


    int cnt = 0;
    QString extension = QString(".txt");
    do {
        ++cnt;
        QString suffix = QString("_") + QString::number(cnt);
        _file.setFileName(filename + suffix + extension);
    } while(_file.exists());

    if(!_file.open(QIODevice::ReadWrite)) {
        qCritical() << "Failed to open" << _file.fileName() << "-" << _file.errorString();
        return;
    }
    _need_to_write_header = true;

    _time.start();
    _previous_elapsed = 0;

    _cnt = 0;
    QObject::disconnect(&_receiver,&QUdpSocket::readyRead,this,&CompensationOptitrack::on_def);
    QObject::connect(&_optitrack,&OptiListener::new_data,this,&CompensationOptitrack::on_new_data);
}

void CompensationOptitrack::stop() {
    QObject::disconnect(&_optitrack,&OptiListener::new_data,this,&CompensationOptitrack::on_new_data);
    _osmer.forward(0);
    _pronosup.forward(0);
    _file.close();
    QObject::connect(&_receiver,&QUdpSocket::readyRead,this,&CompensationOptitrack::on_def);
}

void CompensationOptitrack::on_new_data(optitrack_data_t data) {
    unsigned int init_cnt = _settings.value("init_count", 10).toInt();
    int btn_sync = digitalRead(_settings.value("btn_sync", 29).toInt());

    Eigen::Vector3d posA, posElbow, posEE;
    double timeWithDelta = _time.elapsed() / 1000.;
    double deltaTtable = timeWithDelta - _previous_elapsed;
    double absTtable = _abs_time.elapsed() / 1000.;
    _previous_elapsed = timeWithDelta;

    int timerTask = 1;

    double qBras[4], qTronc[4];
    _imu_bras.get_quat(qBras);
    _imu_tronc.get_quat(qTronc);
    qDebug() << "IMU Bras : " << qBras[0] << " " << qBras[1] << " " << qBras[2] << " " << qBras[3];
    qDebug() << "IMU Tronc : " << qTronc[0] << " " << qTronc[1] << " " << qTronc[2] << " " << qTronc[3];

    double debugData[13];

    int index_acromion = -1, index_EE = -1, index_elbow = -1;

    for (int i = 0; i < data.nRigidBodies; i++){
        if(data.rigidBodies[i].ID == _settings.value("acromion_id", 3).toInt()) {
            posA[0] = data.rigidBodies[i].x*100;
            posA[1] = data.rigidBodies[i].y*100;
            posA[2] = data.rigidBodies[i].z*100;
            index_acromion = i;
        }
        else if(data.rigidBodies[i].ID == _settings.value("elbow_id", 6).toInt()) {
            posElbow[0] = data.rigidBodies[i].x*100;
            posElbow[1] = data.rigidBodies[i].y*100;
            posElbow[2] = data.rigidBodies[i].z*100;
            index_elbow = i;
        }
        else if(data.rigidBodies[i].ID == _settings.value("ee_id", 9).toInt()) {
            posEE[0] = data.rigidBodies[i].x*100;
            posEE[1] = data.rigidBodies[i].y*100;
            posEE[2] = data.rigidBodies[i].z*100;
            index_EE = i;
        }
    }

    double beta = _osmer.angle() * M_PI/180.;

    if(_need_to_write_header) {
        _file.write("delta, time, btn_sync, abs_time, emg1, emg2, timerTask,");
        _file.write(" index_acromion, index_EE, index_elbow, initialAcromionPosition.x, initialAcromionPosition.y, initialAcromionPosition.z, AcromionPosition.x, AcromionPosition.y, AcromionPosition.z,");
        _file.write(" positionEE.x, positionEE.y, positionEE.z, positionEE_inHip.x, positionEE_inHip.y, positionEE_inHip.z, delta, beta_new, beta, dBeta, betaDot, lambda, nbRigidBodies");
        for(int i = 0; i < data.nRigidBodies; i++) {
            _file.write(", ID, bTrackingValid, fError, qw, qx, qy, qz, x, y, z");
        }
        _file.write("\r\n");
        _need_to_write_header = false;
    }

    if(_cnt == 0) {
        unsigned int opti_freq = _settings.value("optitrack_frequency", 100).toInt();
        if(_Lua == 0 && _Lfa == 0) {
            _Lua = qRound((posElbow - posA).norm());
            _Lfa = qRound((posElbow - posEE).norm());
        }
        _lawopti.initialization(posA, posEE, opti_freq);
    }
    else if(_cnt <= init_cnt) {
        _lawopti.initialAcromionPosition(posA, _cnt, init_cnt);
        _lawopti.filter_optitrackData(posA, posEE);
    }
    else {
        _lawopti.controlLaw(posEE, beta, _Lua, _Lfa, _lambda, _threshold);
        _osmer.set_velocity(_lawopti.returnBetaDot_deg());

        if (_cnt % _settings.value("display_count", 50).toInt() == 0){
//            _lawopti.displayData(posEE, beta);
//            qDebug() << "betaDot in deg:" << _lawopti.returnBetaDot_deg();
        }
    }

    _lawopti.writeDebugData(debugData, posEE, beta);

    QTextStream ts(&_file);
    //ts.setPadChar(' ');
    ts << deltaTtable << ' ' << timeWithDelta << ' ' << btn_sync << ' ' << absTtable << ' ' << _adc.readADC_SingleEnded(0) << ' ' << _adc.readADC_SingleEnded(1) << ' ' << timerTask;
    ts << ' ' << qBras[0] << ' ' << qBras[1] << ' ' << qBras[2] << ' ' << qBras[3] << ' ' << qTronc[0] << ' ' << qTronc[1] << ' ' << qTronc[2] << ' ' << qTronc[3];
    ts << ' ' << index_acromion << ' ' << index_EE << ' ' << index_elbow << ' ' << debugData[0] << ' ' << debugData[1] << ' ' << debugData[2] << ' ' << posA[0] << ' ' << posA[1] << ' ' << posA[2];
    ts << ' ' << debugData[3] << ' ' << debugData[4] << ' ' << debugData[5] << ' ' << debugData[6] << ' ' << debugData[7] << ' ' << debugData[8];
    ts << ' ' << debugData[9] << ' ' << debugData[10] << ' ' << debugData[11] << ' ' << debugData[12] << ' ' << _lambda << ' ' << data.nRigidBodies;

    for(int i = 0; i < data.nRigidBodies; i++){
        ts << ' ' << data.rigidBodies[i].ID << ' ' << data.rigidBodies[i].bTrackingValid << ' ' << data.rigidBodies[i].fError;
        ts << ' ' << data.rigidBodies[i].qw << ' ' << data.rigidBodies[i].qx << ' ' << data.rigidBodies[i].qy << ' ' << data.rigidBodies[i].qz;
        ts << ' ' << data.rigidBodies[i].x << ' ' << data.rigidBodies[i].y << ' ' << data.rigidBodies[i].z;
    }
    ts << endl;

    ++_cnt;
}

void CompensationOptitrack::on_activated() {
    _osmer.calibration();
}

void CompensationOptitrack::on_def() {
    while(_receiver.hasPendingDatagrams()) {
        QByteArray dataReceived = _receiver.receiveDatagram().data();
        QTextStream ts(&dataReceived);
        int tmp;

        ts >> tmp;
        _Lt = tmp;

        ts >> tmp;
        _Lua = tmp;

        ts >> tmp;
        _Lfa = tmp;

        ts >> tmp;
        _lsh = tmp;

        ts >> tmp;
        _lambda = tmp;

        ts >> tmp;
        _threshold = tmp*M_PI/180.; // dead zone limit for trunk, in rad.


    }
}
