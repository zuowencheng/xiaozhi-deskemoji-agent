/**
 * @file emotion_response_controller.h
 * @brief AI回复情感响应控制器头文件
 * 
 * 本文件定义了AI回复情感响应控制器的接口，用于根据AI回复内容自动触发表情和舵机动作
 */

#ifndef EMOTION_RESPONSE_CONTROLLER_H
#define EMOTION_RESPONSE_CONTROLLER_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <unordered_map>
#include "emoji_controller.h"
#include "servo_controller.h"
#include "audio_codec.h"
#include "mcp_server.h"

// 前向声明
class EmotionResponseController;
class EmojiController;
class ServoController;
class AudioCodec;

/**
 * @class EmotionResponseController
 * @brief AI回复情感响应控制器类，负责分析AI回复内容并触发相应的表情和舵机动作
 */
class EmotionResponseController {
public:
    /**
     * @brief 构造函数
     * @param emoji_controller 表情控制器指针
     * @param servo_controller 舵机控制器指针
     * @param audio_codec 音频编解码器指针
     */
    EmotionResponseController(EmojiController* emoji_controller, ServoController* servo_controller, AudioCodec* audio_codec = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~EmotionResponseController();
    
    /**
     * @brief 初始化情感响应控制器
     */
    void Initialize();
    
    /**
     * @brief 处理AI回复
     * @param message AI回复消息
     */
    void ProcessAIResponse(const std::string& message);
    
    /**
     * @brief 处理AI回复JSON
     * @param json_obj AI回复JSON对象
     */
    void ProcessAIResponseJson(const cJSON* json_obj);
    
    /**
     * @brief 处理Alert消息
     * @param status 状态消息
     * @param message 详细消息
     * @param emotion 情感标记
     */
    void ProcessAlert(const char* status, const char* message, const char* emotion = "");
    
    /**
     * @brief 注册情感关键词
     * @param emotion 情感类型
     * @param keywords 关键词列表
     */
    void RegisterEmotionKeywords(const std::string& emotion, const std::vector<std::string>& keywords);
    
    /**
     * @brief 设置默认情感
     * @param emotion 默认情感类型
     */
    void SetDefaultEmotion(const std::string& emotion);
    
    /**
     * @brief 手动触发情感响应
     * @param emotion 情感类型
     */
    void TriggerEmotion(const std::string& emotion);
    
    /**
     * @brief 获取当前情感
     * @return 当前情感类型
     */
    const std::string& GetCurrentEmotion() const;
    
    /**
     * @brief 处理音量控制命令
     * @param message AI回复消息
     * @return 如果是音量控制命令返回true，否则返回false
     */
    bool ProcessVolumeCommand(const std::string& message);
    
    /**
     * @brief 处理表情动作命令
     * @param message AI回复消息
     * @return 如果是表情动作命令返回true，否则返回false
     */
    bool ProcessEmotionCommand(const std::string& message);
    

    
    /**
     * @brief 判断消息内容是否应该触发点头动作（表示同意、肯定）
     * @param message 消息内容
     * @return 如果应该点头返回true，否则返回false
     */
    bool ShouldNod(const std::string& message);
    
    /**
     * @brief 判断消息内容是否应该触发摇头动作（表示否定、拒绝）
     * @param message 消息内容
     * @return 如果应该摇头返回true，否则返回false
     */
    bool ShouldShake(const std::string& message);
    
    /**
     * @brief 判断消息内容是否应该触发跳舞动作（表示高兴、庆祝）
     * @param message 消息内容
     * @return 如果应该跳舞返回true，否则返回false
     */
    bool ShouldDance(const std::string& message);
    
    /**
     * @brief 判断消息内容是否应该触发向左看动作
     * @param message 消息内容
     * @return 如果应该向左看返回true，否则返回false
     */
    bool ShouldLookLeft(const std::string& message);
    
    /**
     * @brief 判断消息内容是否应该触发向右看动作
     * @param message 消息内容
     * @return 如果应该向右看返回true，否则返回false
     */
    bool ShouldLookRight(const std::string& message);
    
    /**
     * @brief 判断消息内容是否应该触发抬头动作
     * @param message 消息内容
     * @return 如果应该抬头返回true，否则返回false
     */
    bool ShouldLookUp(const std::string& message);
    
    /**
     * @brief 判断消息内容是否应该触发低头动作
     * @param message 消息内容
     * @return 如果应该低头返回true，否则返回false
     */
    bool ShouldLookDown(const std::string& message);
    
private:
    EmojiController* emoji_controller_;  // 表情控制器
    ServoController* servo_controller_;  // 舵机控制器
    AudioCodec* audio_codec_;  // 音频编解码器
    std::string current_emotion_;        // 当前情感
    std::string default_emotion_;        // 默认情感
    
    // 情感关键词映射表
    std::unordered_map<std::string, std::vector<std::string>> emotion_keywords_;
    
    // 情感动作映射表
    std::unordered_map<std::string, std::function<void()>> emotion_actions_;
    
    /**
     * @brief 初始化情感动作映射
     */
    void InitializeEmotionActions();
    
    /**
     * @brief 初始化情感关键词映射
     */
    void InitializeEmotionKeywords();
    
    /**
     * @brief 分析文本内容
     * @param text 文本内容
     * @return 检测到的情感类型
     */
    std::string AnalyzeText(const std::string& text);
    
    /**
     * @brief 执行情感动作
     * @param emotion 情感类型
     */
    void ExecuteEmotionAction(const std::string& emotion);
};

namespace iot {
    // 设置全局情感响应控制器指针的函数声明
    void SetGlobalEmotionController(EmotionResponseController* controller);
    
    // 获取全局情感响应控制器指针的函数声明
    EmotionResponseController* GetGlobalEmotionController();
} // namespace iot

#endif // EMOTION_RESPONSE_CONTROLLER_H
