/**
 * @file emoji_controller.cc
 * @brief 表情控制模块实现
 */

#include "emoji_controller.h"
#include <esp_log.h>
#include <cmath>       // 添加数学函数支持，如cos和sin
#include <esp_random.h> // 添加随机数生成器支持
#include <cstring>     // 添加strcmp函数支持
#include <functional>  // 添加std::function支持

#define TAG "EmojiController"
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

EmojiController::EmojiController(Display* display) : display_(display) {
}

EmojiController::~EmojiController() {
    if (animation_task_handle_) {
        vTaskDelete(animation_task_handle_);
    }
    if (animation_timer_task_handle_) {
        vTaskDelete(animation_timer_task_handle_);
    }
    
    CleanupEmojiScreen();
}

void EmojiController::Initialize() {
    ESP_LOGI(TAG, "初始化表情控制器");
    
    // 创建动画队列
    animation_queue_ = xQueueCreate(ANIMATION_QUEUE_SIZE, sizeof(AnimationMessage));
    
    // 创建动画任务
    BaseType_t task_created = xTaskCreate(
        AnimationTask,    // 任务函数
        "AnimationTask",  // 任务名称
        4096,            // 堆栈大小
        this,            // 任务参数
        5,               // 任务优先级
        &animation_task_handle_  // 任务句柄
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "创建动画任务失败");
    }
    
    // 创建动画定时器任务
    task_created = xTaskCreate(
        AnimationTimerTask,        // 任务函数
        "AnimationTimerTask",      // 任务名称
        4096,                      // 堆栈大小
        this,                      // 任务参数
        5,                         // 任务优先级
        &animation_timer_task_handle_       // 任务句柄
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "创建动画定时器任务失败");
    }
}

