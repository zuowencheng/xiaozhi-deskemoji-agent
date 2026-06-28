/**
 * @file gesture_sensor.cc
 * @brief PAJ7620U2手势识别传感器驱动实现
 */

#include "gesture_sensor.h"
#include "servo_controller.h"
#include "emoji_controller.h"
#include <esp_log.h>

#define TAG "GestureSensor"

// 静态互斥锁变量初始化
SemaphoreHandle_t GestureSensor::i2c_mutex_ = nullptr;

// PAJ7620U2完整官方初始化序列
const uint8_t initRegisterArray[][2] = {
    // Bank 0 基础配置
    {0xEF, 0x00},
    {0x32, 0x29}, {0x33, 0x01}, {0x34, 0x00}, {0x35, 0x01},
    {0x36, 0x00}, {0x37, 0x07}, {0x38, 0x17}, {0x39, 0x06},
    {0x3A, 0x12}, {0x3F, 0x00}, {0x40, 0x02}, {0x41, 0xFF},
    {0x42, 0x01}, {0x46, 0x2D}, {0x47, 0x0F}, {0x48, 0x3C},
    {0x49, 0x00}, {0x4A, 0x1E}, {0x4B, 0x00}, {0x4C, 0x20},
    {0x4D, 0x00}, {0x4E, 0x1A}, {0x4F, 0x14}, {0x50, 0x00},
    {0x51, 0x10}, {0x52, 0x00}, {0x5C, 0x02}, {0x5D, 0x00},
    {0x5E, 0x10}, {0x5F, 0x3F}, {0x60, 0x27}, {0x61, 0x28},
    {0x62, 0x00}, {0x63, 0x03}, {0x64, 0xF7}, {0x65, 0x03},
    {0x66, 0xD9}, {0x67, 0x03}, {0x68, 0x01}, {0x69, 0xC8},
    {0x6A, 0x40}, {0x6D, 0x04}, {0x6E, 0x00}, {0x6F, 0x00},
    {0x70, 0x80}, {0x71, 0x00}, {0x72, 0x00}, {0x73, 0x00},
    {0x74, 0xF0}, {0x75, 0x00}, {0x80, 0x42}, {0x81, 0x44},
    {0x82, 0x04}, {0x83, 0x20}, {0x84, 0x20}, {0x85, 0x00},
    {0x86, 0x10}, {0x87, 0x00}, {0x88, 0x05}, {0x89, 0x18},
    {0x8A, 0x10}, {0x8B, 0x01}, {0x8C, 0x37}, {0x8D, 0x00},
    {0x8E, 0xF0}, {0x8F, 0x81}, {0x90, 0x06}, {0x91, 0x06},
    {0x92, 0x1E}, {0x93, 0x0D}, {0x94, 0x0A}, {0x95, 0x0A},
    {0x96, 0x0C}, {0x97, 0x05}, {0x98, 0x0A}, {0x99, 0x41},
    {0x9A, 0x14}, {0x9B, 0x0A}, {0x9C, 0x3F}, {0x9D, 0x33},
    {0x9E, 0xAE}, {0x9F, 0xF9}, {0xA0, 0x48}, {0xA1, 0x13},
    {0xA2, 0x10}, {0xA3, 0x08}, {0xA4, 0x30}, {0xA5, 0x19},
    {0xA6, 0x10}, {0xA7, 0x08}, {0xA8, 0x24}, {0xA9, 0x04},
    {0xAA, 0x1E}, {0xAB, 0x1E}, {0xCC, 0x19}, {0xCD, 0x0B},
    {0xCE, 0x13}, {0xCF, 0x64}, {0xD0, 0x21}, {0xD1, 0x0F},
    {0xD2, 0x88}, {0xE0, 0x01}, {0xE1, 0x04}, {0xE2, 0x41},
    {0xE3, 0xD6}, {0xE4, 0x00}, {0xE5, 0x0C}, {0xE6, 0x0A},
    {0xE7, 0x00}, {0xE8, 0x00}, {0xE9, 0x00}, {0xEE, 0x07},
    {0xEF, 0x01}, {0x00, 0x1E}, {0x01, 0x1E}, {0x02, 0x0F},
    {0x03, 0x10}, {0x04, 0x02}, {0x05, 0x00}, {0x06, 0xB0},
    {0x07, 0x04}, {0x08, 0x0D}, {0x09, 0x0E}, {0x0A, 0x9C},
    {0x0B, 0x04}, {0x0C, 0x05}, {0x0D, 0x0F}, {0x0E, 0x02},
    {0x0F, 0x12}, {0x10, 0x02}, {0x11, 0x02}, {0x12, 0x00},
    {0x13, 0x01}, {0x14, 0x05}, {0x15, 0x07}, {0x16, 0x05},
    {0x17, 0x07}, {0x18, 0x01}, {0x19, 0x04}, {0x1A, 0x05},
    {0x1B, 0x0C}, {0x1C, 0x2A}, {0x1D, 0x01}, {0x1E, 0x00},
    {0x21, 0x00}, {0x22, 0x00}, {0x23, 0x00}, {0x25, 0x01},
    {0x26, 0x00}, {0x27, 0x39}, {0x28, 0x7F}, {0x29, 0x08},
    {0x30, 0x03}, {0x31, 0x00}, {0x32, 0x1A}, {0x33, 0x1A},
    {0x34, 0x07}, {0x35, 0x07}, {0x36, 0x01}, {0x37, 0xFF},
    {0x38, 0x36}, {0x39, 0x07}, {0x3A, 0x00}, {0x3E, 0xFF},
    {0x3F, 0x00}, {0x40, 0x77}, {0x41, 0x40}, {0x42, 0x00},
    {0x43, 0x30}, {0x44, 0xA0}, {0x45, 0x5C}, {0x46, 0x00},
    {0x47, 0x00}, {0x48, 0x58}, {0x4A, 0x1E}, {0x4B, 0x1E},
    {0x4C, 0x00}, {0x4D, 0x00}, {0x4E, 0xA0}, {0x4F, 0x80},
    {0x50, 0x00}, {0x51, 0x00}, {0x52, 0x00}, {0x53, 0x00},
    {0x54, 0x00}, {0x57, 0x80}, {0x59, 0x10}, {0x5A, 0x08},
    {0x5B, 0x94}, {0x5C, 0xE8}, {0x5D, 0x08}, {0x5E, 0x3D},
    {0x5F, 0x99}, {0x60, 0x45}, {0x61, 0x40}, {0x63, 0x2D},
    {0x64, 0x02}, {0x65, 0x96}, {0x66, 0x00}, {0x67, 0x97},
    {0x68, 0x01}, {0x69, 0xCD}, {0x6A, 0x01}, {0x6B, 0xB0},
    {0x6C, 0x04}, {0x6D, 0x2C}, {0x6E, 0x01}, {0x6F, 0x32},
    {0x71, 0x00}, {0x72, 0x01}, {0x73, 0x35}, {0x74, 0x00},
    {0x75, 0x33}, {0x76, 0x31}, {0x77, 0x01}, {0x7C, 0x84},
    {0x7D, 0x03}, {0x7E, 0x01}
};

