#pragma once

#include "input/navigation.h"
#include "net/fetch_diag.h"
#include "power/runtime_policy.h"
#include "sense/battery.h"
#include "sense/shtc3.h"
#include "store/nvs.h"

namespace rcpaper {

void console_dump_state(View view, RuntimeMode runtime_mode,
                        bool usb_host_present, const DeviceState& state,
                        const Shtc3Reading& environment,
                        const BatteryReading& battery,
                        const FetchDiag* usage_last = nullptr,
                        const FetchDiag* usage_err = nullptr,
                        const FetchDiag* token_last = nullptr,
                        const FetchDiag* token_err = nullptr);
void console_dump_frame(View view, const DeviceState& state);

}  // namespace rcpaper
