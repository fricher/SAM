#include "general_formulation.h"
#include "utils/check_ptr.h"
#include "utils/log/log.h"
#include "wiringPi.h"
#include <filesystem>
#include <iostream>

GeneralFormulation::GeneralFormulation(std::shared_ptr<SAM::Components> robot)
    : ThreadedLoop("General Formulation", 0.01)
    , _robot(robot)
    , _Lt(40)
    , _Lua(30)
    , _Lfa(20)
    , _lwrist(5)
    , _lhand(10)
    , _lambda(0)
    , _pin_up(24)
    , _pin_down(22)
//    , _lua("lua(cm)", this, 30)
//    , _lfa("lfa(cm)", this, 35)
//    , _lhand("lhand(cm)", this, 5)
//    , _lambdaE("lambda elbow", this, 0)
//    , _lambdaWF("lambda wrist flex", this, 0)
//    , _lambdaWPS("lambda wrist PS", this, 0)
//    , _thresholdE("threshold E", this, 5)
//    , _thresholdWF("threshold WF", this, 5)
//    , _thresholWPS("threshold WPS", this, 5)
{
    if (!check_ptr(_robot->joints.elbow_flexion, _robot->joints.wrist_pronation, _robot->joints.wrist_flexion, _robot->sensors.optitrack)) {
        throw std::runtime_error("General Formulation Control is missing components");
    }

    if (!_receiver.bind("0.0.0.0", 45457)) {
        critical() << "General Formulation: Failed to bind receiver for parameters definition";
    }

    _menu->set_description("GeneralFormulation");
    _menu->set_code("gf");
    _menu->add_item("tare", "Tare IMUs", [this](std::string) { this->tare_IMU(); });
    _menu->add_item("calib", "Calibration", [this](std::string) { this->calibrations(); });

    _menu->add_item(_robot->joints.elbow_flexion->menu());
    _menu->add_item(_robot->joints.wrist_flexion->menu());
    _menu->add_item(_robot->joints.wrist_pronation->menu());
    _menu->add_item(_robot->joints.hand->menu());

    pullUpDnControl(_pin_up, PUD_UP);
    pullUpDnControl(_pin_down, PUD_UP);

    for (int i = 0; i < nbLinks; i++) {
        _threshold[i] = 0. * M_PI / 180;
    }
    l[0] = _lhand; // from center of the hand to the pronosupination joint
    l[1] = _lwrist; //  from the pronosupination joint to the wrist flexion/ext joint
    l[2] = _Lfa; //  from the wrist flex/ext joint to the elbow flex/ext joint
    l[3] = _Lua; // from the elbow joint to the acromion/shoulder joint
}

GeneralFormulation::~GeneralFormulation()
{
    _robot->joints.elbow_flexion->forward(0);
    _robot->joints.wrist_pronation->forward(0);
    stop_and_join();
}

void GeneralFormulation::tare_IMU()
{
    //    _robot->sensors.white_imu->send_command_algorithm_init_then_tare();
    _robot->sensors.red_imu->send_command_algorithm_init_then_tare();

    //    _robot->sensors.yellow_imu->send_command_algorithm_init_then_tare();

    debug("Wait ...");

    usleep(6 * 1000000);
    _robot->user_feedback.buzzer->makeNoise(Buzzer::TRIPLE_BUZZ);
}

void GeneralFormulation::calibrations()
{
    _robot->joints.hand->take_ownership();
    _robot->joints.hand->init_sequence();
    if (_robot->joints.elbow_flexion->is_calibrated() == false) {
        _robot->joints.elbow_flexion->calibrate();
    }
    if (_robot->joints.elbow_flexion->is_calibrated())
        debug() << "Calibration elbow: ok \n";

    if (_robot->joints.wrist_flexion->is_calibrated() == false) {
        _robot->joints.wrist_flexion->calibrate();
    }
    if (_robot->joints.wrist_flexion->is_calibrated())
        debug() << "Calibration wrist flexion: ok \n";

    if (_robot->joints.wrist_pronation->is_calibrated() == false) {
        _robot->joints.wrist_pronation->calibrate();
    }
    if (_robot->joints.wrist_pronation->is_calibrated())
        debug() << "Calibration wrist pronation: ok \n";
}

void GeneralFormulation::receiveData()
{
    while (_receiver.available()) {

        auto data = _receiver.receive();
        std::string buf;
        buf.resize(data.size());
        std::transform(data.begin(), data.end(), buf.begin(), [](std::byte b) -> char { return static_cast<char>(b); });
        std::istringstream ts(buf);
        int tmp;

        ts >> tmp;
        l[3] = tmp; // _Lua

        ts >> tmp;
        l[2] = tmp; // _Lfa

        ts >> tmp;
        l[1] = tmp; // _lwrist

        ts >> tmp;
        l[0] = tmp; // _lhand

        ts >> tmp;
        _lambda = tmp;

        ts >> tmp;
        _threshold[0] = tmp * M_PI / 180.; // dead zone limit for pronosup, in rad.

        ts >> tmp;
        _threshold[1] = tmp * M_PI / 180; // dead zone limit for wrist flex, in rad.

        ts >> tmp;
        _threshold[2] = tmp * M_PI / 180; // dead zone limit for elbow flex, in rad.
    }
}