// 传感器复位序列
const uint8_t resetRegisterArray[][2] = {
    {0xEF, 0x00},  // 选择Bank 0
    {0x32, 0x00},  // 软复位
    {0x33, 0x00},  // 清除状态
};

// 传感器激活序列（完整版）
const uint8_t activateRegisterArray[][2] = {
    {0xEF, 0x00},  // 选择Bank 0
    {0x41, 0xFF},  // 启用所有手势中断
    {0x42, 0x01},  // 启用手势识别模式
    {0x43, 0x00},  // 清除手势标志0
    {0x44, 0x00},  // 清除手势标志1
    {0x45, 0x00},  // 清除额外标志
    {0x46, 0x2D},  // 设置手势识别阈值
    {0x47, 0x0F},  // 设置手势识别参数
    {0x48, 0x3C},  // 设置手势识别窗口
};

// 传感器唤醒序列
const uint8_t wakeupRegisterArray[][2] = {
    {0xEF, 0x00},  // 选择Bank 0
    {0x32, 0x29},  // 重新配置关键参数
    {0x40, 0x02},  // 确保手势识别模式
    {0x41, 0xFF},  // 重新启用手势中断
    {0x42, 0x01},  // 重新启用Wave手势
    {0x43, 0x00},  // 清除可能的错误标志
    {0x44, 0x00},  // 清除可能的错误标志
};

GestureSensor::GestureSensor() 
    : servo_controller_(nullptr),
      emoji_controller_(nullptr),
      gesture_task_handle_(nullptr),
      task_running_(false),
      i2c_bus_(nullptr),
      i2c_dev_(nullptr),
      current_state_(STATE_IDLE),
      gesture_sequence_count_(0),
      last_gesture_time_(0),
      last_gesture_(GESTURE_NONE) {
}

GestureSensor::~GestureSensor() {
    StopGestureTask();
    if (i2c_dev_) {
        i2c_master_bus_rm_device(i2c_dev_);
        i2c_dev_ = nullptr;
    }
}

void GestureSensor::ScanI2CBus() {
    ESP_LOGI(TAG, "开始扫描I2C总线...");
    
    int device_count = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        // 创建临时设备配置
        i2c_device_config_t temp_dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,  // 使用较低的速度扫描
        };
        
        i2c_master_dev_handle_t temp_dev = nullptr;
        esp_err_t ret = i2c_master_bus_add_device(i2c_bus_, &temp_dev_cfg, &temp_dev);
        if (ret == ESP_OK) {
            // 对于PAJ7620U2，尝试简单的写操作而不是读操作
            uint8_t dummy_data = 0x00;
            ret = i2c_master_transmit(temp_dev, &dummy_data, 1, pdMS_TO_TICKS(50));
            
            if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {  // 超时也可能表示设备存在
                ESP_LOGI(TAG, "发现I2C设备: 0x%02X (状态: %s)", addr, esp_err_to_name(ret));
                device_count++;
                
                if (addr == GESTURE_I2C_ADDR) {
                    ESP_LOGI(TAG, "  -> 这是PAJ7620U2手势传感器地址!");
                } else if (addr == 0x3C || addr == 0x3D) {
                    ESP_LOGI(TAG, "  -> 这可能是SSD1306显示屏地址");
                }
            }
            
            // 清理临时设备
            i2c_master_bus_rm_device(temp_dev);
        }
        
        // 添加小延迟避免总线过载
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    
    ESP_LOGI(TAG, "I2C总线扫描完成，发现%d个设备", device_count);
    
    if (device_count == 0) {
        ESP_LOGW(TAG, "警告: 扫描方法可能不适用于所有设备，将跳过扫描步骤");
    }
}

void GestureSensor::SetI2CBus(i2c_master_bus_handle_t i2c_bus) {
    i2c_bus_ = i2c_bus;
    
    if (i2c_bus_ && !i2c_dev_) {
        // 优化的I2C设备配置，提高稳定性
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = GESTURE_I2C_ADDR,
            .scl_speed_hz = 100000,  // 降低到100kHz提高稳定性
        };
        
        esp_err_t ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &i2c_dev_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "创建I2C设备句柄失败: %s", esp_err_to_name(ret));
            i2c_dev_ = nullptr;
        } else {
            ESP_LOGI(TAG, "I2C设备句柄创建成功，速率: 100kHz");
        }
    }
}

