/**
 * @file servo_controller.cc
 * @brief 舵机控制模块实现
 */

#include "servo_controller.h"
#include <esp_log.h>

#define TAG "ServoController"

ServoController::ServoController() {
}

ServoController::~ServoController() {
}

void ServoController::Initialize() {
    ESP_LOGI(TAG, "舵机初始化：水平=GPIO%d(通道0) 垂直=GPIO%d(通道1)，%dHz，脉宽 %d-%dus",
             (int)servo_pins[0], (int)servo_pins[1], LEDC_FREQUENCY,
             SERVO_MIN_PULSEWIDTH, SERVO_MAX_PULSEWIDTH);

    // 配置LEDC定时器
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_TIMER_BIT_WIDTH,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 配置LEDC通道
    for (int i = 0; i < SERVO_CHANNEL_COUNT; i++) {
        ledc_channel_config_t ledc_channel = {
            .gpio_num = servo_pins[i],
            .speed_mode = LEDC_MODE,
            .channel = servo_channels[i],
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER,
            .duty = 0,
            .hpoint = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    }

    // 创建舵机写入递归互斥锁（仲裁层，见头文件注释）
    servo_mutex_ = xSemaphoreCreateRecursiveMutex();
    if (servo_mutex_ == nullptr) {
        ESP_LOGW(TAG, "舵机互斥锁创建失败，退化为无锁直写（不致命，但会丢仲裁）");
    }

    // 设置舵机初始位置
    SetServoAngle(0, SERVO_CENTER_X);
    SetServoAngle(1, SERVO_CENTER_Y);
    ESP_LOGI(TAG, "舵机已归中：水平=%d° 垂直=%d°（若此前不在中位，这一刻会看到舵机动一下）",
             current_x_angle_, current_y_angle_);
}

void ServoController::SelfTest() {
    // 开机可见动作：左 15° → 右 15° → 回中，全程约 0.6s。
    // 作用有两个：① 让主人一眼确认"舵机是活的"；
    //            ② 排查供电 —— 肉眼看不动但日志正常，基本就是舵机 5V 供电不够。
    ESP_LOGI(TAG, "舵机自检开始：左15° -> 右15° -> 回中");
    HeadLeft(15);
    HeadRight(15);
    HeadCenter();
    ESP_LOGI(TAG, "舵机自检结束（水平=%d° 垂直=%d°）。若肉眼看不动，请查舵机 5V 供电/接线",
             current_x_angle_, current_y_angle_);
}


bool ServoController::AcquireControl(uint32_t timeout_ms) {
    if (servo_mutex_ == nullptr) {
        return true;  // 无锁退化模式：视为永远拿到
    }
    if (xSemaphoreTakeRecursive(servo_mutex_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return true;
    }
    ESP_LOGW(TAG, "舵机控制权获取超时(%ums)，仍按当前调用继续", (unsigned)timeout_ms);
    return false;
}

void ServoController::ReleaseControl() {
    if (servo_mutex_ == nullptr) {
        return;
    }
    xSemaphoreGiveRecursive(servo_mutex_);
}

bool ServoController::SetServoAngle(int channel, int angle) {
    // 仲裁：自动动画与手势争舵机时，抢不到锁（持锁者=手势）就放弃本次写入，
    // 由调用方的循环检测返回值 break，实现"手势优先、动画让位"。
    bool locked = false;
    if (servo_mutex_ != nullptr) {
        if (xSemaphoreTakeRecursive(servo_mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
            return false;
        }
        locked = true;
    }

    // 限制角度范围
    int physical_angle = angle;
    if (channel == 0) { // 水平舵机
        // ⚠️ 2026-10-14：X 轴装配取反（见 board_config.h SERVO_X_INVERT）。
        // 约定同 Y：调用方在"逻辑坐标系"（+offset = 头朝右），写 PWM 时镜像。
#if SERVO_X_INVERT
        angle = std::max(SERVO_MIN_X, std::min(SERVO_MAX_X, angle));
        physical_angle = (SERVO_MAX_X + SERVO_MIN_X) - angle;
        physical_angle = std::max(SERVO_MIN_X, std::min(SERVO_MAX_X, physical_angle));
#else
        angle = std::max(SERVO_MIN_X, std::min(SERVO_MAX_X, angle));
        physical_angle = angle;
#endif
        current_x_angle_ = angle;
    } else { // 垂直舵机
        // ⚠️ 2026-10-14：Y 轴装配取反（见 board_config.h SERVO_Y_INVERT）。
        // 约定：current_y_angle_ / HeadUp / HeadDown / HeadCenter 全都工作在
        // "逻辑坐标系"（+25 = 头朝下）；写入舵机那一刻再做镜像翻成物理角，
        // 让 PWM 大角度对应舵机的"上"。这样调用方完全无感。
#if SERVO_Y_INVERT
        angle = std::max(SERVO_MIN_Y, std::min(SERVO_MAX_Y, angle));
        physical_angle = (SERVO_MAX_Y + SERVO_MIN_Y) - angle;
        physical_angle = std::max(SERVO_MIN_Y, std::min(SERVO_MAX_Y, physical_angle));
#else
        angle = std::max(SERVO_MIN_Y, std::min(SERVO_MAX_Y, angle));
        physical_angle = angle;
#endif
        current_y_angle_ = angle;
    }

    // 计算PWM占空比（用物理角度）
    uint32_t pulse_width = SERVO_MIN_PULSEWIDTH + (physical_angle * (SERVO_MAX_PULSEWIDTH - SERVO_MIN_PULSEWIDTH)) / 180;
    uint32_t duty = (pulse_width * ((1 << LEDC_TIMER_BIT_WIDTH) - 1)) / (1000000 / LEDC_FREQUENCY);

    // 设置PWM占空比
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, servo_channels[channel], duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, servo_channels[channel]));

    if (locked) {
        xSemaphoreGiveRecursive(servo_mutex_);
    }
    return true;
}

void ServoController::HeadMove(int x_offset, int y_offset, int servo_delay) {
    // 获取当前角度
    int x_angle = current_x_angle_;
    int y_angle = current_y_angle_;

    // Y 轴逻辑坐标系约定（2026-10-14）：+offset = 头朝下，-offset = 头朝上。
    // 物理装配取反（Y 反装）由 SetServoAngle(1,..) 在写入那一刻镜像，
    // 这里 current_y_angle_ 始终是"逻辑角度"，调用方代码完全无感。
    int to_x_angle = std::max(SERVO_MIN_X, std::min(SERVO_MAX_X, x_angle + x_offset));
    int to_y_angle = std::max(SERVO_MIN_Y, std::min(SERVO_MAX_Y, y_angle + y_offset));

    // 逐步移动到目标位置：循环里 y_angle 跟着 to_y_angle 走（逻辑坐标系）
    while (x_angle != to_x_angle || y_angle != to_y_angle) {
        if (x_angle != to_x_angle) {
            x_angle += (to_x_angle > x_angle ? SERVO_STEP : -SERVO_STEP);
            if (!SetServoAngle(0, x_angle)) break;  // 仲裁让位：手势在控制，本动画收工
        }
        if (y_angle != to_y_angle) {
            y_angle += (to_y_angle > y_angle ? SERVO_STEP : -SERVO_STEP);
            if (!SetServoAngle(1, y_angle)) break;
        }
        vTaskDelay(pdMS_TO_TICKS(servo_delay));
    }
}

void ServoController::HeadNod(int servo_delay) {
    // 确保最小延迟值
    int actual_delay = std::max(10, servo_delay);
    
    // 根据延迟值调整点头频率
    int delay_ms = actual_delay * 15; // 将延迟值转换为毫秒
    
    // 记录初始位置
    int initial_y = current_y_angle_;
    
    // 直接设置舵机角度，不使用HeadMove的逐步移动
    for (int i = 0; i < 3; i++) {
        // 向下点头
        if (!SetServoAngle(1, SERVO_CENTER_Y + 20)) return;  // 仲裁让位即收工
        current_y_angle_ = SERVO_CENTER_Y + 20;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));  // 使用参数控制的延时

        // 向上点头
        if (!SetServoAngle(1, SERVO_CENTER_Y - 20)) return;
        current_y_angle_ = SERVO_CENTER_Y - 20;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));  // 使用参数控制的延时
    }
    
    // 恢复到中心位置
    SetServoAngle(1, SERVO_CENTER_Y);
    current_y_angle_ = SERVO_CENTER_Y;
}

