#pragma once

#include <cstddef>
#include <cstdint>

namespace qjsp {

bool unicode_is_id_start(uint32_t c);
bool unicode_is_id_continue(uint32_t c);

} // namespace qjsp
