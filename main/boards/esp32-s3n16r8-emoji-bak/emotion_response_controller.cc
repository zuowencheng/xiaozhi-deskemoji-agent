/**
 * @file emotion_response_controller.cc
 * @brief AI回复情感响应控制器实现文件
 * 
 * 本文件实现了AI回复情感响应控制器的功能，用于根据AI回复内容自动触发表情和舵机动作
 */

#include "emotion_response_controller.h"
#include "emoji_controller.h"
#include "servo_controller.h"
#include "board.h"
#include "mcp_server.h"
#include "application.h"
#include "audio/audio_codec.h"

#include <esp_log.h>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cJSON.h>

#define TAG "EmotionController"

// 构造函数
EmotionResponseController::EmotionResponseController(EmojiController* emoji_controller, ServoController* servo_controller, AudioCodec* audio_codec)
    : emoji_controller_(emoji_controller), 
      servo_controller_(servo_controller),
      audio_codec_(audio_codec),
      current_emotion_("neutral"),
      default_emotion_("neutral") {
}

// 析构函数
EmotionResponseController::~EmotionResponseController() {
}

// 初始化情感响应控制器
void EmotionResponseController::Initialize() {
    // 初始化情感动作映射
    InitializeEmotionActions();
    
    // 初始化情感关键词映射
    InitializeEmotionKeywords();
    
    ESP_LOGI(TAG, "EmotionResponseController initialized");
}

// 处理AI回复
void EmotionResponseController::ProcessAIResponse(const std::string& message) {
    // 添加详细日志
    ESP_LOGI(TAG, "处理AI回复: %s", message.c_str());
    
    // 首先检查是否是音量控制命令
    if (ProcessVolumeCommand(message)) {
        // 如果是音量控制命令，已经处理完毕，直接返回
        ESP_LOGI(TAG, "识别为音量控制命令，处理完毕");
        return;
    }
    
    // 检查是否是表情动作命令
    if (ProcessEmotionCommand(message)) {
        // 如果是表情动作命令，已经处理完毕，直接返回
        ESP_LOGI(TAG, "识别为表情动作命令，处理完毕");
        return;
    }
    
    // 根据消息内容分析情感和动作
    std::string emotion = "neutral";  // 默认情感为中性
    std::string action = "";          // 默认无特定动作
    
    // 分析文本内容，获取情感类型
    emotion = AnalyzeText(message);
    
    // 根据消息内容判断是否需要执行特定动作
    if (ShouldNod(message)) {
        action = "nod";
        ESP_LOGI(TAG, "内容表示同意或肯定，执行点头动作");
    } else if (ShouldShake(message)) {
        action = "shake";
        ESP_LOGI(TAG, "内容表示否定或拒绝，执行摇头动作");
    } else if (ShouldDance(message)) {
        action = "dance";
        ESP_LOGI(TAG, "内容表示高兴或庆祝，执行跳舞动作");
    } else if (ShouldLookLeft(message)) {
        action = "look_left";
        ESP_LOGI(TAG, "内容提到左边，执行向左看动作");
    } else if (ShouldLookRight(message)) {
        action = "look_right";
        ESP_LOGI(TAG, "内容提到右边，执行向右看动作");
    } else if (ShouldLookUp(message)) {
        action = "look_up";
        ESP_LOGI(TAG, "内容提到上方，执行抬头动作");
    } else if (ShouldLookDown(message)) {
        action = "look_down";
        ESP_LOGI(TAG, "内容提到下方，执行低头动作");
    }
    
    // 执行情感动作
    ESP_LOGI(TAG, "通过文本分析得到情感: %s", emotion.c_str());
    
    // 如果有特定动作，优先执行动作
    if (!action.empty()) {
        ExecuteEmotionAction(action);
    } else {
        // 否则执行情感对应的动作
        ExecuteEmotionAction(emotion);
    }
    
    ESP_LOGI(TAG, "Processed AI response, emotion: %s, action: %s", 
             emotion.c_str(), action.empty() ? "none" : action.c_str());
}

// 处理AI回复JSON
void EmotionResponseController::ProcessAIResponseJson(const cJSON* json_obj) {
    // 暂未实现
}

