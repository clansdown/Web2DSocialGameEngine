#pragma once
// Chat HTTP endpoint registration. Individual handlers live in ChatHandlers.cpp;
// only the registration hook is exposed so ApiHandlers.hpp stays acyclic.
#include <unordered_map>

#include "ApiHandlers.hpp"

// Registers every chat endpoint into the shared handler map. Defined in
// chat/ChatHandlers.cpp. Declared here (and forward-declared in ApiHandlers.hpp)
// so the inline getEndpointHandlers() can call it without a circular include.
void register_chat_handlers(std::unordered_map<std::string, ApiHandler>& handlers);
