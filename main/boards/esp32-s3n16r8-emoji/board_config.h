/**
 * @file board_config.h
 * @brief ESP32 S3 N16R8 Emoji开发板配置
 *
 * 本文件定义了ESP32 S3 N16R8 Emoji开发板的引脚配置和其他硬件相关参数
 *
 * ============================================================================
 * ⭐ 舵机方向：「面对面镜像」约定（2026-10-14 定稿，唯一标准）
 * ============================================================================
 * 主人与设备**面对面**而坐。方向判据只有两条：
 *
 *   1) 水平（手掌/手左右划）：
 *        手朝主人的一侧移动  →  头朝**同一视觉侧**转
 *        （等价说法：头朝设备身体的**另一侧**，即镜像）
 *        例：手向主人的左 → 头向设备的右
 *
 *   2) 垂直（手上下划）：
 *        手向上 → 头向上；手向下 → 头向下（上下无手性，就是同向）
 *
 * 实现开关：`SERVO_X_INVERT` / `SERVO_Y_INVERT`（见下方对应注释）。
 *
 * ⚠️ 方向不对时**只翻这两个宏**，⛔ 不要去改 `gesture_sensor.cc` 里
 *    `ProcessGesture` 的 `HeadLeft/HeadRight/HeadUp/HeadDown`——
 *    因为 `EmojiController::ExecuteLookXxxAnimation` 等动画**内部也会动舵机**
 *    （`HeadMove(...)`），手势是「播动画 + 显式调用」双通道驱动；
 *    只改显式那一路会和动画内部**反向拉扯**。翻镜像宏能连动画一起翻，方向天然一致。
 *
 * 历史坑：X/Y 轴曾因「镜像该不该开」反复翻烙饼——根因是**期望约定变过**
 *    （08:19 要"头跟手同向"，08:48 明确要"面对面镜像"）。
 *    今后**只认上面这两条**，不再依据"上次说没问题"摇摆。
 * ============================================================================
 */

#pragma once

#include "driver/gpio.h"  // 包含ESP-IDF的GPIO头文件，用于GPIO_NUM_xx宏定义
#include "driver/ledc.h"  // 添加包含ledc相关类型定义的头文件

// 开发板名称
#ifndef BOARD_NAME
#define BOARD_NAME "ESP32-S3N16R8-EMOJI"
#endif

// 显示屏配置
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define DISPLAY_I2C_SDA_PIN GPIO_NUM_41
#define DISPLAY_I2C_SCL_PIN GPIO_NUM_42
#define DISPLAY_I2C_ADDR 0x3C

// 手势识别模块配置 - PAJ7620U2
#define GESTURE_I2C_SDA_PIN GPIO_NUM_41  // 手势识别传感器SDA引脚，与显示屏共用I2C总线
#define GESTURE_I2C_SCL_PIN GPIO_NUM_42  // 手势识别传感器SCL引脚，与显示屏共用I2C总线
#define GESTURE_I2C_ADDR 0x73           // PAJ7620U2传感器I2C地址
#define GESTURE_I2C_FREQ_HZ 100000      // I2C通信频率，设置为100kHz以确保稳定性
#define GESTURE_I2C_PORT I2C_NUM_0      // 使用I2C端口0（与显示屏共用）

// 舵机引脚配置
#define SERVO_HORIZONTAL_PIN GPIO_NUM_11  // 水平舵机连接到GPIO11
#define SERVO_VERTICAL_PIN GPIO_NUM_12    // 垂直舵机连接到GPIO12

// 舵机角度范围配置 - 根据boardemoji.ino的设置，但限制运动范围以保护舵机
#define SERVO_CENTER_X 90    // 水平舵机中心位置(90度)
#define SERVO_CENTER_Y 90     // 垂直舵机中心位置(90度)
#define SERVO_MIN_X (SERVO_CENTER_X - 40)  // 水平舵机最小角度(左边界) - 中心位置左侧40度
#define SERVO_MAX_X (SERVO_CENTER_X + 40)  // 水平舵机最大角度(右边界) - 中心位置右侧40度
#define SERVO_MIN_Y (SERVO_CENTER_Y - 20)  // 垂直舵机最小角度(上边界) - 中心位置上方20度
#define SERVO_MAX_Y (SERVO_CENTER_Y + 20)  // 垂直舵机最大角度(下边界) - 中心位置下方20度

// ⚠️ 2026-10-14 主人定稿：**面对面镜像约定**。
//   主人与设备面对面 → 主人手朝主人的一侧移动，设备头应朝**同一侧视觉方向**转
//   （等价说法：头朝设备身体的另一侧）。例：手朝主人的左 → 头朝设备的右。
//   1 = 取反（镜像）  0 = 不取反
//
//   历史：08:19 主人要的是「头跟手同向」（非镜像），当时 X_INVERT=1 被记为"反"；
//   08:48 主人明确澄清要「面对面镜像」→ 非镜像的 X_INVERT=0 才是"反"。
//   两次期望相反，故本值回 1。**今后以"面对面镜像"为唯一标准，别再摇摆。**
#define SERVO_X_INVERT 1

// 摇头/摆头幅度（X 轴）。原 20° 太小看不清，主人 02:38 反馈加大。
// 范围 20~40；>40 接近机械极限可能撞齿轮箱。
// 2026-10-14 主人又反馈还不够大 → 拉到 40°（SERVO_MAX_X=130 留 10° buffer）。
#define SERVO_SHAKE_AMPLITUDE 40