// 定时器任务函数，替代FreeRTOS定时器
void EmojiController::AnimationTimerTask(void* pvParameters) {
    EmojiController* controller = static_cast<EmojiController*>(pvParameters);
    if (controller == nullptr) {
        ESP_LOGE(TAG, "AnimationTimerTask: 无效的控制器指针");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "AnimationTimerTask: 启动");
    
    // 记录上次随机动画的时间
    TickType_t last_random_time = xTaskGetTickCount();
    
    while (true) {
        // 当前时间
        TickType_t current_time = xTaskGetTickCount();
        
        // 检查是否应该执行随机动画
        if (controller->random_animation_enabled_ && 
            (current_time - last_random_time) >= pdMS_TO_TICKS(RANDOM_ANIMATION_INTERVAL_MS)) {
            ESP_LOGI(TAG, "AnimationTimerTask: 准备执行随机动画");
            
            // 再次检查随机动画是否启用（防止在发送消息前状态改变）
            if (controller->random_animation_enabled_) {
                // 更新上次随机动画的时间
                last_random_time = current_time;
                
                // 发送随机动画消息
                if (controller->animation_queue_ != nullptr) {
                    AnimationMessage msg;
                    msg.type = AnimationType::RANDOM;
                    msg.param = 0;
                    
                    if (xQueueSend(controller->animation_queue_, &msg, 0) != pdPASS) {
                        ESP_LOGW(TAG, "AnimationTimerTask: 发送随机动画消息失败");
                    } else {
                        ESP_LOGI(TAG, "AnimationTimerTask: 已发送随机动画消息");
                    }
                }
            } else {
                ESP_LOGI(TAG, "AnimationTimerTask: 随机动画已被禁用，不发送消息");
                // 仍然更新时间，避免连续检查
                last_random_time = current_time;
            }
        }
        
        // 延迟一段时间
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void EmojiController::AnimationTask(void* pvParameters) {
    EmojiController* controller = static_cast<EmojiController*>(pvParameters);
    if (controller == nullptr) {
        ESP_LOGE(TAG, "AnimationTask: 无效的控制器指针");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "AnimationTask: 启动");
    
    AnimationMessage msg;
    
    while (true) {
        // 等待动画消息
        if (xQueueReceive(controller->animation_queue_, &msg, portMAX_DELAY) == pdPASS) {
            // 如果是随机动画但随机动画被禁用，则跳过
            if (msg.type == AnimationType::RANDOM && !controller->random_animation_enabled_) {
                ESP_LOGI(TAG, "AnimationTask: 随机动画已禁用，跳过执行");
                continue;
            }
            
            ESP_LOGI(TAG, "AnimationTask: 收到动画消息，类型: %d, 参数: %d", (int)msg.type, msg.param);
            
            // 根据动画类型执行相应的动画
            // 在执行动画前检查是否有其他动画正在执行
            if (controller->is_animating_) {
                ESP_LOGW(TAG, "AnimationTask: 已有动画正在执行，跳过此次动画");
                continue;
            }
            
            // 设置动画执行标志
            controller->is_animating_ = true;
            
            // 暂停LVGL任务，减少刷新频率
            controller->SuspendLVGLTask();
            
            try {
                // 执行相应的动画
                switch (msg.type) {
                    case AnimationType::BLINK:
                        controller->ExecuteBlinkAnimation(msg.param);
                        break;
                    case AnimationType::HAPPY:
                        controller->ExecuteHappyAnimation();
                        break;
                    case AnimationType::SAD:
                        controller->ExecuteSadAnimation();
                        break;
                    case AnimationType::ANGER:
                        controller->ExecuteAngerAnimation();
                        break;
                    case AnimationType::SURPRISE:
                        controller->ExecuteSurpriseAnimation();
                        break;
                    case AnimationType::WAKEUP:
                        controller->ExecuteWakeupAnimation();
                        break;
                    case AnimationType::SLEEP:
                        controller->ExecuteSleepAnimation();
                        break;
                    case AnimationType::LOOK_LEFT:
                        controller->ExecuteLookLeftAnimation();
                        break;
                    case AnimationType::LOOK_RIGHT:
                        controller->ExecuteLookRightAnimation();
                        break;
                    case AnimationType::HEAD_NOD:
                        controller->ExecuteHeadNodAnimation();
                        break;
                    case AnimationType::HEAD_SHAKE:
                        controller->ExecuteHeadShakeAnimation();
                        break;
                    case AnimationType::HEAD_ROLL:
                        controller->ExecuteHeadRollAnimation();
                        break;
                    case AnimationType::CONFUSED:
                        controller->ExecuteConfusedAnimation();
                        break;
                    case AnimationType::AWKWARD:
                        controller->ExecuteAwkwardAnimation();
                        break;
                    case AnimationType::CRY:
                        controller->ExecuteCryAnimation();
                        break;
                    case AnimationType::LAUGHING:
                        controller->ExecuteLaughingAnimation();
                        break;
                    case AnimationType::FUNNY:
                        controller->ExecuteFunnyAnimation();
                        break;
                    case AnimationType::LOVING:
                        controller->ExecuteLovingAnimation();
                        break;
                    case AnimationType::EMBARRASSED:
                        controller->ExecuteEmbarrassedAnimation();
                        break;
                    case AnimationType::SHOCKED:
                        controller->ExecuteShockedAnimation();
                        break;
                    case AnimationType::THINKING:
                        controller->ExecuteThinkingAnimation();
                        break;
                    case AnimationType::COOL:
                        controller->ExecuteCoolAnimation();
                        break;
                    case AnimationType::RELAXED:
                        controller->ExecuteRelaxedAnimation();
                        break;
                    case AnimationType::DELICIOUS:
                        controller->ExecuteDeliciousAnimation();
                        break;
                    case AnimationType::KISSY:
                        controller->ExecuteKissyAnimation();
                        break;
                    case AnimationType::CONFIDENT:
                        controller->ExecuteConfidentAnimation();
                        break;
                    case AnimationType::SILLY:
                        controller->ExecuteSillyAnimation();
                        break;
                    case AnimationType::RANDOM:
                        controller->ExecuteRandomAnimation();
                        break;
                    default:
                        ESP_LOGW(TAG, "AnimationTask: 未知的动画类型: %d", (int)msg.type);
                        break;
                }
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "AnimationTask异常: %s", e.what());
            } catch (...) {
                ESP_LOGE(TAG, "AnimationTask未知异常");
            }
            
            // 恢复LVGL任务
            controller->ResumeLVGLTask();
            
            // 清除动画执行标志
            controller->is_animating_ = false;
            
            // 每个动画执行完成后添加短暂停，给系统一些恢复的时间
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    
    // 注意：这里永远不会执行到，因为上面的循环是无限的
    vTaskDelete(NULL);
}

lv_obj_t* EmojiController::CreateEmojiScreen() {
    if (emoji_screen_ != nullptr) return emoji_screen_;

    emoji_screen_ = lv_obj_create(nullptr);  // 创建新屏幕
    lv_obj_set_size(emoji_screen_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(emoji_screen_, lv_color_white(), 0);  // 设置为白色背景，实际显示为黑色
    lv_obj_set_style_border_width(emoji_screen_, 0, 0);  // 无边框
    
    // 创建左眼
    left_eye_ = lv_obj_create(emoji_screen_);
    lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
    lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
    lv_obj_set_style_radius(left_eye_, ref_corner_radius_, 0);
    lv_obj_set_style_bg_color(left_eye_, lv_color_black(), 0);  // 设置为黑色眼睛，实际显示为白色
    lv_obj_set_style_border_width(left_eye_, 0, 0);
    
    // 创建右眼
    right_eye_ = lv_obj_create(emoji_screen_);
    lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
    lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
    lv_obj_set_style_radius(right_eye_, ref_corner_radius_, 0);
    lv_obj_set_style_bg_color(right_eye_, lv_color_black(), 0);  // 设置为黑色眼睛，实际显示为白色
    lv_obj_set_style_border_width(right_eye_, 0, 0);
    
    return emoji_screen_;
}

void EmojiController::CleanupEmojiScreen() {
    if (emoji_screen_ != nullptr) {
        lv_obj_del(emoji_screen_);
        emoji_screen_ = nullptr;
        left_eye_ = nullptr;
        right_eye_ = nullptr;
    }
}

void EmojiController::DrawEmoji(bool is_blinking) {
    is_blinking_ = is_blinking;
    
    // 确保屏幕和眼睛对象存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "DrawEmoji: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 尝试获取锁
    bool lock_success = false;
    int retry_count = 0;
    const int max_retries = 3;

    while (!lock_success && retry_count < max_retries) {
        if (display_) {
            try {
                DisplayLockGuard lock(display_);
                
                // 更新眼睛位置和大小
                lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
                lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
                lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
                lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
                
                lock_success = true;
            } catch (...) {
                ESP_LOGW(TAG, "DrawEmoji: 获取显示锁失败，重试 %d/%d", retry_count + 1, max_retries);
                retry_count++;
                vTaskDelay(pdMS_TO_TICKS(10)); // 短暂延迟后重试
            }
        } else {
            ESP_LOGW(TAG, "DrawEmoji: 显示对象不存在");
            return;
        }
    }

    if (!lock_success) {
        ESP_LOGE(TAG, "DrawEmoji: 多次尝试获取显示锁失败");
    }
}

void EmojiController::EyeCenter(bool update_display) {
    // 恢复眼睛到中心位置和默认大小
    left_eye_x_ = DISPLAY_WIDTH / 2 - ref_eye_width_ / 2 - ref_space_between_eye_ / 2;
    left_eye_y_ = DISPLAY_HEIGHT / 2;
    left_eye_width_ = ref_eye_width_;
    left_eye_height_ = ref_eye_height_;
    
    right_eye_x_ = DISPLAY_WIDTH / 2 + ref_eye_width_ / 2 + ref_space_between_eye_ / 2;
    right_eye_y_ = DISPLAY_HEIGHT / 2;
    right_eye_width_ = ref_eye_width_;
    right_eye_height_ = ref_eye_height_;
    
    if (update_display) {
        if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
            ESP_LOGW(TAG, "EyeCenter: 屏幕或眼睛对象不存在");
            return;
        }
        
        if (display_) {
            try {
                DisplayLockGuard lock(display_);
                
                // 直接更新眼睛对象
                lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
                lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
                lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
                lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
                
                // 恢复圆角半径
                lv_obj_set_style_radius(left_eye_, ref_corner_radius_, 0);
                lv_obj_set_style_radius(right_eye_, ref_corner_radius_, 0);
            } catch (...) {
                ESP_LOGW(TAG, "EyeCenter: 获取显示锁失败");
            }
        }
    }
}

void EmojiController::EyeBlink(int speed) {
    PlayAnimation(AnimationType::BLINK, speed);
}

void EmojiController::EyeHappy() {
    PlayAnimation(AnimationType::HAPPY);
}

void EmojiController::EyeSad() {
    PlayAnimation(AnimationType::SAD);
}

void EmojiController::EyeAnger() {
    PlayAnimation(AnimationType::ANGER);
}

void EmojiController::EyeSurprise() {
    PlayAnimation(AnimationType::SURPRISE);
}

void EmojiController::EyeWakeup() {
    PlayAnimation(AnimationType::WAKEUP);
}

void EmojiController::EyeSleep() {
    PlayAnimation(AnimationType::SLEEP);
}

void EmojiController::EyeRight() {
    PlayAnimation(AnimationType::LOOK_RIGHT);
}

void EmojiController::EyeLeft() {
    PlayAnimation(AnimationType::LOOK_LEFT);
}

void EmojiController::Saccade(int direction_x, int direction_y) {
    left_eye_x_ += direction_x;
    left_eye_y_ += direction_y;
    right_eye_x_ += direction_x;
    right_eye_y_ += direction_y;

    DrawEmoji(false);
}

void EmojiController::MoveEye(int direction) {
    // direction == -1 : 向左移动
    // direction == 1 : 向右移动
    
    // 参考原始Arduino代码的参数
    int direction_oversize = 1;  // 调整为与原始代码一致的变形幅度
    int direction_movement_amplitude = 2;  // 调整为与原始代码一致的移动幅度
    int eye_blink_amplitude = 5;
    
    // 确保屏幕和眼睛对象存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "MoveEye: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 第一阶段：眼睛向指定方向移动，同时变小
    for (int i = 0; i < 3; i++) {  // 减少步骤数，与原始代码保持一致
        if (display_) {
            DisplayLockGuard lock(display_);
            
            left_eye_x_ += direction_movement_amplitude * direction;
            right_eye_x_ += direction_movement_amplitude * direction;
            right_eye_height_ -= eye_blink_amplitude;
            left_eye_height_ -= eye_blink_amplitude;
            
            // 向右看时，右眼变大
            if (direction > 0) {
                right_eye_height_ += direction_oversize;
                right_eye_width_ += direction_oversize;
            } else {
                // 向左看时，左眼变大
                left_eye_height_ += direction_oversize;
                left_eye_width_ += direction_oversize;
            }
            
            // 在锁内直接更新显示，不使用DrawEmoji
            lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
            lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
            lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
        }
        
        // 降低延迟时间，减少锁定时间
        vTaskDelay(pdMS_TO_TICKS(10));  // 与原始代码类似的短延迟
    }
    
    // 第二阶段：继续向指定方向移动，眼睛恢复高度
    for (int i = 0; i < 3; i++) {  // 减少步骤数，与原始代码保持一致
        if (display_) {
            DisplayLockGuard lock(display_);
            
            left_eye_x_ += direction_movement_amplitude * direction;
            right_eye_x_ += direction_movement_amplitude * direction;
            right_eye_height_ += eye_blink_amplitude;
            left_eye_height_ += eye_blink_amplitude;
            
            // 向右看时，右眼继续变大
            if (direction > 0) {
                right_eye_height_ += direction_oversize;
                right_eye_width_ += direction_oversize;
            } else {
                // 向左看时，左眼继续变大
                left_eye_height_ += direction_oversize;
                left_eye_width_ += direction_oversize;
            }
            
            // 在锁内直接更新显示，不使用DrawEmoji
            lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
            lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
            lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
        }
        
        // 降低延迟时间，减少锁定时间
        vTaskDelay(pdMS_TO_TICKS(10));  // 与原始代码类似的短延迟
    }
}

// 恢复LVGL任务
void EmojiController::ResumeLVGLTask() {
    if (!lvgl_task_suspended_) {
        return;
    }
    
    // 查找LVGL任务句柄
    TaskHandle_t lvgl_task = nullptr;
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t* task_status_array = new TaskStatus_t[task_count];
    
    if (task_status_array) {
        // 获取所有任务状态
        task_count = uxTaskGetSystemState(task_status_array, task_count, nullptr);
        
        // 查找LVGL任务
        for (UBaseType_t i = 0; i < task_count; i++) {
            if (strcmp(task_status_array[i].pcTaskName, "taskLVGL") == 0) {
                lvgl_task = task_status_array[i].xHandle;
                break;
            }
        }
        
        delete[] task_status_array;
    }
    
    // 如果找到LVGL任务，恢复其优先级
    if (lvgl_task) {
        vTaskPrioritySet(lvgl_task, saved_lvgl_task_priority_);
        lvgl_task_suspended_ = false;
        ESP_LOGI(TAG, "LVGL任务优先级已恢复");
    }
}

// 安全执行动画的辅助函数
bool EmojiController::SafeExecuteAnimation(std::function<void()> animation_func) {
    // 如果已经有动画在执行，跳过此次动画
    if (is_animating_) {
        ESP_LOGW(TAG, "SafeExecuteAnimation: 已有动画正在执行，跳过此次动画");
        return false;
    }
    
    // 设置动画执行标志
    is_animating_ = true;
    
    // 暂停LVGL任务，减少刷新频率
    SuspendLVGLTask();
    
    try {
        // 执行动画函数
        animation_func();
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "SafeExecuteAnimation异常: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "SafeExecuteAnimation未知异常");
    }
    
    // 恢复LVGL任务
    ResumeLVGLTask();
    
    // 清除动画执行标志
    is_animating_ = false;
    
    return true;
}

void EmojiController::PlayAnimation(AnimationType type, int param) {
    ESP_LOGI(TAG, "播放动画，类型: %d, 参数: %d", (int)type, param);
    
    // 检查动画队列是否已创建
    if (animation_queue_ == nullptr) {
        ESP_LOGE(TAG, "PlayAnimation: 动画队列未创建");
        return;
    }
    
    // 创建动画消息
    AnimationMessage msg;
    msg.type = type;
    msg.param = param;
    
    // 发送动画消息到队列
    if (xQueueSend(animation_queue_, &msg, 0) != pdPASS) {
        ESP_LOGW(TAG, "PlayAnimation: 发送动画消息失败");
    } else {
        ESP_LOGI(TAG, "已发送动画消息，类型: %d", static_cast<int>(type));
    }
}

void EmojiController::StopAnimation() {
    // 停止所有动画
    ESP_LOGI(TAG, "停止所有动画");
    
    // 恢复眼睛状态并更新显示
    EyeCenter(true);
    
    // 清空动画队列
    ClearAnimationQueue();
    
    // 清除动画执行标志
    is_animating_ = false;
}

/**
 * @brief 设置随机动画是否启用
 * @param enabled 是否启用随机动画
 */
void EmojiController::SetRandomAnimationEnabled(bool enabled) {
    ESP_LOGI(TAG, "设置随机动画状态: %s", enabled ? "启用" : "禁用");
    random_animation_enabled_ = enabled;
    
    // 如果禁用随机动画，清空动画队列以防止已经在队列中的随机动画被执行
    if (!enabled) {
        ClearAnimationQueue();
    }
}

/**
 * @brief 清空动画队列
 */
void EmojiController::ClearAnimationQueue() {
    if (animation_queue_ == nullptr) {
        return;
    }
    
    ESP_LOGI(TAG, "清空动画队列");
    
    // 清空队列中的所有消息
    AnimationMessage msg;
    while (xQueueReceive(animation_queue_, &msg, 0) == pdPASS) {
        // 只是从队列中移除消息，不做任何处理
        ESP_LOGD(TAG, "从队列中移除动画消息，类型: %d", (int)msg.type);
    }
}

AnimationType EmojiController::SelectRandomAnimation() {
    // 生成一个0-99的随机数
    uint32_t random_value = esp_random() % 100;
    
    // 根据概率分布选择动画类型
    // 眨眼: 60%
    // 向左看: 15%
    // 向右看: 15%
    // 转圈: 5%
    // 疑惑: 5%
    if (random_value < 60) {
        // 60%概率：眨眼
        return AnimationType::BLINK;
    } else if (random_value < 78) {
        // 18%概率：向左看
        return AnimationType::LOOK_LEFT;
    } else if (random_value < 96) {
        // 18%概率：向右看
        return AnimationType::LOOK_RIGHT;
    } else if (random_value < 98) {
        // 2%概率：转圈
        return AnimationType::HEAD_ROLL;
    } else {
        // 2%概率：疑惑
        return AnimationType::CONFUSED;
    }
}

void EmojiController::PlayRandomAnimation() {
    AnimationType type = SelectRandomAnimation();
    PlayAnimation(type);
}

void EmojiController::ExecuteRandomAnimation() {
    AnimationType type = SelectRandomAnimation();
    ESP_LOGI(TAG, "执行随机动画，类型: %d", static_cast<int>(type));
    
    // 检查屏幕和眼睛对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteRandomAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 根据选择的动画类型执行相应的动画
    switch (type) {
        case AnimationType::BLINK:
            ExecuteBlinkAnimation(12);
            break;
        case AnimationType::LOOK_LEFT:
            ExecuteLookLeftAnimation();
            break;
        case AnimationType::LOOK_RIGHT:
            ExecuteLookRightAnimation();
            break;
        case AnimationType::HEAD_NOD:
            ExecuteHeadNodAnimation();
            break;
        case AnimationType::HAPPY:
            ExecuteHappyAnimation();
            break;
        case AnimationType::HEAD_ROLL:
            ExecuteHeadRollAnimation();
            break;
        case AnimationType::SAD:
            ExecuteSadAnimation();
            break;
        case AnimationType::ANGER:
            ExecuteAngerAnimation();
            break;
        case AnimationType::SURPRISE:
            ExecuteSurpriseAnimation();
            break;
        case AnimationType::CONFUSED:
            ExecuteConfusedAnimation();
            break;
        case AnimationType::AWKWARD:
            ExecuteAwkwardAnimation();
            break;
        default:
            ExecuteBlinkAnimation(12);
            break;
    }
}

void EmojiController::ExecuteBlinkAnimation(int speed) {
    ESP_LOGI(TAG, "执行眼眼动画，速度: %d", speed);
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteBlinkAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 检查是否有舵机控制器
    bool has_servo = (servo_controller_ != nullptr);
    
    // 保存原始高度
    int original_left_height_ = left_eye_height_;
    int original_right_height_ = right_eye_height_;
    
    // 眨眼动画：眼睛高度从当前值逐渐减小到0，然后再恢复
    const int steps = 8;  // 增加步骤数，使动画更平滑
    int left_step = original_left_height_ / steps;
    int right_step = original_right_height_ / steps;
    
    // 确保步长至少为1
    left_step = left_step > 0 ? left_step : 1;
    right_step = right_step > 0 ? right_step : 1;
    
    // 计算延迟时间，速度越大，延迟越小
    int delay_ms = 200 / speed;  // 增加基础延迟时间
    if (delay_ms < 10) delay_ms = 10; // 确保最小延迟
    
    // 随机决定是眨眼一次还是连续眨眼两次
    bool double_blink = (esp_random() % 100) < 40;  // 40%的概率连续眨眼两次
    int blink_count = double_blink ? 2 : 1;
    
    // 随机决定是否添加轻微的头部运动 - 只有30%的概率
    bool add_head_movement = has_servo && ((esp_random() % 100) < 30);
    int head_direction = 0; // 0=不动，1=轻微左下，2=轻微右上，3=轻微抬头，4=轻微低头，5=轻微左上，6=轻微右下
    
    if (add_head_movement) {
        // 随机选择一个方向，避免使用模运算减少计算量
        uint32_t rand_val = esp_random() & 0x07; // 取最低3位，值为0-7
        head_direction = (rand_val % 6) + 1; // 1-6，确保范围正确
        ESP_LOGI(TAG, "眨眼时添加头部运动，方向: %d", head_direction);
    }
    
    ESP_LOGI(TAG, "眨眼次数: %d", blink_count);
    
    // 使用一个锁来保护整个眨眼过程
    if (display_) {
        try {
            // 执行一次或两次眨眼
            for (int blink = 0; blink < blink_count; blink++) {
                // 眼睛闭合阶段
                for (int i = 0; i < steps; i++) {
                    DisplayLockGuard lock(display_);
                    
                    left_eye_height_ -= left_step;
                    right_eye_height_ -= right_step;
                    
                    // 确保高度不会变为负数
                    if (left_eye_height_ < 0) left_eye_height_ = 0;
                    if (right_eye_height_ < 0) right_eye_height_ = 0;
                    
                    // 直接更新眼睛对象
                    lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
                    lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
                    lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
                    lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
                    
                    // 每步动画后短暂延迟，使动画更明显
                    vTaskDelay(pdMS_TO_TICKS(delay_ms));
                }
                
                // 如果需要添加头部运动
                if (has_servo && blink == 0) {
                    // 执行轻微的头部运动
                    const int small_angle = 25; // 增加到20度，使动作更明显但仍然自然
                    // const int delay_param = 30; // 舵机运动延迟参数
                    
                    // 使用HeadMove方法实现所有头部运动
                    switch (head_direction) {
                        case 1: // 轻微左下
                            servo_controller_->HeadMove(-small_angle, -small_angle, SERVO_DELAY);
                            break;
                        case 2: // 轻微右上
                            servo_controller_->HeadMove(small_angle, small_angle, SERVO_DELAY);
                            break;
                        case 3: // 轻微抬头
                            servo_controller_->HeadMove(0, small_angle, SERVO_DELAY);
                            break;
                        case 4: // 轻微低头
                            servo_controller_->HeadMove(0, -small_angle, SERVO_DELAY);
                            break;
                        case 5: // 轻微左上
                            servo_controller_->HeadMove(-small_angle, small_angle, SERVO_DELAY);
                            break;
                        case 6: // 轻微右下
                            servo_controller_->HeadMove(small_angle, -small_angle, SERVO_DELAY);
                            break;
                            
                    }
                }
                
                // 短暂保持闭眼状态
                // 如果是连续眨眼，第一次眨眼后闭眼时间短一些
                int closed_delay = double_blink && blink == 0 ? delay_ms : delay_ms * 3;
                vTaskDelay(pdMS_TO_TICKS(closed_delay));
                
                // 眼睛睁开阶段
                for (int i = 0; i < steps; i++) {
                    DisplayLockGuard lock(display_);
                    
                    left_eye_height_ += left_step;
                    right_eye_height_ += right_step;
                    
                    // 确保不会超过原始高度
                    if (left_eye_height_ > original_left_height_) left_eye_height_ = original_left_height_;
                    if (right_eye_height_ > original_right_height_) right_eye_height_ = original_right_height_;
                    
                    // 直接更新眼睛对象
                    lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
                    lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
                    lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
                    lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
                    
                    // 每步动画后短暂延迟，使动画更明显
                    vTaskDelay(pdMS_TO_TICKS(delay_ms));
                }
                
                // 如果是连续眨眼，在两次眨眼之间短暂停顿
                if (double_blink && blink == 0) {
                    vTaskDelay(pdMS_TO_TICKS(delay_ms * 2));
                }
                
                // 在最后一次眨眼完成后，如果有头部运动，恢复头部位置
                if (add_head_movement && has_servo && blink == blink_count - 1) {
                    servo_controller_->HeadCenter(10); // 使用延迟参数10恢复头部位置
                }
            }
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteBlinkAnimation: 获取显示锁失败");
        }
    }
    
    // 确保眼睛高度恢复到原始值
    left_eye_height_ = original_left_height_;
    right_eye_height_ = original_right_height_;
}

void EmojiController::ExecuteHappyAnimation() {
    ESP_LOGI(TAG, "执行开心表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteHappyAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 创建开心表情 - 简化版本，减少循环次数和延迟
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 获取眼睛的位置和大小
            int offset = left_eye_height_ / 2;
            
            // 创建左眼和右眼三角形对象
            lv_obj_t* left_eye_mask = nullptr;
            lv_obj_t* right_eye_mask = nullptr;
            
            // 减少循环次数，从10次减少到5次
            for (int i = 0; i < 5; i++) {
                // 如果已经创建了三角形对象，先删除
                if (left_eye_mask) lv_obj_del(left_eye_mask);
                if (right_eye_mask) lv_obj_del(right_eye_mask);
                
                // 创建左眼三角形
                left_eye_mask = lv_obj_create(emoji_screen_);
                lv_obj_set_style_bg_color(left_eye_mask, lv_color_white(), 0); // 设置为白色
                lv_obj_set_style_border_width(left_eye_mask, 0, 0);
                lv_obj_set_style_radius(left_eye_mask, 0, 0); // 设置为直角
                
                // 创建一个三角形形状的对象
                // 使用三个点来定义三角形区域
                int x1 = left_eye_x_ - left_eye_width_/2 - 5;
                int y1 = left_eye_y_ + offset - 3;
                int x2 = left_eye_x_ + left_eye_width_/2 + 5;
                // y2变量已被使用，保留
                // x3变量已被使用，保留
                int y3 = left_eye_y_ + left_eye_height_ + offset + 3;
                
                // 设置三角形的大小和位置 - 使用矩形近似
                int tri_width = x2 - x1 + 15;  // 增加宽度
                int tri_height = y3 - y1 + 15;  // 增加高度
                
                lv_obj_set_size(left_eye_mask, tri_width, tri_height);
                lv_obj_set_pos(left_eye_mask, x1 - 8, y1 - 8);
                
                // 使用CSS的clip-path属性创建三角形效果
                // 但LVGL可能不支持clip-path，所以我们使用一个简单的变通方法
                // 设置左眼三角形的旋转和倾斜
                lv_obj_set_style_transform_angle(left_eye_mask, 80, 0); // 8度，单位是十分之一度
                lv_obj_set_style_transform_pivot_x(left_eye_mask, 0, 0);
                lv_obj_set_style_transform_pivot_y(left_eye_mask, 0, 0);
                
                // 创建右眼三角形
                right_eye_mask = lv_obj_create(emoji_screen_);
                lv_obj_set_style_bg_color(right_eye_mask, lv_color_white(), 0); // 设置为白色
                lv_obj_set_style_border_width(right_eye_mask, 0, 0);
                lv_obj_set_style_radius(right_eye_mask, 0, 0); // 设置为直角
                
                // 使用三个点来定义三角形区域
                int rx1 = right_eye_x_ + right_eye_width_/2 + 5;
                int ry1 = right_eye_y_ + offset - 3;
                int rx2 = right_eye_x_ - right_eye_width_/2 - 5;
                // ry2变量已被使用，保留
                // rx3变量已被使用，保留
                int ry3 = right_eye_y_ + right_eye_height_ + offset + 3;
                
                // 设置三角形的大小和位置 - 使用矩形近似
                int rtri_width = rx1 - rx2 + 15;  // 增加宽度
                int rtri_height = ry3 - ry1 + 15;  // 增加高度
                
                lv_obj_set_size(right_eye_mask, rtri_width, rtri_height);
                lv_obj_set_pos(right_eye_mask, rx2 - 8, ry1 - 8);
                
                // 设置右眼三角形的旋转和倾斜
                lv_obj_set_style_transform_angle(right_eye_mask, -80, 0); // -8度，单位是十分之一度
                lv_obj_set_style_transform_pivot_x(right_eye_mask, rtri_width, 0);
                lv_obj_set_style_transform_pivot_y(right_eye_mask, 0, 0);
                
                // 更新offset，与参考代码一致
                offset -= 2;
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟，形成动画效果，减少延迟时间
                vTaskDelay(pdMS_TO_TICKS(10)); // 从20ms减少到10ms
            }
            
            // 如果需要，让舵机稍微向上抬头，表示高兴
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadMove(0, -15, 10);  // 负的y偏移会让舵机向上移动
            }
            
            // 显示一段时间，减少显示时间
            vTaskDelay(pdMS_TO_TICKS(1000)); // 从2000ms减少到1000ms
            
            // 清理资源
            if (left_eye_mask) lv_obj_del(left_eye_mask);
            if (right_eye_mask) lv_obj_del(right_eye_mask);
            
            // 恢复眼睛状态
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 刷新显示
            lv_refr_now(NULL);
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "ExecuteHappyAnimation异常: %s", e.what());
        }
    } else {
        ESP_LOGW(TAG, "ExecuteHappyAnimation: 显示对象不存在");
    }
}

