#pragma once

#include <JuceHeader.h>
#include <juce_osc/juce_osc.h>
#include "pulse/JuceIntegration.hpp"

#include <atomic>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class FabricAudioProcessor final
    : public juce::AudioProcessor
    , private juce::OSCReceiver
    , private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    enum class CaptureRecordStyle {
        manual,
        automatic
    };

    enum class CaptureMode {
        passThroughRecord,
        playbackCaptured
    };

    enum class HubMode {
        bridge,
        receiveFromOsc,
        sendIncomingToOsc
    };

    enum class NodeProcessingMode {
        normal,
        bypass,
        mute
    };

    struct IoEventSummary {
        juce::String text;
        bool isNoteOn = false;
        bool isNoteOff = false;
    };

    struct NodeIoSnapshot {
        juce::String moduleName;
        juce::String displayName;
        juce::String family;
        juce::String kind;
        std::vector<IoEventSummary> incoming;
        std::vector<IoEventSummary> outgoing;
        int incomingCount = 0;
        int outgoingCount = 0;
        int incomingActiveCount = 0;
        int outgoingActiveCount = 0;
        std::array<std::uint8_t, 128> incomingActive {};
        std::array<std::uint8_t, 128> outgoingActive {};
        std::array<std::uint8_t, 32> incomingHistory {};
        std::array<std::uint8_t, 32> outgoingHistory {};
    };

    struct IoSnapshot {
        std::vector<IoEventSummary> incoming;
        std::vector<IoEventSummary> outgoing;
        int incomingCount = 0;
        int outgoingCount = 0;
        int incomingActiveCount = 0;
        int outgoingActiveCount = 0;
        std::array<std::uint8_t, 128> incomingActive {};
        std::array<std::uint8_t, 128> outgoingActive {};
        std::array<std::uint8_t, 32> incomingHistory {};
        std::array<std::uint8_t, 32> outgoingHistory {};
        std::vector<NodeIoSnapshot> nodes;
    };

    struct UiDiagnostic {
        juce::String message;
        int line = 0;
        int column = 0;
        bool isError = true;
    };

    struct GraphNode {
        juce::String name;
        juce::String displayName;
        juce::String family;
        juce::String kind;
        juce::String detail;
        int sourceLine = 0;
        NodeProcessingMode mode = NodeProcessingMode::normal;
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

    struct ModulatorChannelSnapshot {
        float level = 0.0f;
        std::vector<int> activeStages;
    };

    struct ModulatorSnapshot {
        juce::String moduleName;
        juce::String mode;
        bool overlapEnabled = false;
        std::vector<ModulatorChannelSnapshot> channels;
    };

    struct SectionControlSnapshot {
        juce::String moduleName;
        juce::StringArray sectionNames;
        int activeSectionIndex = -1;
        double phase = 0.0;
        std::uint64_t advanceCount = 0;
    };

    struct TutorialInfo {
        juce::String name;
        juce::String summary;
        juce::String script;
    };

    struct CaptureStatus {
        CaptureMode mode = CaptureMode::passThroughRecord;
        CaptureRecordStyle recordStyle = CaptureRecordStyle::manual;
        bool isRecording = false;
        bool hasCapture = false;
        int eventCount = 0;
        int noteEventCount = 0;
        double lengthSeconds = 0.0;
        double recordingSeconds = 0.0;
        double exportTempoBpm = 120.0;
    };

    struct HubStatus {
        HubMode mode = HubMode::bridge;
        int receivePort = 9000;
        int sendPort = 9001;
        bool receiveConnected = false;
        bool sendConnected = false;
        std::uint64_t receivedMessageCount = 0;
        std::uint64_t sentMessageCount = 0;
    };

    FabricAudioProcessor();
    ~FabricAudioProcessor() override;

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
    void setPendingScriptText(const juce::String& scriptText, bool notifyUi = false);

    juce::String getScriptText() const;
    juce::String getDiagnosticsText() const;
    std::vector<UiDiagnostic> getDiagnostics() const;
    GraphSnapshot getGraphSnapshot() const;
    std::vector<ModulatorSnapshot> getModulatorSnapshots() const;
    std::vector<SectionControlSnapshot> getSectionControls() const;
    juce::StringArray getTutorialNames() const;
    std::optional<TutorialInfo> getTutorial(int index) const;
    IoSnapshot getIoSnapshot() const;
    bool isHostTempoFollowEnabled() const;
    void setHostTempoFollowEnabled(bool enabled);
    CaptureStatus getCaptureStatus() const;
    CaptureMode getCaptureMode() const;
    void setCaptureMode(CaptureMode mode);
    CaptureRecordStyle getCaptureRecordStyle() const;
    void setCaptureRecordStyle(CaptureRecordStyle style);
    void startCaptureRecording();
    void stopCaptureRecording();
    void clearCapturedMidi();
    bool exportCapturedMidiFile(const juce::File& targetFile) const;
    HubStatus getHubStatus() const;
    void setHubPorts(int receivePort, int sendPort);
    void setHubMode(HubMode mode);
    std::uint64_t getUiRevision() const;
    bool isCompileInProgress() const;
    void requestSectionRecall(const juce::String& moduleName, int sectionIndex);
    void requestSectionAdvance(const juce::String& moduleName);
    void setNodeProcessingMode(const juce::String& moduleName, NodeProcessingMode mode);
    NodeProcessingMode getNodeProcessingMode(const juce::String& moduleName) const;

private:
    struct EngineState {
        std::shared_ptr<pulse::Engine> engine;
        juce::StringArray inputModuleNames;
        juce::StringArray outputModuleNames;
        std::vector<SectionControlSnapshot> sectionControls;
        GraphSnapshot graph;
        double tempoBpm = 120.0;
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
    struct TutorialEntry;

    static juce::String defaultScript();
    static const std::vector<FactoryPreset>& factoryPresets();
    static const std::vector<TutorialEntry>& tutorials();
    CompileResult buildCompileResult(const juce::String& scriptText) const;
    void applyCompileResult(CompileResult result, std::uint64_t requestId);
    static GraphSnapshot graphFromEngine(const pulse::Engine& engine);
    static std::vector<UiDiagnostic> diagnosticsFromPulse(const std::vector<pulse::Diagnostic>& diagnostics);
    static juce::String diagnosticsToText(const std::vector<UiDiagnostic>& diagnostics);
    static std::vector<pulse::Event> midiBufferToEvents(const juce::MidiBuffer& midi);
    static void eventsToMidiBuffer(const std::vector<pulse::Event>& events, double sampleRate, int blockSize, juce::MidiBuffer& midi);
    static IoEventSummary summariseEvent(const pulse::Event& event);
    static IoSnapshot buildIoSnapshot(const std::vector<pulse::Event>& incoming, const std::vector<pulse::Event>& outgoing);
    static std::vector<NodeIoSnapshot> buildNodeIoSnapshots(const pulse::Engine& engine);
    static juce::MidiMessage midiMessageFromEvent(const pulse::Event& event);
    static std::vector<pulse::Event> activeNoteOffEvents(const std::array<std::uint8_t, 128>& activeNotes);
    static void updateActiveNotes(std::array<std::uint8_t, 128>& activeNotes, const std::vector<pulse::Event>& events);
    static void pushHistory(std::array<std::uint8_t, 32>& history, int count);
    static int countActiveNotes(const std::array<std::uint8_t, 128>& activeNotes);
    void processCaptureBlock(juce::MidiBuffer& midiMessages, double sampleRate, int blockSize);
    void processHubBlock(juce::MidiBuffer& midiMessages);
    void refreshHubConnections();
    void oscMessageReceived(const juce::OSCMessage& message) override;

    juce::CriticalSection uiStateLock_;
    mutable juce::SpinLock ioSnapshotLock_;
    mutable juce::CriticalSection captureStateLock_;
    mutable juce::CriticalSection hubStateLock_;
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
    std::unordered_map<juce::String, NodeProcessingMode> nodeModes_;
    std::unordered_map<juce::String, int> activeSectionIndices_;
    std::unordered_map<juce::String, double> activeSectionPhases_;
    std::unordered_map<juce::String, std::uint64_t> sectionAdvanceCounts_;
    std::atomic<bool> followHostTempo_ { true };
    std::array<std::uint8_t, 128> incomingActiveNotes_ {};
    std::array<std::uint8_t, 128> outgoingActiveNotes_ {};
    std::array<std::uint8_t, 32> incomingHistory_ {};
    std::array<std::uint8_t, 32> outgoingHistory_ {};
    IoSnapshot ioSnapshot_;
    std::optional<int> currentProgramIndex_;
    std::atomic<CaptureMode> captureMode_ { CaptureMode::passThroughRecord };
    std::atomic<CaptureRecordStyle> captureRecordStyle_ { CaptureRecordStyle::manual };
    std::atomic<bool> captureRecordingEnabled_ { false };
    std::vector<pulse::Event> capturedMidiEvents_;
    double capturedMidiLengthSeconds_ = 0.0;
    double captureTimelineSeconds_ = 0.0;
    double capturePlaybackCursorSeconds_ = 0.0;
    double captureExportTempoBpm_ = 120.0;
    std::optional<double> captureStartSeconds_;
    double captureRecordStartSeconds_ = 0.0;
    std::atomic<double> captureRecordingSeconds_ { 0.0 };
    std::atomic<bool> captureNeedsFlush_ { false };
    juce::OSCSender hubOscSender_;
    std::vector<juce::MidiMessage> hubPendingIncomingMidi_;
    std::atomic<HubMode> hubMode_ { HubMode::bridge };
    std::atomic<int> hubReceivePort_ { 9000 };
    std::atomic<int> hubSendPort_ { 9001 };
    std::atomic<bool> hubReceiveConnected_ { false };
    std::atomic<bool> hubSendConnected_ { false };
    std::atomic<std::uint64_t> hubReceivedMessageCount_ { 0 };
    std::atomic<std::uint64_t> hubSentMessageCount_ { 0 };
    bool transportStateKnown_ = false;
    bool transportWasPlaying_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FabricAudioProcessor)
};
