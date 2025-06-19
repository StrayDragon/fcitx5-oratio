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
#include <format>

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
    OratioCandidateWord(Oratio *q, const std::string &content, int displayIndex,
                        bool isCommand)
        : q_(q), isCommand_(isCommand) {
        Text text;
        if (isCommand) {
            text.append(std::format("[{}] Execute: ", displayIndex));
            text.append(content);
        } else {
            text.append(std::format("[{}] ", displayIndex));
            text.append(content);
        }
        setText(std::move(text));
    }

    void select(InputContext *inputContext) const override {
        if (isCommand_) {
            // 执行命令
            auto *state = inputContext->propertyFor(&q_->factory());
            state->mode_ = OratioMode::Executing;
            q_->updateUI(inputContext);

            // 同步执行命令（简化实现）
            std::string result =
                executeCommand(std::string(text().stringAt(1)));

            if (!result.empty()) {
                state->results_ = splitOutput(result);
                state->mode_ = OratioMode::ShowingResults;
                q_->updateUI(inputContext);
            } else {
                state->reset(inputContext);
            }
        } else {
            // 提交结果
            inputContext->commitString(text().stringAt(1));
            auto *state = inputContext->propertyFor(&q_->factory());
            state->reset(inputContext);
        }
    }

private:
    std::string executeCommand(const std::string &command) const {
        std::string result;
        FILE *pipe = popen(command.c_str(), "r");
        if (!pipe) {
            return "";
        }
        int c;
        while ((c = fgetc(pipe)) != EOF) {
            result += static_cast<char>(c);
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
    bool isCommand_;
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

        if (keyEvent.key().checkKeyList(
                instance_->globalConfig().defaultPrevPage())) {
            if (auto *pageable = candidateList->toPageable()) {
                if (pageable->hasPrev()) {
                    keyEvent.accept();
                    pageable->prev();
                    inputContext->updateUserInterface(
                        UserInterfaceComponent::InputPanel);
                }
            }
            return;
        }

        if (keyEvent.key().checkKeyList(
                instance_->globalConfig().defaultNextPage())) {
            if (auto *pageable = candidateList->toPageable()) {
                if (pageable->hasNext()) {
                    keyEvent.filterAndAccept();
                    pageable->next();
                    inputContext->updateUserInterface(
                        UserInterfaceComponent::InputPanel);
                }
            }
            return;
        }

        if (auto *cursorMovable = candidateList->toCursorMovable()) {
            if (keyEvent.key().checkKeyList(
                    instance_->globalConfig().defaultPrevCandidate())) {
                keyEvent.filterAndAccept();
                cursorMovable->prevCandidate();
                inputContext->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                return;
            }

            if (keyEvent.key().checkKeyList(
                    instance_->globalConfig().defaultNextCandidate())) {
                keyEvent.filterAndAccept();
                cursorMovable->nextCandidate();
                inputContext->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                return;
            }
        }

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

    if (keyEvent.key().isModifier() || keyEvent.key().hasModifier()) {
        return;
    }
    keyEvent.accept();

    if (keyEvent.key().check(FcitxKey_Escape)) {
        state->reset(inputContext);
        return;
    }
}

void Oratio::updateUI(InputContext *inputContext, bool trigger) {
    auto *state = inputContext->propertyFor(&factory_);
    auto *inputPanel = &inputContext->inputPanel();

    inputPanel->reset();

    auto setAux = [&](const std::string &aux) {
        Text text(aux);
        inputPanel->setAuxUp(text);
    };

    if (state->mode_ == OratioMode::Executing) {
        setAux("Executing...");
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    if (state->mode_ == OratioMode::ShowingResults) {
        auto candidateList = std::make_unique<CommonCandidateList>();
        candidateList->setPageSize(10);

        if (trigger) {
            candidateList->append<OratioCandidateWord>(this, state->results_[0], 1,
                                                      true);
        } else {
            int index = 0;
            const int maxCandidates = 10;
            for (const auto &line : state->results_) {
                if (index >= maxCandidates) {
                    break;
                }
                candidateList->append<OratioCandidateWord>(this, line, index + 1,
                                                          false);
                index++;
            }
        }

        if (!candidateList->empty()) {
            candidateList->setGlobalCursorIndex(0);
        }

        candidateList->setSelectionKey(selectionKeys_);
        candidateList->setLayoutHint(CandidateLayoutHint::Vertical);
        inputPanel->setCandidateList(std::move(candidateList));
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