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
    // 确保最小延迟值
    int actual_delay = std::max(10, servo_delay);
    
    // 根据延迟值调整点头频率
    int delay_ms = actual_delay * 15; // 将延迟值转换为毫秒
    
    // 记录初始位置
    int initial_y = current_y_angle_;
    
    // 直接设置舵机角度，不使用HeadMove的逐步移动
    for (int i = 0; i < 3; i++) {
        // 向下点头
        SetServoAngle(1, SERVO_CENTER_Y + 20);
        current_y_angle_ = SERVO_CENTER_Y + 20;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));  // 使用参数控制的延时
        
        // 向上点头
        SetServoAngle(1, SERVO_CENTER_Y - 20);
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
    
    // 记录初始位置
    int initial_x = current_x_angle_;
    
    // 直接设置舵机角度，不使用HeadMove的逐步移动
    for (int i = 0; i < 2; i++) {
        // 向左摇头
        SetServoAngle(0, SERVO_CENTER_X - 20);
        current_x_angle_ = SERVO_CENTER_X - 20;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));  // 使用参数控制的延时
        
        // 向右摇头
        SetServoAngle(0, SERVO_CENTER_X + 20);
        current_x_angle_ = SERVO_CENTER_X + 20;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));  // 使用参数控制的延时
        
        // 再向左摇头
        SetServoAngle(0, SERVO_CENTER_X - 20);
        current_x_angle_ = SERVO_CENTER_X - 20;
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
