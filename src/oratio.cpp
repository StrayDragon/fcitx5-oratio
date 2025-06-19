/*
 * SPDX-FileCopyrightText: 2024 l8ng
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "oratio.h"

#include <cstdlib>
#include <memory>
#include <sstream>
#include <array>

#include "fcitx-utils/capabilityflags.h"
#include "fcitx-utils/inputbuffer.h"
#include "fcitx-utils/key.h"
#include "fcitx-utils/keysym.h"
#include "fcitx-utils/stringutils.h"
#include "fcitx-utils/textformatflags.h"
#include "fcitx-utils/utf8.h"
#include "fcitx/addonfactory.h"
#include "fcitx/addoninstance.h"
#include "fcitx/addonmanager.h"
#include "fcitx/candidatelist.h"
#include "fcitx/event.h"
#include "fcitx/inputcontext.h"
#include "fcitx/inputcontextmanager.h"
#include "fcitx/inputpanel.h"
#include "fcitx/instance.h"
#include "fcitx/text.h"
#include "fcitx/userinterface.h"
#include "fcitx/userinterfacemanager.h"

namespace fcitx {

enum class OratioMode {
    Off = 0,
    Executing,
    ShowingResults
};

class OratioState : public InputContextProperty {
public:
    OratioState(Oratio *q) : q_(q) {
        buffer_.setMaxSize(100);
    }

    OratioMode mode_ = OratioMode::Off;
    InputBuffer buffer_;
    std::vector<std::string> results_;
    Oratio *q_;

    void reset(InputContext *inputContext) {
        mode_ = OratioMode::Off;
        buffer_.clear();
        buffer_.shrinkToFit();
        results_.clear();
        inputContext->inputPanel().reset();
        inputContext->updatePreedit();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
};

class OratioCandidateWord : public CandidateWord {
public:
    OratioCandidateWord(Oratio *q, const std::string &result, int index)
        : q_(q), result_(result), index_(index) {
        Text text;
        if (index == 0) {
            text.append("[" + std::to_string(index + 1) + "] " + "Execute: " + result);
        } else {
            text.append("[" + std::to_string(index + 1) + "] " + result);
        }
        setText(std::move(text));
    }

    void select(InputContext *inputContext) const override {
        if (index_ == 0) {
            // 执行命令
            auto *state = inputContext->propertyFor(&q_->factory());
            state->mode_ = OratioMode::Executing;
            q_->updateUI(inputContext);

            // 同步执行命令（简化实现）
            std::string result = executeCommand(result_);

            if (!result.empty()) {
                state->results_ = splitOutput(result);
                state->mode_ = OratioMode::ShowingResults;
                q_->updateUI(inputContext);
            } else {
                state->reset(inputContext);
            }
        } else {
            // 提交结果
            inputContext->commitString(result_);
            auto *state = inputContext->propertyFor(&q_->factory());
            state->reset(inputContext);
        }
    }

private:
    std::string executeCommand(const std::string &command) const {
        std::array<char, 128> buffer;
        std::string result;

        auto pipe = popen(command.c_str(), "r");
        if (!pipe) {
            return "";
        }

        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }

        pclose(pipe);
        return result;
    }

    std::vector<std::string> splitOutput(const std::string &output) const {
        std::vector<std::string> lines;
        std::istringstream stream(output);
        std::string line;

        while (std::getline(stream, line)) {
            if (!line.empty()) {
                lines.push_back(line);
            }
        }

        return lines;
    }

    Oratio *q_;
    std::string result_;
    int index_;
};

Oratio::Oratio(Instance *instance)
    : instance_(instance),
      factory_([this](InputContext &) { return new OratioState(this); }) {

    instance_->inputContextManager().registerProperty("oratioState", &factory_);

    // 设置选择键
    KeySym syms[] = {
        FcitxKey_1, FcitxKey_2, FcitxKey_3, FcitxKey_4, FcitxKey_5,
        FcitxKey_6, FcitxKey_7, FcitxKey_8, FcitxKey_9, FcitxKey_0,
    };
    KeyStates states = KeyState::Alt;
    for (auto sym : syms) {
        selectionKeys_.emplace_back(sym, states);
    }

    setupEventHandlers();
    setupAction();
    reloadConfig();
}

Oratio::~Oratio() {}

void Oratio::setupEventHandlers() {
    // 监听触发键
    eventHandlers_.emplace_back(instance_->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::Default,
        [this](Event &event) {
            auto &keyEvent = static_cast<KeyEvent &>(event);
            if (keyEvent.isRelease()) {
                return;
            }

            if (keyEvent.key().checkKeyList(static_cast<const OratioConfig*>(getConfig())->triggerKey.value()) &&
                trigger(keyEvent.inputContext())) {
                keyEvent.filterAndAccept();
                return;
            }
        }));

    // 重置状态的事件处理
    auto reset = [this](Event &event) {
        auto &icEvent = static_cast<InputContextEvent &>(event);
        auto *state = icEvent.inputContext()->propertyFor(&factory_);
        if (state->mode_ != OratioMode::Off) {
            state->reset(icEvent.inputContext());
        }
    };

    eventHandlers_.emplace_back(instance_->watchEvent(
        EventType::InputContextFocusOut, EventWatcherPhase::Default, reset));
    eventHandlers_.emplace_back(instance_->watchEvent(
        EventType::InputContextReset, EventWatcherPhase::Default, reset));
    eventHandlers_.emplace_back(instance_->watchEvent(
        EventType::InputContextSwitchInputMethod, EventWatcherPhase::Default, reset));

    // 处理键盘事件
    eventHandlers_.emplace_back(instance_->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::PreInputMethod,
        [this](Event &event) {
            auto &keyEvent = static_cast<KeyEvent &>(event);
            auto *inputContext = keyEvent.inputContext();
            auto *state = inputContext->propertyFor(&factory_);

            if (state->mode_ == OratioMode::Off) {
                return;
            }

            keyEvent.filter();
            if (keyEvent.isRelease()) {
                return;
            }

            handleKeyEvent(keyEvent);
        }));
}

void Oratio::setupAction() {
    action_ = std::make_unique<OratioAction>();
    instance_->userInterfaceManager().registerAction("oratio", action_.get());
}

bool Oratio::trigger(InputContext *inputContext) {
    auto *state = inputContext->propertyFor(&factory_);

    if (state->mode_ != OratioMode::Off) {
        state->reset(inputContext);
        return true;
    }

    // 显示命令选项
    state->mode_ = OratioMode::ShowingResults;
    state->results_.clear();
    state->results_.push_back(static_cast<const OratioConfig*>(getConfig())->command.value());

    updateUI(inputContext, true);
    return true;
}

void Oratio::handleKeyEvent(KeyEvent &keyEvent) {
    auto *inputContext = keyEvent.inputContext();
    auto *state = inputContext->propertyFor(&factory_);
    auto candidateList = inputContext->inputPanel().candidateList();

    if (candidateList) {
        int idx = keyEvent.key().digitSelection(KeyState::Alt);
        if (idx >= 0) {
            keyEvent.accept();
            if (idx < candidateList->size()) {
                candidateList->candidate(idx).select(inputContext);
            }
            return;
        }

        // 处理翻页
        if (keyEvent.key().checkKeyList(
                instance_->globalConfig().defaultPrevPage())) {
            auto *pageable = candidateList->toPageable();
            if (!pageable->hasPrev()) {
                if (pageable->usedNextBefore()) {
                    keyEvent.accept();
                    return;
                }
            } else {
                keyEvent.accept();
                pageable->prev();
                inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
                return;
            }
        }

        if (keyEvent.key().checkKeyList(
                instance_->globalConfig().defaultNextPage())) {
            keyEvent.filterAndAccept();
            candidateList->toPageable()->next();
            inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
            return;
        }

        // 处理候选项导航
        if (keyEvent.key().checkKeyList(
                instance_->globalConfig().defaultPrevCandidate())) {
            keyEvent.filterAndAccept();
            candidateList->toCursorMovable()->prevCandidate();
            inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
            return;
        }

        if (keyEvent.key().checkKeyList(
                instance_->globalConfig().defaultNextCandidate())) {
            keyEvent.filterAndAccept();
            candidateList->toCursorMovable()->nextCandidate();
            inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
            return;
        }

        // 处理回车
        if (keyEvent.key().check(FcitxKey_Return) ||
            keyEvent.key().check(FcitxKey_KP_Enter)) {
            keyEvent.accept();
            if (!candidateList->empty() && candidateList->cursorIndex() >= 0) {
                candidateList->candidate(candidateList->cursorIndex())
                    .select(inputContext);
            }
            return;
        }
    }

    // 处理修饰键
    if (keyEvent.key().isModifier() || keyEvent.key().hasModifier()) {
        return;
    }

    keyEvent.accept();

    // 处理ESC键
    if (keyEvent.key().check(FcitxKey_Escape)) {
        state->reset(inputContext);
        return;
    }
}

void Oratio::updateUI(InputContext *inputContext, bool trigger) {
    auto *state = inputContext->propertyFor(&factory_);
    inputContext->inputPanel().reset();

    if (state->mode_ == OratioMode::Executing) {
        Text auxUp("Oratio: Executing command...");
        inputContext->inputPanel().setAuxUp(auxUp);

    } else if (state->mode_ == OratioMode::ShowingResults) {
        auto candidateList = std::make_unique<CommonCandidateList>();
        candidateList->setPageSize(instance_->globalConfig().defaultPageSize());

        int maxCandidates = 10;
        int count = 0;

        for (size_t i = 0; i < state->results_.size() && count < maxCandidates; ++i) {
            candidateList->append<OratioCandidateWord>(this, state->results_[i], i);
            count++;
        }

        if (!candidateList->empty()) {
            candidateList->setGlobalCursorIndex(0);
        }
        candidateList->setSelectionKey(selectionKeys_);
        candidateList->setLayoutHint(CandidateLayoutHint::Vertical);
        inputContext->inputPanel().setCandidateList(std::move(candidateList));

        Text auxUp;
        if (trigger) {
            auxUp.append("Oratio: Select command or result");
        } else {
            auxUp.append("Oratio: Command results");
        }
        inputContext->inputPanel().setAuxUp(auxUp);
    }

    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

class OratioModuleFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        return new Oratio(manager->instance());
    }
};

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(oratio, fcitx::OratioModuleFactory)