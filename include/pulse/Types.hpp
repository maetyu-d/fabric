#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pulse {

enum class SignalType {
    midi,
    trigger,
    gate,
    value,
    pitch,
    unknown
};

enum class ModuleFamily {
    input,
    analyze,
    generate,
    shape,
    transform,
    memory,
    project,
    output,
    unknown
};

struct SourceLocation {
    std::size_t line = 0;
    std::size_t column = 0;
};

struct Diagnostic {
    enum class Severity {
        error,
        warning
    };

    Severity severity = Severity::error;
    std::string message;
    SourceLocation location {};
};

inline std::string toString(SignalType type)
{
    switch (type) {
    case SignalType::midi: return "midi";
    case SignalType::trigger: return "trigger";
    case SignalType::gate: return "gate";
    case SignalType::value: return "value";
    case SignalType::pitch: return "pitch";
    case SignalType::unknown: return "unknown";
    }

    return "unknown";
}

inline std::string toString(ModuleFamily family)
{
    switch (family) {
    case ModuleFamily::input: return "input";
    case ModuleFamily::analyze: return "analyze";
    case ModuleFamily::generate: return "generate";
    case ModuleFamily::shape: return "shape";
    case ModuleFamily::transform: return "transform";
    case ModuleFamily::memory: return "memory";
    case ModuleFamily::project: return "project";
    case ModuleFamily::output: return "output";
    case ModuleFamily::unknown: return "unknown";
    }

    return "unknown";
}

inline ModuleFamily parseModuleFamily(std::string_view text)
{
    if (text == "input") return ModuleFamily::input;
    if (text == "analyze") return ModuleFamily::analyze;
    if (text == "generate") return ModuleFamily::generate;
    if (text == "shape") return ModuleFamily::shape;
    if (text == "transform") return ModuleFamily::transform;
    if (text == "memory") return ModuleFamily::memory;
    if (text == "project") return ModuleFamily::project;
    if (text == "output") return ModuleFamily::output;
    return ModuleFamily::unknown;
}

} // namespace pulse
