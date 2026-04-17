#pragma once

#include <JuceHeader.h>
#include "FabricProcessor.h"

#include <unordered_map>
#include <memory>

class FabricAudioProcessorEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer
    , private juce::CodeDocument::Listener
    , private juce::ListBoxModel
    , private juce::KeyListener
{
public:
    explicit FabricAudioProcessorEditor(FabricAudioProcessor&);
    ~FabricAudioProcessorEditor() override;

    void resized() override;
    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class ModuleOverlay final : public juce::Component {
    public:
        explicit ModuleOverlay(FabricAudioProcessorEditor& owner) : owner_(owner) {}
        void paint(juce::Graphics&) override;

    private:
        FabricAudioProcessorEditor& owner_;
    };

    class DiagnosticOverlay final : public juce::Component {
    public:
        explicit DiagnosticOverlay(FabricAudioProcessorEditor& owner) : owner_(owner) {}
        void paint(juce::Graphics&) override;

    private:
        FabricAudioProcessorEditor& owner_;
    };

    class GraphPreviewComponent final : public juce::Component {
    public:
        explicit GraphPreviewComponent(FabricAudioProcessorEditor& owner) : owner_(owner) {}
        void setSnapshot(FabricAudioProcessor::GraphSnapshot snapshot);
        void paint(juce::Graphics&) override;
        void mouseUp(const juce::MouseEvent&) override;

    private:
        struct NodeHitRegion {
            juce::Rectangle<int> body;
            juce::Rectangle<int> bypassButton;
            juce::Rectangle<int> muteButton;
        };

        FabricAudioProcessorEditor& owner_;
        FabricAudioProcessor::GraphSnapshot snapshot_;
        std::unordered_map<juce::String, NodeHitRegion> hitRegions_;
    };

    class SectionPulseComponent final : public juce::Component {
    public:
        explicit SectionPulseComponent(FabricAudioProcessorEditor& owner) : owner_(owner) {}
        void paint(juce::Graphics&) override;

    private:
        FabricAudioProcessorEditor& owner_;
    };

    class IoVisualiserComponent final : public juce::Component {
    public:
        explicit IoVisualiserComponent(FabricAudioProcessorEditor& owner) : owner_(owner) {}
        void setSnapshot(FabricAudioProcessor::IoSnapshot snapshot);
        void paint(juce::Graphics&) override;

    private:
        FabricAudioProcessorEditor& owner_;
        FabricAudioProcessor::IoSnapshot snapshot_;
    };

    class ModulatorInspectorComponent final : public juce::Component {
    public:
        explicit ModulatorInspectorComponent(FabricAudioProcessorEditor& owner) : owner_(owner) {}
        void setSnapshots(std::vector<FabricAudioProcessor::ModulatorSnapshot> snapshots, juce::String inspectedModule);
        void paint(juce::Graphics&) override;
        void mouseUp(const juce::MouseEvent&) override;

    private:
        struct StageHitRegion {
            juce::String moduleName;
            int channel = 0;
            int stage = 0;
            juce::Rectangle<int> bounds;
        };

        FabricAudioProcessorEditor& owner_;
        std::vector<FabricAudioProcessor::ModulatorSnapshot> snapshots_;
        juce::String inspectedModule_;
        std::vector<StageHitRegion> hitRegions_;
    };

    enum class ViewMode {
        editor,
        io,
        lessons
    };

    struct EditableStage {
        juce::String moduleName;
        int channel = 0;
        int stage = 0;
        int lineIndex = -1;
        double level = 0.0;
        double timeMs = 100.0;
        double overlapMs = 0.0;
        juce::String curve = "linear";
    };

    void timerCallback() override;
    void codeDocumentTextInserted(const juce::String&, int) override;
    void codeDocumentTextDeleted(int, int) override;
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;

    void requestCompileNow();
    void refreshFromProcessor();
    void jumpToDiagnostic(int row);
    void rebuildSectionButtons();
    void updateSectionButtonStates();
    double pulseAmountForModule(int moduleIndex) const;
    void updateStateTracking(const FabricAudioProcessor::GraphSnapshot& snapshot);
    juce::String stateSummaryText() const;
    void jumpToModuleLine(int line);
    void jumpToModulatorStage(const juce::String& moduleName, int channel, int stage);
    void cycleNodeMode(const juce::String& moduleName, FabricAudioProcessor::NodeProcessingMode mode);
    void focusModulator(const juce::String& moduleName);
    void selectModulatorStage(const juce::String& moduleName, int channel, int stage);
    void refreshStageEditor();
    std::optional<EditableStage> findEditableStage(const juce::String& moduleName, int channel, int stage) const;
    void applyEditableStage(const EditableStage& stage);
    void applyViewMode();
    void toggleViewMode();
    void loadPatchFromFile();
    void savePatchToFile();
    void exportCapturedMidiToFile();
    void rebuildTutorialBrowser();
    void rebuildIoModuleBrowser();
    void updateTutorialSummary();
    void loadSelectedTutorial();
    void setGraphScope(const juce::String& scope);
    void refreshCaptureControls();
    void refreshHubControls();
    void applyHubPortChanges();

    FabricAudioProcessor& processor_;
    juce::Label titleLabel_;
    juce::Label stateSummaryLabel_;
    juce::ComboBox tutorialBox_;
    juce::ComboBox ioModuleBox_;
    juce::TextButton tutorialLoadButton_ { "Load Lesson" };
    juce::TextButton loadButton_ { "Load" };
    juce::TextButton saveButton_ { "Save" };
    juce::TextButton clockModeButton_;
    juce::Label hubReceivePortLabel_;
    juce::TextEditor hubReceivePortEditor_;
    juce::Label hubSendPortLabel_;
    juce::TextEditor hubSendPortEditor_;
    juce::TextButton hubApplyPortsButton_ { "Apply OSC Ports" };
    juce::TextButton captureModeButton_;
    juce::TextButton captureRecordStyleButton_;
    juce::TextButton startCaptureButton_ { "Start Recording" };
    juce::TextButton stopCaptureButton_ { "Stop Recording" };
    juce::TextButton clearCaptureButton_ { "Clear Take" };
    juce::TextButton exportCaptureButton_ { "Export MIDI" };
    juce::TextButton compileButton_ { "Recompile" };
    juce::CodeDocument document_;
    std::unique_ptr<juce::CodeTokeniser> tokeniser_;
    std::unique_ptr<juce::CodeEditorComponent> editor_;
    ModuleOverlay moduleOverlay_ { *this };
    DiagnosticOverlay diagnosticOverlay_ { *this };
    juce::ListBox diagnosticsList_;
    juce::TextEditor diagnosticsSummary_;
    juce::TextEditor helpSummary_;
    juce::TextEditor lessonSummary_;
    juce::Label stageEditorLabel_;
    juce::Label stageLevelLabel_;
    juce::Label stageTimeLabel_;
    juce::Label stageOverlapLabel_;
    juce::Label stageCurveLabel_;
    juce::Slider stageLevelSlider_;
    juce::Slider stageTimeSlider_;
    juce::Slider stageOverlapSlider_;
    juce::ComboBox stageCurveBox_;
    juce::Viewport graphViewport_;
    GraphPreviewComponent graphPreview_ { *this };
    IoVisualiserComponent ioVisualiser_ { *this };
    ModulatorInspectorComponent modulatorInspector_ { *this };
    std::vector<FabricAudioProcessor::UiDiagnostic> diagnostics_;
    FabricAudioProcessor::GraphSnapshot graphSnapshot_;
    FabricAudioProcessor::IoSnapshot ioSnapshot_;
    std::vector<FabricAudioProcessor::ModulatorSnapshot> modulatorSnapshots_;
    std::vector<FabricAudioProcessor::SectionControlSnapshot> sectionControls_;
    juce::StringArray tutorialNames_;
    int selectedTutorialIndex_ = 0;
    juce::String selectedIoModuleName_;
    std::optional<EditableStage> selectedStage_;
    bool stageEditorUpdating_ = false;
    SectionPulseComponent sectionPulseOverlay_ { *this };
    juce::OwnedArray<juce::Label> sectionLabels_;
    juce::OwnedArray<juce::TextButton> sectionAdvanceButtons_;
    juce::OwnedArray<juce::TextButton> sectionButtons_;
    std::vector<int> sectionButtonModuleIndices_;
    std::vector<int> sectionButtonSectionIndices_;
    std::vector<std::uint64_t> seenAdvanceCounts_;
    std::vector<double> pulseAmounts_;
    std::unordered_map<juce::String, juce::String> lastNodeDetails_;
    std::unordered_map<juce::String, juce::StringArray> nodeStateHistory_;
    std::uint64_t seenRevision_ = 0;
    ViewMode viewMode_ = ViewMode::editor;
    juce::String inspectedModulatorName_;
    juce::String graphScopePrefix_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    juce::File lastPatchFile_;
    juce::File lastMidiExportFile_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FabricAudioProcessorEditor)
};
