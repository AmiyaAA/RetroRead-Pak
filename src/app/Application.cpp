#include "app/Application.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <thread>
#include <memory>
#include <sstream>
#include <utility>

#include "ui/BookListScene.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
std::uint64_t currentTimeMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::uint64_t readProcessJiffies() {
#ifdef _WIN32
    return 0;
#else
    std::ifstream stat("/proc/self/stat");
    if (!stat.is_open()) {
        return 0;
    }

    std::string line;
    std::getline(stat, line);
    const std::size_t closingParen = line.rfind(')');
    if (closingParen == std::string::npos || closingParen + 2 >= line.size()) {
        return 0;
    }

    std::istringstream rest(line.substr(closingParen + 2));
    std::string token;
    std::uint64_t utime = 0;
    std::uint64_t stime = 0;
    for (int index = 3; rest >> token; ++index) {
        if (index == 14) {
            utime = std::stoull(token);
        } else if (index == 15) {
            stime = std::stoull(token);
            break;
        }
    }

    return utime + stime;
#endif
}

}

Application::Application(
    std::unique_ptr<Renderer> renderer,
    std::unique_ptr<Input> input,
    std::unique_ptr<FileSystem> fileSystem,
    std::unique_ptr<Clock> clock)
    : renderer_(std::move(renderer)),
      input_(std::move(input)),
      fileSystem_(std::move(fileSystem)),
      clock_(std::move(clock)) {
}

bool Application::initialize() {
    if (!renderer_ || !input_ || !fileSystem_ || !clock_) {
        return false;
    }

    if (!renderer_->initialize() || !input_->initialize() || !fileSystem_->initialize()) {
        return false;
    }
    textBlipPlayer_.initialize();

    settings_ = ReaderSettings{};
    settingsStore_.load(*fileSystem_, settings_);
    library_.scan(*fileSystem_);
    progressStore_.load(*fileSystem_);
    sceneManager_.setRoot(std::make_unique<BookListScene>(*this));
    running_ = true;
    return true;
}

void Application::run() {
    constexpr float kIdleSleepSeconds = 1.0f / 30.0f;
    float idleSeconds = 0.0f;
    bool forceRender = true;

    while (running_) {
        const float dt = clock_->tick();
        updatePerformanceStats(dt);

        input_->poll();
        if (input_->quitRequested()) {
            requestQuit();
        }

        // 电源键：关屏休眠
        if (input_->wasPressed(Action::Power)) {
            enterSleep();
            forceRender = true;
            if (!running_) {
                break;
            }
            continue;
        }

        sceneManager_.update(dt);

        const bool hasInput =
            input_->wasPressed(Action::Confirm) || input_->wasPressed(Action::FastForward) ||
            input_->wasPressed(Action::ToggleAuto) || input_->wasPressed(Action::Screenshot) ||
            input_->wasPressed(Action::OpenMenu) || input_->wasPressed(Action::Start) ||
            input_->wasPressed(Action::Back) || input_->wasPressed(Action::Secondary) ||
            input_->wasPressed(Action::Up) || input_->wasPressed(Action::Down) ||
            input_->wasPressed(Action::Left) || input_->wasPressed(Action::Right) ||
            input_->wasPressed(Action::PrevChapter) || input_->wasPressed(Action::NextChapter) ||
            input_->isHeld(Action::Skip);

        const bool shouldRender =
            forceRender || hasInput || sceneManager_.shouldRenderContinuously() || sceneManager_.consumeRenderRequest();
        if (shouldRender) {
            renderer_->beginFrame();
            sceneManager_.render(*renderer_);
            if (input_->wasPressed(Action::Screenshot)) {
                const std::string screenshotPath = buildScreenshotPath();
                if (!screenshotPath.empty()) {
                    renderer_->saveScreenshot(screenshotPath);
                }
            }
            renderer_->endFrame();
            forceRender = false;
        }

        if (hasInput) {
            idleSeconds = 0.0f;
            continue;
        }

        idleSeconds += dt;
        if (idleSeconds >= 0.25f) {
            std::this_thread::sleep_for(std::chrono::duration<float>(kIdleSleepSeconds));
        }
    }
}

