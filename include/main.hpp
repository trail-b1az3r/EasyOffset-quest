#pragma once

#include "scotland2/shared/modloader.h"

#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "beatsaber-hook/shared/utils/logging.hpp"

#include "paper2_scotland2/shared/logger.hpp"

#include "_config.hpp"

namespace EasyOffset {
inline constexpr auto Logger = Paper::ConstLoggerContext("EasyOffset");

modloader::ModInfo const& GetModInfo();

// Loads config etc. on first use (needs il2cpp to be ready)
void EnsureInitialized();

void InstallHooks();
}
