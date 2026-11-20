/*
 * ff-bgm1x: libretro 센서 인터페이스 (설명은 sensors.h)
 */

#include "sensors.h"

#include <algorithm>

#include "log.h"

namespace libretrodroid {

namespace {

const int SENSOR_TYPES[] = {
    ASENSOR_TYPE_ACCELEROMETER,
    ASENSOR_TYPE_GYROSCOPE,
    ASENSOR_TYPE_LIGHT,
};

// 기기 기준 (x, y) 를 화면 기준으로 바꾼다 (Surface.ROTATION_0..3)
void toScreenAxes(int rotation, float x, float y, float& outX, float& outY) {
    switch (rotation) {
        case 1:
            outX = -y;
            outY = x;
            break;
        case 2:
            outX = -x;
            outY = -y;
            break;
        case 3:
            outX = y;
            outY = -x;
            break;
        default:
            outX = x;
            outY = y;
            break;
    }
}

}

Sensors& Sensors::getInstance() {
    static Sensors instance;
    return instance;
}

bool Sensors::callbackSetSensorState(unsigned port, enum retro_sensor_action action, unsigned rate) {
    if (port != 0) return false;
    return getInstance().setState(action, rate);
}

float Sensors::callbackGetSensorInput(unsigned port, unsigned id) {
    if (port != 0) return 0.0f;
    return getInstance().getInput(id);
}

void Sensors::setScreenRotation(int rotation) {
    screenRotation = rotation;
}

bool Sensors::ensureQueue() {
    if (queue) return true;

    if (!manager) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        manager = ASensorManager_getInstance();
#pragma clang diagnostic pop
    }
    if (!manager) {
        LOGE("Sensors: cannot get sensor manager");
        return false;
    }

    // 큐는 처음 쓰는 스레드(코어가 센서를 켜는 에뮬레이션 스레드)의 looper 에 붙인다
    looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    queue = ASensorManager_createEventQueue(manager, looper, 0, nullptr, nullptr);
    if (!queue) {
        LOGE("Sensors: cannot create event queue");
        return false;
    }
    return true;
}

bool Sensors::enable(Kind kind, unsigned rate) {
    if (enabled[kind]) return true;
    if (!ensureQueue()) return false;

    if (!sensors[kind]) {
        sensors[kind] = ASensorManager_getDefaultSensor(manager, SENSOR_TYPES[kind]);
    }
    if (!sensors[kind]) {
        LOGI("Sensors: sensor type %d not available", SENSOR_TYPES[kind]);
        return false;
    }
    if (ASensorEventQueue_enableSensor(queue, sensors[kind]) < 0) {
        LOGE("Sensors: cannot enable sensor type %d", SENSOR_TYPES[kind]);
        return false;
    }
    if (rate > 0) {
        int32_t periodUs = std::max(ASensor_getMinDelay(sensors[kind]), (int32_t) (1000000 / rate));
        ASensorEventQueue_setEventRate(queue, sensors[kind], periodUs);
    }
    enabled[kind] = true;
    LOGI("Sensors: enabled sensor type %d", SENSOR_TYPES[kind]);
    return true;
}

void Sensors::disable(Kind kind) {
    if (!enabled[kind]) return;
    ASensorEventQueue_disableSensor(queue, sensors[kind]);
    enabled[kind] = false;
}

bool Sensors::setState(enum retro_sensor_action action, unsigned rate) {
    std::lock_guard<std::mutex> lock(mutex);
    switch (action) {
        case RETRO_SENSOR_ACCELEROMETER_ENABLE:
            return enable(ACCELEROMETER, rate);
        case RETRO_SENSOR_ACCELEROMETER_DISABLE:
            disable(ACCELEROMETER);
            return true;
        case RETRO_SENSOR_GYROSCOPE_ENABLE:
            return enable(GYROSCOPE, rate);
        case RETRO_SENSOR_GYROSCOPE_DISABLE:
            disable(GYROSCOPE);
            return true;
        case RETRO_SENSOR_ILLUMINANCE_ENABLE:
            return enable(ILLUMINANCE, rate);
        case RETRO_SENSOR_ILLUMINANCE_DISABLE:
            disable(ILLUMINANCE);
            return true;
        default:
            return false;
    }
}

void Sensors::poll() {
    if (!queue) return;
    ALooper_pollOnce(0, nullptr, nullptr, nullptr);

    const int rotation = screenRotation;
    ASensorEvent event {};
    while (ASensorEventQueue_getEvents(queue, &event, 1) > 0) {
        switch (event.type) {
            case ASENSOR_TYPE_ACCELEROMETER:
                toScreenAxes(rotation, event.acceleration.x, event.acceleration.y, values[0], values[1]);
                values[2] = event.acceleration.z;
                break;
            case ASENSOR_TYPE_GYROSCOPE:
                toScreenAxes(rotation, event.vector.x, event.vector.y, values[3], values[4]);
                values[5] = event.vector.z;
                break;
            case ASENSOR_TYPE_LIGHT:
                values[6] = event.light;
                break;
            default:
                break;
        }
    }
}

float Sensors::getInput(unsigned id) {
    std::lock_guard<std::mutex> lock(mutex);
    if (id > RETRO_SENSOR_ILLUMINANCE) return 0.0f;
    poll();
    return values[id];
}

void Sensors::deinit() {
    std::lock_guard<std::mutex> lock(mutex);
    if (queue) {
        for (int kind = 0; kind < KIND_COUNT; kind++) {
            disable(static_cast<Kind>(kind));
        }
        ASensorManager_destroyEventQueue(manager, queue);
        queue = nullptr;
    }
    looper = nullptr;
    sensors = {};
    values = {};
}

}
