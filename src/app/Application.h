#pragma once

#include <cstdint>
#include <string>
#include <memory>

#include "audio/TextBlipPlayer.h"
#include "core/BookCache.h"
#include "core/BookLibrary.h"
#include "core/ProgressStore.h"
#include "core/SettingsStore.h"
#include "core/BookTypes.h"
#include "platform/Clock.h"
#include "platform/FileSystem.h"
#include "platform/Input.h"
#include "platform/Renderer.h"
#include "ui/SceneManager.h"

class Application {
public:
    Application(
        std::unique_ptr<Renderer> renderer,
        std::unique_ptr<Input> input,
        std::unique_ptr<FileSystem> fileSystem,
        std::unique_ptr<Clock> clock);

    bool initialize();
    void run();
    void shutdown();

    bool isRunning() const;
    void requestQuit();

    Renderer& renderer();
    Input& input();
    FileSystem& fileSystem();
    Clock& clock();
    SceneManager& sceneManager();
    BookLibrary& library();
    BookCache& bookCache();
    ProgressStore& progressStore();
    SettingsStore& settingsStore();
    TextBlipPlayer& textBlipPlayer();

    const ReaderSettings& settings() const;
    ReaderSettings& settings();
    std::string performanceHudText() const;
    int batteryPercent() const;  // -1 表示不支持
    std::string currentTimeString() const;  // 返回 HH:MM 格式的本地时间

    void enterSleep();  // 电源键触发：关屏并等待唤醒

private:
    std::string buildScreenshotPath() const;
    std::string performanceLogPath() const;
    void updatePerformanceStats(float dt);
    void sampleCpuUsage();
    void appendPerformanceLogLine() const;

    // 屏幕电源控制
    static std::string findBacklightPath();
    void screenSleep();
    void screenWake();
    int savedBrightness_ = -1;
    std::string backlightPath_;

    bool running_ = false;
    float averageFrameMs_ = 0.0f;
    float averageFps_ = 0.0f;
    float sampledCpuPercent_ = -1.0f;
    float cpuSampleTimer_ = 0.0f;
    float perfLogTimer_ = 0.0f;
    std::uint64_t lastCpuSampleMs_ = 0;
    std::uint64_t lastProcessJiffies_ = 0;
    ReaderSettings settings_;
    BookLibrary library_;
    BookCache bookCache_;
    ProgressStore progressStore_;
    SettingsStore settingsStore_;
    TextBlipPlayer textBlipPlayer_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<Input> input_;
    std::unique_ptr<FileSystem> fileSystem_;
    std::unique_ptr<Clock> clock_;
    SceneManager sceneManager_;
};