// 处理音量控制命令
bool EmotionResponseController::ProcessVolumeCommand(const std::string& message) {
    // ESP_LOGI(TAG, "处理音量控制命令: %s", message.c_str());
    
    // 获取AudioCodec实例
    AudioCodec* codec = audio_codec_;
    if (!codec) {
        codec = Board::GetInstance().GetAudioCodec();
        if (!codec) {
            ESP_LOGE(TAG, "无法获取AudioCodec实例");
            return false;
        }
    }
    
    // 转换为小写进行处理
    std::string lower_text = message;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    
    // 匹配各种音量设置命令模式 - 使用字符串查找替代正则表达式
    // 查找"音量设为"、"音量调为"、"音量设置为"等模式
    size_t pos = 0;
    int volume = -1;
    
    // 检查各种音量设置命令格式
    const char* volume_patterns[] = {
        "音量设为", "音量调为", "音量设置为", "音量调到", "声音设为", 
        "声音调为", "声音设置为", "声音调到", "把音量设为", "把音量调为", 
        "把音量设置为", "把音量调到", "将音量设为", "将音量调为", "将音量设置为", "将音量调到"
    };
    
    for (const char* pattern : volume_patterns) {
        pos = lower_text.find(pattern);
        if (pos != std::string::npos) {
            // 找到匹配的模式，提取后面的数字
            pos += strlen(pattern);
            std::string volume_str;
            
            // 提取数字部分
            while (pos < lower_text.length() && std::isdigit(lower_text[pos])) {
                volume_str += lower_text[pos];
                pos++;
            }
            
            if (!volume_str.empty()) {
                volume = std::stoi(volume_str);
                break;
            }
        }
    }
    
    if (volume >= 0) {
        // 确保音量在0-100范围内
        volume = std::max(0, std::min(100, volume));
        
        // 设置音量
        codec->SetOutputVolume(volume);
        ESP_LOGI(TAG, "设置音量为: %d", volume);
        
        // 触发开心表情
        ExecuteEmotionAction("happy");
        return true;
    }
    
    // 匹配"音量增加"或"音量加大"或"增加音量"或"加大音量"的模式
    const char* volume_up_patterns[] = {
        "音量增加", "音量加大", "增加音量", "加大音量"
    };
    
    bool volume_up = false;
    for (const char* pattern : volume_up_patterns) {
        if (lower_text.find(pattern) != std::string::npos) {
            volume_up = true;
            break;
        }
    }
    
    if (volume_up) {
        // 获取当前音量并增加10
        int volume = codec->output_volume() + 10;
        volume = std::min(100, volume);
        
        // 设置新音量
        codec->SetOutputVolume(volume);
        ESP_LOGI(TAG, "增加音量到: %d", volume);
        
        // 触发开心表情
        ExecuteEmotionAction("happy");
        return true;
    }
    
    // 匹配"音量减小"或"音量降低"或"减小音量"或"降低音量"的模式
    const char* volume_down_patterns[] = {
        "音量减小", "音量降低", "减小音量", "降低音量"
    };
    
    bool volume_down = false;
    for (const char* pattern : volume_down_patterns) {
        if (lower_text.find(pattern) != std::string::npos) {
            volume_down = true;
            break;
        }
    }
    
    if (volume_down) {
        // 获取当前音量并减少10
        int volume = codec->output_volume() - 10;
        volume = std::max(0, volume);
        
        // 设置新音量
        codec->SetOutputVolume(volume);
        ESP_LOGI(TAG, "减小音量到: %d", volume);
        
        // 触发开心表情
        ExecuteEmotionAction("happy");
        return true;
    }
    
    // 匹配"静音"或"关闭声音"的模式
    const char* mute_patterns[] = {
        "静音", "关闭声音", "声音关闭"
    };
    
    bool mute = false;
    for (const char* pattern : mute_patterns) {
        if (lower_text.find(pattern) != std::string::npos) {
            mute = true;
            break;
        }
    }
    
    if (mute) {
        // 设置音量为0
        codec->SetOutputVolume(0);
        ESP_LOGI(TAG, "静音");
        
        // 触发开心表情
        ExecuteEmotionAction("happy");
        return true;
    }
    
    // 匹配"最大音量"或"音量最大"的模式
    const char* max_volume_patterns[] = {
        "最大音量", "音量最大"
    };
    
    bool max_volume = false;
    for (const char* pattern : max_volume_patterns) {
        if (lower_text.find(pattern) != std::string::npos) {
            max_volume = true;
            break;
        }
    }
    
    if (max_volume) {
        // 设置音量为100
        codec->SetOutputVolume(100);
        ESP_LOGI(TAG, "设置最大音量");
        
        // 触发开心表情
        ExecuteEmotionAction("happy");
        return true;
    }
    
    // 没有匹配到任何音量控制命令
    return false;
}

