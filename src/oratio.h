#ifndef _FCITX5_MODULES_ORATIO_ORATIO_H_
#define _FCITX5_MODULES_ORATIO_ORATIO_H_

#include <memory>
#include <vector>
#include <string>

#include "fcitx-config/configuration.h"
#include "fcitx-config/iniparser.h"
#include "fcitx-utils/key.h"
#include "fcitx/addonfactory.h"
#include "fcitx/addoninstance.h"
#include "fcitx/addonmanager.h"
#include "fcitx/inputcontextproperty.h"
#include "fcitx/instance.h"
#include "fcitx/action.h"

namespace fcitx {

FCITX_CONFIGURATION(
    OratioConfig,
    Option<std::string> command{this, "Command", "Command to execute",
                               "/home/l8ng/Kits/Apps/funasr/samples/rust/sample/target/release/rust_audio_client run --with-auto-input"};
    KeyListOption triggerKey{this, "TriggerKey", "Trigger Key",
                           {Key("Control+Shift+Alt+E")}, KeyListConstrain()};
);

class OratioState;

class OratioAction : public Action {
public:
    std::string shortText(InputContext *) const override {
        return "Oratio";
    }

    std::string icon(InputContext *) const override {
        return "fcitx-oratio";
    }
};

class Oratio : public AddonInstance {
    static constexpr char configFile[] = "conf/oratio.conf";

public:
    Oratio(Instance *instance);
    ~Oratio();

    Instance *instance() { return instance_; }

    bool trigger(InputContext *inputContext);
    void updateUI(InputContext *inputContext, bool trigger = false);
    auto &factory() { return factory_; }

    void reloadConfig() override {
        readAsIni(config_, configFile);
    }

    const Configuration *getConfig() const override {
        return &config_;
    }

    void setConfig(const RawConfig &config) override {
        config_.load(config, true);
        safeSaveAsIni(config_, configFile);
    }

private:
    void executeCommand();
    void handleKeyEvent(KeyEvent &keyEvent);
    void setupEventHandlers();
    void setupAction();

    // FCITX_ADDON_EXPORT_FUNCTION(Oratio, trigger);

    Instance *instance_;
    OratioConfig config_;
    std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>> eventHandlers_;
    std::unique_ptr<OratioAction> action_;
    KeyList selectionKeys_;
    FactoryFor<OratioState> factory_;
};

} // namespace fcitx

#endif // _FCITX5_MODULES_ORATIO_ORATIO_H_