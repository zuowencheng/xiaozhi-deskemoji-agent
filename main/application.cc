#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
#include "boards/esp32-s3n16r8-emoji/gesture_sensor.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

// ★ 2026-10-13 手势传感器全局桥（定义在 boards/esp32-s3n16r8-emoji/emoji_board.cc）。
//   服务端 set_gesture_mode 工具经 system/gesture_mode 命令驱动它切模式。
extern GestureSensor* g_gesture_sensor;

#define TAG "Application"


static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto assets = board.GetAssets();
    if (!assets) {
        ESP_LOGE(TAG, "Assets is not set for board %s", BOARD_NAME);
        return;
    }

    if (!assets->partition_valid()) {
        ESP_LOGE(TAG, "Assets partition is not valid for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");
    if (!download_url.empty()) {
        settings.EraseKey("download_url");
    }
    if (download_url.empty() && !assets->checksum_valid()) {
        download_url = assets->default_assets_url();
    }

    if (!download_url.empty()) {
        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveMode(false);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets->Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveMode(true);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    }

    // Apply assets
    assets->Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota.HasNewVersion()) {
            if (UpgradeFirmware(ota)) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation (don't break, just fall through)
        }

        // No new version, mark the current version as valid
        ota.MarkCurrentVersionValid();
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            // ★ 2026-10-13 修「手势唤醒后说话没人理/极慢」：
            //   这里原来硬写 kListeningModeManualStop。而服务端在 manual 模式下
            //   （core/handle/receiveAudioHandle.py）只把音频往缓存里塞，
            //   **VAD 永不触发收尾**，ASR 流不结束 → startToChat 不执行 →
            //   LLM 一次都没被调用。表现就是屏幕显示"聆听中"、人说话却毫无反应。
            //   改成与语音唤醒（ToggleChatState）完全一致的取值，
            //   手势唤醒 = 语音唤醒，交互流程对齐。
            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            // 同上：打断播报后进入的聆听，也必须与语音唤醒同模式。
            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else {
        // ★ 2026-10-14 静默失败留痕。
        //   StartListening() 只认 idle / speaking 两种状态，其余（connecting /
        //   activating / audio_testing / upgrading / fatal…）**什么都不做就返回**。
        //   主人看到的是"它点头了、像被唤醒了"，但服务端一条 listen 都没收到 ——
        //   屏幕上"没进聆听"和"进了但没声音"长得一模一样，必须打出来。
        ESP_LOGW(TAG, "[唤醒失败] StartListening 被忽略：当前状态=%d（只认 idle/speaking）",
                 (int)device_state_);
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    // ★★ 2026-10-03 提示音总闸：只放行「说完话提示音（可以说话了）」，
    //   其余全部静音 —— 连接成功 success、表情 exclamation、低电量 low_battery、
    //   按键 success、激活码逐位 0~9…一律不播。
    //   主人原话「开场提示音永久去掉」。放在 Initialize 之后、Start 之前，
    //   保证任何早期 PlaySound 也会被拦。
    {
        static const std::string_view kAllowed[] = {
            Lang::Sounds::OGG_LISTENING,          // 「说完话可以说了」——唯一保留
            Lang::Sounds::OGG_GESTURE_WELCOME,    // 手势唤醒开场白（2026-10-13）
            Lang::Sounds::OGG_GESTURE_BYE,        // 手势关闭退场语（2026-10-13）
        };
        audio_service_.SetSoundAllowlist(kAllowed,
                                         sizeof(kAllowed) / sizeof(kAllowed[0]));
    }
    audio_service_.Start();

    // ★ 2026-10-14 开机后台预热 AFE（详见 AudioService::PrepareVoiceProcessing）
    //   建 AFE 要 ~3.7s，绝不能卡在主循环上，所以丢到独立任务里跑。
    //   开机几秒内完成，之后任何唤醒都是「零等待」。
    xTaskCreate([](void* arg) {
        static_cast<Application*>(arg)->GetAudioService().PrepareVoiceProcessing();
        vTaskDelete(NULL);
    }, "afe_prewarm", 8192, this, 1, NULL);

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version or get the MQTT broker address
    Ota ota;
    CheckNewVersion(ota);

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (device_state_ == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        // ★ 2026-10-05 记录这次「说话」是怎么来的：
                        //   说话前是 idle ⇒ 用户没开口，是服务端推的**主动播报**；
                        //   说话前是 listening ⇒ 用户刚说完话，是正常对话的回复。
                        //   ★ 必须在 SetDeviceState 之前判断，否则状态已经被改掉了。
                        speaking_from_idle_ = (device_state_ == kDeviceStateIdle);
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (device_state_ == kDeviceStateSpeaking) {
                        // ★ 2026-10-05 主动播报播完 → 回**待命**，不要再回聆听。
                        //   原上游行为：AutoStop 模式下一律回聆听。这对「用户说话→回复」是对的（要多轮对话），
                        //   但对「服务端主动推的播报」是错的：用户根本没开口，播完却把麦克风一直开着，
                        //   屏幕一直显示聆听中，环境噪声还会触发识别 —— 费电也费钱。
                        //   ★ 只改主动播报这条路径，正常对话的多轮体验完全不变。
                        if (listening_mode_ == kListeningModeManualStop || speaking_from_idle_) {
                            ESP_LOGI(TAG, "TTS stop: %s，回到待机",
                                     speaking_from_idle_ ? "主动播报结束" : "ManualStop 模式");
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                        speaking_from_idle_ = false;
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else if (strcmp(command->valuestring, "sound") == 0) {
                    // ★ 2026-10-03 服务端可控提示音。
                    //   为什么不在 SetDeviceState(kDeviceStateListening) 里自动播？
                    //   因为那样"说完话要不要响一声"写死在固件里，后台改不了；
                    //   改走 system/sound 命令后，**管理台勾一下就生效，不用重新刷机**。
                    //   发 {"type":"system","command":"sound","sound":"listening"|"popup"}
                    auto snd = cJSON_GetObjectItem(root, "sound");
                    bool is_popup = (snd && cJSON_IsString(snd) &&
                                     strcmp(snd->valuestring, "popup") == 0);
                    Schedule([this, is_popup]() {
                        audio_service_.PlaySound(is_popup ? Lang::Sounds::OGG_POPUP
                                                          : Lang::Sounds::OGG_LISTENING);
                    });
                } else if (strcmp(command->valuestring, "standby") == 0) {
                    // ★ 2026-10-03 主人说「退下吧」要真的待机，不能还在后台聆听。
                    //   发 {"type":"system","command":"standby"} → 关麦 + 回待机，
                    //   设备只剩唤醒词监听，不再往服务端送音频。
                    ESP_LOGI(TAG, "System command: standby（真实待机）");
                    Schedule([this]() {
                        aborted_ = true;
                        SetDeviceState(kDeviceStateIdle);
                    });
                } else if (strcmp(command->valuestring, "gesture_mode") == 0) {
                    // ★ 2026-10-13 手势识别模式开关（服务端 set_gesture_mode 工具）。
                    //   发 {"type":"system","command":"gesture_mode","enabled":true|false}
                    //   true  = 开启：左右上下全响应 + 随机卖萌动画停
                    //   false = 关闭（默认）：只认前推开/关聆听 + 随机动画恢复
                    auto en = cJSON_GetObjectItem(root, "enabled");
                    bool enabled = cJSON_IsBool(en) ? cJSON_IsTrue(en) : false;
                    ESP_LOGI(TAG, "System command: gesture_mode enabled=%d", enabled);
                    Schedule([this, enabled]() {
                        if (g_gesture_sensor) {
                            g_gesture_sensor->SetGestureModeEnabled(enabled);
                        } else {
                            ESP_LOGW(TAG, "gesture_mode: 手势传感器不可用");
                        }
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                // ★ 2026-10-03 传空 sound：原来这里播 OGG_VIBRATION（"嘟"一声），
                //   属于"该响的时候它自己响"，主人要求开场一律静音。
                //   底层白名单也会拦，这里直接不传更干净。
                Alert(status->valuestring, message->valuestring, emotion->valuestring, "");
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    bool protocol_started = protocol_->Start();

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota.HasServerTime();
    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // 2026-10-03 主人反馈：连上服务器时这一声「叮」特别刺耳，去掉。
        // 原来播的是官方 OGG_SUCCESS（尖锐的提示音），只要不播就行。
        // 需要提示音的地方统一由服务端发 system/sound 决定（可在后台勾选）。
        // audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }

    // Start the main event loop task with priority 3
    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            // ★ 2026-10-14 主循环存活心跳。手势任务优先级 7、主循环被饿死时
            //   Schedule 进来的任务可以好几秒不执行（表现为"唤醒了却没进聆听"），
            //   而这在日志上和"别的环节坏了"长得一样。每 10 秒报一次，
            //   一眼看出到底是主循环没跑，还是跑了但状态没变。
            if (clock_ticks_ % 10 == 0) {
                ESP_LOGI(TAG, "[主循环] 存活 tick=%ld 状态=%d", (long)clock_ticks_, (int)device_state_);
            }
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();

            // ★★ 2026-10-04 聆听超时（主人：别一直挂在「聆听中」，费电也费钱）
            // 根因：上游在 AutoStop 模式下 tts/stop 之后**一律回 kDeviceStateListening**
            //   （下面 SetDeviceState 里 tts/stop 那段，git diff 确认是上游原版、非本地改动），
            //   而本板 AFE 没有"语音说完就自动停止聆听"的逻辑，于是只要你不再开口，
            //   它就永远停在聆听中 —— 麦克风一直开、屏幕一直显示聆听、环境噪声随时触发识别。
            // 处理：连续 N 秒听不到声音就回待机（唤醒词检测随之恢复）。
            //   ★ N 秒从服务端 hello 下发（后台可调，默认 180），0 或负数 = 永不超时。
            //   ★ 有声音立即清零，所以**不会掐断你正在说的话**；
            //     超时后想继续聊，说句唤醒词即可（本来也要说）。
            if (device_state_ == kDeviceStateListening) {
                // ★ 2026-10-14 静默类故障必须留痕（踩过多次）：
                //   「该听见的没听见」和「循环压根没跑」在日志上长得一模一样。
                //   每 2 秒报一次采集链路体检，一眼看出 AFE 是不是掉线了。
                if (clock_ticks_ % 2 == 0) {
                    ESP_LOGI(TAG, "[聆听体检] 已聆听 %ds：AFE采集=%d 有声=%d 声道开=%d 入队=%lu 已发=%lu",
                             listening_silent_ticks_,
                             (int)audio_service_.IsAudioProcessorRunning(),
                             (int)audio_service_.IsVoiceDetected(),
                             (int)(protocol_ ? protocol_->IsAudioChannelOpened() : 0),
                             (unsigned long)audio_service_.GetEncodeInCount(),
                             (unsigned long)audio_service_.GetSendOutCount());
                }
                if (audio_service_.IsVoiceDetected()) {
                    listening_silent_ticks_ = 0;
                } else {
                    listening_silent_ticks_++;
                    int limit = protocol_ ? protocol_->listening_timeout_sec() : 180;
                    if (limit > 0 && listening_silent_ticks_ >= limit) {
                        ESP_LOGI(TAG, "Listening: 连续 %d 秒没听到声音，回到待机", limit);
                        SetDeviceState(kDeviceStateIdle);
                    }
                }
            }
        
            // Print the debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
                // SystemInfo::PrintTaskList();
                SystemInfo::PrintHeapStats();
            }

            // ★ 本地改造（2026-10-02）：桌面常供电设备 —— 保持与服务端的长连接
            // 背景：本设备是插电台式机，不需要"空闲就断开省电"。
            //   服务端要能随时推送「任务完成播报」「定时提醒」这类主动语音，
            //   前提是设备此刻挂在 WebSocket 上（否则服务端 /notify 只能收到 503）。
            // 实测：设备原来只在唤醒时连接，说完话就断，主动播报无处可推。
            // 做法：每 5 秒看一次，处于 idle 且未连接就补上；对话中不打扰。
            if (clock_ticks_ % 5 == 0
                && device_state_ == kDeviceStateIdle
                && protocol_ && !protocol_->IsAudioChannelOpened()) {
                ESP_LOGI(TAG, "KeepAlive: idle 且未连接，尝试保持长连接");
                Schedule([this]() {
                    if (device_state_ == kDeviceStateIdle
                        && protocol_ && !protocol_->IsAudioChannelOpened()) {
                        if (protocol_->OpenAudioChannel()) {
                            ESP_LOGI(TAG, "KeepAlive: 已建立常连（idle 等待服务端主动播报）");
                        } else {
                            ESP_LOGW(TAG, "KeepAlive: 建连失败，5 秒后重试");
                        }
                    }
                });
            }

            // ★★ 2026-10-03 后台重启后的自动重连（主人报「重启后台后屏幕一直显示
            //   无法连接服务，直到主动唤醒才恢复」）。
            // 根因：MAIN_EVENT_ERROR 分支里 SetDeviceState(kIdle) 就结束了，
            //   设备再也不去开音频通道 → 永远停在"连接不上"，必须靠唤醒触发
            //   StartListening 里的 OpenAudioChannel 才会恢复。
            // 做法：每 5 秒探测一次，**不限当前状态**（只要不是正在对话/说话），
            //   没连接就主动重连。16 秒后仍不成功 → 显示"连接中"而不是"无法连接"，
            //   因为此时它确实在不断重试，展示"正在连接"更贴近事实。
            if (clock_ticks_ % 5 == 0
                && protocol_ && !protocol_->IsAudioChannelOpened()
                && device_state_ != kDeviceStateSpeaking
                && device_state_ != kDeviceStateListening) {
                if (server_unreachable_ticks_ == 0) {
                    ESP_LOGI(TAG, "Reconnect: 音频通道未开，主动重连服务端");
                    Schedule([this]() {
                        if (protocol_ && !protocol_->IsAudioChannelOpened()
                            && device_state_ != kDeviceStateSpeaking
                            && device_state_ != kDeviceStateListening) {
                            if (protocol_->OpenAudioChannel()) {
                                ESP_LOGI(TAG, "Reconnect: 已重新连上后台");
                            } else {
                                ESP_LOGW(TAG, "Reconnect: 重连失败，继续等待下一轮");
                            }
                        }
                    });
                }
                server_unreachable_ticks_++;
                // 16 秒（4 轮）还连不上 → 显示"连接中"，别再显示"无法连接"
                if (server_unreachable_ticks_ == 4) {
                    auto display = Board::GetInstance().GetDisplay();
                    if (display) {
                        display->SetStatus(Lang::Strings::CONNECTING);
                        display->SetChatMessage("system", "正在重连后台…");
                    }
                    ESP_LOGW(TAG, "Reconnect: 多次尝试未成功，屏幕改为显示「正在重连」");
                }
            }
            // 连上了就清零计数
            if (protocol_ && protocol_->IsAudioChannelOpened()
                && server_unreachable_ticks_ != 0) {
                server_unreachable_ticks_ = 0;
                ESP_LOGI(TAG, "Reconnect: 已连接，恢复正常");
            }
        }
    }
}

void Application::OnWakeWordDetected() {
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#endif
        // ⚠️ 2026-10-03 踩坑记录（勿再重复）：
        //   这里我曾把 AutoStop 改成 ManualStop，理由是「本板无 AEC，做不到边放边听，
        //   说完话应该回待机」。**这个改动是错的**，实测症状是：
        //     主人说「你好小智」→ 设备刚进聆听态、主人还没开口
        //     → 服务端 VAD 判空音频 → 立刻发 tts stop
        //     → ManualStop 分支直接 SetDeviceState(kIdle) → 屏幕变「待命」
        //     → 主人后面说的话**完全不录**，「叫小智没反应」。
        //   根因：ManualStop 的语义是"**本段音频结束就收工**"，
        //   它不等 VAD 判完，设计上就是给"按键说话"那种一次性交互用的。
        //   语音唤醒场景必须用 AutoStop —— 由**服务端 VAD 判断说完了没**，
        //   设备只负责继续收音。这正是上游 `aec_mode_ == kAecOff ? AutoStop : Realtime`
        //   这行的用意（本板无 AEC → 走 AutoStop）。
        //   之前"话被切碎/一直聆听"的真凶是 VAD 参数被调错，不是这个模式。
        // 2026-10-03：这里原本补过一句 PlaySound(OGG_POPUP)，但唤醒后紧接着就进
        // kDeviceStateListening，那边会播 OGG_LISTENING —— 两个音连着响很怪，已移除。
        // 提示音统一在 SetDeviceState(kDeviceStateListening) 里播。
        // ⚠️ 顺带记一笔坑：PlaySound 只认 **Opus in Ogg**（找 "OpusHead"），
        //    之前用 soundfile 生成的是 OGG Vorbis → seen_head 永远 false → 一个字节不播。
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // Send the state change event
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");
            // 进入聆听 → 静默计数从头开始（配合上面 CLOCK_TICK 的超时）
            listening_silent_ticks_ = 0;

            // ★ 2026-10-03 提示音改为**服务端可控**：
            //   这里原来硬播 OGG_LISTENING，导致"说完话要不要响一声"只能改固件重刷。
            //   现在由服务端在 tts/stop 之后发 system/sound 命令（见 OnIncomingJson），
            //   管理台勾一下就生效。想恢复自动播，把下面这行取消注释即可：
            // audio_service_.PlaySound(Lang::Sounds::OGG_LISTENING);

            // ★ 2026-10-13 修「假开启」：原逻辑用 IsAudioProcessorRunning()
            //   间接推断"服务端是否在聆听会话中"——processor 在跑 ≠ 服务端
            //   知道。Speaking/异常路径漏掉 SendStartListening 时，设备
            //   显示"聆听中"但服务端没收过 listen，说话全程没人理。
            //   服务端 listen start 幂等（reset_audio_states），
            //   每次进入聆听都明发，杜绝本地与服务端状态漂移。
            protocol_->SendStartListening(listening_mode_);
            audio_service_.EnableVoiceProcessing(true);
            audio_service_.EnableWakeWordDetection(false);
            // ★ 2026-10-14 留痕：进聆听的瞬间 AFE 到底起没起来。
            //   「屏幕显示聆听中但说话没反应」和「AFE 压根没跑」在日志上原本长得一样。
            ESP_LOGI(TAG, "[聆听体检] 进入聆听：AFE采集=%d 唤醒词=%d 模式=%d",
                     (int)audio_service_.IsAudioProcessorRunning(),
                     (int)audio_service_.IsWakeWordRunning(), (int)listening_mode_);
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
#if CONFIG_USE_AFE_WAKE_WORD
                audio_service_.EnableWakeWordDetection(true);
#else
                audio_service_.EnableWakeWordDetection(false);
#endif
            }
            audio_service_.ResetDecoder();
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(Ota& ota, const std::string& url) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    // Use provided URL or get from OTA object
    std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
    std::string version_info = url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";
    
    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());
    
    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);
    
    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveMode(false);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = ota.StartUpgradeFromUrl(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveMode(true); // Restore power save mode
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    if (protocol_ == nullptr) {
        return;
    }

    // Make sure you are using main thread to send MCP message
    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
        ESP_LOGI(TAG, "Send MCP message in main thread");
        protocol_->SendMcpMessage(payload);
    } else {
        ESP_LOGI(TAG, "Send MCP message in sub thread");
        Schedule([this, payload = std::move(payload)]() {
            protocol_->SendMcpMessage(payload);
        });
    }
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

// 新增：接收外部音频数据（如音乐播放）
void Application::AddAudioData(AudioStreamPacket&& packet) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (device_state_ == kDeviceStateIdle && codec->output_enabled()) {
        // packet.payload包含的是原始PCM数据（int16_t）
        if (packet.payload.size() >= 2) {
            size_t num_samples = packet.payload.size() / sizeof(int16_t);
            std::vector<int16_t> pcm_data(num_samples);
            memcpy(pcm_data.data(), packet.payload.data(), packet.payload.size());
            
            // 检查采样率是否匹配，如果不匹配则进行简单重采样
            if (packet.sample_rate != codec->output_sample_rate()) {
                // ESP_LOGI(TAG, "Resampling music audio from %d to %d Hz", 
                //         packet.sample_rate, codec->output_sample_rate());
                
                // 验证采样率参数
                if (packet.sample_rate <= 0 || codec->output_sample_rate() <= 0) {
                    ESP_LOGE(TAG, "Invalid sample rates: %d -> %d", 
                            packet.sample_rate, codec->output_sample_rate());
                    return;
                }
                
                std::vector<int16_t> resampled;
                
                if (packet.sample_rate > codec->output_sample_rate()) {
                    ESP_LOGI(TAG, "Music Player: Adjust the sampling rate from %d Hz to %d Hz", 
                        codec->output_sample_rate(), packet.sample_rate);

                    // 尝试动态切换采样率
                    if (codec->SetOutputSampleRate(packet.sample_rate)) {
                        ESP_LOGI(TAG, "Successfully switched to music playback sampling rate: %d Hz", packet.sample_rate);
                    } else {
                        ESP_LOGW(TAG, "Unable to switch sampling rate, continue using current sampling rate: %d Hz", codec->output_sample_rate());
                    }
                } else {
                    if (packet.sample_rate > codec->output_sample_rate()) {
                        // 下采样：简单丢弃部分样本
                        float downsample_ratio = static_cast<float>(packet.sample_rate) / codec->output_sample_rate();
                        size_t expected_size = static_cast<size_t>(pcm_data.size() / downsample_ratio + 0.5f);
                        std::vector<int16_t> resampled(expected_size);
                        size_t resampled_index = 0;
                        
                        for (size_t i = 0; i < pcm_data.size(); ++i) {
                            if (i % static_cast<size_t>(downsample_ratio) == 0) {
                                resampled[resampled_index++] = pcm_data[i];
                            }
                        }
                        
                        pcm_data = std::move(resampled);
                        ESP_LOGI(TAG, "Downsampled %d -> %d samples (ratio: %.2f)", 
                                pcm_data.size(), resampled.size(), downsample_ratio);
                    } else if (packet.sample_rate < codec->output_sample_rate()) {
                        // 上采样：线性插值
                        float upsample_ratio = codec->output_sample_rate() / static_cast<float>(packet.sample_rate);
                        size_t expected_size = static_cast<size_t>(pcm_data.size() * upsample_ratio + 0.5f);
                        resampled.reserve(expected_size);
                    
                        for (size_t i = 0; i < pcm_data.size(); ++i) {
                            // 添加原始样本
                            resampled.push_back(pcm_data[i]);
                        
                        
                            // 计算需要插值的样本数
                            int interpolation_count = static_cast<int>(upsample_ratio) - 1;
                            if (interpolation_count > 0 && i + 1 < pcm_data.size()) {
                                int16_t current = pcm_data[i];
                                int16_t next = pcm_data[i + 1];
                                for (int j = 1; j <= interpolation_count; ++j) {
                                    float t = static_cast<float>(j) / (interpolation_count + 1);
                                    int16_t interpolated = static_cast<int16_t>(current + (next - current) * t);
                                    resampled.push_back(interpolated);
                                }
                            } else if (interpolation_count > 0) {
                                // 最后一个样本，直接重复
                                for (int j = 1; j <= interpolation_count; ++j) {
                                    resampled.push_back(pcm_data[i]);
                                }
                            }
                        }

                        ESP_LOGI(TAG, "Upsampled %d -> %d samples (ratio: %.2f)", 
                            pcm_data.size(), resampled.size(), upsample_ratio);
                    }
                }
                
                pcm_data = std::move(resampled);
            }
            
            // 确保音频输出已启用
            if (!codec->output_enabled()) {
                codec->EnableOutput(true);
            }
            
            // 发送PCM数据到音频编解码器
            codec->OutputData(pcm_data);
            
            audio_service_.UpdateOutputTimestamp();
        }
    }
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}