void EmojiController::ExecuteSadAnimation() {
    ESP_LOGI(TAG, "执行悲伤表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteSadAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 创建悲伤表情 - 使用三角形
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 获取眼睛的位置和大小
            int offset = left_eye_height_ / 4;  // 减小初始偏移量
            
            // 创建左眼和右眼三角形对象
            lv_obj_t* left_eye_mask = nullptr;
            lv_obj_t* right_eye_mask = nullptr;
            
            // 参考boardemoji.ino中的实现
            // 循环绘制多个三角形，逐渐移动位置，形成动画效果
            for (int i = 0; i < 10; i++) {
                // 如果已经创建了三角形对象，先删除
                if (left_eye_mask) lv_obj_del(left_eye_mask);
                if (right_eye_mask) lv_obj_del(right_eye_mask);
                
                // 创建左眼三角形
                left_eye_mask = lv_obj_create(emoji_screen_);
                lv_obj_set_style_bg_color(left_eye_mask, lv_color_white(), 0); // 设置为白色
                lv_obj_set_style_border_width(left_eye_mask, 0, 0);
                lv_obj_set_style_radius(left_eye_mask, 0, 0); // 设置为直角
                
                // 计算开心表情的三角形坐标
                int happy_x1 = left_eye_x_ - left_eye_width_/2 - 5;
                int happy_y1 = left_eye_y_ + offset - 3;
                int happy_x2 = left_eye_x_ + left_eye_width_/2 + 5;
                int happy_y2 = left_eye_y_ + 5 + offset;
                int happy_x3 = left_eye_x_ - left_eye_width_/2 - 5;
                int happy_y3 = left_eye_y_ + left_eye_height_ + offset + 3;
                
                // 计算悲伤表情的三角形坐标（垂直翻转）
                int sad_x1 = happy_x1;
                int sad_y1 = 2 * left_eye_y_ - happy_y1;
                int sad_x2 = happy_x2;
                // int sad_y2 = 2 \* left_eye_y_ - happy_y2;
                // int sad_x3 = happy_x3;
                int sad_y3 = 2 * left_eye_y_ - happy_y3;
                
                // 设置三角形的大小和位置
                int tri_width = sad_x2 - sad_x1 + 15;
                int tri_height = abs(sad_y3 - sad_y1) + 15;
                
                // 确定左上角坐标（取最小的x和y）
                int top_left_x = sad_x1 - 8;
                int top_left_y = std::min(sad_y1, sad_y3) - 15;  
                
                lv_obj_set_size(left_eye_mask, tri_width, tri_height);
                lv_obj_set_pos(left_eye_mask, top_left_x, top_left_y);
                
                // 设置左眼三角形的旋转和倾斜
                lv_obj_set_style_transform_angle(left_eye_mask, -100, 0); // -10度，单位是十分之一度
                // 设置旋转中心点为三角形左下角
                lv_obj_set_style_transform_pivot_x(left_eye_mask, 0, 0);
                lv_obj_set_style_transform_pivot_y(left_eye_mask, tri_height, 0);
                
                // 创建右眼三角形
                right_eye_mask = lv_obj_create(emoji_screen_);
                lv_obj_set_style_bg_color(right_eye_mask, lv_color_white(), 0); // 设置为白色
                lv_obj_set_style_border_width(right_eye_mask, 0, 0);
                lv_obj_set_style_radius(right_eye_mask, 0, 0); // 设置为直角
                
                // 计算开心表情的三角形坐标
                int happy_rx1 = right_eye_x_ + right_eye_width_/2 + 5;
                int happy_ry1 = right_eye_y_ + offset - 3;
                int happy_rx2 = right_eye_x_ - right_eye_width_/2 - 5;
                int happy_ry2 = right_eye_y_ + 5 + offset;
                int happy_rx3 = right_eye_x_ + right_eye_width_/2 + 5;
                int happy_ry3 = right_eye_y_ + right_eye_height_ + offset + 3;
                
                // 计算悲伤表情的三角形坐标（垂直翻转）
                int sad_rx1 = happy_rx1;
                int sad_ry1 = 2 * right_eye_y_ - happy_ry1;
                int sad_rx2 = happy_rx2;
                // int sad_ry2 = 2 \* right_eye_y_ - happy_ry2;
                // int sad_rx3 = happy_rx3;
                int sad_ry3 = 2 * right_eye_y_ - happy_ry3;
                
                // 设置三角形的大小和位置
                int rtri_width = sad_rx1 - sad_rx2 + 15;
                int rtri_height = abs(sad_ry3 - sad_ry1) + 15;
                
                // 确定左上角坐标（取最小的x和y）
                int rtop_left_x = sad_rx2 - 8;
                int rtop_left_y = std::min(sad_ry1, sad_ry3) - 15;  
                
                lv_obj_set_size(right_eye_mask, rtri_width, rtri_height);
                lv_obj_set_pos(right_eye_mask, rtop_left_x, rtop_left_y);
                
                // 设置右眼三角形的旋转和倾斜
                lv_obj_set_style_transform_angle(right_eye_mask, 100, 0); // 10度，单位是十分之一度
                // 设置旋转中心点为三角形右下角
                lv_obj_set_style_transform_pivot_x(right_eye_mask, rtri_width, 0);
                lv_obj_set_style_transform_pivot_y(right_eye_mask, rtri_height, 0);
                
                // 更新offset，与参考代码一致
                offset -= 2;
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            
            // 如果需要，让舵机稍微向下低头，表示悲伤
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadMove(0, 20, 1);  // 向下点头
                servo_controller_->HeadMove(0, -20, 1);  // 向上抬头
            }
            
            // 显示一段时间
            vTaskDelay(pdMS_TO_TICKS(2000));
            
            // 删除创建的对象
            if (left_eye_mask) lv_obj_del(left_eye_mask);
            if (right_eye_mask) lv_obj_del(right_eye_mask);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteSadAnimation: 获取显示锁失败");
        }
    }
    
    // 添加短暂延迟，确保显示更新完成
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
    
    // 恢复原始状态
    EyeCenter();
}

