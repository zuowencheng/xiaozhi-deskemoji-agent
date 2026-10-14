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
#include <freertos/semphr.h>
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
     * @brief 开机自检：左 15° → 右 15° → 回中（约 0.6s）
     *
     * 让主人一眼看到"舵机是活的"，同时用于排查舵机供电是否足够。
     * 是否调用由 board_config.h 的 SERVO_BOOT_SELFTEST 控制。
     */
    void SelfTest();
    
    /**
     * @brief 设置舵机角度
     * @param channel 舵机通道 (0:水平, 1:垂直)
     * @param angle 角度 (0-180)
     * @return 是否真正写入；false = 仲裁让位（控制权被手势持着）
     */
    bool SetServoAngle(int channel, int angle);
    
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

    // ---- 舵机仲裁层（2026-10-13 新增）----
    // 背景：手势任务和自动表情动画都在直接写舵机角度，没有任何互斥，
    // 同时跑就互相打架（表现：舵机在动但不跟手势）。
    // 规则：所有角度写入经过递归互斥锁串行化；手势处理期间持锁，
    // 自动动画抢不到锁（50ms 超时）即让位收工，保证"手势优先"。
    // 用递归锁是因为手势持锁后调的 HeadLeft() 内部还会再过 SetServoAngle。
    bool AcquireControl(uint32_t timeout_ms);
    void ReleaseControl();

private:
    // 舵机角度
    int current_x_angle_ = SERVO_CENTER_X;
    int current_y_angle_ = SERVO_CENTER_Y;

    // 舵机写入递归互斥锁（nullptr = 未初始化，退化为无锁直写）
    SemaphoreHandle_t servo_mutex_ = nullptr;
};
