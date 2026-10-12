#pragma once

#include <vector>
#include <string>

#include "core/BookTypes.h"
#include "text/TextTyper.h"
#include "ui/AppScene.h"
#include "ui/widgets/DialogueBox.h"

enum class ReaderState {
    Typing,
    WaitingForInput,
    AutoAdvanceDelay,
    Paused,
    Finished,
};

class ReaderScene : public AppScene {
public:
    ReaderScene(Application& app, BookScript book, ReadingProgress progress);

    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render(Renderer& renderer) override;
    bool shouldRenderContinuously() const override;
    bool consumeRenderRequest() override;

private:
    void handleInput();
    void updateTyping(float dt);
    void revealCurrentSentence();
    void skipCurrentSentence();  // 跳过当前句，不播放打字动画
    void moveToNextSentence();
    void moveToPreviousSentence();
    void moveToPreviousChapter();
    void moveToNextChapter();
    void persistProgress();
    bool hasMorePagedLines(Renderer& renderer) const;
    void advancePage(Renderer& renderer);
    bool canAdvanceTypedPage(Renderer& renderer) const;
    const std::vector<std::string>& allVisibleLines(Renderer& renderer);
    std::vector<int>& visibleRevealWidthsOnCurrentPage(Renderer& renderer) const;
    std::size_t visibleLineCapacity(Renderer& renderer) const;
    std::size_t visibleCharsOnCurrentPage(Renderer& renderer) const;
    void refreshLayoutCache(Renderer& renderer);
    void invalidateLayoutCache();

    const Chapter* currentChapter() const;
    const Sentence* currentSentence() const;

private:
    BookScript book_;
    ReadingProgress progress_;
    ReaderState state_ = ReaderState::Typing;
    TextTyper typer_;
    DialogueBox dialogueBox_;
    float autoAdvanceTimer_ = 0.0f;
    float sentencePauseSeconds_ = 0.35f;
    std::size_t pageStartLine_ = 0;
    std::size_t maxVisibleLines_ = 4;
    mutable bool layoutCacheValid_ = false;
    mutable std::string cachedSentenceText_;
    mutable int cachedTextWidth_ = 0;
    mutable int cachedBodyFont_ = 0;
    mutable FontPreset cachedFontPreset_ = FontPreset::Normal;
    mutable std::vector<std::string> cachedWrappedLines_;
    mutable std::vector<int> cachedLineWidths_;
    mutable std::vector<std::size_t> cachedLineCharCounts_;
    mutable std::vector<std::vector<std::string>> cachedLineCodepoints_;
    mutable std::vector<int> cachedVisibleRevealWidths_;
    bool renderRequested_ = false;
    float skipHoldTimer_ = 0.0f;   // Y 长按计时，第一次触发需要初始延迟
    int skipSkipCount_ = 0;         // 当前长按已触发次数
    static constexpr float kSkipInitialDelay_ = 0.4f;  // 初始长按延迟(s)
    static constexpr float kSkipRepeatInterval_ = 0.08f;  // 重复跳过间隔(s)
};
