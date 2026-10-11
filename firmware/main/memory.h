#pragma once

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define EXTERNAL_RAM EXT_RAM_BSS_ATTR
#else
#define EXTERNAL_RAM
#endif