void ServoController::HeadShake(int servo_delay) {
    // 确保最小延迟值
    int actual_delay = std::max(10, servo_delay);

    // 根据延迟值调整摇头频率
    int delay_ms = actual_delay * 15; // 将延迟值转换为毫秒

    // 摇头幅度（2026-10-14 主人反馈 ±20° 太小，加大到 ±30°）
    const int amplitude = SERVO_SHAKE_AMPLITUDE;

    // 记录初始位置
    int initial_x = current_x_angle_;

    // 直接设置舵机角度，不使用HeadMove的逐步移动
    for (int i = 0; i < 2; i++) {
        // 向左摇头
        if (!SetServoAngle(0, SERVO_CENTER_X - amplitude)) return;  // 仲裁让位即收工
        current_x_angle_ = SERVO_CENTER_X - amplitude;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));  // 使用参数控制的延时

        // 向右摇头
        if (!SetServoAngle(0, SERVO_CENTER_X + amplitude)) return;
        current_x_angle_ = SERVO_CENTER_X + amplitude;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));  // 使用参数控制的延时

        // 再向左摇头
        if (!SetServoAngle(0, SERVO_CENTER_X - amplitude)) return;
        current_x_angle_ = SERVO_CENTER_X - amplitude;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));  // 使用参数控制的延时
    }

    // 恢复到中心位置
    SetServoAngle(0, SERVO_CENTER_X);
    current_x_angle_ = SERVO_CENTER_X;
}