bool GestureSensor::Initialize() {
    ESP_LOGI(TAG, "开始初始化PAJ7620U2手势识别传感器");
    
    if (!i2c_dev_) {
        ESP_LOGE(TAG, "I2C设备句柄未初始化");
        return false;
    }
    
    // 步骤0: 快速检查传感器连接状态 (Check connection first to fail fast)
    ESP_LOGI(TAG, "步骤0: 检查传感器连接状态...");
    
    // 给一点时间让I2C总线稳定
    vTaskDelay(pdMS_TO_TICKS(50));
    
    uint8_t part_id = 0;
    esp_err_t probe_ret = ESP_FAIL;
    uint8_t reg_addr = 0x00; // Part ID register
    bool is_nack = false;

    // 尝试探测多次 (增加到6次)
    const int PROBE_RETRIES = 6;
    for (int i = 0; i < PROBE_RETRIES; i++) {
        // 尝试获取锁并读取
        if (i2c_mutex_) {
            if (xSemaphoreTake(i2c_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
                probe_ret = i2c_master_transmit_receive(i2c_dev_, &reg_addr, 1, &part_id, 1, pdMS_TO_TICKS(50));
                xSemaphoreGive(i2c_mutex_);
            } else {
                ESP_LOGW(TAG, "获取I2C锁超时，无法检测传感器");
                probe_ret = ESP_ERR_TIMEOUT;
            }
        } else {
            probe_ret = i2c_master_transmit_receive(i2c_dev_, &reg_addr, 1, &part_id, 1, pdMS_TO_TICKS(50));
        }

        if (probe_ret == ESP_OK) {
            is_nack = false;
            break;
        }
        
        // 检查是否是NACK (ESP_ERR_TIMEOUT通常表示NACK或超时)
        // 如果是INVALID_STATE，可能是总线忙或驱动状态问题，不代表物理断开
        if (probe_ret == ESP_ERR_TIMEOUT) {
            is_nack = true;
            // 如果是NACK，可能真的没接，但也可能是偶然，继续重试几次
        } else {
            is_nack = false;
        }
        
        // 每次失败后等待一段时间，逐渐增加间隔
        vTaskDelay(pdMS_TO_TICKS(50 + (i * 10)));
    }

    // 判定逻辑：
    // 1. 如果成功 (ESP_OK) -> 通过
    // 2. 如果是 INVALID_STATE -> 判定为不确定（可能是总线忙），允许尝试继续初始化
    // 3. 其他所有错误 (超时/NACK/FAIL等) -> 判定为未连接，返回失败
    
    if (probe_ret != ESP_OK) {
        // 仅对 INVALID_STATE 这一种特殊情况进行宽容处理
        // 因为在ESP32S3上，当I2C总线刚初始化或与其他设备共享时，偶发此错误
        if (probe_ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "传感器检测返回异常状态 (ESP_ERR_INVALID_STATE)，尝试继续初始化...");
            // 如果真的没接，后续的“5次连续写入失败”检查会拦截并终止
        } else {
            // 其他所有错误均视为未连接
            ESP_LOGW(TAG, "未检测到PAJ7620U2传感器: %s", esp_err_to_name(probe_ret));
            ESP_LOGW(TAG, "手势识别功能将被禁用");
            return false;
        }
    } else {
        ESP_LOGI(TAG, "检测到传感器 (Part ID: 0x%02X)，继续初始化", part_id);
    }

    ESP_LOGI(TAG, "I2C设备句柄已准备就绪，开始传感器初始化");
    
    // 等待传感器上电稳定
    ESP_LOGI(TAG, "等待传感器上电稳定...");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 步骤1: 执行软复位
    ESP_LOGI(TAG, "步骤1: 执行传感器软复位...");
    if (!ResetSensor()) {
        ESP_LOGW(TAG, "软复位失败，判断传感器可能未连接，终止初始化");
        return false;
    }
    
    // 等待复位完成
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // 步骤2: 测试I2C连接
    ESP_LOGI(TAG, "步骤2: 测试I2C连接...");
    bool connection_ok = false;
    for (int test_retry = 0; test_retry < 5; test_retry++) {
        uint8_t test_data = 0;
        esp_err_t test_result = ReadRegister(0x00, &test_data);
        if (test_result == ESP_OK) {
            ESP_LOGI(TAG, "I2C连接测试成功 (重试%d次)，读取数据: 0x%02X", test_retry, test_data);
            connection_ok = true;
            break;
        } else {
            ESP_LOGW(TAG, "I2C连接测试失败 (重试%d/5): %s", test_retry + 1, esp_err_to_name(test_result));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    if (!connection_ok) {
        ESP_LOGE(TAG, "PAJ7620U2传感器I2C连接失败");
        ESP_LOGE(TAG, "请检查:");
        ESP_LOGE(TAG, "1. 传感器是否正确连接到SDA(GPIO41)和SCL(GPIO42)");
        ESP_LOGE(TAG, "2. 传感器是否正常供电(3.3V)");
        ESP_LOGE(TAG, "3. I2C地址是否正确(0x%02X)", GESTURE_I2C_ADDR);
        return false;
    }
    
    // 步骤3: 选择Bank 0并验证
    ESP_LOGI(TAG, "步骤3: 选择Bank 0并验证...");
    esp_err_t bank_result = ESP_FAIL;
    for (int retry = 0; retry < 5; retry++) {
        bank_result = SelectRegisterBank(PAJ7620_BANK0);
        if (bank_result == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            uint8_t bank_check = 0;
            esp_err_t bank_verify = ReadRegister(PAJ7620_REGITER_BANK_SEL, &bank_check);
            if (bank_verify == ESP_OK && bank_check == PAJ7620_BANK0) {
                ESP_LOGI(TAG, "成功选择Bank 0 (重试%d次)，验证值: 0x%02X", retry, bank_check);
                break;
            } else {
                ESP_LOGW(TAG, "Bank验证失败，验证结果: %s, 值: 0x%02X", esp_err_to_name(bank_verify), bank_check);
                bank_result = ESP_FAIL;
            }
        }
        
        if (retry < 4) {
            ESP_LOGW(TAG, "选择Bank 0失败，重试 %d/5: %s", retry + 1, esp_err_to_name(bank_result));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    if (bank_result != ESP_OK) {
        ESP_LOGE(TAG, "选择Bank 0最终失败: %s", esp_err_to_name(bank_result));
        return false;
    }
    
    // 步骤4: 写入初始化序列
    ESP_LOGI(TAG, "步骤4: 写入初始化序列，共%d个寄存器", sizeof(initRegisterArray) / sizeof(initRegisterArray[0]));
    
    int success_count = 0;
    int consecutive_failures = 0;
    int total_registers = sizeof(initRegisterArray) / sizeof(initRegisterArray[0]);
    
    // 分批写入，在关键点增加延迟和验证
    for (int i = 0; i < total_registers; i++) {
        uint8_t reg_addr = initRegisterArray[i][0];
        uint8_t reg_value = initRegisterArray[i][1];
        
        // 对关键寄存器进行重试写入
        esp_err_t write_result = ESP_FAIL;
        int write_retries = (reg_addr == 0xEF) ? 3 : 1;  // Bank选择寄存器重试3次
        
        for (int retry = 0; retry < write_retries; retry++) {
            write_result = WriteRegister(reg_addr, reg_value);
            if (write_result == ESP_OK) {
                success_count++;
                consecutive_failures = 0; // 重置连续失败计数
                break;
            } else if (retry < write_retries - 1) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        
        if (write_result != ESP_OK) {
            ESP_LOGW(TAG, "写入寄存器0x%02X失败: %s (值: 0x%02X)", reg_addr, esp_err_to_name(write_result), reg_value);
            consecutive_failures++;
            
            // 如果连续失败超过5次，认为传感器已断开或严重故障，终止初始化以避免长时间等待
            if (consecutive_failures >= 5) {
                ESP_LOGE(TAG, "连续写入失败超过5次，判定传感器断开或故障，终止初始化");
                return false;
            }
        }
        
        // 在关键节点添加延迟和状态检查
        if (reg_addr == 0xEF) {  // Bank切换后加长延迟
            vTaskDelay(pdMS_TO_TICKS(50));
        } else if (i % 10 == 0) {  // 每10个寄存器
            vTaskDelay(pdMS_TO_TICKS(20));
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    
    ESP_LOGI(TAG, "初始化序列写入完成，成功: %d/%d (成功率: %d%%)", 
             success_count, total_registers, (success_count * 100) / total_registers);
    
    // 降低成功率要求，因为某些寄存器可能是只读的
    if (success_count < total_registers * 0.7) {  // 降低到70%成功率
        ESP_LOGW(TAG, "初始化序列写入成功率较低 (%d%%)，但继续尝试激活", (success_count * 100) / total_registers);
    }
    
    // 步骤5: 执行传感器激活序列
    ESP_LOGI(TAG, "步骤5: 执行传感器激活序列...");
    for (int i = 0; i < sizeof(activateRegisterArray) / sizeof(activateRegisterArray[0]); i++) {
        esp_err_t activate_result = WriteRegister(activateRegisterArray[i][0], activateRegisterArray[i][1]);
        if (activate_result != ESP_OK) {
            ESP_LOGW(TAG, "激活寄存器0x%02X写入失败: %s", activateRegisterArray[i][0], esp_err_to_name(activate_result));
        } else {
            ESP_LOGD(TAG, "激活寄存器0x%02X写入成功: 0x%02X", activateRegisterArray[i][0], activateRegisterArray[i][1]);
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    
    // 步骤6: 等待传感器完全激活
    ESP_LOGI(TAG, "步骤6: 等待传感器完全激活...");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 步骤7: 最终功能验证和状态检查
    ESP_LOGI(TAG, "步骤7: 进行最终功能验证...");
    
    // 多次读取状态寄存器，观察是否有变化
    for (int check = 0; check < 5; check++) {
        uint8_t gesture_flag_0 = 0, gesture_flag_1 = 0, state_reg = 0, approach_reg = 0;
        
        esp_err_t read0 = ReadRegister(PAJ7620_ADDR_GES_PS_DET_FLAG_0, &gesture_flag_0);
        esp_err_t read1 = ReadRegister(PAJ7620_ADDR_GES_PS_DET_FLAG_1, &gesture_flag_1);
        esp_err_t read_state = ReadRegister(PAJ7620_ADDR_STATE_INDICATOR, &state_reg);
        esp_err_t read_approach = ReadRegister(PAJ7620_ADDR_PS_APPROACH_STATE, &approach_reg);
        
        ESP_LOGI(TAG, "验证读取%d: 手势[0x%02X,0x%02X] 状态[0x%02X] 接近[0x%02X] 结果[%s,%s,%s,%s]", 
                 check + 1, gesture_flag_0, gesture_flag_1, state_reg, approach_reg,
                 esp_err_to_name(read0), esp_err_to_name(read1), 
                 esp_err_to_name(read_state), esp_err_to_name(read_approach));
        
        if (read0 == ESP_OK && read1 == ESP_OK) {
            ESP_LOGI(TAG, "传感器寄存器读取正常，可能已经就绪");
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    ESP_LOGI(TAG, "PAJ7620U2手势识别传感器初始化完成");
    ESP_LOGI(TAG, "请在传感器上方5-15cm处做明显的手势动作进行测试");
    ESP_LOGI(TAG, "建议: 手势要慢一点、幅度大一点，让传感器有足够时间检测");
    
    return true;
}

GestureType GestureSensor::ReadGesture() {
    uint8_t data0 = 0, data1 = 0;
    
    // 紧急修复：简化错误处理，避免栈溢出
    esp_err_t result0 = ReadRegister(PAJ7620_ADDR_GES_PS_DET_FLAG_0, &data0);
    if (result0 != ESP_OK) {
        static int consecutive_failures = 0;
        consecutive_failures++;
        
        // 严格限制重试，防止栈溢出
        if (consecutive_failures > 100) {
            ESP_LOGE(TAG, "手势传感器连续失败超过100次，停止重试");
            consecutive_failures = 0;
            return GESTURE_NONE;
        }
        
        // 仅记录错误，不做复杂处理
        if (consecutive_failures % 50 == 1) {
            ESP_LOGW(TAG, "I2C读取失败 (失败次数: %d): %s", consecutive_failures, esp_err_to_name(result0));
        }
        return GESTURE_NONE;
    } else {
        // 读取成功，重置失败计数器
        static int consecutive_failures = 0;
        if (consecutive_failures > 0) {
            ESP_LOGI(TAG, "手势传感器I2C通信恢复正常（之前连续失败%d次）", consecutive_failures);
            consecutive_failures = 0;
        }
    }
    
    esp_err_t result1 = ReadRegister(PAJ7620_ADDR_GES_PS_DET_FLAG_1, &data1);
    if (result1 != ESP_OK) {
        // 简化错误处理，直接返回
        return GESTURE_NONE;
    }
    
    // 简化状态检查，减少栈使用
    static int debug_counter = 0;
    if (++debug_counter % 1000 == 0) {  // 减少日志频率
        ESP_LOGI(TAG, "手势检测状态 - 循环%d次", debug_counter);
    }
    
    // 简化手势检测逻辑，减少复杂日志
    if (data0 != 0 || data1 != 0) {
        // 过滤器常见的噪声数据
        if (data0 > 0x3F) {
            return GESTURE_NONE;
        }
    }
    
    // 简化手势映射，减少日志输出
    GestureType detected_gesture = GESTURE_NONE;
    
    // 简化方向映射（根据实际测试结果）
    if (data0 & GES_DOWN_FLAG) {
        detected_gesture = GESTURE_RIGHT;
    } else if (data0 & GES_UP_FLAG) {
        detected_gesture = GESTURE_LEFT;
    } else if (data0 & GES_LEFT_FLAG) {
        detected_gesture = GESTURE_UP;
    } else if (data0 & GES_RIGHT_FLAG) {
        detected_gesture = GESTURE_DOWN;
    } else if (data0 & GES_FORWARD_FLAG) {
        detected_gesture = GESTURE_FORWARD;
    } else if (data0 & GES_BACKWARD_FLAG) {
        detected_gesture = GESTURE_BACKWARD;
    } else if (data0 & GES_CLOCKWISE_FLAG) {
        detected_gesture = GESTURE_CLOCKWISE;
    } else if (data0 & GES_COUNT_CLOCKWISE_FLAG) {
        detected_gesture = GESTURE_COUNT_CLOCKWISE;
    } else if (data1 & GES_WAVE_FLAG) {
        detected_gesture = GESTURE_WAVE;
    }
    
    // 简化手势验证和防抖动
    static TickType_t last_gesture_time = 0;
    TickType_t current_time = xTaskGetTickCount();
    
    if (detected_gesture != GESTURE_NONE) {
        // 防抖动：500ms内忽略重复手势
        if (pdTICKS_TO_MS(current_time - last_gesture_time) < 500) {
            return GESTURE_NONE;
        }
        
        ESP_LOGI(TAG, "检测到手势: %d", detected_gesture);
        
        // 清除手势标志
        WriteRegister(PAJ7620_ADDR_GES_PS_DET_FLAG_0, 0x00);
        WriteRegister(PAJ7620_ADDR_GES_PS_DET_FLAG_1, 0x00);
        
        last_gesture_time = current_time;
        return detected_gesture;
    }
    
    return GESTURE_NONE;
}

void GestureSensor::SetServoController(ServoController* servo_controller) {
    servo_controller_ = servo_controller;
}

void GestureSensor::SetEmojiController(EmojiController* emoji_controller) {
    emoji_controller_ = emoji_controller;
}

void GestureSensor::StartGestureTask() {
    if (!task_running_) {
        task_running_ = true;
        xTaskCreate(
            GestureTaskFunction,
            "GestureTask",
            8192,  // 增加栈大小到8192，解决栈溢出问题
            this,
            7,     // 调整优先级，确保稳定性
            &gesture_task_handle_
        );
        ESP_LOGI(TAG, "手势检测任务已启动，栈大小: 8192，优先级: 7");
    }
}

void GestureSensor::StopGestureTask() {
    if (task_running_) {
        task_running_ = false;
        if (gesture_task_handle_ != nullptr) {
            vTaskDelete(gesture_task_handle_);
            gesture_task_handle_ = nullptr;
        }
        ESP_LOGI(TAG, "手势检测任务已停止");
    }
}

void GestureSensor::TestGestureActions() {
    if (!servo_controller_) {
        ESP_LOGE(TAG, "测试失败：舵机控制器未设置");
        return;
    }
    
    ESP_LOGI(TAG, "开始测试手势动作...");
    
    // 测试所有手势动作
    ESP_LOGI(TAG, "测试: 舵机左转");
    servo_controller_->HeadLeft();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "测试: 舵机右转");
    servo_controller_->HeadRight();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "测试: 舵机抬头");
    servo_controller_->HeadUp();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "测试: 舵机低头");
    servo_controller_->HeadDown();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "测试: 舵机居中");
    servo_controller_->HeadCenter();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "测试: 舵机摇头");
    servo_controller_->HeadShake();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "测试: 舵机点头");
    servo_controller_->HeadNod();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "所有手势动作测试完成");
}

bool GestureSensor::ResetSensor() {
    ESP_LOGI(TAG, "执行PAJ7620U2传感器软复位...");
    
    // 执行复位序列
    for (int i = 0; i < sizeof(resetRegisterArray) / sizeof(resetRegisterArray[0]); i++) {
        esp_err_t result = WriteRegister(resetRegisterArray[i][0], resetRegisterArray[i][1]);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "复位寄存器0x%02X写入失败: %s", resetRegisterArray[i][0], esp_err_to_name(result));
            // 如果连复位指令都写入失败，说明连接有问题，直接返回失败
            return false;
        } else {
            ESP_LOGD(TAG, "复位寄存器0x%02X写入成功: 0x%02X", resetRegisterArray[i][0], resetRegisterArray[i][1]);
        }
        vTaskDelay(pdMS_TO_TICKS(50));  // 复位操作需要更长延迟
    }
    
    // 等待复位完成
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 测试复位后的访问
    uint8_t test_data = 0;
    esp_err_t test_result = ReadRegister(0x00, &test_data);
    if (test_result == ESP_OK) {
        ESP_LOGI(TAG, "传感器软复位成功，测试读取: 0x%02X", test_data);
        return true;
    } else {
        ESP_LOGW(TAG, "传感器软复位后测试失败: %s", esp_err_to_name(test_result));
        return false;
    }
}

bool GestureSensor::WakeupSensor() {
    ESP_LOGI(TAG, "尝试唤醒PAJ7620U2传感器...");
    
    // 执行唤醒序列
    int success_count = 0;
    for (int i = 0; i < sizeof(wakeupRegisterArray) / sizeof(wakeupRegisterArray[0]); i++) {
        esp_err_t result = WriteRegister(wakeupRegisterArray[i][0], wakeupRegisterArray[i][1]);
        if (result == ESP_OK) {
            success_count++;
        } else {
            ESP_LOGW(TAG, "唤醒寄存器0x%02X写入失败: %s", wakeupRegisterArray[i][0], esp_err_to_name(result));
        }
        vTaskDelay(pdMS_TO_TICKS(15)); // 稍微增加延迟
    }
    
    ESP_LOGI(TAG, "唤醒序列完成，成功写入: %d/%d", 
             success_count, sizeof(wakeupRegisterArray) / sizeof(wakeupRegisterArray[0]));
    
    // 等待传感器响应
    vTaskDelay(pdMS_TO_TICKS(150)); // 增加等待时间
    
    // 测试唤醒后的访问 - 多次测试
    bool wakeup_success = false;
    for (int test = 0; test < 3; test++) {
        uint8_t test_data = 0;
        esp_err_t test_result = WriteRegister(PAJ7620_ADDR_GES_PS_DET_FLAG_0, 0x00); // 先清除标志
        vTaskDelay(pdMS_TO_TICKS(10));
        test_result = ReadRegister(PAJ7620_ADDR_GES_PS_DET_FLAG_0, &test_data);
        
        if (test_result == ESP_OK) {
            ESP_LOGI(TAG, "传感器唤醒成功 (测试%d/3)，测试读取: 0x%02X", test + 1, test_data);
            wakeup_success = true;
            break;
        } else {
            ESP_LOGW(TAG, "传感器唤醒测试%d失败: %s", test + 1, esp_err_to_name(test_result));
            if (test < 2) {
                vTaskDelay(pdMS_TO_TICKS(50)); // 等待后再次测试
            }
        }
    }
    
    if (!wakeup_success) {
        ESP_LOGW(TAG, "传感器唤醒后测试失败，但继续尝试手势检测");
    }
    
    return wakeup_success;
}

bool GestureSensor::QuickWakeup() {
    // 快速唤醒序列，只执行最关键的寄存器操作
    ESP_LOGD(TAG, "执行快速唤醒...");
    
    // 关键唤醒命令
    const uint8_t quickWakeupArray[][2] = {
        {PAJ7620_REGITER_BANK_SEL, 0x00},  // 选择Bank 0
        {0x32, 0x29},  // 重新激活传感器
        {0x41, 0xFF},  // 重新启用手势中断
        {0x43, 0x00},  // 清除手势标志
        {0x44, 0x00},  // 清除手势标志
    };
    
    for (int i = 0; i < sizeof(quickWakeupArray) / sizeof(quickWakeupArray[0]); i++) {
        WriteRegister(quickWakeupArray[i][0], quickWakeupArray[i][1]);
        vTaskDelay(pdMS_TO_TICKS(5)); // 短延迟以提高速度
    }
    
    // 短暂等待
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 快速测试
    uint8_t test_data = 0;
    esp_err_t result = ReadRegister(PAJ7620_ADDR_GES_PS_DET_FLAG_0, &test_data);
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "快速唤醒成功");
        return true;
    } else {
        ESP_LOGD(TAG, "快速唤醒失败: %s", esp_err_to_name(result));
        return false;
    }
}

