#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "ApiHandlers.hpp"

// Registers both player support/notification endpoints and the staff console
// endpoint family.
void register_staff_handlers(std::unordered_map<std::string, ApiHandler>& handlers);
