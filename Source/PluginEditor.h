#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <memory>

class PulsePluginAudioProcessorEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer
    , private juce::CodeDocument::Listener
    , private juce::ListBoxModel
{
public:
    explicit PulsePluginAudioProcessorEditor(PulsePluginAudioProcessor&);
    ~PulsePluginAudioProcessorEditor() override;

    void resized() override;
    void paint(juce::Graphics&) override;

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

    void timerCallback() override;
    void codeDocumentTextInserted(const juce::String&, int) override;
    void codeDocumentTextDeleted(int, int) override;
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void requestCompileNow();
    void refreshFromProcessor();
    void jumpToDiagnostic(int row);
    void rebuildSectionButtons();
    void updateSectionButtonStates();
    double pulseAmountForModule(int moduleIndex) const;

    PulsePluginAudioProcessor& processor_;
    juce::Label titleLabel_;
    juce::Label statusLabel_;
    juce::TextButton compileButton_ { "Compile Now" };
    juce::CodeDocument document_;
    std::unique_ptr<juce::CodeTokeniser> tokeniser_;
    std::unique_ptr<juce::CodeEditorComponent> editor_;
    ModuleOverlay moduleOverlay_ { *this };
    DiagnosticOverlay diagnosticOverlay_ { *this };
    juce::ListBox diagnosticsList_;
    juce::TextEditor diagnosticsSummary_;
    GraphPreviewComponent graphPreview_;
    std::vector<PulsePluginAudioProcessor::UiDiagnostic> diagnostics_;
    std::vector<PulsePluginAudioProcessor::SectionControlSnapshot> sectionControls_;
    SectionPulseComponent sectionPulseOverlay_ { *this };
    juce::OwnedArray<juce::Label> sectionLabels_;
    juce::OwnedArray<juce::TextButton> sectionAdvanceButtons_;
    juce::OwnedArray<juce::TextButton> sectionButtons_;
    std::vector<int> sectionButtonModuleIndices_;
    std::vector<int> sectionButtonSectionIndices_;
    std::vector<std::uint64_t> seenAdvanceCounts_;
    std::vector<double> pulseAmounts_;
    std::uint64_t seenRevision_ = 0;
    double lastEditTimeMs_ = 0.0;
    bool pendingDebouncedCompile_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsePluginAudioProcessorEditor)
};
