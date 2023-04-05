#pragma once

// SPDX-License-Identifier: MIT

#include "types.h"



size_t convertCheckFatLabel(const char *const label, char *dosLabel);
size_t convertCheckExfatLabel(const char *const label, char16_t *utf16Label);
