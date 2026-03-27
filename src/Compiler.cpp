#include "pulse/Compiler.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace pulse {

namespace {

SignalType signalTypeProperty(const Module& module)
{
    for (const auto& property : module.properties) {
        if (property.key != "signal" || property.values.empty()) {
            continue;
        }
        if (property.values[0] == "midi") return SignalType::midi;
        if (property.values[0] == "trigger") return SignalType::trigger;
        if (property.values[0] == "gate") return SignalType::gate;
        if (property.values[0] == "value") return SignalType::value;
        if (property.values[0] == "pitch") return SignalType::pitch;
    }
    return SignalType::unknown;
}

PortInfo makePort(std::string name, SignalType type)
{
    return PortInfo { std::move(name), type };
}

std::vector<PortInfo> defaultInputs(const Module& module)
{
    if (module.family == ModuleFamily::input) return {};
    if (module.family == ModuleFamily::output) return { makePort("in", SignalType::midi) };

    if (module.family == ModuleFamily::analyze && module.kind == "motion") {
        return { makePort("in", SignalType::midi), makePort("clock", SignalType::trigger) };
    }

    if (module.family == ModuleFamily::generate && module.kind == "clock") return {};
    if (module.family == ModuleFamily::generate && (module.kind == "pattern" || module.kind == "fibonacci" || module.kind == "phrase" || module.kind == "progression" || module.kind == "growth" || module.kind == "swarm" || module.kind == "collapse" || module.kind == "field" || module.kind == "formula" || module.kind == "moment" || module.kind == "table" || module.kind == "markov" || module.kind == "tree")) {
        if (module.kind == "growth") {
            return { makePort("trigger", SignalType::trigger), makePort("phrase", SignalType::value) };
        }
        if (module.kind == "collapse") {
            return { makePort("trigger", SignalType::trigger), makePort("phrase", SignalType::value), makePort("section", SignalType::value) };
        }
        return { makePort("trigger", SignalType::trigger) };
    }

    if (module.family == ModuleFamily::generate && module.kind == "chance") {
        return { makePort("trigger", SignalType::trigger) };
    }

    if (module.family == ModuleFamily::generate && module.kind == "section") {
        return {
            makePort("trigger", SignalType::trigger),
            makePort("select", SignalType::value),
            makePort("advance", SignalType::trigger),
            makePort("in", SignalType::midi)
        };
    }

    if (module.family == ModuleFamily::generate && module.kind == "random") {
        return { makePort("trigger", SignalType::trigger), makePort("in", SignalType::midi) };
    }
    if (module.family == ModuleFamily::shape && module.kind == "stages") {
        return { makePort("trigger", SignalType::trigger), makePort("rate", SignalType::value) };
    }

    if (module.family == ModuleFamily::shape && module.kind == "modulator") {
        std::vector<PortInfo> ports { makePort("trigger", SignalType::trigger), makePort("rate", SignalType::value) };
        for (int channel = 1; channel <= 4; ++channel) {
            ports.push_back(makePort("ch" + std::to_string(channel) + "_trigger", SignalType::trigger));
            ports.push_back(makePort("ch" + std::to_string(channel) + "_reset", SignalType::trigger));
            ports.push_back(makePort("ch" + std::to_string(channel) + "_select", SignalType::value));
            for (int stage = 1; stage <= 13; ++stage) {
                const auto prefix = "ch" + std::to_string(channel) + "_s" + std::to_string(stage) + "_";
                ports.push_back(makePort(prefix + "trigger", SignalType::trigger));
                ports.push_back(makePort(prefix + "reset", SignalType::trigger));
                ports.push_back(makePort(prefix + "level", SignalType::value));
                ports.push_back(makePort(prefix + "time", SignalType::value));
                ports.push_back(makePort(prefix + "curve", SignalType::value));
            }
        }
        return ports;
    }

    if (module.family == ModuleFamily::shape && module.kind == "lists") {
        return {
            makePort("note", SignalType::midi),
            makePort("threshold", SignalType::value),
            makePort("random", SignalType::trigger)
        };
    }

    if (module.family == ModuleFamily::project && module.kind == "to_notes") {
        return {
            makePort("in", SignalType::pitch),
            makePort("values", SignalType::value),
            makePort("velocity", SignalType::value),
            makePort("gate", SignalType::gate),
            makePort("time", SignalType::value),
            makePort("phrase", SignalType::value)
        };
    }

    if (module.family == ModuleFamily::transform && module.kind == "quantize") {
        return { makePort("in", SignalType::midi), makePort("pitch", SignalType::pitch), makePort("values", SignalType::value) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "passthrough") {
        return { makePort("in", signalTypeProperty(module)) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "sieve") {
        return { makePort("pitch", SignalType::pitch), makePort("trigger", SignalType::trigger) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "warp") {
        return { makePort("pitch", SignalType::pitch), makePort("values", SignalType::value) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "equation") {
        return {
            makePort("in", SignalType::midi),
            makePort("trigger", SignalType::trigger),
            makePort("value", SignalType::value),
            makePort("pitch", SignalType::pitch)
        };
    }

    if (module.family == ModuleFamily::transform && module.kind == "split") {
        return { makePort("in", SignalType::midi) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "arp") {
        return { makePort("in", SignalType::midi), makePort("trigger", SignalType::trigger) };
    }

    if (module.family == ModuleFamily::transform && (module.kind == "groove" || module.kind == "retrig" || module.kind == "length")) {
        return { makePort("in", SignalType::midi) };
    }

    if (module.family == ModuleFamily::memory && module.kind == "smear") {
        return { makePort("in", SignalType::midi), makePort("drift", SignalType::value) };
    }

    if (module.family == ModuleFamily::memory && module.kind == "cutup") {
        return { makePort("in", SignalType::midi), makePort("trigger", SignalType::trigger) };
    }

    if (module.family == ModuleFamily::memory && (module.kind == "slicer" || module.kind == "pool")) {
        return { makePort("in", SignalType::midi), makePort("trigger", SignalType::trigger) };
    }

    return { makePort("in", SignalType::midi) };
}

std::vector<PortInfo> defaultOutputs(const Module& module)
{
    if (module.family == ModuleFamily::input && module.kind == "midi") {
        return { makePort("out", SignalType::midi) };
    }

    if (module.family == ModuleFamily::analyze && module.kind == "motion") {
        return {
            makePort("pulse1", SignalType::trigger),
            makePort("pulse2", SignalType::trigger),
            makePort("pulse3", SignalType::trigger),
            makePort("pulse4", SignalType::trigger),
            makePort("pulse5", SignalType::trigger),
            makePort("pulse6", SignalType::trigger),
            makePort("pulse7", SignalType::trigger),
            makePort("pulse8", SignalType::trigger),
            makePort("speed", SignalType::value),
            makePort("even", SignalType::gate),
            makePort("odd", SignalType::gate)
        };
    }

    if (module.family == ModuleFamily::generate && module.kind == "clock") {
        return {
            makePort("out", SignalType::trigger),
            makePort("beat", SignalType::trigger),
            makePort("bar", SignalType::trigger),
            makePort("accent", SignalType::value)
        };
    }

    if (module.family == ModuleFamily::generate && (module.kind == "pattern" || module.kind == "fibonacci" || module.kind == "random")) {
        return { makePort("out", SignalType::pitch) };
    }

    if (module.family == ModuleFamily::generate && module.kind == "chance") {
        return {
            makePort("out", SignalType::value),
            makePort("pitch", SignalType::pitch),
            makePort("trigger", SignalType::trigger),
            makePort("index", SignalType::value)
        };
    }

    if (module.family == ModuleFamily::generate && module.kind == "field") {
        return {
            makePort("out", SignalType::pitch),
            makePort("density", SignalType::value)
        };
    }

    if (module.family == ModuleFamily::generate && module.kind == "formula") {
        return {
            makePort("out", SignalType::pitch),
            makePort("gate", SignalType::gate),
            makePort("time", SignalType::value),
            makePort("step", SignalType::value)
        };
    }

    if (module.family == ModuleFamily::generate && module.kind == "moment") {
        return {
            makePort("out", SignalType::pitch),
            makePort("state", SignalType::value)
        };
    }

    if (module.family == ModuleFamily::generate && module.kind == "table") {
        return {
            makePort("out", SignalType::value),
            makePort("pitch", SignalType::pitch),
            makePort("time", SignalType::value),
            makePort("gate", SignalType::gate),
            makePort("index", SignalType::value),
            makePort("trigger", SignalType::trigger)
        };
    }

    if (module.family == ModuleFamily::generate && module.kind == "markov") {
        return {
            makePort("value", SignalType::value),
            makePort("out", SignalType::pitch),
            makePort("time", SignalType::value),
            makePort("gate", SignalType::gate),
            makePort("state", SignalType::value)
        };
    }

    if (module.family == ModuleFamily::generate && module.kind == "tree") {
        return {
            makePort("out", SignalType::value),
            makePort("pitch", SignalType::pitch),
            makePort("time", SignalType::value),
            makePort("gate", SignalType::gate),
            makePort("index", SignalType::value),
            makePort("trigger", SignalType::trigger)
        };
    }

    if (module.family == ModuleFamily::generate && module.kind == "growth") {
        return {
            makePort("out", SignalType::pitch),
            makePort("density", SignalType::value),
            makePort("gate", SignalType::gate)
        };
    }

    if (module.family == ModuleFamily::generate && module.kind == "swarm") {
        return { makePort("out", SignalType::pitch), makePort("density", SignalType::value) };
    }

    if (module.family == ModuleFamily::generate && module.kind == "collapse") {
        return { makePort("out", SignalType::pitch), makePort("state", SignalType::value) };
    }

    if (module.family == ModuleFamily::generate && module.kind == "phrase") {
        return { makePort("out", SignalType::value) };
    }

    if (module.family == ModuleFamily::generate && module.kind == "progression") {
        return { makePort("out", SignalType::value) };
    }

    if (module.family == ModuleFamily::generate && module.kind == "section") {
        return { makePort("out", SignalType::value), makePort("section", SignalType::value) };
    }

    if (module.family == ModuleFamily::shape && module.kind == "stages") {
        return { makePort("out", SignalType::value) };
    }

    if (module.family == ModuleFamily::shape && module.kind == "modulator") {
        std::vector<PortInfo> ports {
            makePort("out", SignalType::value),
            makePort("ch1", SignalType::value),
            makePort("ch2", SignalType::value),
            makePort("ch3", SignalType::value),
            makePort("ch4", SignalType::value)
        };
        for (int channel = 1; channel <= 4; ++channel) {
            for (int stage = 1; stage <= 13; ++stage) {
                const auto prefix = "ch" + std::to_string(channel) + "_s" + std::to_string(stage) + "_";
                ports.push_back(makePort(prefix + "gate", SignalType::gate));
                ports.push_back(makePort(prefix + "end", SignalType::trigger));
            }
        }
        return ports;
    }

    if (module.family == ModuleFamily::shape && module.kind == "lists") {
        return {
            makePort("pitch", SignalType::pitch),
            makePort("time", SignalType::value),
            makePort("gate", SignalType::gate)
        };
    }

    if (module.family == ModuleFamily::project && module.kind == "to_notes") {
        return { makePort("out", SignalType::midi) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "split") {
        return {
            makePort("low", SignalType::midi),
            makePort("mid", SignalType::midi),
            makePort("high", SignalType::midi)
        };
    }

    if (module.family == ModuleFamily::transform && (module.kind == "groove" || module.kind == "retrig" || module.kind == "length")) {
        return { makePort("out", SignalType::midi) };
    }

    if (module.family == ModuleFamily::output) return {};

    if (module.family == ModuleFamily::transform && module.kind == "quantize") {
        return { makePort("out", SignalType::midi), makePort("pitch", SignalType::pitch) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "passthrough") {
        return { makePort("out", signalTypeProperty(module)) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "sieve") {
        return { makePort("out", SignalType::pitch), makePort("trigger", SignalType::trigger) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "warp") {
        return { makePort("out", SignalType::pitch) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "equation") {
        return {
            makePort("out", SignalType::midi),
            makePort("value", SignalType::value),
            makePort("pitch", SignalType::pitch)
        };
    }

    if (module.family == ModuleFamily::transform && module.kind == "filter") {
        return { makePort("out", SignalType::midi) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "bits") {
        return { makePort("out", SignalType::midi) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "delay") {
        return { makePort("out", SignalType::midi) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "bounce") {
        return { makePort("out", SignalType::midi) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "loop") {
        return { makePort("out", SignalType::midi) };
    }

    if (module.family == ModuleFamily::transform && module.kind == "arp") {
        return { makePort("out", SignalType::midi) };
    }

    if (module.family == ModuleFamily::memory && module.kind == "smear") {
        return { makePort("out", SignalType::midi), makePort("pitch", SignalType::pitch) };
    }

    if (module.family == ModuleFamily::memory && module.kind == "cutup") {
        return { makePort("out", SignalType::midi) };
    }

    if (module.family == ModuleFamily::memory && (module.kind == "slicer" || module.kind == "pool")) {
        return { makePort("out", SignalType::midi), makePort("index", SignalType::value) };
    }

    return { makePort("out", SignalType::midi) };
}

std::string endpointPortOrDefault(const Endpoint& endpoint, const NodeInfo& node, bool output)
{
    if (endpoint.port.has_value()) return *endpoint.port;
    const auto& ports = output ? node.outputs : node.inputs;
    return ports.empty() ? std::string {} : ports.front().name;
}

} // namespace

std::optional<CompiledPatch> Compiler::compile(const Patch& patch, std::vector<Diagnostic>& diagnostics) const
{
    CompiledPatch compiled;
    compiled.source = patch;

    std::unordered_map<std::string, std::size_t> nodeIndexByName;
    for (const auto& module : patch.modules) {
        if (nodeIndexByName.contains(module.name)) {
            diagnostics.push_back({
                Diagnostic::Severity::error,
                "Duplicate module name `" + module.name + "`.",
                module.location
            });
            return std::nullopt;
        }

        nodeIndexByName.emplace(module.name, compiled.nodes.size());
        compiled.nodes.push_back(buildNodeInfo(module));
    }

    std::vector<std::vector<std::size_t>> edges(compiled.nodes.size());
    std::vector<std::size_t> indegree(compiled.nodes.size(), 0);

    for (const auto& connection : patch.connections) {
        const auto fromIt = nodeIndexByName.find(connection.from.module);
        const auto toIt = nodeIndexByName.find(connection.to.module);

        if (fromIt == nodeIndexByName.end()) {
            diagnostics.push_back({
                Diagnostic::Severity::error,
                "Unknown module `" + connection.from.module + "`.",
                connection.from.location
            });
            return std::nullopt;
        }

        if (toIt == nodeIndexByName.end()) {
            diagnostics.push_back({
                Diagnostic::Severity::error,
                "Unknown module `" + connection.to.module + "`.",
                connection.to.location
            });
            return std::nullopt;
        }

        const auto fromIndex = fromIt->second;
        const auto toIndex = toIt->second;
        const auto& fromNode = compiled.nodes[fromIndex];
        const auto& toNode = compiled.nodes[toIndex];

        const auto fromPortName = endpointPortOrDefault(connection.from, fromNode, true);
        const auto toPortName = endpointPortOrDefault(connection.to, toNode, false);

        const auto fromPort = findOutputPort(fromNode, fromPortName);
        if (!fromPort.has_value()) {
            diagnostics.push_back({
                Diagnostic::Severity::error,
                "Module `" + fromNode.name + "` has no output port `" + fromPortName + "`.",
                connection.location
            });
            return std::nullopt;
        }

        const auto toPort = findInputPort(toNode, toPortName);
        if (!toPort.has_value()) {
            diagnostics.push_back({
                Diagnostic::Severity::error,
                "Module `" + toNode.name + "` has no input port `" + toPortName + "`.",
                connection.location
            });
            return std::nullopt;
        }

        if (fromPort->type != toPort->type) {
            diagnostics.push_back({
                Diagnostic::Severity::error,
                "Cannot connect `" + fromNode.name + "." + fromPortName + "` to `" + toNode.name + "." + toPortName
                    + "`. `" + fromNode.name + "` outputs " + toString(fromPort->type) + ", but `"
                    + toNode.name + "` expects " + toString(toPort->type) + ".",
                connection.location
            });
            return std::nullopt;
        }

        compiled.connections.push_back({
            fromIndex,
            fromPortName,
            toIndex,
            toPortName,
            fromPort->type
        });

        edges[fromIndex].push_back(toIndex);
        ++indegree[toIndex];
    }

    std::queue<std::size_t> ready;
    for (std::size_t i = 0; i < indegree.size(); ++i) {
        if (indegree[i] == 0) {
            ready.push(i);
        }
    }

    while (!ready.empty()) {
        const auto node = ready.front();
        ready.pop();
        compiled.executionOrder.push_back(node);
        for (const auto neighbor : edges[node]) {
            if (--indegree[neighbor] == 0) {
                ready.push(neighbor);
            }
        }
    }

    if (compiled.executionOrder.size() != compiled.nodes.size()) {
        diagnostics.push_back({
            Diagnostic::Severity::error,
            "Connection graph contains a cycle. Version 1 requires an acyclic patch graph.",
            patch.location
        });
        return std::nullopt;
    }

    return compiled;
}

NodeInfo Compiler::buildNodeInfo(const Module& module)
{
    NodeInfo info;
    info.name = module.name;
    info.family = module.family;
    info.kind = module.kind;
    info.inputs = defaultInputs(module);
    info.outputs = defaultOutputs(module);
    return info;
}

std::optional<PortInfo> Compiler::findInputPort(const NodeInfo& node, const std::string& name)
{
    const auto it = std::find_if(node.inputs.begin(), node.inputs.end(), [&](const PortInfo& port) {
        return port.name == name;
    });

    if (it == node.inputs.end()) return std::nullopt;
    return *it;
}

std::optional<PortInfo> Compiler::findOutputPort(const NodeInfo& node, const std::string& name)
{
    const auto it = std::find_if(node.outputs.begin(), node.outputs.end(), [&](const PortInfo& port) {
        return port.name == name;
    });

    if (it == node.outputs.end()) return std::nullopt;
    return *it;
}

} // namespace pulse