void GeneralFormulation::displayPin()
{
    int pin_down_value = digitalRead(_pin_down);
    int pin_up_value = digitalRead(_pin_up);
    debug() << "PinUp: " << pin_up_value;
    debug() << "PinDown: " << pin_down_value;
}

bool GeneralFormulation::setup()
{
    debug() << "Begin setup \n";
    // Check for calibration
    if ((_robot->joints.elbow_flexion->is_calibrated() == false) || (_robot->joints.wrist_flexion->is_calibrated() == false) || (_robot->joints.wrist_pronation->is_calibrated() == false)) {
        debug() << "Actuators not calibrated";
        return false;
    }

    _robot->joints.wrist_pronation->set_encoder_position(0);
    std::string filename("GalF");
    std::string suffix;

    int cnt = 0;
    std::string extension(".txt");
    do {
        ++cnt;
        suffix = "_" + std::to_string(cnt);
    } while (std::filesystem::exists(filename + suffix + extension));

    _file = std::ofstream(filename + suffix + extension);
    if (!_file.good()) {
        critical() << "Failed to open" << (filename + suffix + extension);
        return false;
    }
    _need_to_write_header = true;
    _start_time = clock::now();

    _cnt = 0;
    theta[0] = 0.;
    theta[1] = 0.;
    theta[2] = 0.;
    theta[3] = 0.;
    return true;
}

