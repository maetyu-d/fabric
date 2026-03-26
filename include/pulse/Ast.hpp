#pragma once

#include "pulse/Types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pulse {

struct Property {
    std::string key;
    std::vector<std::string> values;
    SourceLocation location {};
};

struct Module {
    ModuleFamily family = ModuleFamily::unknown;
    std::string kind;
    std::string name;
    std::vector<Property> properties;
    SourceLocation location {};
};

struct Endpoint {
    std::string module;
    std::optional<std::string> port;
    SourceLocation location {};
};

struct Connection {
    Endpoint from;
    Endpoint to;
    SourceLocation location {};
};

struct GlobalSetting {
    std::string key;
    std::vector<std::string> values;
    SourceLocation location {};
};

struct Patch {
    std::string name;
    std::vector<GlobalSetting> globals;
    std::vector<Module> modules;
    std::vector<Connection> connections;
    SourceLocation location {};
};

} // namespace pulse