// 处理Alert消息
void EmotionResponseController::ProcessAlert(const char* status, const char* message, const char* emotion) {
    // 如果提供了明确的情感标记，直接使用
    if (emotion && strlen(emotion) > 0) {
        ExecuteEmotionAction(emotion);
        ESP_LOGI(TAG, "Processed Alert with emotion: %s", emotion);
        return;
    }
    
    // 否则分析状态和消息内容
    std::string combined_text = std::string(status) + " " + std::string(message);
    std::string detected_emotion = AnalyzeText(combined_text);
    
    // 执行情感动作
    ExecuteEmotionAction(detected_emotion);
    
    ESP_LOGI(TAG, "Processed Alert, detected emotion: %s", detected_emotion.c_str());
}

// 注册情感关键词
void EmotionResponseController::RegisterEmotionKeywords(const std::string& emotion, const std::vector<std::string>& keywords) {
    emotion_keywords_[emotion] = keywords;
}

// 设置默认情感
void EmotionResponseController::SetDefaultEmotion(const std::string& emotion) {
    default_emotion_ = emotion;
}

// 手动触发情感响应
void EmotionResponseController::TriggerEmotion(const std::string& emotion) {
    ESP_LOGI(TAG, "触发情感: %s", emotion.c_str());
    current_emotion_ = emotion;
    
    // 使用情绪动作映射
    ESP_LOGI(TAG, "执行情绪动作: %s", emotion.c_str());
    ExecuteEmotionAction(emotion);
}

// 获取当前情感
const std::string& EmotionResponseController::GetCurrentEmotion() const {
    return current_emotion_;
}

// 初始化情感动作映射
void EmotionResponseController::InitializeEmotionActions() {
    // 定义各种情感对应的动作
    
    // 开心系列情感动作
    emotion_actions_["happy"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::HAPPY);
    };
    
    emotion_actions_["laughing"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::LAUGHING);
    };
    
    emotion_actions_["funny"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::FUNNY);
    };
    
    // 悲伤系列情感动作
    emotion_actions_["sad"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::SAD);
    };
    
    emotion_actions_["cry"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::CRY);
    };
    
    // 愤怒情感动作
    emotion_actions_["anger"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::ANGER);
    };
    
    // 惊讶系列情感动作
    emotion_actions_["surprise"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::SURPRISE);
    };
    
    emotion_actions_["shocked"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::SHOCKED);
    };
    
    // 困惑和思考情感动作
    emotion_actions_["confused"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::CONFUSED);
    };
    
    emotion_actions_["thinking"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::THINKING);
    };
    
    // 中性情感动作
    emotion_actions_["neutral"] = [this]() {
        // 显示中性表情
        emoji_controller_->EyeCenter();
        emoji_controller_->EyeBlink();
    };
    
    // 睡眠情感动作
    emotion_actions_["sleep"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::SLEEP);
    };
    
    // 唤醒情感动作
    emotion_actions_["wakeup"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::WAKEUP);
    };
    
    // 左看情感动作
    emotion_actions_["look_left"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::LOOK_LEFT);
    };
    
    // 右看情感动作
    emotion_actions_["look_right"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::LOOK_RIGHT);
    };
    
    // 点头情感动作
    emotion_actions_["nod"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::HEAD_NOD);
    };
    
    // 摇头情感动作
    emotion_actions_["shake"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::HEAD_SHAKE);
    };
    
    // 抬头情感动作
    emotion_actions_["look_up"] = [this]() {
        emoji_controller_->EyeUp();
        servo_controller_->HeadUp();
    };
    
    // 低头情感动作
    emotion_actions_["look_down"] = [this]() {
        emoji_controller_->EyeDown();
        servo_controller_->HeadDown();
    };
    
    // 居中情感动作
    emotion_actions_["look_center"] = [this]() {
        emoji_controller_->EyeCenter();
        servo_controller_->HeadCenter();
    };
    
    // 点头并开心情感动作
    emotion_actions_["nod_happy"] = [this]() {
        emoji_controller_->EyeHappy();
        servo_controller_->HeadNod(15);
    };
    
    // 摇头并生气情感动作
    emotion_actions_["shake_angry"] = [this]() {
        emoji_controller_->EyeAnger();
        servo_controller_->HeadShake(10);
    };
    
    // 转圈转圈圈情感动作
    emotion_actions_["spin"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::HEAD_ROLL);
    };
    
    // 眨眼情感动作
    emotion_actions_["blink"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::BLINK, 12);
    };
    
    // 跳舞情感动作
    emotion_actions_["dance"] = [this]() {
        // 显示开心表情
        emoji_controller_->EyeHappy();
        
        // 执行跳舞动作：有频率的点头加转圈
        // 先点头几次
        servo_controller_->HeadNod(15);
        servo_controller_->HeadRoll();
        
        // 恢复中心位置
        servo_controller_->HeadCenter(10);
    };
    
    // 尴尬情感动作
    emotion_actions_["awkward"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::AWKWARD);
    };
    
    // 爱心表情动作
    emotion_actions_["loving"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::LOVING);
    };
    
    // 亲吻表情动作
    emotion_actions_["kissy"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::KISSY);
    };
    
    // 酷酷的表情动作
    emotion_actions_["cool"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::COOL);
    };
    
    // 自信表情动作
    emotion_actions_["confident"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::CONFIDENT);
    };
    
    // 放松表情动作
    emotion_actions_["relaxed"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::RELAXED);
    };
    
    // 美味表情动作
    emotion_actions_["delicious"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::DELICIOUS);
    };
    
    // 奇怪表情动作
    emotion_actions_["silly"] = [this]() {
        emoji_controller_->PlayAnimation(AnimationType::SILLY);
    };
}