void Application::shutdown() {
    if (fileSystem_) {
        progressStore_.save(*fileSystem_);
        settingsStore_.save(*fileSystem_, settings_);
    }
    if (input_) {
        input_->shutdown();
    }
    textBlipPlayer_.shutdown();
    if (renderer_) {
        renderer_->shutdown();
    }
    running_ = false;
}

bool Application::isRunning() const {
    return running_;
}

void Application::requestQuit() {
    running_ = false;
}

Renderer& Application::renderer() {
    return *renderer_;
}

Input& Application::input() {
    return *input_;
}

FileSystem& Application::fileSystem() {
    return *fileSystem_;
}

Clock& Application::clock() {
    return *clock_;
}

SceneManager& Application::sceneManager() {
    return sceneManager_;
}

BookLibrary& Application::library() {
    return library_;
}

BookCache& Application::bookCache() {
    return bookCache_;
}

ProgressStore& Application::progressStore() {
    return progressStore_;
}

SettingsStore& Application::settingsStore() {
    return settingsStore_;
}

TextBlipPlayer& Application::textBlipPlayer() {
    return textBlipPlayer_;
}

const ReaderSettings& Application::settings() const {
    return settings_;
}

ReaderSettings& Application::settings() {
    return settings_;
}

std::string Application::buildScreenshotPath() const {
    if (!fileSystem_) {
        return {};
    }

    const std::string screenshotsDir = fileSystem_->savesPath() + "/Screenshots";
    if (!const_cast<FileSystem&>(*fileSystem_).createDirectories(screenshotsDir)) {
        return {};
    }

    const auto now = std::chrono::system_clock::now();
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::ostringstream path;
    path << screenshotsDir << "/RetroRead-" << millis << ".bmp";
    return path.str();
}

void Application::updatePerformanceStats(float dt) {
    const float frameMs = dt * 1000.0f;
    averageFrameMs_ = averageFrameMs_ <= 0.0f ? frameMs : averageFrameMs_ * 0.92f + frameMs * 0.08f;

    const float fps = dt > 0.0001f ? 1.0f / dt : 0.0f;
    averageFps_ = averageFps_ <= 0.0f ? fps : averageFps_ * 0.92f + fps * 0.08f;

    cpuSampleTimer_ += dt;
    if (cpuSampleTimer_ >= 0.5f) {
        cpuSampleTimer_ = 0.0f;
        sampleCpuUsage();
    }

    if (settings_.performanceMode == PerformanceMode::Log) {
        perfLogTimer_ += dt;
        if (perfLogTimer_ >= 0.2f) {
            perfLogTimer_ = 0.0f;
            appendPerformanceLogLine();
        }
    } else {
        perfLogTimer_ = 0.0f;
    }
}

void Application::sampleCpuUsage() {
    const std::uint64_t nowMs = currentTimeMs();
    const std::uint64_t processJiffies = readProcessJiffies();
    if (processJiffies == 0 || lastCpuSampleMs_ == 0 || lastProcessJiffies_ == 0) {
        lastCpuSampleMs_ = nowMs;
        lastProcessJiffies_ = processJiffies;
        return;
    }

#ifndef _WIN32
    const std::uint64_t deltaMs = nowMs > lastCpuSampleMs_ ? nowMs - lastCpuSampleMs_ : 0;
    const std::uint64_t deltaJiffies =
        processJiffies >= lastProcessJiffies_ ? processJiffies - lastProcessJiffies_ : 0;
    if (deltaMs > 0) {
        const long hz = sysconf(_SC_CLK_TCK);
        if (hz > 0) {
            sampledCpuPercent_ =
                std::min(999.0f, static_cast<float>(deltaJiffies) * 100000.0f / (static_cast<float>(hz) * deltaMs));
        }
    }
#endif

    lastCpuSampleMs_ = nowMs;
    lastProcessJiffies_ = processJiffies;
}

std::string Application::performanceHudText() const {
    std::ostringstream out;
    out << std::fixed << std::setprecision(0) << averageFps_ << " FPS  ";
    out << std::setprecision(1) << averageFrameMs_ << " ms";
    if (sampledCpuPercent_ >= 0.0f) {
        out << "  CPU " << std::setprecision(1) << sampledCpuPercent_ << "%";
    }
    return out.str();
}

std::string Application::performanceLogPath() const {
    if (!fileSystem_) {
        return {};
    }
    return fileSystem_->savesPath() + "/perf.log";
}

