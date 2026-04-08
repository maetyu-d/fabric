#pragma once

#include "pulse/Compiler.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pulse {

enum class NodeProcessingMode {
    normal,
    bypass,
    mute
};

struct Event {
    SignalType type = SignalType::unknown;
    double time = 0.0;
    std::vector<int> ints;
    std::vector<double> floats;

    [[nodiscard]] static Event makeTrigger(double time = 0.0);
    [[nodiscard]] static Event makeValue(double value, double time = 0.0);
    [[nodiscard]] static Event makePitch(double value, double time = 0.0);
    [[nodiscard]] static Event makeNoteOn(int note, int velocity, int channel = 1, double time = 0.0);
    [[nodiscard]] static Event makeNoteOff(int note, int velocity = 0, int channel = 1, double time = 0.0);

    [[nodiscard]] bool isNoteOn() const;
    [[nodiscard]] bool isNoteOff() const;
    [[nodiscard]] int noteNumber() const;
    [[nodiscard]] int velocityValue() const;
    [[nodiscard]] int channel() const;
    [[nodiscard]] double valueOr(double fallback = 0.0) const;
};

class EventBuffer {
public:
    void clear();
    void push(Event event);
    void append(const EventBuffer& other);
    void append(const std::vector<Event>& events);
    [[nodiscard]] const std::vector<Event>& events() const;

private:
    std::vector<Event> events_;
};

struct ProcessContext {
    double sampleRate = 44100.0;
    std::uint32_t blockSize = 512;
    double bpm = 120.0;
};

struct ModulatorChannelStateSnapshot {
    double level = 0.0;
    std::vector<int> activeStages;
};

struct ModulatorStateSnapshot {
    int channelCount = 0;
    std::string mode;
    bool overlapEnabled = false;
    std::vector<ModulatorChannelStateSnapshot> channels;
};

class RuntimeNode {
public:
    virtual ~RuntimeNode() = default;

    virtual void reset(double sampleRate) = 0;
    virtual void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) = 0;

    virtual void setExternalEvents(const std::vector<Event>& events);
    [[nodiscard]] virtual std::optional<int> currentSectionIndex() const;
    [[nodiscard]] virtual std::optional<double> currentSectionPhase() const;
    [[nodiscard]] virtual std::optional<std::uint64_t> sectionAdvanceCount() const;
    [[nodiscard]] virtual std::optional<std::string> activeStateLabel() const;
    [[nodiscard]] virtual std::optional<ModulatorStateSnapshot> modulatorState() const;
};

class StubRuntimeNode final : public RuntimeNode {
public:
    StubRuntimeNode(NodeInfo info, Module module);

    void reset(double sampleRate) override;
    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override;

    [[nodiscard]] const NodeInfo& info() const;

private:
    NodeInfo info_;
    Module module_;
    double sampleRate_ = 44100.0;
};

class RuntimeGraph {
public:
    explicit RuntimeGraph(CompiledPatch patch);

    void reset(double sampleRate);
    void process(const ProcessContext& context);
    void setInputEvents(const std::string& moduleName, const std::vector<Event>& events);
    void setNodeMode(const std::string& moduleName, NodeProcessingMode mode);
    [[nodiscard]] NodeProcessingMode nodeMode(const std::string& moduleName) const;

    [[nodiscard]] const CompiledPatch& patch() const;
    [[nodiscard]] const EventBuffer* outputBuffer(std::size_t nodeIndex, const std::string& port) const;
    [[nodiscard]] const EventBuffer* inputBuffer(std::size_t nodeIndex, const std::string& port) const;
    [[nodiscard]] std::optional<std::size_t> findNodeIndex(const std::string& moduleName) const;
    [[nodiscard]] std::vector<Event> outputEvents(const std::string& moduleName) const;
    [[nodiscard]] std::optional<int> currentSectionIndex(const std::string& moduleName) const;
    [[nodiscard]] std::optional<double> currentSectionPhase(const std::string& moduleName) const;
    [[nodiscard]] std::optional<std::uint64_t> sectionAdvanceCount(const std::string& moduleName) const;
    [[nodiscard]] std::optional<std::string> activeStateLabel(const std::string& moduleName) const;
    [[nodiscard]] std::optional<ModulatorStateSnapshot> modulatorState(const std::string& moduleName) const;

private:
    struct PortBuffers {
        std::unordered_map<std::string, EventBuffer> inputs;
        std::unordered_map<std::string, EventBuffer> outputs;
    };

    CompiledPatch patch_;
    std::vector<std::unique_ptr<RuntimeNode>> nodes_;
    std::vector<PortBuffers> buffers_;
    std::unordered_map<std::string, std::vector<Event>> renderedOutputs_;
    std::vector<NodeProcessingMode> nodeModes_;
};

} // namespace pulse
