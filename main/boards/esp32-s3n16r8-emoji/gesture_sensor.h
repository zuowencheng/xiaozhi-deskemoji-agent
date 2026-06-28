/**
 * @file gesture_sensor.h
 * @brief PAJ7620U2手势识别传感器驱动头文件
 * 
 * 本文件定义了PAJ7620U2手势识别传感器的驱动接口，用于检测手势并触发相应的舵机动作
 */

#pragma once

#include "board_config.h"
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>

// PAJ7620U2手势识别传感器寄存器地址定义
#define PAJ7620_ADDR_BASE 0x00
#define PAJ7620_REGITER_BANK_SEL 0xEF
#define PAJ7620_BANK0 0
#define PAJ7620_BANK1 1

// Bank0寄存器地址
#define PAJ7620_ADDR_SUSPEND_CMD 0x3
#define PAJ7620_ADDR_GES_PS_DET_MASK_0 0x41
#define PAJ7620_ADDR_GES_PS_DET_MASK_1 0x42
#define PAJ7620_ADDR_GES_PS_DET_FLAG_0 0x43
#define PAJ7620_ADDR_GES_PS_DET_FLAG_1 0x44
#define PAJ7620_ADDR_STATE_INDICATOR 0x45
#define PAJ7620_ADDR_PS_HIGH_THRESHOLD 0x69
#define PAJ7620_ADDR_PS_LOW_THRESHOLD 0x6A
#define PAJ7620_ADDR_PS_APPROACH_STATE 0x6B
#define PAJ7620_ADDR_PS_RAW_DATA 0x6C

// 手势识别标志位定义
#define GES_RIGHT_FLAG 0x01
#define GES_LEFT_FLAG 0x02
#define GES_UP_FLAG 0x04
#define GES_DOWN_FLAG 0x08
#define GES_FORWARD_FLAG 0x10
#define GES_BACKWARD_FLAG 0x20
#define GES_CLOCKWISE_FLAG 0x40
#define GES_COUNT_CLOCKWISE_FLAG 0x80

#define GES_WAVE_FLAG 0x01

// 手势类型枚举
enum GestureType {
    GESTURE_NONE = 0,
    GESTURE_RIGHT = 1,
    GESTURE_LEFT = 2,
    GESTURE_UP = 4,
    GESTURE_DOWN = 8,
    GESTURE_FORWARD = 16,
    GESTURE_BACKWARD = 32,
    GESTURE_CLOCKWISE = 64,
    GESTURE_COUNT_CLOCKWISE = 128,
    GESTURE_WAVE = 256
};

// 手势状态枚举，用于连续手势检测
enum GestureState {
    STATE_IDLE,
    STATE_LEFT_RIGHT_SEQUENCE,  // 左右摆动序列
    STATE_UP_DOWN_SEQUENCE      // 上下摆动序列
};

/**
 * @class GestureSensor
 * @brief PAJ7620U2手势识别传感器类
 */
class GestureSensor {
public:
    /**
     * @brief 构造函数
     */
    GestureSensor();
    
    /**
     * @brief 析构函数
     */
    ~GestureSensor();
    
    /**
     * @brief 初始化手势传感器
     * @return 初始化结果，true表示成功，false表示失败
     */
    bool Initialize();
    
    /**
     * @brief 读取手势
     * @return 检测到的手势类型
     */
    GestureType ReadGesture();
    
    /**
     * @brief 设置舵机控制器指针
     * @param servo_controller 舵机控制器指针
     */
    void SetServoController(class ServoController* servo_controller);
    
    /**
     * @brief 设置表情控制器指针
     * @param emoji_controller 表情控制器指针
     */
    void SetEmojiController(class EmojiController* emoji_controller);
    
    /**
     * @brief 设置I2C总线句柄
     * @param i2c_bus I2C总线句柄
     */
    void SetI2CBus(i2c_master_bus_handle_t i2c_bus);
    
    /**
     * @brief 启动手势检测任务
     */
    void StartGestureTask();
    
    /**
     * @brief 停止手势检测任务
     */
    void StopGestureTask();
    
    /**
     * @brief 测试手势功能，手动触发不同的手势动作
     */
    void TestGestureActions();
    
    /**
     * @brief 唤醒传感器，解决运行时I2C通信问题
     * @return 唤醒是否成功
     */
    bool WakeupSensor();
    
    /**
     * @brief 复位传感器，执行软复位序列
     * @return 复位是否成功
     */
    bool ResetSensor();
    
    /**
     * @brief 快速唤醒传感器，用于I2C错误恢复
     * @return 唤醒是否成功
     */
    bool QuickWakeup();
    
    /**
     * @brief 设置I2C互斥锁，用于总线共享协调
     * @param mutex I2C互斥锁句柄
     */
    static void SetI2CMutex(SemaphoreHandle_t mutex);

private:
    /**
     * @brief 扫描I2C总线，查找可用设备
     */
    void ScanI2CBus();
    /**
     * @brief 写入寄存器
     * @param reg 寄存器地址
     * @param data 数据
     * @return 写入结果
     */
    esp_err_t WriteRegister(uint8_t reg, uint8_t data);
    
    /**
     * @brief 读取寄存器
     * @param reg 寄存器地址
     * @param data 读取的数据指针
     * @return 读取结果
     */
    esp_err_t ReadRegister(uint8_t reg, uint8_t* data);
    
    /**
     * @brief 选择寄存器组
     * @param bank 寄存器组
     * @return 选择结果
     */
    esp_err_t SelectRegisterBank(uint8_t bank);
    
    /**
     * @brief 手势检测任务
     * @param pvParameters 任务参数
     */
    static void GestureTaskFunction(void* pvParameters);
    
    /**
     * @brief 处理检测到的手势
     * @param gesture 手势类型
     */
    void ProcessGesture(GestureType gesture);
    
    /**
     * @brief 检测连续手势（摇头、点头）
     * @param gesture 当前手势
     */
    void CheckSequentialGestures(GestureType gesture);

private:
    class ServoController* servo_controller_;    // 舵机控制器指针
    class EmojiController* emoji_controller_;     // 表情控制器指针
    TaskHandle_t gesture_task_handle_;          // 手势检测任务句柄
    bool task_running_;                         // 任务运行状态
    
    // I2C相关句柄
    i2c_master_bus_handle_t i2c_bus_;           // I2C总线句柄
    i2c_master_dev_handle_t i2c_dev_;           // I2C设备句柄
    
    // 连续手势检测相关变量
    GestureState current_state_;                // 当前状态
    int gesture_sequence_count_;                // 手势序列计数
    TickType_t last_gesture_time_;             // 上一次手势时间
    GestureType last_gesture_;                 // 上一次手势类型
    
    static const int SEQUENCE_TIMEOUT_MS = 2000;  // 序列超时时间(毫秒)
    static const int MIN_SEQUENCE_COUNT = 3;      // 最小序列计数(连续3次才认为是摇头或点头)
    
    // I2C总线互斥锁，用于与显示屏共享总线
    static SemaphoreHandle_t i2c_mutex_;
};