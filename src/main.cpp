#include "main.hpp"

#include "AdjustmentModes.hpp"
#include "Calibration.hpp"
#include "Config.hpp"
#include "Gizmos.hpp"
#include "UI.hpp"

#include "bsml/shared/BSML.hpp"
#include "custom-types/shared/register.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

namespace EasyOffset {
modloader::ModInfo const& GetModInfo() { return modInfo; }
void EnsureInitialized() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    Config::Load();
    AdjustmentModes::Initialize();
    Gizmos::Initialize();
    Calibration::Initialize();
    Logger.info("EasyOffset initialized");
}

}

MOD_EXTERN_FUNC void setup(CModInfo* info) noexcept {
    *info = modInfo.to_c();
    Paper::Logger::RegisterFileContextId(EasyOffset::Logger.tag);
}

MOD_EXTERN_FUNC void late_load() noexcept {
    il2cpp_functions::Init();
    custom_types::Register::AutoRegister();
    BSML::Init();
    EasyOffset::UI::Register();
    EasyOffset::InstallHooks();
    EasyOffset::Logger.info("EasyOffset loaded");
}