void EmotionResponseController::InitializeEmotionKeywords() {
    // 清空现有的映射
    emotion_keywords_.clear();
    
    // 睡眠情感关键词 （小智框架有 sleepy 但表现可能不同）
    std::vector<std::string> sleep_keywords = {
        "睡觉", "睡眠", "睡了", "困了", "和平呢", "睡觉呢", "睡觉了", 
        "晚安", "安慰", "晕", "晕晕的", "好累", "累了", "打哺欠", "好困",
        "sleep", "sleeping", "sleepy", "tired", "exhausted", "rest", "nap", "goodnight"
    };
    RegisterEmotionKeywords("sleep", sleep_keywords);
    
    // 唤醒情感关键词
    std::vector<std::string> wakeup_keywords = {
        "醒醒", "醒醒呀", "醒醒了", "醒来", "起床", "醒醒眼", "醒神",
        "早安", "红斗呀", "新的一天", "发生", "来吧", "闲着了",
        "wake", "wakeup", "awake", "awaken", "rise", "arise", "morning", "hello"
    };
    RegisterEmotionKeywords("wakeup", wakeup_keywords);
    
    ESP_LOGI(TAG, "情感关键词初始化完成");
}

// 分析文本内容
std::string EmotionResponseController::AnalyzeText(const std::string& text) {
    // 转换为小写进行匹配
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    
    // 遍历情感关键词映射表，查找匹配的关键词
    for (const auto& emotion_pair : emotion_keywords_) {
        const std::string& emotion = emotion_pair.first;
        const std::vector<std::string>& keywords = emotion_pair.second;
        
        for (const std::string& keyword : keywords) {
            // 使用简单的字符串查找替代正则表达式，减少栈使用
            if (lower_text.find(keyword) != std::string::npos) {
                return emotion;
            }
        }
    }
    
    // 如果没有匹配的关键词，返回默认情感
    return default_emotion_;
}

// 执行情感动作
void EmotionResponseController::ExecuteEmotionAction(const std::string& emotion) {
    ESP_LOGI(TAG, "执行情感动作: %s", emotion.c_str());
    
    // 更新当前情感
    current_emotion_ = emotion;
    
    // 查找并执行情感动作
    auto it = emotion_actions_.find(emotion);
    if (it != emotion_actions_.end()) {
        // 如果找到对应的情感动作，则执行
        it->second();
    } else {
        // 如果没有找到对应的情感动作，则使用默认表情
        if (emoji_controller_ != nullptr) {
            emoji_controller_->PlayAnimation(AnimationType::BLINK, 10);
        }
    }
}