void Application::appendPerformanceLogLine() const {
    if (!fileSystem_) {
        return;
    }

    const std::string path = performanceLogPath();
    std::ifstream in(path);
    std::string content;
    if (in.is_open()) {
        std::ostringstream existing;
        existing << in.rdbuf();
        content = existing.str();
    }

    std::ostringstream line;
    line << "fps=" << std::fixed << std::setprecision(1) << averageFps_
         << "\tframe_ms=" << std::setprecision(2) << averageFrameMs_;
    if (sampledCpuPercent_ >= 0.0f) {
        line << "\tcpu=" << std::setprecision(1) << sampledCpuPercent_;
    }
    line << '\n';

    fileSystem_->writeTextFile(path, content + line.str());
}

int Application::batteryPercent() const {
#ifdef _WIN32
    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps) && sps.BatteryLifePercent != 255) {
        return static_cast<int>(sps.BatteryLifePercent);
    }
    return -1;
#else
    // 环境变量优先：允许设备特定路径覆盖
    if (const char* envPath = std::getenv("NEXTREADING_BATTERY_CAPACITY_PATH")) {
        std::ifstream f(envPath);
        if (f.is_open()) {
            int val = -1;
            f >> val;
            if (val >= 0 && val <= 100) {
                return val;
            }
        }
    }

    // TrimUI/Linux 设备通常通过 sysfs 暴露电量
    static const char* kPaths[] = {
        "/sys/class/power_supply/battery/capacity",
        "/sys/class/power_supply/BAT0/capacity",
        "/sys/class/power_supply/BAT1/capacity",
        "/sys/class/power_supply/axp2202-battery/capacity",
        "/sys/class/power_supply/bd71827-battery/capacity",
        "/sys/devices/platform/axp2202.10/power_supply/battery/capacity",
    };
    for (const char* path : kPaths) {
        std::ifstream f(path);
        if (f.is_open()) {
            int val = -1;
            f >> val;
            if (val >= 0 && val <= 100) {
                return val;
            }
        }
    }
    return -1;
#endif
}

std::string Application::findBacklightPath() {
    // 环境变量优先
    if (const char* envPath = std::getenv("NEXTREADING_BACKLIGHT_PATH")) {
        if (fs::exists(envPath)) {
            return envPath;
        }
    }

#ifndef _WIN32
    // 自动扫描 /sys/class/backlight/ 下的设备
    const fs::path backlightRoot("/sys/class/backlight");
    if (fs::exists(backlightRoot)) {
        for (const auto& entry : fs::directory_iterator(backlightRoot)) {
            const fs::path brightnessFile = entry.path() / "brightness";
            if (fs::exists(brightnessFile)) {
                return brightnessFile.string();
            }
        }
    }
#endif
    return {};
}

void Application::screenSleep() {
    if (backlightPath_.empty()) {
        backlightPath_ = findBacklightPath();
    }
    if (backlightPath_.empty()) {
        return;
    }

    std::ifstream in(backlightPath_);
    if (!in.is_open()) {
        return;
    }
    in >> savedBrightness_;
    in.close();

    std::ofstream out(backlightPath_);
    if (out.is_open()) {
        out << 0;
    }
}

void Application::screenWake() {
    if (backlightPath_.empty() || savedBrightness_ < 0) {
        return;
    }

    std::ofstream out(backlightPath_);
    if (out.is_open()) {
        out << savedBrightness_;
    }
    savedBrightness_ = -1;
}

void Application::enterSleep() {
    screenSleep();

    // 先等电源键松开，避免同一个按下事件立刻唤醒
    while (running_ && input_->isHeld(Action::Power)) {
        input_->poll();
        if (input_->quitRequested()) {
            requestQuit();
            screenWake();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // 等待任意按键唤醒
    while (running_) {
        input_->poll();
        if (input_->quitRequested()) {
            requestQuit();
            break;
        }

        const bool wakeTriggered =
            input_->wasPressed(Action::Power) ||
            input_->wasPressed(Action::Confirm) ||
            input_->wasPressed(Action::Back) ||
            input_->wasPressed(Action::Start) ||
            input_->wasPressed(Action::OpenMenu) ||
            input_->isHeld(Action::Power) ||
            input_->isHeld(Action::Confirm);

        if (wakeTriggered) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    screenWake();
    // 丢弃休眠期间累积的时钟增量
    clock_->tick();
}
