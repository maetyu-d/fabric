#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <unordered_map>
#include <memory>

class PulsePluginAudioProcessorEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer
    , private juce::CodeDocument::Listener
    , private juce::ListBoxModel
    , private juce::KeyListener
{
public:
    explicit PulsePluginAudioProcessorEditor(PulsePluginAudioProcessor&);
    ~PulsePluginAudioProcessorEditor() override;

    void resized() override;
    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class ModuleOverlay final : public juce::Component {
    public:
        explicit ModuleOverlay(PulsePluginAudioProcessorEditor& owner) : owner_(owner) {}
        void paint(juce::Graphics&) override;

    private:
        PulsePluginAudioProcessorEditor& owner_;
    };

    class DiagnosticOverlay final : public juce::Component {
    public:
        explicit DiagnosticOverlay(PulsePluginAudioProcessorEditor& owner) : owner_(owner) {}
        void paint(juce::Graphics&) override;

    private:
        PulsePluginAudioProcessorEditor& owner_;
    };

    class GraphPreviewComponent final : public juce::Component {
    public:
        void setSnapshot(PulsePluginAudioProcessor::GraphSnapshot snapshot);
        void paint(juce::Graphics&) override;

    private:
        PulsePluginAudioProcessor::GraphSnapshot snapshot_;
    };

    class SectionPulseComponent final : public juce::Component {
    public:
        explicit SectionPulseComponent(PulsePluginAudioProcessorEditor& owner) : owner_(owner) {}
        void paint(juce::Graphics&) override;

    private:
        PulsePluginAudioProcessorEditor& owner_;
    };

    class IoVisualiserComponent final : public juce::Component {
    public:
        void setSnapshot(PulsePluginAudioProcessor::IoSnapshot snapshot);
        void paint(juce::Graphics&) override;

    private:
        PulsePluginAudioProcessor::IoSnapshot snapshot_;
    };

    enum class ViewMode {
        editor,
        io
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
    void updateStateTracking(const PulsePluginAudioProcessor::GraphSnapshot& snapshot);
    juce::String stateSummaryText() const;
    void applyViewMode();
    void toggleViewMode();
    void loadPatchFromFile();
    void savePatchToFile();

    PulsePluginAudioProcessor& processor_;
    juce::Label titleLabel_;
    juce::Label stateSummaryLabel_;
    juce::TextButton loadButton_ { "Load" };
    juce::TextButton saveButton_ { "Save" };
    juce::TextButton clockModeButton_;
    juce::TextButton compileButton_ { "Compile Now" };
    juce::CodeDocument document_;
    std::unique_ptr<juce::CodeTokeniser> tokeniser_;
    std::unique_ptr<juce::CodeEditorComponent> editor_;
    ModuleOverlay moduleOverlay_ { *this };
    DiagnosticOverlay diagnosticOverlay_ { *this };
    juce::ListBox diagnosticsList_;
    juce::TextEditor diagnosticsSummary_;
    GraphPreviewComponent graphPreview_;
    IoVisualiserComponent ioVisualiser_;
    std::vector<PulsePluginAudioProcessor::UiDiagnostic> diagnostics_;
    PulsePluginAudioProcessor::GraphSnapshot graphSnapshot_;
    PulsePluginAudioProcessor::IoSnapshot ioSnapshot_;
    std::vector<PulsePluginAudioProcessor::SectionControlSnapshot> sectionControls_;
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
    double lastEditTimeMs_ = 0.0;
    bool pendingDebouncedCompile_ = false;
    ViewMode viewMode_ = ViewMode::editor;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    juce::File lastPatchFile_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsePluginAudioProcessorEditor)
};
