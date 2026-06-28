#include "wifi_board.h"
#include "audio/codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "board_config.h"
#include "mcp_server.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "emoji_controller.h"
#include "servo_controller.h"
#include "emotion_response_controller.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <cstring>   // 添加cstring头文件，用于strchr()函数
#include <stdlib.h>  // 添加标准库头文件，用于rand()函数
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <driver/i2c.h>
#include <driver/i2c_master.h>  // 添加新版I2C驱动API头文件
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#include <lvgl.h>
#include <time.h>
#include <sys/time.h>

#define TAG "EmojiBoard"

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);

// 声明一个静态函数，用于处理AI回复
static void ProcessAIResponseTask(void* arg);

// 前向声明EmojiBoard类
class EmojiBoard;

// 全局变量，用于存储和访问EmojiBoard实例
EmojiBoard* g_board_instance = nullptr;

// 任务函数声明
static void StateMonitorTask(void* arg);

// 任务参数结构体，用于传递参数给静态任务函数
struct TaskParams {
    EmojiBoard* board;  // 使用EmojiBoard*代替void*
    EmotionResponseController* emotion_controller;
};

// 自定义OledDisplay类，用于捕获AI回复内容并触发表情和动作
class EmojiDisplay : public OledDisplay {
private:
    EmojiBoard* board_ = nullptr;
    bool processing_ai_response_ = false; // 添加标志，防止递归调用

public:
    // 重写SetEmotion方法，根据小智AI框架识别的表情触发我们自己的表情动画
    virtual void SetEmotion(const char* emotion) override;
    
    EmojiDisplay(EmojiBoard* board, esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel, 
                int width, int height, bool flip_x, bool flip_y) 
        : OledDisplay(io, panel, width, height, flip_x, flip_y), board_(board) {
        ESP_LOGI(TAG, "创建EmojiDisplay实例");
    }

    // 重写SetChatMessage方法，在显示AI回复时同时触发表情和动作
    void SetChatMessage(const char* role, const char* content) override;
};

class EmojiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    
    // 表情和舵机控制器
    EmojiController* emoji_controller_ = nullptr;
    ServoController* servo_controller_ = nullptr;
    
    // 情感响应控制器
    EmotionResponseController* emotion_controller_ = nullptr;
    
    // 表情模式标志
    bool is_emoji_mode_ = false;
    
    // 对话模式屏幕
    lv_obj_t* chat_screen_ = nullptr;
    
    // 上一次处理的AI回复
    std::string last_ai_response_;
    
    // 处理AI回复的方法
    void ProcessAIResponseInternal(const char* message) {
        // 如果消息为空，则不处理
        if (!message || message[0] == '\0') {
            return;
        }
        
        ESP_LOGI(TAG, "处理AI回复: %s", message);
        
        // 检查是否与上一次相同，如果相同则不处理
        if (last_ai_response_ == message) {
            return;
        }
        
        // 更新上一次处理的AI回复
        last_ai_response_ = message;
        
        // 检查是否包含特殊字符标记，如果有则立即处理
        if (message[0] && strchr("{}<>/\\$!?^*#~", message[0]) != nullptr) {
            ESP_LOGI(TAG, "检测到特殊字符标记: %c", message[0]);
            emotion_controller_->ProcessAIResponse(message);
        } else {
            // 如果没有特殊字符标记，则正常处理AI回复
            emotion_controller_->ProcessAIResponse(message);
        }
    }

    void InitializeDisplayI2c() {
        // 使用ESP-IDF v5.3.2的新版I2C驱动API
        i2c_master_bus_config_t bus_config = {};
        bus_config.i2c_port = I2C_NUM_0;
        bus_config.sda_io_num = DISPLAY_I2C_SDA_PIN;
        bus_config.scl_io_num = DISPLAY_I2C_SCL_PIN;
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = true;
        
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
        ESP_LOGI(TAG, "I2C总线初始化成功");
    }

    void InitializeSsd1306Display() {
        esp_lcd_panel_io_i2c_config_t io_config = {};
        io_config.dev_addr = DISPLAY_I2C_ADDR;
        io_config.on_color_trans_done = nullptr;
        io_config.user_ctx = nullptr;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 6;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.flags.dc_low_on_data = 0;
        io_config.flags.disable_control_phase = 0;
        io_config.scl_speed_hz = 400 * 1000;

        // 使用新版I2C面板IO API
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));
        ESP_LOGI(TAG, "LCD面板IO初始化成功");

        ESP_LOGI(TAG, "安装SSD1306驱动");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_LOGI(TAG, "SSD1306驱动安装成功");

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }

        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        // 使用自定义的EmojiDisplay类
        display_ = new EmojiDisplay(this, panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, true, true);

        // 保存对话模式屏幕的引用
        chat_screen_ = lv_screen_active();
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            // 无论在哪种模式下，短按BOOT按钮都进入录音状态
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

        boot_button_.OnLongPress([this]() {
            is_emoji_mode_ = !is_emoji_mode_;
            if (is_emoji_mode_) {
                // 进入表情模式
                SwitchScreen(true);
                emoji_controller_->StartBlinkTimer();
                emoji_controller_->EyeCenter();
                GetDisplay()->ShowNotification("表情模式");
                
                // 初始化舵机位置
                servo_controller_->HeadCenter();
            } else {
                // 退出表情模式
                emoji_controller_->StopBlinkTimer();
                SwitchScreen(false);
                GetDisplay()->ShowNotification("对话模式");
                emoji_controller_->CleanupEmojiScreen();
                
                // 舵机回到中心位置
                servo_controller_->HeadCenter();
            }
        });

        volume_up_button_.OnClick([this]() {
            // 无论在哪种模式下，音量+按钮都控制音量增加
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            // 无论在哪种模式下，长按音量+按钮都将音量设为最大
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            // 无论在哪种模式下，音量-按钮都控制音量减小
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            // 无论在哪种模式下，长按音量-按钮都将音量设为0
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeIot() {
        // 新的MCP架构不再需要手动初始化Thing，由框架自动管理
        ESP_LOGI(TAG, "新版MCP架构已自动管理设备功能");
    }
    
    // 切换屏幕
    void SwitchScreen(bool to_emoji_mode) {
        DisplayLockGuard lock(display_);
        if (to_emoji_mode) {
            lv_obj_t* emoji_screen = emoji_controller_->GetEmojiScreen();
            if (!emoji_screen) {
                emoji_screen = emoji_controller_->CreateEmojiScreen();
            }
            lv_scr_load(emoji_screen);
        } else {
            lv_scr_load(chat_screen_);
        }
    }

    // 声明静态任务函数为友元函数，使其可以访问私有成员
    friend void StateMonitorTask(void* arg);

    // 声明EmojiDisplay为友元类，使其能够访问EmojiBoard的私有成员
    friend class EmojiDisplay;

    // 声明ProcessAIResponseTask为友元函数，使其能够访问私有成员
    friend void ProcessAIResponseTask(void* arg);

public:
    EmojiBoard() :
        boot_button_(BOOT_BUTTON_PIN),
        volume_up_button_(VOLUME_UP_BUTTON_PIN),
        volume_down_button_(VOLUME_DOWN_BUTTON_PIN) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        
        // 创建表情控制器和舵机控制器
        emoji_controller_ = new EmojiController(display_);
        servo_controller_ = new ServoController();
        
        // 初始化控制器
        emoji_controller_->Initialize();
        servo_controller_->Initialize();
        
        // 设置表情控制器的舵机控制器
        emoji_controller_->SetServoController(servo_controller_);
        
        // 创建并初始化情感响应控制器
        emotion_controller_ = new EmotionResponseController(emoji_controller_, servo_controller_, GetAudioCodec());
        emotion_controller_->Initialize();
        
        // 手势识别功能已移除
        ESP_LOGI(TAG, "手势识别功能已移除");
        
        // 新版MCP架构不再需要设置全局情感控制器指针
        // iot::SetGlobalEmotionController(emotion_controller_);
        
        InitializeButtons();
        InitializeIot();
        
        // 创建一个任务来监听设备状态变化
        TaskParams* state_params = new TaskParams();
        state_params->emotion_controller = emotion_controller_;
        state_params->board = this;
        xTaskCreate(StateMonitorTask, "StateMonitor", 8192, state_params, 1, NULL);
        
        // 将自身实例赋值给全局变量
        g_board_instance = this;
    }

    ~EmojiBoard() {
        delete emoji_controller_;
        delete servo_controller_;
        delete emotion_controller_;
        // 清除全局变量
        g_board_instance = nullptr;
    }

    virtual Led* GetLed() override {
        static SingleLed led(LED_PIN);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            I2S_SPEAKER_BCLK_PIN, I2S_SPEAKER_WS_PIN, I2S_DATA_OUT_PIN,
            I2S_MIC_SCK_PIN, I2S_MIC_WS_PIN, I2S_DATA_IN_PIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    /**
     * @brief 处理用户输入的命令
     * @param message 用户输入的消息
     */
    void ProcessUserCommand(const char* message) {
        if (!message || message[0] == '\0') {
            return;
        }
        
        // 直接使用情感响应控制器处理用户命令
        if (emotion_controller_) {
            // 检查是否是表情动作命令
            if (emotion_controller_->ProcessEmotionCommand(message)) {
                ESP_LOGI(TAG, "用户输入的表情动作命令已处理: %s", message);
                return;
            }
            
            // 检查是否是音量控制命令
            if (emotion_controller_->ProcessVolumeCommand(message)) {
                ESP_LOGI(TAG, "用户输入的音量控制命令已处理: %s", message);
                return;
            }
        }
    }

    // 处理AI回复的公共方法，可以被外部调用
    void ProcessAIResponse(const char* message) {
        ProcessAIResponseInternal(message);
    }
};

// 在EmojiBoard类定义后实现EmojiDisplay::SetChatMessage方法
void EmojiDisplay::SetChatMessage(const char* role, const char* content) {
    // 检查是否正在处理AI回复，避免递归调用
    if (processing_ai_response_) {
        // 如果正在处理AI回复，只调用父类方法显示消息，不进行额外处理
        OledDisplay::SetChatMessage(role, content);
        return;
    }
    
    // 设置标志，表示正在处理AI回复
    processing_ai_response_ = true;
    
    // 首先调用父类方法显示消息
    OledDisplay::SetChatMessage(role, content);
    
    // 如果是AI回复，则处理内容
    if (role && strcmp(role, "assistant") == 0 && content && content[0] != '\0') {
        ESP_LOGI(TAG, "EmojiDisplay捕获AI回复: %s", content);
        
        // 调用EmojiBoard的ProcessAIResponse方法处理AI回复
        if (board_ && board_->emotion_controller_) {
            // 简化处理逻辑，只更新last_ai_response_并调用ProcessAIResponse
            if (board_->last_ai_response_ != content) {
                board_->last_ai_response_ = content;
                
                // 检查是否包含特殊字符标记，如果有则直接处理
                if (content[0] && strchr("{}<>/\\$!?^*#~", content[0]) != nullptr) {
                    ESP_LOGI(TAG, "检测到特殊字符标记: %c", content[0]);
                    
                    // 直接调用情感控制器处理特殊字符
                    board_->emotion_controller_->ProcessAIResponse(content);
                    
                    // 不再创建额外的任务处理AI回复，避免栈溢出
                } else {
                    // 使用后台任务处理AI回复，避免在主循环中处理复杂逻辑
                    // 增加栈大小，避免栈溢出
                    xTaskCreate(ProcessAIResponseTask, "ai_response", 8192, strdup(content), 1, NULL);
                }
            }
        }
    }
    
    // 重置标志
    processing_ai_response_ = false;
}

// 监听设备状态的静态任务函数
static void StateMonitorTask(void* arg) {
    TaskParams* params = static_cast<TaskParams*>(arg);
    EmojiBoard* board = params->board;
    if (!board) {
        ESP_LOGE(TAG, "Board instance is null");
        vTaskDelete(nullptr);
        return;
    }
    
    // 获取情感控制器
    auto* emotion_controller = params->emotion_controller;
    if (!emotion_controller) {
        ESP_LOGE(TAG, "Emotion controller is null");
        vTaskDelete(nullptr);
        return;
    }
    
    // 初始化随机数生成器
    srand(time(nullptr));
    
    // 初始化变量
    DeviceState last_state = kDeviceStateIdle;
    TickType_t last_speak_end_time = 0;
    bool in_conversation = false;
    
    // 确保初始状态下随机动画是启用的
    if (board->emoji_controller_) {
        board->emoji_controller_->SetRandomAnimationEnabled(true);
        ESP_LOGI(TAG, "初始化：启用随机表情动画");
    }
    
    // 监控设备状态
    while (true) {
        // 获取当前设备状态
        DeviceState current_state = Application::GetInstance().GetDeviceState();
        
        // 如果设备状态发生变化，记录日志
        if (current_state != last_state) {
            ESP_LOGI(TAG, "设备状态变化: %d -> %d", last_state, current_state);
        }
        
        // 如果设备状态从idle变为speaking或listening，说明对话开始
        if (last_state == kDeviceStateIdle && 
            (current_state == kDeviceStateSpeaking || current_state == kDeviceStateListening)) {
            // 标记为对话中
            in_conversation = true;
            
            // 停止随机表情动画
            if (board->emoji_controller_) {
                board->emoji_controller_->SetRandomAnimationEnabled(false);
                board->emoji_controller_->ClearAnimationQueue();
                ESP_LOGI(TAG, "对话开始，停止随机表情动画");
            }
            
            // 如果是AI开始回复
            if (current_state == kDeviceStateSpeaking) {
                // 随机触发一种积极情感（开心或惊讶）
                static const char* positive_emotions[] = {
                    "happy", "surprise"
                };
                int random_index = rand() % (sizeof(positive_emotions) / sizeof(positive_emotions[0]));
                emotion_controller->TriggerEmotion(positive_emotions[random_index]);
                
                ESP_LOGI(TAG, "AI开始回复，触发积极情感: %s", positive_emotions[random_index]);
            }
        }
        // 如果设备状态变为speaking或listening，但不是从idle变过来，说明对话继续
        else if ((current_state == kDeviceStateSpeaking || current_state == kDeviceStateListening) && 
                 !in_conversation) {
            // 标记为对话中
            in_conversation = true;
            
            // 停止随机表情动画
            if (board->emoji_controller_) {
                board->emoji_controller_->SetRandomAnimationEnabled(false);
                board->emoji_controller_->ClearAnimationQueue();
                ESP_LOGI(TAG, "对话继续，停止随机表情动画");
            }
        }
        
        // 如果设备状态从speaking变为idle，说明AI刚刚回复完毕
        if (last_state == kDeviceStateSpeaking && current_state == kDeviceStateIdle) {
            // 记录AI回复结束的时间
            last_speak_end_time = xTaskGetTickCount();
            
            // 获取最近的AI回复内容
            const std::string& ai_response = board->last_ai_response_;
            
            // 如果有AI回复内容，则基于内容分析情感
            if (!ai_response.empty()) {
                ESP_LOGI(TAG, "AI回复结束，基于内容分析情感: %s", ai_response.c_str());
                
                // 直接处理AI回复内容，触发相应的表情和动作
                emotion_controller->ProcessAIResponse(ai_response);
            } else {
                // 如果没有AI回复内容，则使用随机情感
                static const char* emotions[] = {
                    "happy", "sad", "surprise", "confused", "neutral", "look_left", "look_right"
                };
                int random_index = rand() % (sizeof(emotions) / sizeof(emotions[0]));
                emotion_controller->TriggerEmotion(emotions[random_index]);
                
                ESP_LOGI(TAG, "AI回复结束，无内容，使用随机情感: %s", emotions[random_index]);
            }
        }
        
        // 如果设备状态从listening变为idle，说明用户刚刚输入完毕
        if (last_state == kDeviceStateListening && current_state == kDeviceStateIdle) {
            // 尝试处理用户输入的表情动作命令
            // 注意：这里我们不能直接获取用户输入内容，因为框架没有提供这个接口
            // 但我们可以在AI回复前检查常见的表情动作命令
            
            // 模拟处理几个常见的表情动作命令
            static const std::vector<std::string> common_commands = {
                "向左看", "向右看", "看左边", "看右边", "左看", "右看",
                "开心", "笑一笑", "高兴", "笑", "微笑",
                "悲伤", "伤心", "难过", "哭",
                "惊讶", "吃惊", "惊喜",
                "困惑", "疑惑", "迷惑",
                "正常", "平静", "中性", "恢复"
            };
            
            for (const auto& cmd : common_commands) {
                if (emotion_controller->ProcessEmotionCommand(cmd)) {
                    ESP_LOGI(TAG, "用户输入结束，尝试处理常见表情动作命令: %s", cmd.c_str());
                    break;
                }
            }
        }
        
        // 如果设备状态从listening或speaking变为idle，认为对话可能结束
        if ((last_state == kDeviceStateListening || last_state == kDeviceStateSpeaking) && 
            current_state == kDeviceStateIdle) {
            TickType_t current_time = xTaskGetTickCount();
            
            // 如果已经过了3秒，认为对话结束
            if (in_conversation && 
                ((current_time - last_speak_end_time) > pdMS_TO_TICKS(3000))) {
                in_conversation = false;
                
                // 恢复随机表情动画
                if (board->emoji_controller_) {
                    board->emoji_controller_->SetRandomAnimationEnabled(true);
                    ESP_LOGI(TAG, "对话结束，恢复随机表情动画");
                }
                
                // 触发中性情感
                emotion_controller->TriggerEmotion("neutral");
                ESP_LOGI(TAG, "对话结束，恢复中性情感");
            }
        }
        
        // 更新上一次的设备状态
        last_state = current_state;
        
        // 延时100毫秒
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 注意：这里永远不会执行到，因为上面的循环是无限的
    delete params;
    vTaskDelete(NULL);
}

// 声明一个静态函数，用于处理AI回复
static void ProcessAIResponseTask(void* arg) {
    char* message = (char*)arg;
    if (message) {
        // 获取情感响应控制器
        EmotionResponseController* emotion_controller = nullptr;
        
        // 直接从EmojiBoard获取
        if (g_board_instance) {
            // 由于ProcessAIResponseTask是EmojiBoard的友元函数，可以直接访问私有成员
            emotion_controller = g_board_instance->emotion_controller_;
            ESP_LOGI("AIResponseTask", "从EmojiBoard获取情感控制器");
        }
        
        if (emotion_controller) {
            ESP_LOGI("AIResponseTask", "处理AI回复: %s", message);
            emotion_controller->ProcessAIResponse(message);
        } else {
            ESP_LOGW("AIResponseTask", "无法获取情感控制器，无法处理AI回复");
        }
    }
    
    // 释放消息内存
    if (message) {
        free(message);
    }
    
    // 删除任务
    vTaskDelete(NULL);
}

// 实现EmojiDisplay::SetEmotion方法
void EmojiDisplay::SetEmotion(const char* emotion) {
    ESP_LOGI(TAG, "小智AI框架识别到表情: %s", emotion);
    
    // 调用父类的SetEmotion方法，保持原有功能
    OledDisplay::SetEmotion(emotion);
    
    // 防止递归调用
    if (processing_ai_response_) {
        return;
    }
    
    // 将小智AI框架的表情映射到我们的表情动作
    if (board_ && board_->emotion_controller_) {
        std::string emotion_str(emotion);
        
        // 映射小智AI框架的表情到我们的表情动作
        std::string mapped_emotion;
        
        // 开心系列表情
        if (emotion_str == "happy") {
            mapped_emotion = "happy";
        } else if (emotion_str == "laughing") {
            mapped_emotion = "laughing";
        } else if (emotion_str == "funny") {
            mapped_emotion = "funny";
        } 
        // 悲伤系列表情
        else if (emotion_str == "sad") {
            mapped_emotion = "sad";
        } else if (emotion_str == "crying") {
            mapped_emotion = "cry";
        } 
        // 生气表情
        else if (emotion_str == "angry") {
            mapped_emotion = "anger";
        } 
        // 惊讶系列表情
        else if (emotion_str == "surprised") {
            mapped_emotion = "surprise";
        } else if (emotion_str == "shocked") {
            mapped_emotion = "shocked";
        } 
        // 困惑和思考表情
        else if (emotion_str == "confused") {
            mapped_emotion = "confused";
        } else if (emotion_str == "thinking") {
            mapped_emotion = "thinking";
        } 
        // 尴尬表情
        else if (emotion_str == "embarrassed") {
            mapped_emotion = "awkward";
        } 
        // 睡觉表情
        else if (emotion_str == "sleepy") {
            mapped_emotion = "sleep";
        } 
        // 眨眼表情
        else if (emotion_str == "winking") {
            mapped_emotion = "blink";
        } 
        // 酷酷的表情
        else if (emotion_str == "cool") {
            mapped_emotion = "cool";
        } 
        // 自信表情
        else if (emotion_str == "confident") {
            mapped_emotion = "confident";
        } 
        // 放松表情
        else if (emotion_str == "relaxed") {
            mapped_emotion = "relaxed";
        } 
        // 爱心和亲吻表情
        else if (emotion_str == "loving") {
            mapped_emotion = "loving";
        } else if (emotion_str == "kissy") {
            mapped_emotion = "kissy";
        } 
        // 美味表情
        else if (emotion_str == "delicious") {
            mapped_emotion = "delicious";
        } 
        // 奇怪表情
        else if (emotion_str == "silly") {
            mapped_emotion = "silly";
        } 
        // 中性表情
        else if (emotion_str == "neutral") {
            mapped_emotion = "neutral";
        } 
        // 对于其他表情，使用默认的中性表情
        else {
            mapped_emotion = "neutral";
            ESP_LOGW(TAG, "未识别的表情类型: %s，使用默认的中性表情", emotion_str.c_str());
        }
        
        ESP_LOGI(TAG, "映射到我们的表情动作: %s", mapped_emotion.c_str());
        
        // 创建一个单独的任务来执行表情动作，避免阻塞主线程
        // 复制表情字符串，因为它将在任务中使用
        char* emotion_copy = strdup(mapped_emotion.c_str());
        if (emotion_copy) {
            xTaskCreate([](void* arg) {
                char* emotion = (char*)arg;
                // 获取EmojiBoard实例
                if (g_board_instance && g_board_instance->emotion_controller_) {
                    // 使用TriggerEmotion方法触发表情动作，这是一个公有方法
                    g_board_instance->emotion_controller_->TriggerEmotion(emotion);
                }
                // 释放复制的字符串
                free(emotion);
                vTaskDelete(NULL);
            }, "emotion_task", 4096, emotion_copy, 1, NULL);
        }
    }
}

DECLARE_BOARD(EmojiBoard);