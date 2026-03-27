#pragma once

#include "pulse/Compiler.hpp"
#include "pulse/Parser.hpp"
#include "pulse/Runtime.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pulse {

class Engine {
public:
    bool loadPatchText(const std::string& source);
    void reset(double sampleRate, std::uint32_t blockSize);
    void process(const ProcessContext& context);
    void setInputEvents(const std::string& moduleName, const std::vector<Event>& events);
    void setNodeMode(const std::string& moduleName, NodeProcessingMode mode);
    [[nodiscard]] NodeProcessingMode nodeMode(const std::string& moduleName) const;

    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const;
    [[nodiscard]] const RuntimeGraph* graph() const;
    [[nodiscard]] std::vector<Event> outputEvents(const std::string& moduleName) const;
    [[nodiscard]] std::optional<int> currentSectionIndex(const std::string& moduleName) const;
    [[nodiscard]] std::optional<double> currentSectionPhase(const std::string& moduleName) const;
    [[nodiscard]] std::optional<std::uint64_t> sectionAdvanceCount(const std::string& moduleName) const;
    [[nodiscard]] std::optional<std::string> activeStateLabel(const std::string& moduleName) const;
    [[nodiscard]] std::optional<ModulatorStateSnapshot> modulatorState(const std::string& moduleName) const;

private:
    Parser parser_;
    Compiler compiler_;
    std::vector<Diagnostic> diagnostics_;
    std::optional<Patch> patch_;
    std::optional<CompiledPatch> compiled_;
    std::unique_ptr<RuntimeGraph> runtime_;
};

/*
    JUCE integration sketch:

    - Hold one `pulse::Engine` inside your `juce::AudioProcessor`.
    - On script edit:
        1. call `loadPatchText`
        2. if successful, rebuild the runtime graph off the audio thread
        3. swap the active graph atomically
    - In `prepareToPlay`, call `engine.reset(sampleRate, samplesPerBlock)`.
    - In `processBlock`, translate incoming `juce::MidiBuffer` into input events,
      call `engine.process`, then translate output events back into `juce::MidiBuffer`.

    This header intentionally avoids a hard JUCE dependency so the core language
    can be compiled and tested without the plugin project around it.
*/

} // namespace pulse