// 处理表情动作命令
bool EmotionResponseController::ProcessEmotionCommand(const std::string& message) {
    // 转换为小写进行匹配
    std::string lower_text = message;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    
    ESP_LOGI(TAG, "检查是否包含表情动作命令: %s", message.c_str());
    
    // 检查是否包含小智框架识别的情绪
    // 小智框架识别的情绪通常以特定的格式出现，如 "[emotion:happy]"
    const std::string emotion_prefix = "[emotion:";
    const std::string emotion_suffix = "]";
    
    size_t prefix_pos = lower_text.find(emotion_prefix);
    if (prefix_pos != std::string::npos) {
        size_t start_pos = prefix_pos + emotion_prefix.length();
        size_t end_pos = lower_text.find(emotion_suffix, start_pos);
        
        if (end_pos != std::string::npos) {
            // 提取小智框架识别的情绪
            std::string recognized_emotion = lower_text.substr(start_pos, end_pos - start_pos);
            ESP_LOGI(TAG, "检测到小智框架识别的情绪: %s", recognized_emotion.c_str());
            
            // 直接使用原有的情绪动作映射
            ESP_LOGI(TAG, "使用原有情绪动作映射: %s", recognized_emotion.c_str());
            ExecuteEmotionAction(recognized_emotion);
            return true;
        }
    }
    
    // 只保留动作命令的关键词识别，移除与小智框架重复的情感关键词识别
    
    // 向左看命令
    const char* look_left_keywords[] = {"看向左边", "向左看", "左转", "往左看", "左看", "看左边"};
    for (const char* keyword : look_left_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到向左看命令");
            ExecuteEmotionAction("look_left");
            return true;
        }
    }
    
    // 向右看命令
    const char* look_right_keywords[] = {"看向右边", "向右看", "右转", "往右看", "右看", "看右边"};
    for (const char* keyword : look_right_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到向右看命令");
            ExecuteEmotionAction("look_right");
            return true;
        }
    }
    
    // 抬头命令
    const char* look_up_keywords[] = {"抬头", "向上看", "看上面", "抬头看"};
    for (const char* keyword : look_up_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到抬头命令");
            ExecuteEmotionAction("look_up");
            return true;
        }
    }
    
    // 低头命令
    const char* look_down_keywords[] = {"低头", "向下看", "看下面", "低头看"};
    for (const char* keyword : look_down_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到低头命令");
            ExecuteEmotionAction("look_down");
            return true;
        }
    }
    
    // 居中命令
    const char* look_center_keywords[] = {"居中", "回正", "恢复正常", "回到中心"};
    for (const char* keyword : look_center_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到居中命令");
            ExecuteEmotionAction("look_center");
            return true;
        }
    }
    
    // 点头命令
    const char* nod_keywords[] = {"点头", "点下头", "说是", "表示同意", "说是的"};
    for (const char* keyword : nod_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到点头命令");
            ExecuteEmotionAction("nod");
            return true;
        }
    }
    
    // 摇头命令
    const char* shake_keywords[] = {"摇头", "摇下头", "说不是", "表示否定", "说不是的"};
    for (const char* keyword : shake_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到摇头命令");
            ExecuteEmotionAction("shake");
            return true;
        }
    }
    
    // 转圈命令
    const char* spin_keywords[] = {"转圈", "圈圈", "绕圈", "转个圈", "转一圈"};
    for (const char* keyword : spin_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到转圈命令");
            ExecuteEmotionAction("spin");
            return true;
        }
    }
    
    // 跳舞命令
    const char* dance_keywords[] = {"跳舞", "舞蹈", "跳个舞", "来支舞", "跳一段"};
    for (const char* keyword : dance_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到跳舞命令");
            ExecuteEmotionAction("dance");
            return true;
        }
    }
    
    //眨眼命令
    const char* blink_keywords[] = {"眨眼", "眨一下", "眨一眨", "眨", "眨眼了"};
    for (const char* keyword : blink_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到眨眼命令");
            ExecuteEmotionAction("blink");
            return true;
        }
    }
    
    // 如果没有匹配到任何表情动作命令，返回false
    return false;
}

// 判断是否应该点头（表示同意、肯定）
bool EmotionResponseController::ShouldNod(const std::string& message) {
    // 转换为小写进行匹配
    std::string lower_text = message;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    
    // 首先检查是否包含否定词，如果包含则不应该点头
    const char* negative_keywords[] = {
        "不", "否", "没", "无", "别", "莫", "勿", "非", "未"
    };
    
    for (const char* keyword : negative_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            // 包含否定词，不应该点头
            ESP_LOGI(TAG, "检测到否定词 '%s'，不执行点头动作", keyword);
            return false;
        }
    }
    
    // 表示同意或肯定的关键词
    const char* nod_keywords[] = {
         "是的！", "对的！","对的。", "正确！", "同意！", "理解！", "明白！", "懂！", "知道！", "没问题！",
         "嗯嗯","赞成！", "支持！", "认同！",  "点头", "点个头", "点了个头", "nods", "nod"
    };
    
    for (const char* keyword : nod_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到肯定词 '%s'，执行点头动作", keyword);
            return true;
        }
    }
    
    return false;
}