void GestureSensor::SetI2CMutex(SemaphoreHandle_t mutex) {
    i2c_mutex_ = mutex;
    ESP_LOGI(TAG, "I2C互斥锁已设置，启用总线共享协调");
}

esp_err_t GestureSensor::WriteRegister(uint8_t reg, uint8_t data) {
    if (!i2c_dev_) {
        ESP_LOGE(TAG, "I2C设备句柄未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 获取I2C互斥锁
    if (i2c_mutex_ && xSemaphoreTake(i2c_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGD(TAG, "获取I2C互斥锁超时");
        return ESP_ERR_TIMEOUT;
    }
    
    uint8_t write_buf[2] = {reg, data};
    esp_err_t result = ESP_FAIL;
    
    // 简单的重试机制
    for (int retry = 0; retry < 2; retry++) {
        result = i2c_master_transmit(i2c_dev_, write_buf, sizeof(write_buf), pdMS_TO_TICKS(150));
        if (result == ESP_OK) {
            break;
        } else if (retry == 0) {
            ESP_LOGD(TAG, "I2C写入寄存器0x%02X失败，重试: %s", reg, esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(3)); // 减少延迟
        }
    }
    
    // 释放I2C互斥锁
    if (i2c_mutex_) {
        xSemaphoreGive(i2c_mutex_);
    }
    
    return result;
}

esp_err_t GestureSensor::ReadRegister(uint8_t reg, uint8_t* data) {
    if (!i2c_dev_) {
        ESP_LOGE(TAG, "I2C设备句柄未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 获取I2C互斥锁，缩短等待时间
    if (i2c_mutex_ && xSemaphoreTake(i2c_mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGD(TAG, "获取I2C互斥锁超时");
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t result = ESP_FAIL;
    const int max_retries = 2;
    
    for (int retry = 0; retry < max_retries; retry++) {
        result = i2c_master_transmit_receive(i2c_dev_, &reg, 1, data, 1, pdMS_TO_TICKS(150)); // 减少超时
        
        if (result == ESP_OK) {
            static int consecutive_errors = 0;
            if (consecutive_errors > 0) {
                ESP_LOGD(TAG, "I2C读取恢复正常（之前连续错误%d次）", consecutive_errors);
                consecutive_errors = 0;
            }
            break;
        } else {
            static int consecutive_errors = 0;
            consecutive_errors++;
            
            if (retry < max_retries - 1) {
                if (retry == 0) {
                    ESP_LOGD(TAG, "I2C读取寄存器0x%02X失败，尝试快速唤醒: %s", reg, esp_err_to_name(result));
                    // 释放锁后唤醒，避免死锁
                    if (i2c_mutex_) {
                        xSemaphoreGive(i2c_mutex_);
                    }
                    QuickWakeup();
                    // 重新获取锁
                    if (i2c_mutex_ && xSemaphoreTake(i2c_mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
                        return ESP_ERR_TIMEOUT;
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(5 * (retry + 1))); // 减少延迟: 5ms, 10ms
            } else {
                if (consecutive_errors % 30 == 1) { // 进一步减少日志频率
                    ESP_LOGW(TAG, "I2C读取寄存器0x%02X最终失败 (连续错误%d次): %s", reg, consecutive_errors, esp_err_to_name(result));
                }
            }
        }
    }
    
    // 释放I2C互斥锁
    if (i2c_mutex_) {
        xSemaphoreGive(i2c_mutex_);
    }
    
    return result;
}

esp_err_t GestureSensor::SelectRegisterBank(uint8_t bank) {
    return WriteRegister(PAJ7620_REGITER_BANK_SEL, bank);
}

void GestureSensor::GestureTaskFunction(void* pvParameters) {
    GestureSensor* sensor = static_cast<GestureSensor*>(pvParameters);
    ESP_LOGI(TAG, "手势检测任务开始运行，采用deskemoji风格的简洁处理");
    
    int loop_count = 0;
    int success_count = 0;
    TickType_t last_health_check = xTaskGetTickCount();
    
    while (sensor->task_running_) {
        GestureType gesture = sensor->ReadGesture();
        
        if (gesture != GESTURE_NONE) {
            ESP_LOGI(TAG, "检测到手势: %d，立即处理", gesture);
            
            // 参考deskemoji：检测到手势后立即复位舵机位置
            if (sensor->servo_controller_) {
                sensor->servo_controller_->HeadCenter();
            }
            
            // 立即处理手势，无需额外延迟
            sensor->ProcessGesture(gesture);
            success_count++;
            
            // 手势处理后的短暂延迟，防止重复触发
            vTaskDelay(pdMS_TO_TICKS(300));  // 减少到300ms，提高响应性
        }
        
        // 简化健康检查，每30秒执行一次
        TickType_t current_time = xTaskGetTickCount();
        if (pdTICKS_TO_MS(current_time - last_health_check) > 30000) {
            ESP_LOGI(TAG, "手势传感器健康检查 - 总成功: %d, 循环: %d", success_count, loop_count);
            last_health_check = current_time;
        }
        
        // 减少检测间隔，提高响应性（类似deskemoji的快速响应）
        vTaskDelay(pdMS_TO_TICKS(50));  // 从100ms减少到50ms
        
        // 轻量级的CPU让出
        if (++loop_count % 20 == 0) {
            taskYIELD();
        }
    }
    
    ESP_LOGI(TAG, "手势检测任务退出");
    vTaskDelete(NULL);
}

void GestureSensor::ProcessGesture(GestureType gesture) {
    ESP_LOGI(TAG, "ProcessGesture被调用，手势类型: %d（采用deskemoji风格处理）", gesture);
    
    if (!servo_controller_) {
        ESP_LOGE(TAG, "错误：舵机控制器未设置，无法执行手势动作");
        return;
    }
    
    // 参考deskemoji：先重置舵机位置，然后执行动作
    // 简化处理逻辑，减少不必要的延迟
    
    switch (gesture) {
        case GESTURE_LEFT:
            ESP_LOGI(TAG, "执行动作：向左手势 -> 头部向左看表情");
            if (emoji_controller_) {
                emoji_controller_->PlayAnimation(AnimationType::LOOK_LEFT);
            }
            servo_controller_->HeadLeft(30);
            vTaskDelay(pdMS_TO_TICKS(1000));  // 减少到1秒
            servo_controller_->HeadCenter();  // 恢复到中心位置
            break;
            
        case GESTURE_RIGHT:
            ESP_LOGI(TAG, "执行动作：向右手势 -> 头部向右看表情");
            if (emoji_controller_) {
                emoji_controller_->PlayAnimation(AnimationType::LOOK_RIGHT);
            }
            servo_controller_->HeadRight(30);
            vTaskDelay(pdMS_TO_TICKS(1000));
            servo_controller_->HeadCenter();  // 恢复到中心位置
            break;
            
        case GESTURE_UP:
            ESP_LOGI(TAG, "执行动作：向上手势 -> 抬头表情");
            if (emoji_controller_) {
                emoji_controller_->EyeUp();
            }
            servo_controller_->HeadUp(25);
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (emoji_controller_) {
                emoji_controller_->EyeCenter();
            }
            servo_controller_->HeadCenter();  // 恢复到中心位置
            break;
            
        case GESTURE_DOWN:
            ESP_LOGI(TAG, "执行动作：向下手势 -> 低头表情");
            if (emoji_controller_) {
                emoji_controller_->EyeDown();
            }
            servo_controller_->HeadDown(25);
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (emoji_controller_) {
                emoji_controller_->EyeCenter();
            }
            servo_controller_->HeadCenter();  // 恢复到中心位置
            break;
            
        case GESTURE_FORWARD:
            ESP_LOGI(TAG, "执行动作：向前手势 -> 惊讶表情 + 启动聊天");
            if (emoji_controller_) {
                emoji_controller_->PlayAnimation(AnimationType::SURPRISE);
                emoji_controller_->EyeBlink();
            }
            // 参考deskemoji: 向前手势可以触发特殊功能（如启动语音交互）
            // start_chat = true; // 这里可以添加启动语音交互的逻辑
            vTaskDelay(pdMS_TO_TICKS(1500));
            break;
            
        case GESTURE_BACKWARD:
            ESP_LOGI(TAG, "执行动作：向后手势 -> 简单眨眼");
            if (emoji_controller_) {
                emoji_controller_->EyeBlink();
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
            
        case GESTURE_WAVE:
            ESP_LOGI(TAG, "执行动作：挥手手势 -> 开心表情 + 摇头");
            if (emoji_controller_) {
                emoji_controller_->PlayAnimation(AnimationType::HAPPY);
            }
            servo_controller_->HeadShake(3);  // 摇头3次
            if (emoji_controller_) {
                emoji_controller_->EyeBlink();
            }
            servo_controller_->HeadCenter();  // 恢复到中心位置
            break;
            
        case GESTURE_CLOCKWISE:
            ESP_LOGI(TAG, "执行动作：顺时针手势 -> 开心表情");
            if (emoji_controller_) {
                emoji_controller_->PlayAnimation(AnimationType::HAPPY);
            }
            servo_controller_->HeadCenter();
            if (emoji_controller_) {
                emoji_controller_->EyeBlink();
            }
            break;
            
        case GESTURE_COUNT_CLOCKWISE:
            ESP_LOGI(TAG, "执行动作：逆时针手势 -> 随机动画");
            if (emoji_controller_) {
                // 参考deskemoji的随机动画功能
                emoji_controller_->PlayAnimation(AnimationType::RANDOM);
            }
            break;
            
        default:
            ESP_LOGW(TAG, "未知的手势类型: %d", gesture);
            break;
    }
    
    // 参考deskemoji：动作完成后记录时间戳
    static TickType_t last_gesture_time = xTaskGetTickCount();
    last_gesture_time = xTaskGetTickCount();
    
    ESP_LOGI(TAG, "ProcessGesture完成，手势表情动作已处理");
}

void GestureSensor::CheckSequentialGestures(GestureType gesture) {
    TickType_t current_time = xTaskGetTickCount();
    
    if (pdTICKS_TO_MS(current_time - last_gesture_time_) > SEQUENCE_TIMEOUT_MS) {
        current_state_ = STATE_IDLE;
        gesture_sequence_count_ = 0;
    }
    
    switch (current_state_) {
        case STATE_IDLE:
            if (gesture == GESTURE_LEFT || gesture == GESTURE_RIGHT) {
                current_state_ = STATE_LEFT_RIGHT_SEQUENCE;
                gesture_sequence_count_ = 1;
                last_gesture_ = gesture;
            } else if (gesture == GESTURE_UP || gesture == GESTURE_DOWN) {
                current_state_ = STATE_UP_DOWN_SEQUENCE;
                gesture_sequence_count_ = 1;
                last_gesture_ = gesture;
            }
            break;
            
        case STATE_LEFT_RIGHT_SEQUENCE:
            if ((gesture == GESTURE_LEFT && last_gesture_ == GESTURE_RIGHT) ||
                (gesture == GESTURE_RIGHT && last_gesture_ == GESTURE_LEFT)) {
                gesture_sequence_count_++;
                last_gesture_ = gesture;
                
                if (gesture_sequence_count_ >= MIN_SEQUENCE_COUNT) {
                    ESP_LOGI(TAG, "检测到连续左右摆动 -> 舵机摇头");
                    servo_controller_->HeadShake();
                    current_state_ = STATE_IDLE;
                    gesture_sequence_count_ = 0;
                }
            } else {
                current_state_ = STATE_IDLE;
                gesture_sequence_count_ = 0;
            }
            break;
            
        case STATE_UP_DOWN_SEQUENCE:
            if ((gesture == GESTURE_UP && last_gesture_ == GESTURE_DOWN) ||
                (gesture == GESTURE_DOWN && last_gesture_ == GESTURE_UP)) {
                gesture_sequence_count_++;
                last_gesture_ = gesture;
                
                if (gesture_sequence_count_ >= MIN_SEQUENCE_COUNT) {
                    ESP_LOGI(TAG, "检测到连续上下摆动 -> 舵机点头");
                    servo_controller_->HeadNod();
                    current_state_ = STATE_IDLE;
                    gesture_sequence_count_ = 0;
                }
            } else {
                current_state_ = STATE_IDLE;
                gesture_sequence_count_ = 0;
            }
            break;
    }
    
    last_gesture_time_ = current_time;
}