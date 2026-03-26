#pragma once

#include <JuceHeader.h>
#include "pulse/JuceIntegration.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class PulsePluginAudioProcessor final : public juce::AudioProcessor
{
public:
    struct UiDiagnostic {
        juce::String message;
        int line = 0;
        int column = 0;
        bool isError = true;
    };

    struct GraphNode {
        juce::String name;
        juce::String family;
        juce::String kind;
    };

    struct GraphConnection {
        juce::String fromName;
        juce::String fromPort;
        juce::String toName;
        juce::String toPort;
    };

    struct GraphSnapshot {
        std::vector<GraphNode> nodes;
        std::vector<GraphConnection> connections;
    };

    struct SectionControlSnapshot {
        juce::String moduleName;
        juce::StringArray sectionNames;
        int activeSectionIndex = -1;
        double phase = 0.0;
        std::uint64_t advanceCount = 0;
    };

    PulsePluginAudioProcessor();
    ~PulsePluginAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool compileScript(const juce::String& scriptText);
    void requestCompile(const juce::String& scriptText);

    juce::String getScriptText() const;
    juce::String getDiagnosticsText() const;
    std::vector<UiDiagnostic> getDiagnostics() const;
    GraphSnapshot getGraphSnapshot() const;
    std::vector<SectionControlSnapshot> getSectionControls() const;
    std::uint64_t getUiRevision() const;
    bool isCompileInProgress() const;
    void requestSectionRecall(const juce::String& moduleName, int sectionIndex);
    void requestSectionAdvance(const juce::String& moduleName);

private:
    struct EngineState {
        std::shared_ptr<pulse::Engine> engine;
        juce::StringArray inputModuleNames;
        juce::StringArray outputModuleNames;
        std::vector<SectionControlSnapshot> sectionControls;
        GraphSnapshot graph;
    };

    struct CompileResult {
        std::shared_ptr<EngineState> engineState;
        juce::String scriptText;
        juce::String diagnosticsText;
        std::vector<UiDiagnostic> diagnostics;
        bool success = false;
    };

    class CompileJob;
    struct FactoryPreset;

    static juce::String defaultScript();
    static const std::vector<FactoryPreset>& factoryPresets();
    CompileResult buildCompileResult(const juce::String& scriptText) const;
    void applyCompileResult(CompileResult result, std::uint64_t requestId);
    static GraphSnapshot graphFromEngine(const pulse::Engine& engine);
    static std::vector<UiDiagnostic> diagnosticsFromPulse(const std::vector<pulse::Diagnostic>& diagnostics);
    static juce::String diagnosticsToText(const std::vector<UiDiagnostic>& diagnostics);
    static std::vector<pulse::Event> midiBufferToEvents(const juce::MidiBuffer& midi);
    static void eventsToMidiBuffer(const std::vector<pulse::Event>& events, double sampleRate, int blockSize, juce::MidiBuffer& midi);

    juce::CriticalSection uiStateLock_;
    juce::ThreadPool compilePool_ { 1 };
    std::shared_ptr<EngineState> activeState_;
    juce::String scriptText_;
    juce::String diagnosticsText_;
    std::vector<UiDiagnostic> diagnostics_;
    GraphSnapshot graphSnapshot_;
    std::vector<SectionControlSnapshot> sectionControls_;
    std::atomic<double> sampleRate_ { 44100.0 };
    std::atomic<int> blockSize_ { 512 };
    std::atomic<std::uint64_t> requestedCompileId_ { 0 };
    std::atomic<std::uint64_t> uiRevision_ { 0 };
    std::atomic<bool> compileInProgress_ { false };
    juce::CriticalSection sectionRequestLock_;
    std::unordered_map<juce::String, int> pendingSectionRecalls_;
    std::unordered_map<juce::String, int> pendingSectionAdvances_;
    std::unordered_map<juce::String, int> activeSectionIndices_;
    std::unordered_map<juce::String, double> activeSectionPhases_;
    std::unordered_map<juce::String, std::uint64_t> sectionAdvanceCounts_;
    std::optional<int> currentProgramIndex_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsePluginAudioProcessor)
};