// 判断是否应该摇头（表示否定、拒绝）
bool EmotionResponseController::ShouldShake(const std::string& message) {
    // 转换为小写进行匹配
    std::string lower_text = message;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    
    // 表示否定或拒绝的关键词
    const char* shake_keywords[] = {
        "不是！", "不是。", "不对！", "不对。", "不行！", "不行。", "不可以", "不能", "不要！","不要。",
        "不同意！", "不同意。","拒绝！",  "不接受！",
        "不好！", "不好。", "不正确", "不准确", "不允许", "不可能！", "不可能。""没有。", "不存在", "摇头", "摇个头", "摇了个头"
    };
    
    // 先检查完整的否定短语
    for (const char* keyword : shake_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            ESP_LOGI(TAG, "检测到否定短语 '%s'，执行摇头动作", keyword);
            return true;
        }
    }
    
    // 再检查单独的否定词（确保消息内容只有否定词或者以否定词开头）
    const char* simple_negatives[] = {
        "不", "否", "没"
    };
    
    for (const char* keyword : simple_negatives) {
        // 如果消息只有这个词，或者以这个词开头后跟空格或标点符号
        if (lower_text == keyword) {
            ESP_LOGI(TAG, "检测到单独否定词 '%s'，执行摇头动作", keyword);
            return true;
        }
        
        // 检查是否以该关键词开头
        if (lower_text.find(keyword) == 0 && lower_text.length() > strlen(keyword)) {
            // 获取关键词后的第一个字符
            char next_char = lower_text[strlen(keyword)];
            // 检查是否为空格或英文标点
            if (next_char == ' ' || next_char == '.' || next_char == ',') {
                ESP_LOGI(TAG, "检测到单独否定词 '%s'，执行摇头动作", keyword);
                return true;
            }
        }
    }
    
    return false;
}

// 判断是否应该跳舞（表示高兴、庆祝）
bool EmotionResponseController::ShouldDance(const std::string& message) {
    // 转换为小写进行匹配
    std::string lower_text = message;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    
    // 表示高兴或庆祝的关键词
    const char* dance_keywords[] = {
        "跳舞", "舞蹈", "跳", "舞", "动感", "节奏", "音乐", "律动", "跳个舞",
        "dance", "dancing", "jump", "move", "groove", "rhythm", "music", "beat"
    };
    
    for (const char* keyword : dance_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

// 判断是否应该向左看
bool EmotionResponseController::ShouldLookLeft(const std::string& message) {
    // 转换为小写进行匹配
    std::string lower_text = message;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    
    // 提到左边的关键词
    const char* look_left_keywords[] = {
        "左边", "左侧", "左方", "向左", "往左", "左转", "左看", "看左边"
    };
    
    for (const char* keyword : look_left_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

// 判断是否应该向右看
bool EmotionResponseController::ShouldLookRight(const std::string& message) {
    // 转换为小写进行匹配
    std::string lower_text = message;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    
    // 提到右边的关键词
    const char* look_right_keywords[] = {
        "右边", "右侧", "右方", "向右", "往右", "右转", "右看", "看右边"
    };
    
    for (const char* keyword : look_right_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

// 判断是否应该抬头
bool EmotionResponseController::ShouldLookUp(const std::string& message) {
    // 转换为小写进行匹配
    std::string lower_text = message;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    
    // 提到上方的关键词
    const char* look_up_keywords[] = {
        "上面", "上方", "上边", "向上", "往上", "抬头", "仰头", "看上面", "看天空", "天上"
    };
    
    for (const char* keyword : look_up_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

// 判断是否应该低头
bool EmotionResponseController::ShouldLookDown(const std::string& message) {
    // 转换为小写进行匹配
    std::string lower_text = message;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    
    // 提到下方的关键词
    const char* look_down_keywords[] = {
        "下面", "下方", "下边", "向下", "往下", "低头", "俯首", "看下面", "看地面", "地上"
    };
    
    for (const char* keyword : look_down_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

// 情感物联网接口实现
namespace iot {
    // 全局情感响应控制器指针，用于默认构造函数
    static EmotionResponseController* g_emotion_controller = nullptr;
    
    // 设置全局情感响应控制器指针
    void SetGlobalEmotionController(EmotionResponseController* controller) {
        g_emotion_controller = controller;
    }
    
    // 获取全局情感响应控制器指针
    EmotionResponseController* GetGlobalEmotionController() {
        return g_emotion_controller;
    }
} // namespace iot
