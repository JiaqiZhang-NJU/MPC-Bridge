// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>

namespace Bridge {
// This entry accepts public video-page URLs. It never accepts process arguments or local paths.
bool NormalizeOnlineUrl(std::string_view input, std::string& output, std::string& error);
}
