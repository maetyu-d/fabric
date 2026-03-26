#pragma once

#include "pulse/Ast.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pulse {

struct PortInfo {
    std::string name;
    SignalType type = SignalType::unknown;
};

struct NodeInfo {
    std::string name;
    ModuleFamily family = ModuleFamily::unknown;
    std::string kind;
    std::vector<PortInfo> inputs;
    std::vector<PortInfo> outputs;
};

struct CompiledConnection {
    std::size_t fromNode = 0;
    std::string fromPort;
    std::size_t toNode = 0;
    std::string toPort;
    SignalType type = SignalType::unknown;
};

struct CompiledPatch {
    Patch source;
    std::vector<NodeInfo> nodes;
    std::vector<std::size_t> executionOrder;
    std::vector<CompiledConnection> connections;
};

class Compiler {
public:
    [[nodiscard]] std::optional<CompiledPatch> compile(const Patch& patch, std::vector<Diagnostic>& diagnostics) const;

private:
    [[nodiscard]] static NodeInfo buildNodeInfo(const Module& module);
    [[nodiscard]] static std::optional<PortInfo> findInputPort(const NodeInfo& node, const std::string& name);
    [[nodiscard]] static std::optional<PortInfo> findOutputPort(const NodeInfo& node, const std::string& name);
};

} // namespace pulse