void ServoController::HeadRoll(int servo_delay) {
    // 完全参考boardemoji.ino中的实现
    HeadCenter();
    HeadDown(SERVO_OFFSET_Y/2+5);
    HeadMove(SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, servo_delay);
    HeadMove(-SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, servo_delay);
    HeadMove(-SERVO_OFFSET_X, SERVO_OFFSET_Y/2, servo_delay);
    HeadMove(SERVO_OFFSET_X, SERVO_OFFSET_Y/2, servo_delay);
    HeadMove(-SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, servo_delay);
    HeadMove(SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, servo_delay);
    HeadMove(SERVO_OFFSET_X, SERVO_OFFSET_Y/2, servo_delay);
    HeadMove(-SERVO_OFFSET_X, SERVO_OFFSET_Y/2, servo_delay);
    HeadCenter();
}

void ServoController::HeadUp(int offset) {
    HeadMove(0, -offset);
}

void ServoController::HeadDown(int offset) {
    HeadMove(0, offset);
}

void ServoController::HeadLeft(int offset) {
    HeadMove(-offset, 0);
}

void ServoController::HeadRight(int offset) {
    HeadMove(offset, 0);
}

void ServoController::HeadCenter(int servo_delay) {
    // 参考原始boardemoji.ino中的实现，通过HeadMove方法逐步移动到中心位置
    // 计算当前位置到中心位置的偏移量
    int x_offset = SERVO_CENTER_X - current_x_angle_;
    int y_offset = SERVO_CENTER_Y - current_y_angle_;
    
    // 通过HeadMove方法逐步移动到中心位置
    HeadMove(x_offset, y_offset, servo_delay);
}
