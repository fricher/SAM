#ifndef TOUCHBIONICSHAND_H
#define TOUCHBIONICSHAND_H

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

/**
 * @brief The TouchBionicsHand class allows to control the TouchBionics hands.
 */
class TouchBionicsHand {
public:
    enum ACTION {
        STOP,
        THUMB_CLOSING,
        THUMB_OPENING,
        FOREFINGER_CLOSING,
        FOREFINGER_OPENING,
        MIDDLEFINGER_CLOSING,
        MIDDLEFINGER_OPENING,
        RINGFINGER_CLOSING,
        RINGFINGER_OPENING,
        LITTLEFINGER_CLOSING,
        LITTLEFINGER_OPENING,
        PINCH_CLOSING,
        PINCH_OPENING,
        HAND_CLOSING,
        HAND_OPENING,
        THUMB_EXT_CLOSING,
        THUMB_EXT_OPENING,
        THUMB_INT_CLOSING,
        THUMB_INT_OPENING,
        PINCH_CLOSING_ALL,
        PINCH_OPENING_ALL,
        HAND_CLOSING_ALL,
        HAND_OPENING_ALL,
        ALL_BUT_PINCH_CLOSING,
        ALL_BUT_PINCH_OPENING,
        TRIPLE_PINCH_CLOSING,
        TRIPLE_PINCH_OPENING,
    };

    enum POSTURE {
        HAND_POSTURE,
        PINCH_POSTURE,
        TRIPLE_PINCH_POSTURE,
        ONLY_THUMB_POSTURE,
        GLOVE_POSTURE = 24,
    };

    TouchBionicsHand(char* filename);
    ~TouchBionicsHand();
    void move(int action);
    void setPosture(POSTURE posture);
    int getSpeed() { return _speed; }
    void setSpeed(int newSpeed) { _speed = newSpeed; }

private:
    int _f;
    int _speed;
    char _cmd[13];
    int _last_action;
    int _count;
    const int _NB_OF_CMD_TO_RESEND = 3;
};

#endif // TOUCHBIONICSHAND_H