void EmojiController::ExecuteLaughingAnimation() {
    ESP_LOGI(TAG, "执行大笑表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteLaughingAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸
            const int original_left_width_ = left_eye_width_;
            const int original_left_height_ = left_eye_height_;
            const int original_right_width_ = right_eye_width_;
            const int original_right_height_ = right_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
            
            // 大笑表情 - 眼睛变成弯月形状（通过缩小高度和旋转实现）
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建动画效果 - 眼睛高度缩小，并且向上移动
            const int animation_steps = 8;
            const int animation_delay_ms = 10;
            const float min_height_factor = 0.3f; // 最小高度为原始高度的30%
            
            // 眼睛缩小动画
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                float height_factor = 1.0f - progress * (1.0f - min_height_factor);
                
                // 计算当前尺寸
                int current_left_height = original_left_height_ * height_factor;
                int current_right_height = original_right_height_ * height_factor;
                
                // 更新眼睛高度和位置（向上移动）
                lv_obj_set_height(left_eye_, current_left_height);
                lv_obj_set_height(right_eye_, current_right_height);
                
                // 眼睛位置向上移动，保持底部位置不变
                int y_offset = (original_left_height_ - current_left_height) / 2;
                lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, 
                              left_eye_y_ - current_left_height/2 - y_offset);
                lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, 
                              right_eye_y_ - current_right_height/2 - y_offset);
                
                // 设置圆角 - 大笑时增加圆角
                int current_radius = original_corner_radius_ + (int)(progress * 10);
                lv_obj_set_style_radius(left_eye_, current_radius, 0);
                lv_obj_set_style_radius(right_eye_, current_radius, 0);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 如果有舵机控制器，则控制舵机向后仰头表示大笑
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadUp(15); // 抬头角度大一些，表示大笑
            }
            
            // 添加抖动效果，模拟笑得发抖
            const int shake_steps = 5;
            const int shake_delay_ms = 50;
            const int shake_offset = 2;
            
            for (int i = 0; i < shake_steps; i++) {
                // 左右小幅度抖动
                int offset_x = (i % 2 == 0) ? shake_offset : -shake_offset;
                
                // 更新位置
                lv_obj_set_pos(left_eye_, 
                              left_eye_x_ - left_eye_width_/2 + offset_x, 
                              lv_obj_get_y(left_eye_));
                lv_obj_set_pos(right_eye_, 
                              right_eye_x_ - right_eye_width_/2 + offset_x, 
                              lv_obj_get_y(right_eye_));
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(shake_delay_ms));
            }
            
            // 保持大笑状态一段时间
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // 恢复原始状态
            lv_obj_set_size(left_eye_, original_left_width_, original_left_height_);
            lv_obj_set_size(right_eye_, original_right_width_, original_right_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - original_left_width_/2, left_eye_y_ - original_left_height_/2);
            lv_obj_set_pos(right_eye_, right_eye_x_ - original_right_width_/2, right_eye_y_ - original_right_height_/2);
            lv_obj_set_style_radius(left_eye_, original_corner_radius_, 0);
            lv_obj_set_style_radius(right_eye_, original_corner_radius_, 0);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteLaughingAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteConfidentAnimation() {
    ESP_LOGI(TAG, "执行自信表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteConfidentAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸和位置
            // 未使用的变量，注释掉
            // const int original_left_width_ = left_eye_width_;
            // 未使用的变量，注释掉
            // const int original_left_height_ = left_eye_height_;
            // 未使用的变量，注释掉
            // const int original_right_width_ = right_eye_width_;
            // 未使用的变量，注释掉
            // const int original_right_height_ = right_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
            
            // 自信表情 - 眼睛缩窄并且微微抬头
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建自信动画效果 - 眼睛缩窄
            const int animation_steps = 8;
            const int animation_delay_ms = 15;
            const float min_height_factor = 0.5f; // 最小高度为原始高度的50%
            
            // 眼睛缩窄动画
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                float height_factor = 1.0f - progress * (1.0f - min_height_factor);
                
                // 计算当前尺寸
                int current_left_height = original_left_height_ * height_factor;
                int current_right_height = original_right_height_ * height_factor;
                
                // 更新眼睛高度和位置
                lv_obj_set_height(left_eye_, current_left_height);
                lv_obj_set_height(right_eye_, current_right_height);
                
                lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, 
                              left_eye_y_ - current_left_height/2);
                lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, 
                              right_eye_y_ - current_right_height/2);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 如果有舵机控制器，则控制舵机抬头
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadUp(10); // 抬头表示自信
            }
            
            // 添加眼睛看向右侧的效果
            EyeRight();
            vTaskDelay(pdMS_TO_TICKS(300));
            
            // 添加头部微微点头的效果
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadNod(5); // 头部小幅度点头
            }
            
            // 保持自信状态一段时间
            vTaskDelay(pdMS_TO_TICKS(800));
            
            // 恢复原始状态
            lv_obj_set_height(left_eye_, original_left_height_);
            lv_obj_set_height(right_eye_, original_right_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - original_left_width_/2, left_eye_y_ - original_left_height_/2);
            lv_obj_set_pos(right_eye_, right_eye_x_ - original_right_width_/2, right_eye_y_ - original_right_height_/2);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteConfidentAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置和眼睛位置
    EyeCenter(true);
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteSillyAnimation() {
    ESP_LOGI(TAG, "执行偷笑表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteSillyAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸和位置
            // 未使用的变量，注释掉
            // const int original_left_width_ = left_eye_width_;
            // 未使用的变量，注释掉
            // const int original_left_height_ = left_eye_height_;
            // 未使用的变量，注释掉
            // const int original_right_width_ = right_eye_width_;
            // 未使用的变量，注释掉
            // const int original_right_height_ = right_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
            
            // 偷笑表情 - 一只眼睛缩小，一只眼睛正常
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建偷笑动画效果 - 左眼缩小
            const int animation_steps = 8;
            const int animation_delay_ms = 15;
            const float min_scale = 0.4f; // 最小缩放比例
            
            // 左眼缩小动画
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                float scale = 1.0f - progress * (1.0f - min_scale);
                
                // 计算当前尺寸
                int current_width = original_left_width_ * scale;
                int current_height = original_left_height_ * scale;
                
                // 只缩小左眼
                lv_obj_set_size(left_eye_, current_width, current_height);
                lv_obj_set_pos(left_eye_, left_eye_x_ - current_width/2, left_eye_y_ - current_height/2);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 如果有舵机控制器，则控制舵机歪头
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadMove(10, 0, 10); // 右倾斜
            }
            
            // 添加眼睛左右移动的效果
            const int look_steps = 2;
            const int look_delay_ms = 300;
            
            for (int i = 0; i < look_steps; i++) {
                // 左看
                EyeLeft();
                vTaskDelay(pdMS_TO_TICKS(look_delay_ms));
                
                // 右看
                EyeRight();
                vTaskDelay(pdMS_TO_TICKS(look_delay_ms));
            }
            
            // 添加头部微微摇摆的效果
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadShake(5); // 头部小幅度摇摆
            }
            
            // 恢复中心位置
            EyeCenter(true);
            vTaskDelay(pdMS_TO_TICKS(200));
            
            // 恢复原始状态
            lv_obj_set_size(left_eye_, original_left_width_, original_left_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - original_left_width_/2, left_eye_y_ - original_left_height_/2);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteSillyAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteDeliciousAnimation() {
    ESP_LOGI(TAG, "执行美味表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteDeliciousAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸和位置
            // 未使用的变量，注释掉
            // const int original_left_width_ = left_eye_width_;
            // 未使用的变量，注释掉
            // const int original_left_height_ = left_eye_height_;
            // 未使用的变量，注释掉
            // const int original_right_width_ = right_eye_width_;
            // 未使用的变量，注释掉
            // const int original_right_height_ = right_eye_height_;
            
            // 美味表情 - 眼睛缩小并且微微上移
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建美味动画效果 - 眼睛缩小并上移
            const int animation_steps = 8;
            const int animation_delay_ms = 15;
            const float min_scale = 0.7f; // 最小缩放比例
            const int max_y_offset = -3;  // 最大Y轴偏移（向上为负）
            
            // 眼睛缩小并上移动画
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                float scale = 1.0f - progress * (1.0f - min_scale);
                int y_offset = progress * max_y_offset;
                
                // 计算当前尺寸
                int current_left_width = original_left_width_ * scale;
                int current_left_height = original_left_height_ * scale;
                int current_right_width = original_right_width_ * scale;
                int current_right_height = original_right_height_ * scale;
                
                // 更新眼睛大小和位置
                lv_obj_set_size(left_eye_, current_left_width, current_left_height);
                lv_obj_set_size(right_eye_, current_right_width, current_right_height);
                
                lv_obj_set_pos(left_eye_, 
                              left_eye_x_ - current_left_width/2, 
                              left_eye_y_ - current_left_height/2 + y_offset);
                lv_obj_set_pos(right_eye_, 
                              right_eye_x_ - current_right_width/2, 
                              right_eye_y_ - current_right_height/2 + y_offset);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 如果有舵机控制器，则控制舵机微微点头
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadNod(5); // 头部小幅度点头
            }
            
            // 添加舵机左右小幅度摇摆，模拟品尝美食
            if (servo_controller_ != nullptr) {
                // 左右小幅度摇摆
                servo_controller_->HeadMove(5, 0, 10);
                vTaskDelay(pdMS_TO_TICKS(300));
                servo_controller_->HeadMove(-5, 0, 10);
                vTaskDelay(pdMS_TO_TICKS(300));
                servo_controller_->HeadMove(5, 0, 10);
            }
            
            // 保持美味状态一段时间
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // 恢复原始状态
            lv_obj_set_size(left_eye_, original_left_width_, original_left_height_);
            lv_obj_set_size(right_eye_, original_right_width_, original_right_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - original_left_width_/2, left_eye_y_ - original_left_height_/2);
            lv_obj_set_pos(right_eye_, right_eye_x_ - original_right_width_/2, right_eye_y_ - original_right_height_/2);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteDeliciousAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteKissyAnimation() {
    ESP_LOGI(TAG, "执行亲亲表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteKissyAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸和位置
            // 未使用的变量，注释掉
            // const int original_left_width_ = left_eye_width_;
            // 未使用的变量，注释掉
            // const int original_left_height_ = left_eye_height_;
            // 未使用的变量，注释掉
            // const int original_right_width_ = right_eye_width_;
            // 未使用的变量，注释掉
            // const int original_right_height_ = right_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
            
            // 亲亲表情 - 眼睛变小变圆
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建亲亲动画效果 - 眼睛变小变圆
            const int animation_steps = 10;
            const int animation_delay_ms = 15;
            const float min_scale = 0.5f; // 最小缩放比例
            
            // 眼睛变小变圆动画
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                float scale = 1.0f - progress * (1.0f - min_scale);
                
                // 计算当前尺寸
                int current_width = original_left_width_ * scale;
                int current_height = original_left_height_ * scale;
                
                // 更新眼睛大小和位置
                lv_obj_set_size(left_eye_, current_width, current_height);
                lv_obj_set_size(right_eye_, current_width, current_height);
                
                lv_obj_set_pos(left_eye_, left_eye_x_ - current_width/2, left_eye_y_ - current_height/2);
                lv_obj_set_pos(right_eye_, right_eye_x_ - current_width/2, right_eye_y_ - current_height/2);
                
                // 设置圆形眼睛
                int current_radius = original_corner_radius_ + (int)(progress * (current_width/2 - original_corner_radius_));
                lv_obj_set_style_radius(left_eye_, current_radius, 0);
                lv_obj_set_style_radius(right_eye_, current_radius, 0);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 如果有舵机控制器，则控制舵机微微前倾
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadMove(0, -10, 10); // 头部向前倾斜
            }
            
            // 添加嘴型动作（通过眼睛控制模拟）
            const int kiss_steps = 3;
            const int kiss_delay_ms = 200;
            
            for (int i = 0; i < kiss_steps; i++) {
                // 嘴型缩小（眼睛变得更小）
                float pucker_scale = 0.3f;
                int pucker_width = original_left_width_ * pucker_scale;
                int pucker_height = original_left_height_ * pucker_scale;
                
                lv_obj_set_size(left_eye_, pucker_width, pucker_height);
                lv_obj_set_size(right_eye_, pucker_width, pucker_height);
                
                lv_obj_set_pos(left_eye_, left_eye_x_ - pucker_width/2, left_eye_y_ - pucker_height/2);
                lv_obj_set_pos(right_eye_, right_eye_x_ - pucker_width/2, right_eye_y_ - pucker_height/2);
                
                // 完全圆形
                lv_obj_set_style_radius(left_eye_, pucker_width/2, 0);
                lv_obj_set_style_radius(right_eye_, pucker_width/2, 0);
                
                lv_refr_now(NULL);
                vTaskDelay(pdMS_TO_TICKS(kiss_delay_ms));
                
                // 嘴型恢复一点
                float relax_scale = 0.5f;
                int relax_width = original_left_width_ * relax_scale;
                int relax_height = original_left_height_ * relax_scale;
                
                lv_obj_set_size(left_eye_, relax_width, relax_height);
                lv_obj_set_size(right_eye_, relax_width, relax_height);
                
                lv_obj_set_pos(left_eye_, left_eye_x_ - relax_width/2, left_eye_y_ - relax_height/2);
                lv_obj_set_pos(right_eye_, right_eye_x_ - relax_width/2, right_eye_y_ - relax_height/2);
                
                // 圆形眼睛
                lv_obj_set_style_radius(left_eye_, relax_width/2, 0);
                lv_obj_set_style_radius(right_eye_, relax_width/2, 0);
                
                lv_refr_now(NULL);
                vTaskDelay(pdMS_TO_TICKS(kiss_delay_ms));
            }
            
            // 保持亲亲状态一段时间
            vTaskDelay(pdMS_TO_TICKS(300));
            
            // 恢复原始状态
            lv_obj_set_size(left_eye_, original_left_width_, original_left_height_);
            lv_obj_set_size(right_eye_, original_right_width_, original_right_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - original_left_width_/2, left_eye_y_ - original_left_height_/2);
            lv_obj_set_pos(right_eye_, right_eye_x_ - original_right_width_/2, right_eye_y_ - original_right_height_/2);
            lv_obj_set_style_radius(left_eye_, original_corner_radius_, 0);
            lv_obj_set_style_radius(right_eye_, original_corner_radius_, 0);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteKissyAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteCoolAnimation() {
    ESP_LOGI(TAG, "执行酷酷表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteCoolAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸和位置
            // 未使用的变量，注释掉
            // const int original_left_width_ = left_eye_width_;
            // 未使用的变量，注释掉
            // const int original_left_height_ = left_eye_height_;
            // 未使用的变量，注释掉
            // const int original_right_width_ = right_eye_width_;
            // 未使用的变量，注释掉
            // const int original_right_height_ = right_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
            
            // 酷酷表情 - 眼睛缩小并且变窄
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建酷酷动画效果 - 眼睛缩窄
            const int animation_steps = 8;
            const int animation_delay_ms = 15;
            const float min_height_factor = 0.4f; // 最小高度为原始高度的40%
            
            // 眼睛缩窄动画
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                float height_factor = 1.0f - progress * (1.0f - min_height_factor);
                
                // 计算当前尺寸
                int current_left_height = original_left_height_ * height_factor;
                int current_right_height = original_right_height_ * height_factor;
                
                // 更新眼睛高度和位置
                lv_obj_set_height(left_eye_, current_left_height);
                lv_obj_set_height(right_eye_, current_right_height);
                
                lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, 
                              left_eye_y_ - current_left_height/2);
                lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, 
                              right_eye_y_ - current_right_height/2);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 如果有舵机控制器，则控制舵机微微抬头
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadUp(5); // 微微抬头
            }
            
            // 添加眼睛看向右侧的效果
            EyeRight();
            vTaskDelay(pdMS_TO_TICKS(300));
            
            // 添加头部微微摇摆的效果
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadNod(5); // 头部小幅度点头
            }
            
            // 保持酷酷状态一段时间
            vTaskDelay(pdMS_TO_TICKS(800));
            
            // 恢复原始状态
            lv_obj_set_height(left_eye_, original_left_height_);
            lv_obj_set_height(right_eye_, original_right_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - original_left_width_/2, left_eye_y_ - original_left_height_/2);
            lv_obj_set_pos(right_eye_, right_eye_x_ - original_right_width_/2, right_eye_y_ - original_right_height_/2);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteCoolAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置和眼睛位置
    EyeCenter(true);
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteRelaxedAnimation() {
    ESP_LOGI(TAG, "执行放松表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteRelaxedAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸和位置
            // 未使用的变量，注释掉
            // const int original_left_width_ = left_eye_width_;
            // 未使用的变量，注释掉
            // const int original_left_height_ = left_eye_height_;
            // 未使用的变量，注释掉
            // const int original_right_width_ = right_eye_width_;
            // 未使用的变量，注释掉
            // const int original_right_height_ = right_eye_height_;
            
            // 放松表情 - 眼睛缩小并且缩窄，类似于半睡半醒状态
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建放松动画效果 - 眼睛缩小和缩窄
            const int animation_steps = 10;
            const int animation_delay_ms = 20;
            const float min_height_factor = 0.5f; // 最小高度为原始高度的50%
            const float min_width_factor = 0.9f;  // 最小宽度为原始宽度的90%
            
            // 眼睛缩小和缩窄动画
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                float height_factor = 1.0f - progress * (1.0f - min_height_factor);
                float width_factor = 1.0f - progress * (1.0f - min_width_factor);
                
                // 计算当前尺寸
                int current_left_width = original_left_width_ * width_factor;
                int current_left_height = original_left_height_ * height_factor;
                int current_right_width = original_right_width_ * width_factor;
                int current_right_height = original_right_height_ * height_factor;
                
                // 更新眼睛大小和位置
                lv_obj_set_size(left_eye_, current_left_width, current_left_height);
                lv_obj_set_size(right_eye_, current_right_width, current_right_height);
                
                lv_obj_set_pos(left_eye_, left_eye_x_ - current_left_width/2, 
                              left_eye_y_ - current_left_height/2);
                lv_obj_set_pos(right_eye_, right_eye_x_ - current_right_width/2, 
                              right_eye_y_ - current_right_height/2);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 如果有舵机控制器，则控制舵机微微低头
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadDown(5); // 微微低头
            }
            
            // 添加缓慢的眼睛眼睛动作
            ExecuteBlinkAnimation(2); // 缓慢眼睛
            
            // 保持放松状态一段时间
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // 恢复原始状态
            lv_obj_set_size(left_eye_, original_left_width_, original_left_height_);
            lv_obj_set_size(right_eye_, original_right_width_, original_right_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - original_left_width_/2, left_eye_y_ - original_left_height_/2);
            lv_obj_set_pos(right_eye_, right_eye_x_ - original_right_width_/2, right_eye_y_ - original_right_height_/2);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteRelaxedAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteShockedAnimation() {
    ESP_LOGI(TAG, "执行震惊表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteShockedAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸和位置
            // 未使用的变量，注释掉
            // const int original_left_width_ = left_eye_width_;
            // 未使用的变量，注释掉
            // const int original_left_height_ = left_eye_height_;
            // 未使用的变量，注释掉
            // const int original_right_width_ = right_eye_width_;
            // 未使用的变量，注释掉
            // const int original_right_height_ = right_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
            
            // 震惊表情 - 眼睛突然变大，圆形
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建震惊动画效果 - 眼睛突然变大
            const int animation_steps = 5;
            const int animation_delay_ms = 10;
            const float max_scale = 1.5f; // 最大放大倍数
            
            // 眼睛突然变大动画
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                float scale = 1.0f + progress * (max_scale - 1.0f);
                
                // 计算当前尺寸
                int current_width = original_left_width_ * scale;
                int current_height = original_left_height_ * scale;
                
                // 更新眼睛大小和位置
                lv_obj_set_size(left_eye_, current_width, current_height);
                lv_obj_set_size(right_eye_, current_width, current_height);
                
                lv_obj_set_pos(left_eye_, left_eye_x_ - current_width/2, left_eye_y_ - current_height/2);
                lv_obj_set_pos(right_eye_, right_eye_x_ - current_width/2, right_eye_y_ - current_height/2);
                
                // 完全圆形的眼睛
                lv_obj_set_style_radius(left_eye_, current_width/2, 0);
                lv_obj_set_style_radius(right_eye_, current_width/2, 0);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 如果有舵机控制器，则控制舵机向后仰头表示震惊
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadUp(15); // 向后仰头
            }
            
            // 添加微微震动效果
            const int shake_steps = 8;
            const int shake_delay_ms = 30;
            const int shake_offset = 1;
            
            for (int i = 0; i < shake_steps; i++) {
                // 随机方向的小幅度抖动
                int offset_x = (i % 2 == 0) ? shake_offset : -shake_offset;
                int offset_y = ((i / 2) % 2 == 0) ? shake_offset : -shake_offset;
                
                // 更新位置
                lv_obj_set_pos(left_eye_, 
                              left_eye_x_ - lv_obj_get_width(left_eye_)/2 + offset_x, 
                              left_eye_y_ - lv_obj_get_height(left_eye_)/2 + offset_y);
                lv_obj_set_pos(right_eye_, 
                              right_eye_x_ - lv_obj_get_width(right_eye_)/2 + offset_x, 
                              right_eye_y_ - lv_obj_get_height(right_eye_)/2 + offset_y);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(shake_delay_ms));
            }
            
            // 保持震惊状态一段时间
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // 恢复原始状态
            lv_obj_set_size(left_eye_, original_left_width_, original_left_height_);
            lv_obj_set_size(right_eye_, original_right_width_, original_right_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - original_left_width_/2, left_eye_y_ - original_left_height_/2);
            lv_obj_set_pos(right_eye_, right_eye_x_ - original_right_width_/2, right_eye_y_ - original_right_height_/2);
            lv_obj_set_style_radius(left_eye_, original_corner_radius_, 0);
            lv_obj_set_style_radius(right_eye_, original_corner_radius_, 0);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteShockedAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteThinkingAnimation() {
    ESP_LOGI(TAG, "执行思考表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteThinkingAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸和位置
            // 未使用的变量，注释掉
            // const int original_left_width_ = left_eye_width_;
            // 未使用的变量，注释掉
            // const int original_left_height_ = left_eye_height_;
            // 未使用的变量，注释掉
            // const int original_right_width_ = right_eye_width_;
            // 未使用的变量，注释掉
            // const int original_right_height_ = right_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
            
            // 思考表情 - 一只眼睛缩小，一只眼睛正常
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建思考动画效果 - 右眼缩小
            const int animation_steps = 8;
            const int animation_delay_ms = 20;
            const float min_scale = 0.3f; // 最小缩放比例
            
            // 右眼缩小动画
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                float scale = 1.0f - progress * (1.0f - min_scale);
                
                // 计算当前尺寸
                int current_width = original_right_width_ * scale;
                int current_height = original_right_height_ * scale;
                
                // 只缩小右眼
                lv_obj_set_size(right_eye_, current_width, current_height);
                lv_obj_set_pos(right_eye_, right_eye_x_ - current_width/2, right_eye_y_ - current_height/2);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 如果有舵机控制器，则控制舵机歪头
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadMove(-10, 5, 10); // 左倾斜，略微抬头
            }
            
            // 添加眼睛向上看的效果
            EyeUp();
            vTaskDelay(pdMS_TO_TICKS(300));
            
            // 添加眼睛左右移动的效果，模拟思考
            const int look_steps = 3;
            const int look_delay_ms = 500;
            
            for (int i = 0; i < look_steps; i++) {
                // 左看
                EyeLeft();
                vTaskDelay(pdMS_TO_TICKS(look_delay_ms));
                
                // 右看
                EyeRight();
                vTaskDelay(pdMS_TO_TICKS(look_delay_ms));
            }
            
            // 恢复中心位置
            EyeCenter(true);
            vTaskDelay(pdMS_TO_TICKS(200));
            
            // 恢复原始状态
            lv_obj_set_size(right_eye_, original_right_width_, original_right_height_);
            lv_obj_set_pos(right_eye_, right_eye_x_ - original_right_width_/2, right_eye_y_ - original_right_height_/2);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteThinkingAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteLovingAnimation() {
    ESP_LOGI(TAG, "执行爱心表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteLovingAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸
            const int original_left_width_ = left_eye_width_;
            const int original_left_height_ = left_eye_height_;
            const int original_right_width_ = right_eye_width_;
            const int original_right_height_ = right_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
            
            // 爱心表情 - 眼睛变成心形
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建心形眼睛动画
            const int animation_steps = 10;
            const int animation_delay_ms = 20;
            
            // 心形变化动画
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                
                // 计算当前尺寸 - 心形的宽度稍微比高度大
                int heart_width = original_left_width_ * (0.8f + progress * 0.2f);
                int heart_height = original_left_height_ * (0.7f + progress * 0.3f);
                
                // 更新眼睛大小
                lv_obj_set_size(left_eye_, heart_width, heart_height);
                lv_obj_set_size(right_eye_, heart_width, heart_height);
                
                // 更新眼睛位置
                lv_obj_set_pos(left_eye_, left_eye_x_ - heart_width/2, left_eye_y_ - heart_height/2);
                lv_obj_set_pos(right_eye_, right_eye_x_ - heart_width/2, right_eye_y_ - heart_height/2);
                
                // 心形效果 - 使用圆角和形状实现
                // 心形顶部尖尖，底部圆润
                int heart_radius = original_corner_radius_ + (int)(progress * 15);
                lv_obj_set_style_radius(left_eye_, heart_radius, 0);
                lv_obj_set_style_radius(right_eye_, heart_radius, 0);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 添加心跳效果
            const int pulse_steps = 4;
            const int pulse_cycles = 3;
            const int pulse_delay_ms = 100;
            const float max_scale = 1.2f;
            
            for (int cycle = 0; cycle < pulse_cycles; cycle++) {
                // 心跳放大
                for (int step = 0; step < pulse_steps; step++) {
                    float progress = (float)step / pulse_steps;
                    float scale = 1.0f + progress * (max_scale - 1.0f);
                    
                    int current_width = original_left_width_ * scale;
                    int current_height = original_left_height_ * scale;
                    
                    lv_obj_set_size(left_eye_, current_width, current_height);
                    lv_obj_set_size(right_eye_, current_width, current_height);
                    
                    lv_obj_set_pos(left_eye_, left_eye_x_ - current_width/2, left_eye_y_ - current_height/2);
                    lv_obj_set_pos(right_eye_, right_eye_x_ - current_width/2, right_eye_y_ - current_height/2);
                    
                    lv_refr_now(NULL);
                    vTaskDelay(pdMS_TO_TICKS(pulse_delay_ms));
                }
                
                // 心跳缩小
                for (int step = 0; step < pulse_steps; step++) {
                    float progress = (float)step / pulse_steps;
                    float scale = max_scale - progress * (max_scale - 1.0f);
                    
                    int current_width = original_left_width_ * scale;
                    int current_height = original_left_height_ * scale;
                    
                    lv_obj_set_size(left_eye_, current_width, current_height);
                    lv_obj_set_size(right_eye_, current_width, current_height);
                    
                    lv_obj_set_pos(left_eye_, left_eye_x_ - current_width/2, left_eye_y_ - current_height/2);
                    lv_obj_set_pos(right_eye_, right_eye_x_ - current_width/2, right_eye_y_ - current_height/2);
                    
                    lv_refr_now(NULL);
                    vTaskDelay(pdMS_TO_TICKS(pulse_delay_ms));
                }
            }
            
            // 如果有舵机控制器，则控制舵机做小幅度摇摆
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadRoll(10); // 头部小幅度摇摆
            }
            
            // 保持爱心状态一段时间
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // 恢复原始状态
            lv_obj_set_size(left_eye_, original_left_width_, original_left_height_);
            lv_obj_set_size(right_eye_, original_right_width_, original_right_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - original_left_width_/2, left_eye_y_ - original_left_height_/2);
            lv_obj_set_pos(right_eye_, right_eye_x_ - original_right_width_/2, right_eye_y_ - original_right_height_/2);
            lv_obj_set_style_radius(left_eye_, original_corner_radius_, 0);
            lv_obj_set_style_radius(right_eye_, original_corner_radius_, 0);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteLovingAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteEmbarrassedAnimation() {
    ESP_LOGI(TAG, "执行尴尬表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteEmbarrassedAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸和位置
            // 未使用的变量，注释掉
            // const int original_left_width_ = left_eye_width_;
            // 未使用的变量，注释掉
            // const int original_left_height_ = left_eye_height_;
            // 未使用的变量，注释掉
            // const int original_right_width_ = right_eye_width_;
            // 未使用的变量，注释掉
            // const int original_right_height_ = right_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
            const int original_left_x = left_eye_x_;
            const int original_left_y = left_eye_y_;
            const int original_right_x = right_eye_x_;
            const int original_right_y = right_eye_y_;
            
            // 尴尬表情 - 眼睛微微下移，并且变小
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建动画效果
            const int animation_steps = 8;
            const int animation_delay_ms = 15;
            const float min_scale = 0.7f; // 最小缩放比例
            const int max_y_offset = 5;   // 最大Y轴偏移
            
            // 眼睛缩小并下移动画
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                float scale = 1.0f - progress * (1.0f - min_scale);
                int y_offset = progress * max_y_offset;
                
                // 计算当前尺寸
                int current_left_width = original_left_width_ * scale;
                int current_left_height = original_left_height_ * scale;
                int current_right_width = original_right_width_ * scale;
                int current_right_height = original_right_height_ * scale;
                
                // 更新眼睛大小和位置
                lv_obj_set_size(left_eye_, current_left_width, current_left_height);
                lv_obj_set_size(right_eye_, current_right_width, current_right_height);
                
                lv_obj_set_pos(left_eye_, 
                              original_left_x - current_left_width/2, 
                              original_left_y - current_left_height/2 + y_offset);
                lv_obj_set_pos(right_eye_, 
                              original_right_x - current_right_width/2, 
                              original_right_y - current_right_height/2 + y_offset);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 如果有舵机控制器，则控制舵机低头
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadDown(10); // 低头表示尴尬
            }
            
            // 添加左右微微抖动效果
            const int shake_steps = 6;
            const int shake_delay_ms = 80;
            const int shake_offset = 2;
            
            for (int i = 0; i < shake_steps; i++) {
                // 左右小幅度抖动
                int offset_x = (i % 2 == 0) ? shake_offset : -shake_offset;
                
                // 更新位置
                lv_obj_set_pos(left_eye_, 
                              lv_obj_get_x(left_eye_) + offset_x, 
                              lv_obj_get_y(left_eye_));
                lv_obj_set_pos(right_eye_, 
                              lv_obj_get_x(right_eye_) + offset_x, 
                              lv_obj_get_y(right_eye_));
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(shake_delay_ms));
            }
            
            // 保持尴尬状态一段时间
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // 恢复原始状态
            lv_obj_set_size(left_eye_, original_left_width_, original_left_height_);
            lv_obj_set_size(right_eye_, original_right_width_, original_right_height_);
            lv_obj_set_pos(left_eye_, original_left_x - original_left_width_/2, original_left_y - original_left_height_/2);
            lv_obj_set_pos(right_eye_, original_right_x - original_right_width_/2, original_right_y - original_right_height_/2);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteEmbarrassedAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteFunnyAnimation() {
    ESP_LOGI(TAG, "执行滑稽表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteFunnyAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸
            // 未使用的变量，注释掉
            // const int original_left_width_ = left_eye_width_;
            // 未使用的变量，注释掉
            // const int original_left_height_ = left_eye_height_;
            // 未使用的变量，注释掉
            // const int original_right_width_ = right_eye_width_;
            // 未使用的变量，注释掉
            // const int original_right_height_ = right_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
            
            // 滑稽表情 - 一只眼睛大一只眼睛小
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 创建动画效果
            const int animation_steps = 10;
            const int animation_delay_ms = 15;
            
            // 左眼变大，右眼变小
            for (int step = 0; step < animation_steps; step++) {
                float progress = (float)step / animation_steps;
                
                // 左眼放大到1.5倍
                float left_scale = 1.0f + progress * 0.5f;
                int left_width = original_left_width_ * left_scale;
                int left_height = original_left_height_ * left_scale;
                
                // 右眼缩小到0.6倍
                float right_scale = 1.0f - progress * 0.4f;
                int right_width = original_right_width_ * right_scale;
                int right_height = original_right_height_ * right_scale;
                
                // 更新眼睛大小和位置
                lv_obj_set_size(left_eye_, left_width, left_height);
                lv_obj_set_pos(left_eye_, left_eye_x_ - left_width/2, left_eye_y_ - left_height/2);
                
                lv_obj_set_size(right_eye_, right_width, right_height);
                lv_obj_set_pos(right_eye_, right_eye_x_ - right_width/2, right_eye_y_ - right_height/2);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 如果有舵机控制器，则控制舵机歪头
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadMove(10, 5, 10); // 右倾斜，略微抬头
            }
            
            // 保持滑稽状态一段时间
            vTaskDelay(pdMS_TO_TICKS(800));
            
            // 恢复原始状态
            lv_obj_set_size(left_eye_, original_left_width_, original_left_height_);
            lv_obj_set_size(right_eye_, original_right_width_, original_right_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - original_left_width_/2, left_eye_y_ - original_left_height_/2);
            lv_obj_set_pos(right_eye_, right_eye_x_ - original_right_width_/2, right_eye_y_ - original_right_height_/2);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteFunnyAnimation: 获取显示锁失败");
        }
    }
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
}

void EmojiController::ExecuteAngerAnimation() {
    ESP_LOGI(TAG, "执行愤怒表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteAngerAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
                        // 保存原始尺寸
            const int original_left_width_ = left_eye_width_;
            const int original_left_height_ = left_eye_height_;
            const int original_right_width_ = right_eye_width_;
            const int original_right_height_ = right_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
// 创建愤怒表情 - 基于悲伤表情，但左右三角形交换位置
            // 首先确保眼睛可见
            lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
            
            // 获取眼睛的位置和大小
            int offset = left_eye_height_ / 4;  // 减小初始偏移量
            
            // 创建左眼和右眼三角形对象
            lv_obj_t* left_eye_mask = nullptr;
            lv_obj_t* right_eye_mask = nullptr;
            
            // 参考悲伤表情实现，但交换左右三角形
            // 循环绘制多个三角形，逐渐移动位置，形成动画效果
            for (int i = 0; i < 10; i++) {
                // 如果已经创建了三角形对象，先删除
                if (left_eye_mask) lv_obj_del(left_eye_mask);
                if (right_eye_mask) lv_obj_del(right_eye_mask);
                
                // 创建左眼三角形 - 使用右眼悲伤表情的三角形形状
                left_eye_mask = lv_obj_create(emoji_screen_);
                lv_obj_set_style_bg_color(left_eye_mask, lv_color_white(), 0); // 设置为白色
                lv_obj_set_style_border_width(left_eye_mask, 0, 0);
                lv_obj_set_style_radius(left_eye_mask, 0, 0); // 设置为直角
                
                // 计算开心表情的三角形坐标 - 使用右眼的参数
                int happy_rx1 = right_eye_x_ + right_eye_width_/2 + 5;
                int happy_ry1 = right_eye_y_ + offset - 3;
                int happy_rx2 = right_eye_x_ - right_eye_width_/2 - 5;
                int happy_ry2 = right_eye_y_ + 5 + offset;
                int happy_rx3 = right_eye_x_ + right_eye_width_/2 + 5;
                int happy_ry3 = right_eye_y_ + right_eye_height_ + offset + 3;
                
                // 计算悲伤表情的三角形坐标（垂直翻转）- 使用右眼的参数但应用于左眼
                int sad_rx1 = happy_rx1;
                int sad_ry1 = 2 * right_eye_y_ - happy_ry1;
                int sad_rx2 = happy_rx2;
                // int sad_ry2 = 2 \* right_eye_y_ - happy_ry2;
                // int sad_rx3 = happy_rx3;
                int sad_ry3 = 2 * right_eye_y_ - happy_ry3;
                
                // 调整坐标以适应左眼位置
                int left_offset_x = left_eye_x_ - right_eye_x_;
                
                // 设置三角形的大小和位置
                int rtri_width = sad_rx1 - sad_rx2 + 15;
                int rtri_height = abs(sad_ry3 - sad_ry1) + 15;
                
                // 确定左上角坐标（取最小的x和y）
                int rtop_left_x = sad_rx2 - 8 + left_offset_x;
                int rtop_left_y = std::min(sad_ry1, sad_ry3) - 15;  
                
                lv_obj_set_size(left_eye_mask, rtri_width, rtri_height);
                lv_obj_set_pos(left_eye_mask, rtop_left_x, rtop_left_y);
                
                // 设置左眼三角形的旋转和倾斜 - 使用右眼悲伤表情的参数
                lv_obj_set_style_transform_angle(left_eye_mask, 150, 0); // 15度，单位是十分之一度
                // 设置旋转中心点为三角形右下角
                lv_obj_set_style_transform_pivot_x(left_eye_mask, rtri_width, 0);
                lv_obj_set_style_transform_pivot_y(left_eye_mask, rtri_height, 0);
                
                // 创建右眼三角形 - 使用左眼悲伤表情的三角形形状
                right_eye_mask = lv_obj_create(emoji_screen_);
                lv_obj_set_style_bg_color(right_eye_mask, lv_color_white(), 0); // 设置为白色
                lv_obj_set_style_border_width(right_eye_mask, 0, 0);
                lv_obj_set_style_radius(right_eye_mask, 0, 0); // 设置为直角
                
                // 计算开心表情的三角形坐标 - 使用左眼的参数
                int happy_x1 = left_eye_x_ - left_eye_width_/2 - 5;
                int happy_y1 = left_eye_y_ + offset - 3;
                int happy_x2 = left_eye_x_ + left_eye_width_/2 + 5;
                int happy_y2 = left_eye_y_ + 5 + offset;
                int happy_x3 = left_eye_x_ - left_eye_width_/2 - 5;
                int happy_y3 = left_eye_y_ + left_eye_height_ + offset + 3;
                
                // 计算悲伤表情的三角形坐标（垂直翻转）- 使用左眼的参数但应用于右眼
                int sad_x1 = happy_x1;
                int sad_y1 = 2 * left_eye_y_ - happy_y1;
                int sad_x2 = happy_x2;
                // int sad_y2 = 2 \* left_eye_y_ - happy_y2;
                // int sad_x3 = happy_x3;
                int sad_y3 = 2 * left_eye_y_ - happy_y3;
                
                // 调整坐标以适应右眼位置
                int right_offset_x = right_eye_x_ - left_eye_x_;
                
                // 设置三角形的大小和位置
                int tri_width = sad_x2 - sad_x1 + 15;
                int tri_height = abs(sad_y3 - sad_y1) + 15;
                
                // 确定左上角坐标（取最小的x和y）
                int top_left_x = sad_x1 - 8 + right_offset_x;
                int top_left_y = std::min(sad_y1, sad_y3) - 15;  
                
                lv_obj_set_size(right_eye_mask, tri_width, tri_height);
                lv_obj_set_pos(right_eye_mask, top_left_x, top_left_y);
                
                // 设置右眼三角形的旋转和倾斜 - 使用左眼悲伤表情的参数
                lv_obj_set_style_transform_angle(right_eye_mask, -150, 0); // -15度，单位是十分之一度
                // 设置旋转中心点为三角形左下角
                lv_obj_set_style_transform_pivot_x(right_eye_mask, 0, 0);
                lv_obj_set_style_transform_pivot_y(right_eye_mask, tri_height, 0);
                
                // 更新offset，与参考代码一致
                offset -= 2;
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟，形成动画效果
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            
            // 如果有舵机控制器，则控制舵机摇头表示生气
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadShake(1);
            }
            
            // 显示一段时间
            vTaskDelay(pdMS_TO_TICKS(2000));
            
            // 删除创建的对象
            if (left_eye_mask) lv_obj_del(left_eye_mask);
            if (right_eye_mask) lv_obj_del(right_eye_mask);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteAngerAnimation: 获取显示锁失败");
        }
    }
    
    // 添加短暂延迟，确保显示更新完成
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
    
    // 恢复原始状态
    EyeCenter();
}

void EmojiController::ExecuteSurpriseAnimation() {
    ESP_LOGI(TAG, "执行惊讶表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteSurpriseAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 实现眼睛先缩小10%，然后恢复默认大小，再放大10%的效果
            // 保存原始尺寸
            const int original_width = left_eye_width_;
            const int original_height = left_eye_height_;
            const int original_corner_radius_ = ref_corner_radius_;
            
            // 动画参数
            const float scale_factors[] = {0.9f, 1.0f, 1.1f, 1.0f};  // 缩小10%，恢复，放大10%，恢复
            const int animation_steps = 5;  // 每个阶段的步骤数
            const int animation_delay_ms = 15;  // 每步延迟
            
            // 执行动画
            for (int phase = 0; phase < 4; phase++) {  // 4个阶段：缩小，恢复，放大，恢复
                float start_factor = (phase == 0) ? 1.0f : scale_factors[phase-1];
                float end_factor = scale_factors[phase];
                
                // 在当前阶段内平滑过渡
                for (int step = 0; step < animation_steps; step++) {
                    // 计算当前步骤的缩放因子
                    float progress = (float)step / animation_steps;
                    float current_factor = start_factor + (end_factor - start_factor) * progress;
                    
                    // 计算当前尺寸
                    int current_width = original_width * current_factor;
                    int current_height = original_height * current_factor;
                    int current_corner_radius = std::max(1, (int)(original_corner_radius_ * current_factor));
                    
                    // 更新左眼大小和位置
                    lv_obj_set_size(left_eye_, current_width, current_height);
                    lv_obj_set_pos(left_eye_, left_eye_x_ - current_width/2, left_eye_y_ - current_height/2);
                    
                    // 更新右眼大小和位置
                    lv_obj_set_size(right_eye_, current_width, current_height);
                    lv_obj_set_pos(right_eye_, right_eye_x_ - current_width/2, right_eye_y_ - current_height/2);
                    
                    // 更新圆角半径
                    lv_obj_set_style_radius(left_eye_, current_corner_radius, 0);
                    lv_obj_set_style_radius(right_eye_, current_corner_radius, 0);
                    
                    // 刷新显示
                    lv_refr_now(NULL);
                    
                    // 短暂延迟
                    vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
                }
                
                // 在阶段结束时暂停一下，特别是在放大和恢复阶段
                if (phase == 2) {  // 放大阶段结束后
                    vTaskDelay(pdMS_TO_TICKS(200));  // 保持放大状态一段时间
                }
            }
            
            // 如果有舵机控制器，则控制舵机抬头表示惊讶
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadUp(SERVO_OFFSET_Y/2);
            }
            
            // 显示一段时间
            vTaskDelay(pdMS_TO_TICKS(500));
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteSurpriseAnimation: 获取显示锁失败");
        }
    }
    
    // 添加短暂延迟，确保显示更新完成
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
    
    // 恢复原始状态
    EyeCenter();
}

