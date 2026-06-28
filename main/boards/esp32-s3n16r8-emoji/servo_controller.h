/**
 * @file servo_controller.h
 * @brief 舵机控制模块头文件
 * 
 * 本文件定义了舵机控制模块的接口，用于控制水平和垂直舵机的运动
 */

#pragma once

#include "board_config.h"
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>

// 舵机相关常量，使用board_config.h中已定义的常量
// 注意：SERVO_CENTER_X、SERVO_CENTER_Y、SERVO_MIN_X、SERVO_MAX_X、SERVO_MIN_Y、SERVO_MAX_Y、SERVO_STEP、SERVO_DELAY
// 已在board_config.h中定义

// 定义舵机偏移量，用于HeadRoll等函数
#define SERVO_OFFSET_X 40
#define SERVO_OFFSET_Y 25

/**
 * @class ServoController
 * @brief 舵机控制类，负责管理和控制舵机的运动
 */
class ServoController {
public:
    /**
     * @brief 构造函数
     */
    ServoController();
    
    /**
     * @brief 析构函数
     */
    ~ServoController();
    
    /**
     * @brief 初始化舵机控制器
     */
    void Initialize();
    
    /**
     * @brief 设置舵机角度
     * @param channel 舵机通道 (0:水平, 1:垂直)
     * @param angle 角度 (0-180)
     */
    void SetServoAngle(int channel, int angle);
    
    /**
     * @brief 移动头部
     * @param x_offset X轴偏移量
     * @param y_offset Y轴偏移量
     * @param servo_delay 舵机移动延迟
     */
    void HeadMove(int x_offset, int y_offset, int servo_delay = SERVO_DELAY);
    
    /**
     * @brief 头部向上移动
     * @param offset 偏移量
     */
    void HeadUp(int offset = 20);
    
    /**
     * @brief 头部向下移动
     * @param offset 偏移量
     */
    void HeadDown(int offset = 20);
    
    /**
     * @brief 头部向左移动
     * @param offset 偏移量
     */
    void HeadLeft(int offset = 20);
    
    /**
     * @brief 头部向右移动
     * @param offset 偏移量
     */
    void HeadRight(int offset = 20);
    
    /**
     * @brief 头部居中
     * @param servo_delay 舵机移动延迟
     */
    void HeadCenter(int servo_delay = SERVO_DELAY);
    
    /**
     * @brief 头部点头
     * @param servo_delay 舵机移动延迟
     */
    void HeadNod(int servo_delay = 1);
    
    /**
     * @brief 头部摇头
     * @param servo_delay 舵机移动延迟
     */
    void HeadShake(int servo_delay = 1);
    
    /**
     * @brief 头部转圈
     * @param servo_delay 舵机移动延迟
     */
    void HeadRoll(int servo_delay = SERVO_DELAY);
    
    /**
     * @brief 获取当前水平角度
     * @return 水平角度
     */
    int GetCurrentXAngle() const { return current_x_angle_; }
    
    /**
     * @brief 获取当前垂直角度
     * @return 垂直角度
     */
    int GetCurrentYAngle() const { return current_y_angle_; }

private:
    // 舵机角度
    int current_x_angle_ = SERVO_CENTER_X;
    int current_y_angle_ = SERVO_CENTER_Y;
};
