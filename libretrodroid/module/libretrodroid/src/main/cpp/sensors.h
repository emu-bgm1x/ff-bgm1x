/*
 * ff-bgm1x: libretro 센서 인터페이스 (RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE).
 * 코어가 켜 달라고 한 센서(가속도, 자이로, 조도)만 NDK 센서 API 로 켜고,
 * 코어가 값을 읽을 때 쌓인 이벤트를 꺼내 최신 값을 돌려준다.
 * 값은 Android 단위 그대로 (가속도 m/s^2, 자이로 rad/s, 조도 lux).
 * 가속도와 자이로의 X/Y 는 화면 회전에 맞춰 화면 기준 축으로 바꾼다.
 */

#ifndef LIBRETRODROID_SENSORS_H
#define LIBRETRODROID_SENSORS_H

#include <array>
#include <atomic>
#include <mutex>

#include <android/looper.h>
#include <android/sensor.h>

#include "../../libretro-common/include/libretro.h"

namespace libretrodroid {

class Sensors {
public:
    static Sensors& getInstance();

    // 코어가 부르는 함수 (libretro 시그니처)
    static bool callbackSetSensorState(unsigned port, enum retro_sensor_action action, unsigned rate);
    static float callbackGetSensorInput(unsigned port, unsigned id);

    // Surface.ROTATION_0..3 (UI 스레드에서 알려 준다)
    void setScreenRotation(int rotation);
    // 게임을 닫을 때 모든 센서를 끄고 큐를 지운다
    void deinit();

private:
    enum Kind { ACCELEROMETER = 0, GYROSCOPE = 1, ILLUMINANCE = 2, KIND_COUNT = 3 };

    bool setState(enum retro_sensor_action action, unsigned rate);
    float getInput(unsigned id);
    bool ensureQueue();
    bool enable(Kind kind, unsigned rate);
    void disable(Kind kind);
    void poll();

    std::mutex mutex;
    ASensorManager* manager = nullptr;
    ALooper* looper = nullptr;
    ASensorEventQueue* queue = nullptr;
    std::array<const ASensor*, KIND_COUNT> sensors {};
    std::array<bool, KIND_COUNT> enabled {};

    // 화면 기준으로 바꾼 값: 가속도 xyz, 자이로 xyz, 조도
    std::array<float, 7> values {};
    std::atomic<int> screenRotation {0};
};

}

#endif //LIBRETRODROID_SENSORS_H
