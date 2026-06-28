/**
 * @file servo_controller.cc
 * @brief 舵机控制模块实现
 */

#include "servo_controller.h"
#include <esp_log.h>
#include <algorithm>

#define TAG "ServoController"

ServoController::ServoController() {
}

ServoController::~ServoController() {
}

void ServoController::Initialize() {
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

    // 设置舵机初始位置
    SetServoAngle(0, SERVO_CENTER_X);
    SetServoAngle(1, SERVO_CENTER_Y);
}

void ServoController::SetServoAngle(int channel, int angle) {
    // 限制角度范围
    if (channel == 0) { // 水平舵机
        angle = std::max(SERVO_MIN_X, std::min(SERVO_MAX_X, angle));
        current_x_angle_ = angle;
    } else { // 垂直舵机
        angle = std::max(SERVO_MIN_Y, std::min(SERVO_MAX_Y, angle));
        current_y_angle_ = angle;
    }

    // 计算PWM占空比
    uint32_t pulse_width = SERVO_MIN_PULSEWIDTH + (angle * (SERVO_MAX_PULSEWIDTH - SERVO_MIN_PULSEWIDTH)) / 180;
    uint32_t duty = (pulse_width * ((1 << LEDC_TIMER_BIT_WIDTH) - 1)) / (1000000 / LEDC_FREQUENCY);

    // 设置PWM占空比
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, servo_channels[channel], duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, servo_channels[channel]));
}

void ServoController::HeadMove(int x_offset, int y_offset, int servo_delay) {
    // 获取当前角度
    int x_angle = current_x_angle_;
    int y_angle = current_y_angle_;
    
    // 计算目标角度
    int to_x_angle = std::max(SERVO_MIN_X, std::min(SERVO_MAX_X, x_angle + x_offset));
    int to_y_angle = std::max(SERVO_MIN_Y, std::min(SERVO_MAX_Y, y_angle + y_offset));
    
    // 逐步移动到目标位置，完全参考boardemoji.ino中的实现
    while (x_angle != to_x_angle || y_angle != to_y_angle) {
        if (x_angle != to_x_angle) {
            x_angle += (to_x_angle > x_angle ? SERVO_STEP : -SERVO_STEP);
            SetServoAngle(0, x_angle);
        }
        if (y_angle != to_y_angle) {
            y_angle += (to_y_angle > y_angle ? SERVO_STEP : -SERVO_STEP);
            SetServoAngle(1, y_angle);
        }
        vTaskDelay(pdMS_TO_TICKS(servo_delay));
    }
}

void ServoController::HeadNod(int servo_delay) {
    // 完全参考boardemoji.ino中的实现：使用相对移动，3次点头
    for (int i = 0; i < 3; i++) {
        HeadMove(0, 40, servo_delay);  // 向下移动40度
        HeadMove(0, -40, servo_delay); // 向上移动40度
    }
}

void ServoController::HeadShake(int servo_delay) {
    // 完全参考boardemoji.ino中的实现：使用相对移动序列
    HeadMove(-20, 0, servo_delay);  // 向左移动20度
    HeadMove(40, 0, servo_delay);   // 向右移动40度（相对当前位置）
    HeadMove(-40, 0, servo_delay);  // 向左移动40度
    HeadMove(40, 0, servo_delay);   // 向右移动40度
    HeadMove(-40, 0, servo_delay);  // 向左移动40度
    HeadMove(20, 0, servo_delay);   // 向右移动20度，回到中心
}

void ServoController::HeadRoll(int servo_delay) {
    // 完全参考boardemoji.ino中的实现，使用原版的X_OFFSET和Y_OFFSET值
    // 原版：X_OFFSET=25, Y_OFFSET=50，这里使用SERVO_OFFSET_X=40, SERVO_OFFSET_Y=25
    HeadCenter();
    HeadDown(SERVO_OFFSET_Y/2+5);  // 向下移动17.5度
    HeadMove(SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, servo_delay);   // 右上
    HeadMove(-SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, servo_delay);  // 左上
    HeadMove(-SERVO_OFFSET_X, SERVO_OFFSET_Y/2, servo_delay);   // 左下
    HeadMove(SERVO_OFFSET_X, SERVO_OFFSET_Y/2, servo_delay);    // 右下
    HeadMove(-SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, servo_delay);  // 左上
    HeadMove(SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, servo_delay);   // 右上
    HeadMove(SERVO_OFFSET_X, SERVO_OFFSET_Y/2, servo_delay);    // 右下
    HeadMove(-SERVO_OFFSET_X, SERVO_OFFSET_Y/2, servo_delay);   // 左下
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

void ServoController::SmoothMoveTo(int target_x_angle, int target_y_angle, int servo_delay) {
    // 限制目标角度范围
    target_x_angle = std::max(SERVO_MIN_X, std::min(SERVO_MAX_X, target_x_angle));
    target_y_angle = std::max(SERVO_MIN_Y, std::min(SERVO_MAX_Y, target_y_angle));
    
    // 计算移动步长，基于移动速度
    int step_size = move_speed_;
    
    // 逐步移动到目标位置
    while (current_x_angle_ != target_x_angle || current_y_angle_ != target_y_angle) {
        if (current_x_angle_ != target_x_angle) {
            int diff = target_x_angle - current_x_angle_;
            int step = (diff > 0) ? std::min(step_size, diff) : std::max(-step_size, diff);
            current_x_angle_ += step;
            SetServoAngle(0, current_x_angle_);
        }
        if (current_y_angle_ != target_y_angle) {
            int diff = target_y_angle - current_y_angle_;
            int step = (diff > 0) ? std::min(step_size, diff) : std::max(-step_size, diff);
            current_y_angle_ += step;
            SetServoAngle(1, current_y_angle_);
        }
        vTaskDelay(pdMS_TO_TICKS(servo_delay));
    }
}

void ServoController::SetMoveSpeed(int speed) {
    // 限制速度范围在1-10之间
    move_speed_ = std::max(1, std::min(10, speed));
    ESP_LOGI(TAG, "Servo move speed set to: %d", move_speed_);
}