void GeneralFormulation::loop(double, clock::time_point time)
{
    debug() << "Begin loop\n";
    int init_cnt = 10;
    double timeWithDelta = (time - _start_time).count();

    receiveData();
    debug() << "lhand: " << l[0] << "\n";
    debug() << "lambda: " << _lambda << "\n";
    debug() << "threshold WPS: " << _threshold[0] << "\n";
    debug() << "threshold WFE: " << _threshold[1] << "\n";
    debug() << "threshold elbow: " << _threshold[2] << "\n";

    _robot->sensors.optitrack->update();
    optitrack_data_t data = _robot->sensors.optitrack->get_last_data();
    debug() << "nbRigid Bodies: " << data.nRigidBodies << "\n";

    double debugData[60];

    /// WRITE FILE HEADERS
    if (_need_to_write_header) {
        _file << " time, pinUp, pinDown,";
        _file << " qBras.w, qBras.x, qBras.y, qBras.z, qTronc.w, qTronc.x, qTronc.y, qTronc.z,";
        _file << " qFA.w, qFA.x, qFA.y, qFA.z,";
        _file << " to complete";
        _file << " nbRigidBodies";
        for (int i = 0; i < data.nRigidBodies; i++) {
            _file << ", ID, bTrackingValid, fError, qw, qx, qy, qz, x, y, z";
        }
        _file << "\r\n";
        _need_to_write_header = false;
    }

    ///GET DATA
    /// OPTITRACK
    int index_acromion = -1, index_hip = -1, index_hand = -1;
    Eigen::Vector3d posA = Eigen::Vector3d::Zero(), posHip = Eigen::Vector3d::Zero();
    Eigen::Quaterniond qHip;
    qHip.w() = 0.;
    qHip.x() = 0.;
    qHip.y() = 0.;
    qHip.z() = 0.;
    Eigen::Quaterniond qHand;
    qHand.w() = 0.;
    qHand.x() = 0.;
    qHand.y() = 0.;
    qHand.z() = 0.;

    for (int i = 0; i < data.nRigidBodies; i++) {
        if (data.rigidBodies[i].ID == 3) {
            posA[0] = data.rigidBodies[i].x * 100;
            posA[1] = data.rigidBodies[i].y * 100;
            posA[2] = data.rigidBodies[i].z * 100;
            index_acromion = i;
        } else if (data.rigidBodies[i].ID == 7) {
            posHip[0] = data.rigidBodies[i].x * 100;
            posHip[1] = data.rigidBodies[i].y * 100;
            posHip[2] = data.rigidBodies[i].z * 100;
            qHip.w() = data.rigidBodies[i].qw;
            qHip.x() = data.rigidBodies[i].qx;
            qHip.y() = data.rigidBodies[i].qy;
            qHip.z() = data.rigidBodies[i].z;
            index_hip = i;
        } else if (data.rigidBodies[i].ID == 4) {
            qHand.w() = data.rigidBodies[i].qw;
            qHand.x() = data.rigidBodies[i].qx;
            qHand.y() = data.rigidBodies[i].qy;
            qHand.z() = data.rigidBodies[i].z;
            index_hand = i;
        }
    }

    /// WRIST
    double pronoSupEncoder = _robot->joints.wrist_pronation->read_encoder_position();
    double wristFlexEncoder = _robot->joints.wrist_flexion->read_encoder_position();
    /// ELBOW
    double elbowEncoder = _robot->joints.elbow_flexion->read_encoder_position();
    theta[1] = pronoSupEncoder / _robot->joints.wrist_pronation->r_incs_per_deg();
    theta[2] = wristFlexEncoder / _robot->joints.wrist_flexion->r_incs_per_deg();
    theta[3] = elbowEncoder / _robot->joints.elbow_flexion->r_incs_per_deg();
    debug() << "theta(deg): " << theta[1] << ", " << theta[2] << ", " << theta[3] << "\r\n";
    theta = theta * M_PI / 180;
    debug() << "theta(rad): " << theta[1] << ", " << theta[2] << ", " << theta[3] << "\r\n";
    /// IMU
    double qBras[4], qTronc[4], qFA[4];
    //    _robot->sensors.white_imu->get_quat(qBras);
    _robot->sensors.red_imu->get_quat(qTronc);
    //    _robot->sensors.yellow_imu->get_quat(qFA);
    /// PIN PUSH-BUTTONS CONTROL
    int pin_down_value = digitalRead(_pin_down);
    int pin_up_value = digitalRead(_pin_up);

    /// CONTROL LOOP
    if (_cnt == 0) {
        _lawJ.initialization(posA, qHip, 1 / period());
    } else if (_cnt <= init_cnt) {
        _lawJ.initialPositions(posA, posHip, qHip, _cnt, init_cnt);
    } else {
        _lawJ.rotationMatrices(qHand, qHip, _cnt, init_cnt);
        _lawJ.updateFrames(theta, l);
        _lawJ.controlLaw(posA, _lambda, _threshold);

        Eigen::Matrix<double, nbLinks, 1, Eigen::DontAlign> thetaDot_toSend = _lawJ.returnthetaDot_deg();
        debug() << "pronation vel :" << thetaDot_toSend[1] << "\n";
        debug() << "wrist flex vel :" << thetaDot_toSend[2] << "\n";
        debug() << "elbow flex vel :" << thetaDot_toSend[3] << "\n";
        //        _robot->joints.wrist_pronation->set_velocity_safe(thetaDot_toSend[1]);
        //        _robot->joints.wrist_flexion->set_velocity_safe(thetaDot_toSend[2]);
        //        _robot->joints.elbow_flexion->set_velocity_safe(thetaDot_toSend[3]);
    }

    _lawJ.writeDebugData(debugData, theta);
    /// WRITE DATA
    _file << timeWithDelta << ' ' << pin_down_value << ' ' << pin_up_value << ' ' << _Lua << ' ' << _Lfa << ' ' << _lwrist << ' ' << _lhand;
    _file << ' ' << _lambda << ' ' << _threshold[0] << ' ' << _threshold[1] << ' ' << _threshold[2];
    //    _file << ' ' << qBras[0] << ' ' << qBras[1] << ' ' << qBras[2] << ' ' << qBras[3] << ' ' << qTronc[0] << ' ' << qTronc[1] << ' ' << qTronc[2] << ' ' << qTronc[3];
    //    _file << ' ' << qFA[0] << ' ' << qFA[1] << ' ' << qFA[2] << ' ' << qFA[3];
    for (int i = 0; i < 60; i++) {
        _file << ' ' << debugData[i];
    }
    //    _file << ' ' << debugData[0] << ' ' << debugData[1] << ' ' << debugData[2] << ' ' << debugData[3];
    _file << ' ' << pronoSupEncoder << ' ' << wristFlexEncoder << ' ' << elbowEncoder;
    _file << ' ' << data.nRigidBodies;

    for (int i = 0; i < data.nRigidBodies; i++) {
        _file << ' ' << data.rigidBodies[i].ID << ' ' << data.rigidBodies[i].bTrackingValid << ' ' << data.rigidBodies[i].fError;
        _file << ' ' << data.rigidBodies[i].qw << ' ' << data.rigidBodies[i].qx << ' ' << data.rigidBodies[i].qy << ' ' << data.rigidBodies[i].qz;
        _file << ' ' << data.rigidBodies[i].x << ' ' << data.rigidBodies[i].y << ' ' << data.rigidBodies[i].z;
    }
    _file << std::endl;

    ++_cnt;
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() << "ms" << std::endl;
}

void GeneralFormulation::cleanup()
{
    _robot->joints.wrist_pronation->forward(0);
    _robot->joints.wrist_flexion->forward(0);
    _robot->joints.elbow_flexion->move_to(0, 20);
    _robot->joints.hand->release_ownership();
    _file.close();
}