// 单次手势左右/上下偏移（2026-10-14 主人反馈单次 30° 不够，加到 40°）。
// 跟随 SERVO_MAX_X/MIN_X 边界自动 clamp。
// Y 轴单次用 30°（与 HeadShake 头抬维度对齐）。
#define GESTURE_OFFSET_X 40
#define GESTURE_OFFSET_Y 30

// ⚠️ 2026-10-14 主人二次实测（面对面临界帧）：手上抬 → 头实际下俯，反了。
//    同一轮反馈「面对面镜像」，垂直方向 = 「头跟手同向」（上下无手性）。
//    历史：02:30 曾设 Y_INVERT=1 修"上下反"，08:19 主人认可；
//    08:48 主人再测仍为反 → 说明上次的镜像方向判断与实物不符。
//    本轮翻回 0。**判据：手向上 → 头向上。**
#define SERVO_Y_INVERT 0

// 原始boardemoji.ino中的步进和延迟值
#define SERVO_STEP 1           // Arduino中的STEP值
#define SERVO_DELAY 10         // Arduino中的SERVO_DELAY值

// 开机舵机自检：左15°→右15°→回中（约0.6s）
//   1 = 开机做一次可见摆动（便于确认舵机通路 + 排查供电）
//   0 = 关闭，开机只归中不出声
#define SERVO_BOOT_SELFTEST 1

// 舵机PWM配置，参考Arduino原始代码设置
#define SERVO_MIN_PULSEWIDTH 500        // 最小脉宽(微秒)
#define SERVO_MAX_PULSEWIDTH 2500       // 最大脉宽(微秒)
#define LEDC_TIMER_BIT_WIDTH LEDC_TIMER_14_BIT  // 14位分辨率
#define LEDC_TIMER LEDC_TIMER_0         // 使用定时器0
#define LEDC_MODE LEDC_LOW_SPEED_MODE   // 使用低速模式
#define LEDC_FREQUENCY 50               // PWM频率50Hz

// 舵机通道配置
#define SERVO_CHANNEL_COUNT 2                      // 舵机通道数量
const ledc_channel_t servo_channels[SERVO_CHANNEL_COUNT] = {
    LEDC_CHANNEL_0,  // 水平舵机使用通道0
    LEDC_CHANNEL_1   // 垂直舵机使用通道1
};

// 舵机引脚数组配置
//
// ⚠️ 2026-10-14 主人实测：手掌左右划 → 头上下点；手掌上下划 → 头左右摇。
//    即**两个舵机的物理轴是对调的**：逻辑"水平"通道（HeadLeft/HeadRight）
//    真正驱动的是上下点头那个连杆，反之亦然。
//    这是装配/插头对调造成的，**改这里一行即可，千万别去拆电机重插** ——
//    拆装有把齿轮和连杆弄断的风险，而软件只是换个 GPIO。
//    改完语义仍然正确：索引 0 = 左右转，索引 1 = 上下抬。
const uint8_t servo_pins[SERVO_CHANNEL_COUNT] = {
    SERVO_VERTICAL_PIN,    // 左右转（物理上装在这个插头的舵机是水平轴）
    SERVO_HORIZONTAL_PIN,  // 上下抬（物理上装在这个插头的舵机是垂直轴）
};

// 按钮配置
#define BOOT_BUTTON_PIN GPIO_NUM_0        // BOOT按钮连接到GPIO0
#define VOLUME_UP_BUTTON_PIN GPIO_NUM_40   // 音量加按钮连接到GPIO40
#define VOLUME_DOWN_BUTTON_PIN GPIO_NUM_39 // 音量减按钮连接到GPIO39

// 音频配置 - INMP441麦克风 + MAX98357A音频放大器
// INMP441麦克风引脚连接:
// - VDD: 3.3V
// - GND: GND
// - SD: GPIO6 (I2S_DATA_IN_PIN)
// - L/R: GND (接地，工作在左声道模式)
// - WS: GPIO4 (I2S_MIC_WS_PIN)
// - SCK: GPIO5 (I2S_MIC_SCK_PIN)
//
// MAX98357A音频放大器引脚连接:
// - VIN: 3.3V
// - GND: GND
// - DIN: GPIO7 (I2S_DATA_OUT_PIN)
// - BCLK: GPIO15 (I2S_SPEAKER_BCLK_PIN)
// - LRC: GPIO16 (I2S_SPEAKER_WS_PIN)
// - GAIN: GND
// - SD: 3.3V (使能引脚，高电平启用)

// 音频时钟配置
#define I2S_SPEAKER_BCLK_PIN GPIO_NUM_15  // 音频放大器时钟(MAX98357A BCLK)
#define I2S_MIC_SCK_PIN GPIO_NUM_5       // 麦克风时钟(INMP441 SCK)

// 分别定义麦克风和扬声器的WS引脚
#define I2S_MIC_WS_PIN GPIO_NUM_4        // 麦克风WS(INMP441 WS)
#define I2S_SPEAKER_WS_PIN GPIO_NUM_16   // 音频放大器WS(MAX98357A LRC)

// 数据引脚
#define I2S_DATA_OUT_PIN GPIO_NUM_7       // I2S数据输出线，连接到GPIO7（扬声器DIN）
#define I2S_DATA_IN_PIN GPIO_NUM_6        // I2S数据输入线，连接到GPIO6（麦克风SD）

// 音频采样率配置
#define AUDIO_INPUT_SAMPLE_RATE 24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// LED配置
#define LED_PIN GPIO_NUM_48                // 板载LED连接到GPIO48
