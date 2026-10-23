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
     * @brief 启动手势任务存活看门狗（2026-10-23）
     *
     * GestureTaskFunction 每轮循环刷新 last_loop_tick_ms_；看门狗每 5 秒检查一次，
     * 超过 15 秒没刷新（串口上表现为「手势诊断心跳」断流）就判定任务卡死，
     * 强制 vTaskDelete + 重建任务，避免只能靠给设备断电恢复。
     */
    void StartWatchdog();
    
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

    /**
     * @brief 手势识别模式开关（2026-10-13）
     * true  = 开启：左右上下等全部手势响应（待命/聆听两态都认）
     * false = 关闭（默认）：只认掌心前推（开/关聆听），其余手势忽略
     * 由服务端 set_gesture_mode 工具经 system/gesture_mode 命令驱动。
     */
    void SetGestureModeEnabled(bool enabled);

    /**
     * @brief 查询手势识别模式是否开启
     */
    bool IsGestureModeEnabled() const { return gesture_mode_enabled_; }

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
     * @brief 手势任务存活看门狗任务（2026-10-23）
     */
    static void WatchdogTaskFunction(void* pvParameters);

    /**
     * @brief 当前毫秒 tick（供看门狗与主循环共用）
     */
    static uint32_t NowMs();
    
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

    // ★ 2026-10-23 存活看门狗：GestureTaskFunction 每轮刷新 last_loop_tick_ms_，
    //   看门狗测到超过 WATCHDOG_STALL_MS 没刷新就重建任务。
    volatile uint32_t last_loop_tick_ms_;       // 主循环最后一次刷卡时间（ms）
    TaskHandle_t watchdog_task_handle_;         // 看门狗任务句柄
    uint32_t watchdog_recover_count_;           // 累计重建次数（串口可查）

    static constexpr uint32_t WATCHDOG_STALL_MS = 15000;  // 判定卡死的阈值
    
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

    // ★ 2026-10-13 手势识别模式：默认关（只认前推开/关聆听）。
    //   服务端语音「开启手势识别」置 true——随机卖萌动画同步停掉，
    //   左右上下挥手全部响应；「关闭手势识别」置 false 并恢复随机动画。
    bool gesture_mode_enabled_ = false;

    // ★ 2026-10-14 兜底状态位：手势模式开启时是否已确认「随机动画已关」。
    //   为什么需要它：SetGestureModeEnabled(true) 内部会调
    //   emoji_controller_->SetRandomAnimationEnabled(false)，但该调用有 null guard——
    //   若 emoji_controller_ 此刻还没注入，关闭动作被静默跳过，于是随机动画照跑
    //   （主人看到"开了手势它自己在待机时摇头晃脑"）。
    //   兜底：GestureTaskFunction 每轮循环若发现"手势模式开着但没确认关过"，
    //   补调一次并置位。**只补一次**，绝不能每轮都调——因为
    //   SetRandomAnimationEnabled(false) 内部会 ClearAnimationQueue()，
    //   高频调用会把 LLM/手势的表情动画一起清掉。
    bool random_off_confirmed_ = false;
};