void EmojiController::ExecuteWakeupAnimation() {
    ESP_LOGI(TAG, "执行唤醒表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteWakeupAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter();
    
    // 使用DisplayLockGuard确保线程安全
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
            lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
            lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteWakeupAnimation: 获取显示锁失败");
        }
    }
    
    // 显示一段时间
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 恢复原始状态
    EyeCenter();
}

void EmojiController::ExecuteSleepAnimation() {
    ESP_LOGI(TAG, "执行睡眠表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteSleepAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 使用DisplayLockGuard确保线程安全
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 完全按照boardemoji.ino中的eye_sleep实现
            // 只设置眼睛高度为2像素，宽度保持不变
            left_eye_height_ = 2;
            right_eye_height_ = 2;
            
            // 更新眼睛显示
            lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
            lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
            lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
            
            // 刷新显示
            lv_refr_now(NULL);
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteSleepAnimation: 获取显示锁失败");
        }
    }
    
    // 如果有舵机控制器，则控制舵机低头
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadDown(SERVO_OFFSET_Y);
    }
    
    // 显示一段时间
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void EmojiController::EyeConfused() {
    PlayAnimation(AnimationType::CONFUSED);
}

void EmojiController::ExecuteConfusedAnimation() {
    ESP_LOGI(TAG, "执行疑惑表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteConfusedAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter();
    
    // 使用DisplayLockGuard确保线程安全
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_ / 2);
            lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/4);
            lv_obj_set_size(right_eye_, right_eye_width_, left_eye_height_ / 2);
            lv_obj_set_pos(right_eye_, right_eye_x_ - left_eye_width_/2, right_eye_y_ - left_eye_height_/4);
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteConfusedAnimation: 获取显示锁失败");
        }
    }
    
    // 显示一段时间
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 恢复原始状态
    EyeCenter();
}

void EmojiController::ExecuteLookLeftAnimation() {
    ESP_LOGI(TAG, "执行向左看动画");
    
    // 同时执行表情和舵机动作
    // 如果有舵机控制器，则控制舵机向左转
    if (servo_controller_ != nullptr) {
        // 同时开始舵机动作和表情动画
        servo_controller_->HeadMove(-SERVO_OFFSET_X, 0, SERVO_DELAY);
        ESP_LOGI(TAG, "舵机向左转动");
    }
    
    // 检查对象是否存在，如果不存在则只执行舵机动作
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteLookLeftAnimation: 屏幕或眼睛对象不存在，仅执行舵机动作");
        return;
    }
    
    // 向左看动画
    MoveEye(-1);
    
    // 保持一段时间
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 恢复阶段
    // 定义与MoveEye函数相同的参数
    int direction = -1;
    int direction_movement_amplitude = 2;
    
    // 第三阶段：开始恢复，向反方向移动，同时眼睛变小
    for (int i = 0; i < 3; i++) {
        if (display_) {
            DisplayLockGuard lock(display_);
            
            left_eye_x_ -= direction_movement_amplitude * direction;
            right_eye_x_ -= direction_movement_amplitude * direction;
            right_eye_height_ -= 5;
            left_eye_height_ -= 5;
            
            // 向左看恢复时，左眼变小
            left_eye_height_ -= 1;  // 调整为与原始代码一致的变形幅度
            left_eye_width_ -= 1;
            
            // 在锁内直接更新显示，不使用DrawEmoji
            lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
            lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
            lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // 第四阶段：继续恢复，眼睛恢复高度
    for (int i = 0; i < 3; i++) {
        if (display_) {
            DisplayLockGuard lock(display_);
            
            left_eye_x_ -= direction_movement_amplitude * direction;
            right_eye_x_ -= direction_movement_amplitude * direction;
            right_eye_height_ += 5;
            left_eye_height_ += 5;
            
            // 向左看恢复时，左眼继续变小
            left_eye_height_ -= 1;  // 调整为与原始代码一致的变形幅度
            left_eye_width_ -= 1;
            
            // 在锁内直接更新显示，不使用DrawEmoji
            lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
            lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
            lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // 如果有舵机控制器，则控制舵机恢复中心位置
    if (servo_controller_ != nullptr) {
        // 缓慢恢复到中心位置，使用与执行动作相同的延迟
        servo_controller_->HeadCenter(SERVO_DELAY);
    }
    
    // 完全恢复到中心位置
    EyeCenter();
}

void EmojiController::ExecuteLookRightAnimation() {
    ESP_LOGI(TAG, "执行向右看动画");
    
    // 同时执行表情和舵机动作
    // 如果有舵机控制器，则控制舵机向右转
    if (servo_controller_ != nullptr) {
        // 同时开始舵机动作和表情动画
        servo_controller_->HeadMove(SERVO_OFFSET_X, 0, SERVO_DELAY);
        ESP_LOGI(TAG, "舵机向右转动");
    }
    
    // 检查对象是否存在，如果不存在则只执行舵机动作
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteLookRightAnimation: 屏幕或眼睛对象不存在，仅执行舵机动作");
        return;
    }
    
    // 向右看动画
    MoveEye(1);
    
    // 保持一段时间
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 恢复阶段
    // 定义与MoveEye函数相同的参数
    int direction = 1;
    int direction_movement_amplitude = 2;
    
    // 第三阶段：开始恢复，向反方向移动，同时眼睛变小
    for (int i = 0; i < 3; i++) {
        if (display_) {
            DisplayLockGuard lock(display_);
            
            left_eye_x_ -= direction_movement_amplitude * direction;
            right_eye_x_ -= direction_movement_amplitude * direction;
            right_eye_height_ -= 5;
            left_eye_height_ -= 5;
            
            // 向右看恢复时，右眼变小
            right_eye_height_ -= 1;  // 调整为与原始代码一致的变形幅度
            right_eye_width_ -= 1;
            
            // 在锁内直接更新显示，不使用DrawEmoji
            lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
            lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
            lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // 第四阶段：继续恢复，眼睛恢复高度
    for (int i = 0; i < 3; i++) {
        if (display_) {
            DisplayLockGuard lock(display_);
            
            left_eye_x_ -= direction_movement_amplitude * direction;
            right_eye_x_ -= direction_movement_amplitude * direction;
            right_eye_height_ += 5;
            left_eye_height_ += 5;
            
            // 向右看恢复时，右眼继续变小
            right_eye_height_ -= 1;  // 调整为与原始代码一致的变形幅度
            right_eye_width_ -= 1;
            
            // 在锁内直接更新显示，不使用DrawEmoji
            lv_obj_set_size(left_eye_, left_eye_width_, left_eye_height_);
            lv_obj_set_pos(left_eye_, left_eye_x_ - left_eye_width_/2, left_eye_y_ - left_eye_height_/2);
            lv_obj_set_size(right_eye_, right_eye_width_, right_eye_height_);
            lv_obj_set_pos(right_eye_, right_eye_x_ - right_eye_width_/2, right_eye_y_ - right_eye_height_/2);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // 如果有舵机控制器，则控制舵机恢复中心位置
    if (servo_controller_ != nullptr) {
        // 缓慢恢复到中心位置，使用与执行动作相同的延迟
        servo_controller_->HeadCenter(SERVO_DELAY);
    }
    
    // 完全恢复到中心位置
    EyeCenter();
}

void EmojiController::ExecuteHeadNodAnimation() {
    ESP_LOGI(TAG, "执行点头动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteHeadNodAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 点头动画
    // 如果有舵机控制器，则控制舵机点头
    if (servo_controller_ != nullptr) {
        // 使用ServoController中的HeadNod方法，而不是直接调用HeadMove
        servo_controller_->HeadNod(10);
        
        // 恢复中心位置
        servo_controller_->HeadCenter(10);
    } else {
        // 如果没有舵机控制器，则用眼睛模拟点头效果
        for (int i = 0; i < 3; i++) {
            // 眼睛向下
            left_eye_y_ += 10;
            right_eye_y_ += 10;
            DrawEmoji(false);
            vTaskDelay(pdMS_TO_TICKS(200));
            
            // 眼睛恢复
            left_eye_y_ -= 10;
            right_eye_y_ -= 10;
            DrawEmoji(false);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    
    // 显示一段时间
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 恢复原始状态
    EyeCenter();
}

void EmojiController::ExecuteHeadShakeAnimation() {
    ESP_LOGI(TAG, "执行摇头动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteHeadShakeAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 摇头动画
    // 如果有舵机控制器，则控制舵机摇头
    if (servo_controller_ != nullptr) {
        // 参考原始Arduino代码实现摇头动作
        servo_controller_->HeadShake(1);
        
        // 恢复中心位置
        servo_controller_->HeadCenter(1);
    } else {
        // 如果没有舵机控制器，则用眼睛模拟摇头效果
        // 左右移动眼睛
        left_eye_x_ -= 10;
        right_eye_x_ -= 10;
        DrawEmoji(false);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        left_eye_x_ += 20;
        right_eye_x_ += 20;
        DrawEmoji(false);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        left_eye_x_ -= 20;
        right_eye_x_ -= 20;
        DrawEmoji(false);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        left_eye_x_ += 20;
        right_eye_x_ += 20;
        DrawEmoji(false);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        left_eye_x_ -= 20;
        right_eye_x_ -= 20;
        DrawEmoji(false);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        left_eye_x_ += 10;
        right_eye_x_ += 10;
        DrawEmoji(false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 显示一段时间
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 恢复原始状态
    EyeCenter();
}

void EmojiController::ExecuteHeadRollAnimation() {
    ESP_LOGI(TAG, "执行转圈动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteHeadRollAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 转圈动画
    // 如果有舵机控制器，则控制舵机转圈
    if (servo_controller_ != nullptr) {
        // 参考原始Arduino代码实现转圈动作
        servo_controller_->HeadCenter();
        servo_controller_->HeadDown(SERVO_OFFSET_Y/2+5);
        servo_controller_->HeadMove(SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, SERVO_DELAY);
        servo_controller_->HeadMove(-SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, SERVO_DELAY);
        servo_controller_->HeadMove(-SERVO_OFFSET_X, SERVO_OFFSET_Y/2, SERVO_DELAY);
        servo_controller_->HeadMove(SERVO_OFFSET_X, SERVO_OFFSET_Y/2, SERVO_DELAY);
        servo_controller_->HeadMove(-SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, SERVO_DELAY);
        servo_controller_->HeadMove(SERVO_OFFSET_X, -SERVO_OFFSET_Y/2, SERVO_DELAY);
        servo_controller_->HeadMove(SERVO_OFFSET_X, SERVO_OFFSET_Y/2, SERVO_DELAY);
        servo_controller_->HeadMove(-SERVO_OFFSET_X, SERVO_OFFSET_Y/2, SERVO_DELAY);
        servo_controller_->HeadCenter();
    } else {
        // 如果没有舵机控制器，则用眼睛模拟转圈效果
        // 绕圈移动眼睛
        int radius = 5;
        for (int angle = 0; angle < 360; angle += 30) {
            float rad = angle * 3.14159 / 180.0;
            int x_offset = radius * cos(rad);
            int y_offset = radius * sin(rad);
            
            left_eye_x_ = DISPLAY_WIDTH / 2 - ref_eye_width_ / 2 - ref_space_between_eye_ / 2 + x_offset;
            left_eye_y_ = DISPLAY_HEIGHT / 2 + y_offset;
            right_eye_x_ = DISPLAY_WIDTH / 2 + ref_eye_width_ / 2 + ref_space_between_eye_ / 2 + x_offset;
            right_eye_y_ = DISPLAY_HEIGHT / 2 + y_offset;
            
            DrawEmoji(false);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    // 显示一段时间
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 恢复原始状态
    EyeCenter();
}

void EmojiController::InitEmoji() {
    ESP_LOGI(TAG, "初始化表情");
    
    // 创建动画队列
    if (animation_queue_ == nullptr) {
        animation_queue_ = xQueueCreate(ANIMATION_QUEUE_SIZE, sizeof(AnimationMessage));
    }
    
    // 创建动画任务
    if (animation_task_handle_ == nullptr) {
        BaseType_t task_created = xTaskCreate(
            AnimationTask,
            "AnimationTask",
            4096,
            this,
            5,
            &animation_task_handle_
        );
        
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "创建动画任务失败");
        }
    }
    
    // 创建动画定时器任务
    if (animation_timer_task_handle_ == nullptr) {
        BaseType_t task_created = xTaskCreate(
            AnimationTimerTask,
            "AnimationTimerTask",
            4096,
            this,
            5,
            &animation_timer_task_handle_);
        
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "创建动画定时器任务失败");
        }
    }
    
    ESP_LOGI(TAG, "表情初始化完成");
}


void EmojiController::EyeUp() {
    ESP_LOGI(TAG, "眼睛向上");
    
    // 如果有舵机控制器，则同时移动头部
    if (servo_controller_) {
        servo_controller_->HeadUp();
    }
    
    // 使用已有的动画逻辑
    AnimationMessage msg;
    msg.type = AnimationType::HEAD_NOD;  // 使用点头动画作为向上看的动画
    msg.param = 0;
    
    // 发送动画消息到队列
    if (animation_queue_) {
        xQueueSend(animation_queue_, &msg, 0);
    }
}

void EmojiController::EyeDown() {
    ESP_LOGI(TAG, "眼睛向下");
    
    // 如果有舵机控制器，则移动头部
    if (servo_controller_) {
        servo_controller_->HeadDown();
    }
    
    // 不展示悲伤表情，只移动头部
    // 注释掉原来的动画逻辑
    /*
    AnimationMessage msg;
    msg.type = AnimationType::SAD;  // 使用悲伤动画作为向下看的动画
    msg.param = 0;
    
    // 发送动画消息到队列
    if (animation_queue_) {
        xQueueSend(animation_queue_, &msg, 0);
    }
    */
}

void EmojiController::ExecuteAwkwardAnimation() {
    ESP_LOGI(TAG, "执行尴尬表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteAwkwardAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸
            const int original_width = left_eye_width_;
            const int original_height = left_eye_height_;
            
            // 创建三条竖线（汗滴）
            lv_obj_t* sweat_line1 = nullptr;
            lv_obj_t* sweat_line2 = nullptr;
            lv_obj_t* sweat_line3 = nullptr;
            
            // 动画参数
            const int animation_steps = 10;
            const int animation_delay_ms = 20;
            const int target_eye_height = 4;  // 眼睛高度缩小到4像素
            
            // 眼睛高度逐渐缩小的动画
            for (int step = 0; step < animation_steps; step++) {
                // 计算当前高度
                float progress = (float)step / animation_steps;
                int current_height = original_height - progress * (original_height - target_eye_height);
                
                // 更新左眼大小和位置
                lv_obj_set_size(left_eye_, original_width, current_height);
                lv_obj_set_pos(left_eye_, left_eye_x_ - original_width/2, left_eye_y_ - current_height/2);
                
                // 更新右眼大小和位置
                lv_obj_set_size(right_eye_, original_width, current_height);
                lv_obj_set_pos(right_eye_, right_eye_x_ - original_width/2, right_eye_y_ - current_height/2);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 创建右上角的三条竖线（汗滴）
            const int sweat_width = 2;
            const int sweat_heights[] = {10, 10, 12};
            const int sweat_spacing = 4;  // 竖线之间的间距
            
            // 修改竖线位置，确保在屏幕可见区域内
            // 原来的位置可能超出了屏幕范围
            const int sweat_x_start = right_eye_x_ + 15;  // 从右眼右侧开始，但不要太远
            const int sweat_y = 10;  // 位于屏幕上方，确保可见
            
            ESP_LOGI(TAG, "创建竖线1，位置: (%d, %d)，大小: %dx%d", 
                     sweat_x_start, sweat_y, sweat_width, sweat_heights[0]);
            
            // 创建第一条竖线
            sweat_line1 = lv_obj_create(emoji_screen_);
            lv_obj_set_style_bg_color(sweat_line1, lv_color_black(), 0);  // 设置为黑色，在OLED上显示为白色
            lv_obj_set_style_border_width(sweat_line1, 0, 0);
            lv_obj_set_style_radius(sweat_line1, 1, 0);  // 圆角半径
            lv_obj_set_size(sweat_line1, sweat_width, sweat_heights[0]);
            lv_obj_set_pos(sweat_line1, sweat_x_start, sweat_y);
            lv_obj_clear_flag(sweat_line1, LV_OBJ_FLAG_HIDDEN);  // 确保不隐藏
            
            ESP_LOGI(TAG, "创建竖线2，位置: (%d, %d)，大小: %dx%d", 
                     sweat_x_start + sweat_width + sweat_spacing, sweat_y, sweat_width, sweat_heights[1]);
            
            // 创建第二条竖线
            sweat_line2 = lv_obj_create(emoji_screen_);
            lv_obj_set_style_bg_color(sweat_line2, lv_color_black(), 0);  // 设置为黑色，在OLED上显示为白色
            lv_obj_set_style_border_width(sweat_line2, 0, 0);
            lv_obj_set_style_radius(sweat_line2, 1, 0);
            lv_obj_set_size(sweat_line2, sweat_width, sweat_heights[1]);
            lv_obj_set_pos(sweat_line2, sweat_x_start + sweat_width + sweat_spacing, sweat_y);
            lv_obj_clear_flag(sweat_line2, LV_OBJ_FLAG_HIDDEN);  // 确保不隐藏
            
            ESP_LOGI(TAG, "创建竖线3，位置: (%d, %d)，大小: %dx%d", 
                     sweat_x_start + 2 * (sweat_width + sweat_spacing), sweat_y, sweat_width, sweat_heights[2]);
            
            // 创建第三条竖线
            sweat_line3 = lv_obj_create(emoji_screen_);
            lv_obj_set_style_bg_color(sweat_line3, lv_color_black(), 0);  // 设置为黑色，在OLED上显示为白色
            lv_obj_set_style_border_width(sweat_line3, 0, 0);
            lv_obj_set_style_radius(sweat_line3, 1, 0);
            lv_obj_set_size(sweat_line3, sweat_width, sweat_heights[2]);
            lv_obj_set_pos(sweat_line3, sweat_x_start + 2 * (sweat_width + sweat_spacing), sweat_y);
            lv_obj_clear_flag(sweat_line3, LV_OBJ_FLAG_HIDDEN);  // 确保不隐藏
            
            // 刷新显示
            lv_refr_now(NULL);
            
            // 如果有舵机控制器，则控制舵机向右下方倾斜表示尴尬
            if (servo_controller_ != nullptr) {
                servo_controller_->HeadMove(10, -5, 1);  // 轻微右倾
            }
            
            // 显示一段时间
            vTaskDelay(pdMS_TO_TICKS(2000));
            
            // 删除创建的对象
            if (sweat_line1) lv_obj_del(sweat_line1);
            if (sweat_line2) lv_obj_del(sweat_line2);
            if (sweat_line3) lv_obj_del(sweat_line3);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteAwkwardAnimation: 获取显示锁失败");
        }
    }
    
    // 添加短暂延迟，确保显示更新完成
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
    
    // 恢复原始状态
    EyeCenter();
}

void EmojiController::ExecuteCryAnimation() {
    ESP_LOGI(TAG, "执行哭泣表情动画");
    
    // 检查对象是否存在
    if (emoji_screen_ == nullptr || left_eye_ == nullptr || right_eye_ == nullptr) {
        ESP_LOGW(TAG, "ExecuteCryAnimation: 屏幕或眼睛对象不存在");
        return;
    }
    
    // 先居中眼睛
    EyeCenter(false);
    
    if (display_) {
        try {
            DisplayLockGuard lock(display_);
            
            // 保存原始尺寸和位置
            const int original_width = left_eye_width_;
            const int original_height = left_eye_height_;
            const int original_left_y = left_eye_y_;
            const int original_right_y = right_eye_y_;
            
            // 创建泪滴对象
            lv_obj_t* left_tear = nullptr;
            lv_obj_t* right_tear = nullptr;
            
            // 动画参数
            const int animation_steps = 10;
            const int animation_delay_ms = 30;
            const int eye_move_up = 8;  // 眼睛下部上移的像素数
            const int tear_size = 16;   // 泪滴大小
            
            // 眼睛下部上移的动画
            for (int step = 0; step < animation_steps; step++) {
                // 计算当前上移距离
                float progress = (float)step / animation_steps;
                int current_move_up = progress * eye_move_up;
                
                // 更新左眼大小和位置 - 下部上移
                lv_obj_set_size(left_eye_, original_width, original_height - current_move_up);
                lv_obj_set_pos(left_eye_, left_eye_x_ - original_width/2, original_left_y - original_height/2);
                
                // 更新右眼大小和位置 - 下部上移
                lv_obj_set_size(right_eye_, original_width, original_height - current_move_up);
                lv_obj_set_pos(right_eye_, right_eye_x_ - original_width/2, original_right_y - original_height/2);
                
                // 刷新显示
                lv_refr_now(NULL);
                
                // 短暂延迟
                vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
            }
            
            // 创建左眼泪滴
            left_tear = lv_obj_create(emoji_screen_);
            lv_obj_set_style_bg_color(left_tear, lv_color_black(), 0);  // 设置为黑色，在OLED上显示为白色
            lv_obj_set_style_border_width(left_tear, 0, 0);
            lv_obj_set_style_radius(left_tear, tear_size/2, 0);  // 圆形泪滴
            lv_obj_set_size(left_tear, tear_size, tear_size);
            
            // 创建右眼泪滴
            right_tear = lv_obj_create(emoji_screen_);
            lv_obj_set_style_bg_color(right_tear, lv_color_black(), 0);  // 设置为黑色，在OLED上显示为白色
            lv_obj_set_style_border_width(right_tear, 0, 0);
            lv_obj_set_style_radius(right_tear, tear_size/2, 0);  // 圆形泪滴
            lv_obj_set_size(right_tear, tear_size, tear_size);
            
            // 泪滴从眼睛下方流下的动画，重复3次
            const int tear_start_y = original_left_y + original_height/2 - eye_move_up;
            const int tear_end_y = tear_start_y + 20;  // 泪滴下落的距离
            
            // 重复3次泪滴动画
            for (int repeat = 0; repeat < 3; repeat++) {
                // 每次重置泪滴位置到起始位置
                lv_obj_set_pos(left_tear, left_eye_x_ - tear_size/2, tear_start_y);
                lv_obj_set_pos(right_tear, right_eye_x_ - tear_size/2, tear_start_y);
                
                // 泪滴下落动画
                for (int step = 0; step < animation_steps; step++) {
                    // 计算当前泪滴位置
                    float progress = (float)step / animation_steps;
                    int current_tear_y = tear_start_y + progress * (tear_end_y - tear_start_y);
                    
                    // 更新左眼泪滴位置
                    lv_obj_set_pos(left_tear, left_eye_x_ - tear_size/2, current_tear_y);
                    
                    // 更新右眼泪滴位置
                    lv_obj_set_pos(right_tear, right_eye_x_ - tear_size/2, current_tear_y);
                    
                    // 刷新显示
                    lv_refr_now(NULL);
                    
                    // 短暂延迟
                    vTaskDelay(pdMS_TO_TICKS(animation_delay_ms));
                }
                
                // 每次泪滴下落后短暂停顿
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            
            // 显示一段时间
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // 删除泪滴对象
            if (left_tear) lv_obj_del(left_tear);
            if (right_tear) lv_obj_del(right_tear);
            
            // 恢复眼睛原始大小和位置
            lv_obj_set_size(left_eye_, original_width, original_height);
            lv_obj_set_pos(left_eye_, left_eye_x_ - original_width/2, original_left_y - original_height/2);
            lv_obj_set_size(right_eye_, original_width, original_height);
            lv_obj_set_pos(right_eye_, right_eye_x_ - original_width/2, original_right_y - original_height/2);
            
            // 刷新显示
            lv_refr_now(NULL);
            
        } catch (...) {
            ESP_LOGW(TAG, "ExecuteCryAnimation: 获取显示锁失败");
        }
    }
    
    // 添加短暂延迟，确保显示更新完成
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 恢复舵机位置
    if (servo_controller_ != nullptr) {
        servo_controller_->HeadCenter(10);
    }
    
    // 恢复原始状态
    EyeCenter();
}

// 暂停LVGL任务（空实现，防止链接错误）
void EmojiController::SuspendLVGLTask() {
    // TODO: 实现暂停LVGL任务的逻辑
}
