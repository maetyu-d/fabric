#include "PluginEditor.h"

namespace {

constexpr int kDebounceMs = 350;
const auto kBg = juce::Colour::fromRGB(242, 236, 226);
const auto kPanel = juce::Colour::fromRGB(233, 226, 214);
const auto kPanelStrong = juce::Colour::fromRGB(222, 214, 202);
const auto kInk = juce::Colour::fromRGB(36, 38, 41);
const auto kMuted = juce::Colour::fromRGB(110, 107, 100);
const auto kCodeBg = juce::Colour::fromRGB(28, 34, 39);
const auto kCodeGutter = juce::Colour::fromRGB(34, 41, 46);
const auto kCodeText = juce::Colour::fromRGB(222, 225, 219);
const auto kAccent = juce::Colour::fromRGB(201, 110, 46);
const auto kAccentSoft = juce::Colour::fromRGBA(201, 110, 46, 60);
const auto kOutline = juce::Colour::fromRGBA(24, 24, 26, 40);
constexpr float kEditorFontSize = 18.0f;

juce::CodeEditorComponent::ColourScheme makePulseColourScheme()
{
    juce::CodeEditorComponent::ColourScheme scheme;
    scheme.set("Error", juce::Colour::fromRGB(216, 120, 104));
    scheme.set("Comment", juce::Colour::fromRGB(126, 139, 146));
    scheme.set("Keyword", juce::Colour::fromRGB(224, 228, 232));
    scheme.set("Operator", juce::Colour::fromRGB(192, 130, 88));
    scheme.set("Identifier", juce::Colour::fromRGB(217, 221, 215));
    scheme.set("Integer", juce::Colour::fromRGB(122, 170, 196));
    scheme.set("Float", juce::Colour::fromRGB(122, 170, 196));
    scheme.set("String", juce::Colour::fromRGB(162, 191, 136));
    scheme.set("Bracket", juce::Colour::fromRGB(192, 130, 88));
    scheme.set("Punctuation", juce::Colour::fromRGB(149, 156, 160));
    scheme.set("Preprocessor Text", juce::Colour::fromRGB(162, 191, 136));
    return scheme;
}

juce::Colour familyColour(const juce::String& family)
{
    if (family == "input") return juce::Colour::fromRGB(73, 116, 176);
    if (family == "analyze") return juce::Colour::fromRGB(72, 133, 108);
    if (family == "generate") return juce::Colour::fromRGB(174, 110, 49);
    if (family == "shape") return juce::Colour::fromRGB(148, 92, 123);
    if (family == "transform") return juce::Colour::fromRGB(118, 95, 168);
    if (family == "project") return juce::Colour::fromRGB(128, 108, 58);
    if (family == "output") return juce::Colour::fromRGB(150, 78, 68);
    return juce::Colour::fromRGB(128, 126, 121);
}

bool isModuleFamily(const juce::String& token)
{
    return token == "input"
        || token == "analyze"
        || token == "generate"
        || token == "shape"
        || token == "transform"
        || token == "memory"
        || token == "project"
        || token == "output";
}

bool isGlobalDirective(const juce::String& token)
{
    return token == "patch" || token == "scale" || token == "tempo";
}

juce::Colour directiveColour(const juce::String& token)
{
    if (token == "patch") return juce::Colour::fromRGB(197, 180, 126);
    if (token == "scale") return juce::Colour::fromRGB(105, 160, 143);
    if (token == "tempo") return juce::Colour::fromRGB(164, 124, 188);
    return juce::Colour::fromRGB(128, 126, 121);
}

void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour fill, float radius = 20.0f)
{
    g.setColour(fill);
    g.fillRoundedRectangle(bounds.toFloat(), radius);
    g.setColour(kOutline);
    g.drawRoundedRectangle(bounds.toFloat(), radius, 1.0f);
}

struct EditorLayout {
    juce::Rectangle<int> headerPanel;
    juce::Rectangle<int> sectionPanel;
    juce::Rectangle<int> editorPanel;
    juce::Rectangle<int> graphPanel;
    juce::Rectangle<int> diagnosticsPanel;
};

EditorLayout computeLayout(juce::Rectangle<int> bounds, int sectionCount, bool hasDiagnostics)
{
    EditorLayout layout;
    auto area = bounds.reduced(16);

    layout.headerPanel = area.removeFromTop(64);

    if (sectionCount > 0) {
        layout.sectionPanel = area.removeFromTop(48 * sectionCount + 12);
        area.removeFromTop(12);
    }

    constexpr int rightColumnWidth = 318;
    constexpr int columnGap = 14;
    constexpr int panelGap = 14;

    auto content = area;
    auto rightColumn = content.removeFromRight(juce::jmin(rightColumnWidth, juce::jmax(280, bounds.getWidth() / 3)));
    content.removeFromRight(columnGap);

    const int diagnosticsHeight = hasDiagnostics ? 152 : 110;
    layout.diagnosticsPanel = rightColumn.removeFromBottom(diagnosticsHeight);
    rightColumn.removeFromBottom(panelGap);
    layout.graphPanel = rightColumn;
    layout.editorPanel = content;
    return layout;
}

} // namespace

void PulsePluginAudioProcessorEditor::ModuleOverlay::paint(juce::Graphics& g)
{
    if (owner_.editor_ == nullptr) {
        return;
    }

    auto& editor = *owner_.editor_;
    const auto visibleTop = editor.getFirstLineOnScreen();
    const auto visibleBottom = visibleTop + editor.getNumLinesOnScreen() + 1;
    const auto rowHeight = editor.getLineHeight();
    const auto textStart = editor.getCharacterBounds(juce::CodeDocument::Position(owner_.document_, visibleTop, 0)).getX();
    const auto overlayRight = getWidth() - 10;

    struct ModuleBlock {
        int startLine = 0;
        int endLine = 0;
        juce::String family;
        juce::String kind;
        juce::String name;
    };

    std::vector<ModuleBlock> modules;
    std::vector<std::pair<int, juce::String>> directives;
    int firstConnectLine = -1;
    bool inModule = false;
    ModuleBlock current;

    const auto lineCount = owner_.document_.getNumLines();
    for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
        const auto rawLine = owner_.document_.getLine(lineIndex);
        const auto trimmed = rawLine.trim();
        if (trimmed.isEmpty()) {
            continue;
        }

        juce::StringArray tokens;
        tokens.addTokens(trimmed, " \t", {});
        tokens.trim();
        tokens.removeEmptyStrings();
        if (tokens.isEmpty()) {
            continue;
        }

        if (firstConnectLine < 0 && tokens[0] == "connect") {
            firstConnectLine = lineIndex;
        }

        if (!inModule && isGlobalDirective(tokens[0])) {
            directives.emplace_back(lineIndex, tokens[0]);
        }

        if (!inModule) {
            if (tokens.size() >= 2 && isModuleFamily(tokens[0])) {
                inModule = true;
                current = {};
                current.startLine = lineIndex;
                current.endLine = lineIndex;
                current.family = tokens[0];
                current.kind = tokens[1];
                if (tokens.size() >= 3) {
                    current.name = tokens[2];
                }
            }
            continue;
        }

        current.endLine = lineIndex;
        if (trimmed == "end") {
            modules.push_back(current);
            inModule = false;
        }
    }

    for (const auto& [lineIndex, token] : directives) {
        if (lineIndex < visibleTop || lineIndex > visibleBottom) {
            continue;
        }

        const auto lineBounds = editor.getCharacterBounds(juce::CodeDocument::Position(owner_.document_, lineIndex, 0));
        const auto accent = directiveColour(token);
        auto chipBounds = juce::Rectangle<float>(static_cast<float>(overlayRight - 108),
            static_cast<float>(lineBounds.getY() + 3),
            88.0f,
            18.0f);
        g.setColour(accent.withAlpha(0.20f));
        g.fillRoundedRectangle(chipBounds, 9.0f);
        g.setColour(accent.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        g.drawText(token.toUpperCase(), chipBounds.toNearestInt().reduced(8, 1), juce::Justification::centredLeft, true);
    }

    if (firstConnectLine >= visibleTop && firstConnectLine <= visibleBottom) {
        const auto connectBounds = editor.getCharacterBounds(juce::CodeDocument::Position(owner_.document_, firstConnectLine, 0));
        const auto separatorY = connectBounds.getY() - 10;
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
        g.drawLine(static_cast<float>(textStart + 12), static_cast<float>(separatorY), static_cast<float>(overlayRight - 16), static_cast<float>(separatorY), 1.0f);

        auto chipBounds = juce::Rectangle<float>(static_cast<float>(overlayRight - 110),
            static_cast<float>(separatorY - 12),
            90.0f,
            18.0f);
        g.setColour(kAccent.withAlpha(0.18f));
        g.fillRoundedRectangle(chipBounds, 9.0f);
        g.setColour(kAccent.withAlpha(0.88f));
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("CONNECTIONS", chipBounds.toNearestInt().reduced(8, 1), juce::Justification::centred, true);
    }

    for (const auto& module : modules) {
        if (module.endLine < visibleTop || module.startLine > visibleBottom) {
            continue;
        }

        const auto visibleStartLine = juce::jmax(module.startLine, visibleTop);
        const auto visibleEndLine = juce::jmin(module.endLine, visibleBottom);
        const auto startBounds = editor.getCharacterBounds(juce::CodeDocument::Position(owner_.document_, visibleStartLine, 0));
        const auto endBounds = editor.getCharacterBounds(juce::CodeDocument::Position(owner_.document_, visibleEndLine, 0));
        const auto headerTop = startBounds.getY();
        const auto blockTop = startBounds.getY() + rowHeight - 4;
        const auto blockBottom = endBounds.getY() + rowHeight;
        const auto accent = familyColour(module.family);

        g.setColour(accent.withAlpha(0.065f));
        g.fillRoundedRectangle(static_cast<float>(textStart + 14),
            static_cast<float>(blockTop),
            static_cast<float>(overlayRight - textStart - 28),
            static_cast<float>(blockBottom - blockTop),
            10.0f);

        g.setColour(accent.withAlpha(0.40f));
        g.fillRoundedRectangle(static_cast<float>(textStart + 14),
            static_cast<float>(blockTop + 5),
            4.0f,
            static_cast<float>(juce::jmax(14, blockBottom - blockTop - 10)),
            2.0f);

        if (module.startLine >= visibleTop && module.startLine <= visibleBottom) {
            auto chipBounds = juce::Rectangle<float>(static_cast<float>(overlayRight - juce::jmin(210, 88 + (module.kind.length() + module.name.length()) * 6)),
                static_cast<float>(headerTop + 3),
                static_cast<float>(juce::jmin(196, 74 + (module.kind.length() + module.name.length()) * 6)),
                18.0f);
            g.setColour(accent.withAlpha(0.16f));
            g.fillRoundedRectangle(chipBounds, 9.0f);
            g.setColour(accent.withAlpha(0.82f));
            g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
            const auto chipText = module.name.isNotEmpty()
                ? module.family + " / " + module.kind + " / " + module.name
                : module.family + " / " + module.kind;
            g.drawText(chipText, chipBounds.toNearestInt().reduced(8, 1), juce::Justification::centredLeft, true);
        }
    }
}

void PulsePluginAudioProcessorEditor::SectionPulseComponent::paint(juce::Graphics& g)
{
    for (int moduleIndex = 0; moduleIndex < owner_.sectionLabels_.size(); ++moduleIndex) {
        if (moduleIndex >= static_cast<int>(owner_.sectionControls_.size())) {
            continue;
        }

        const auto amount = owner_.pulseAmountForModule(moduleIndex);
        const auto phase = owner_.sectionControls_[static_cast<std::size_t>(moduleIndex)].phase;
        const auto labelBounds = owner_.sectionLabels_[moduleIndex]->getBounds();
        const auto localLabel = owner_.getLocalArea(owner_.sectionLabels_[moduleIndex], owner_.sectionLabels_[moduleIndex]->getLocalBounds());
        const auto barBounds = juce::Rectangle<float>(static_cast<float>(localLabel.getX()),
            static_cast<float>(localLabel.getBottom() - 5),
            static_cast<float>(localLabel.getWidth()),
            3.0f);

        g.setColour(juce::Colour::fromRGBA(40, 40, 45, 45));
        g.fillRoundedRectangle(barBounds, 1.5f);

        g.setColour(juce::Colour::fromRGBA(196, 105, 34, 180));
        g.fillRoundedRectangle(barBounds.withWidth(static_cast<float>(barBounds.getWidth() * std::clamp(phase, 0.0, 1.0))), 1.5f);

        const auto dotBounds = juce::Rectangle<float>(static_cast<float>(labelBounds.getRight() + 6),
            static_cast<float>(labelBounds.getCentreY() - 5),
            10.0f,
            10.0f);
        g.setColour(juce::Colour::fromRGBA(196, 105, 34, static_cast<juce::uint8>(70 + (185.0 * amount))));
        g.fillEllipse(dotBounds);
    }
}

void PulsePluginAudioProcessorEditor::DiagnosticOverlay::paint(juce::Graphics& g)
{
    if (owner_.editor_ == nullptr) {
        return;
    }

    auto& editor = *owner_.editor_;
    const auto visibleTop = editor.getFirstLineOnScreen();
    const auto visibleBottom = visibleTop + editor.getNumLinesOnScreen() + 1;
    const auto textStart = editor.getCharacterBounds(juce::CodeDocument::Position(owner_.document_, visibleTop, 0)).getX();
    const auto rowHeight = editor.getLineHeight();

    for (const auto& diagnostic : owner_.diagnostics_) {
        const auto lineIndex = juce::jmax(0, diagnostic.line - 1);
        if (lineIndex < visibleTop || lineIndex > visibleBottom) {
            continue;
        }

        const auto lineBounds = editor.getCharacterBounds(juce::CodeDocument::Position(owner_.document_, lineIndex, 0));
        const auto colour = diagnostic.isError ? juce::Colour::fromRGBA(180, 42, 42, 50)
                                               : juce::Colour::fromRGBA(180, 140, 38, 44);

        g.setColour(colour);
        g.fillRect(textStart, lineBounds.getY(), getWidth() - textStart - 4, rowHeight);

        g.setColour(diagnostic.isError ? juce::Colour::fromRGB(180, 42, 42)
                                       : juce::Colour::fromRGB(180, 140, 38));
        g.fillEllipse(static_cast<float>(juce::jmax(6, textStart - 14)),
            static_cast<float>(lineBounds.getY() + (rowHeight / 2) - 4),
            8.0f,
            8.0f);
    }
}

void PulsePluginAudioProcessorEditor::GraphPreviewComponent::setSnapshot(PulsePluginAudioProcessor::GraphSnapshot snapshot)
{
    snapshot_ = std::move(snapshot);
    repaint();
}

void PulsePluginAudioProcessorEditor::GraphPreviewComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::transparentBlack);

    auto bounds = getLocalBounds().reduced(8);
    drawPanel(g, bounds, kPanel, 22.0f);
    bounds.reduce(18, 18);

    if (snapshot_.nodes.empty()) {
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawFittedText("Graph preview appears here after a successful compile.", bounds, juce::Justification::centred, 2);
        return;
    }

    const int nodeWidth = juce::jmin(196, bounds.getWidth() - 32);
    const int nodeHeight = 52;
    const int spacing = 20;
    juce::HashMap<juce::String, juce::Rectangle<int>> nodeBounds;

    int y = bounds.getY() + 10;
    for (const auto& node : snapshot_.nodes) {
        auto rect = juce::Rectangle<int>(bounds.getX() + 12, y, nodeWidth, nodeHeight);
        nodeBounds.set(node.name, rect);
        y += nodeHeight + spacing;
    }

    g.setColour(juce::Colours::black.withAlpha(0.16f));
    for (const auto& connection : snapshot_.connections) {
        if (!nodeBounds.contains(connection.fromName) || !nodeBounds.contains(connection.toName)) {
            continue;
        }

        const auto from = nodeBounds[connection.fromName];
        const auto to = nodeBounds[connection.toName];
        juce::Path path;
        path.startNewSubPath(static_cast<float>(from.getRight()), static_cast<float>(from.getCentreY()));
        path.cubicTo(static_cast<float>(from.getRight() + 42), static_cast<float>(from.getCentreY()),
            static_cast<float>(to.getX() - 42), static_cast<float>(to.getCentreY()),
            static_cast<float>(to.getX()), static_cast<float>(to.getCentreY()));
        g.strokePath(path, juce::PathStrokeType(1.35f));
    }

    for (const auto& node : snapshot_.nodes) {
        auto rect = nodeBounds[node.name];
        drawPanel(g, rect, juce::Colour::fromRGBA(255, 255, 255, 170), 16.0f);

        auto accent = rect.removeFromLeft(6);
        g.setColour(familyColour(node.family));
        g.fillRoundedRectangle(accent.toFloat(), 3.0f);

        auto text = rect.reduced(12, 8);
        g.setColour(kInk);
        g.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
        g.drawText(node.name, text.removeFromTop(22), juce::Justification::centredLeft, true);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText(node.family + " / " + node.kind, text, juce::Justification::centredLeft, true);
    }
}

PulsePluginAudioProcessorEditor::PulsePluginAudioProcessorEditor(PulsePluginAudioProcessor& audioProcessor)
    : AudioProcessorEditor(&audioProcessor)
    , processor_(audioProcessor)
    , tokeniser_(std::make_unique<juce::CPlusPlusCodeTokeniser>())
{
    titleLabel_.setText("Pulse", juce::dontSendNotification);
    titleLabel_.setColour(juce::Label::textColourId, kInk);
    titleLabel_.setFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    statusLabel_.setColour(juce::Label::textColourId, kMuted);
    statusLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel_);

    compileButton_.onClick = [this] { requestCompileNow(); };
    compileButton_.setColour(juce::TextButton::buttonColourId, kInk);
    compileButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible(compileButton_);

    document_.replaceAllContent(processor_.getScriptText());
    document_.addListener(this);

    editor_ = std::make_unique<juce::CodeEditorComponent>(document_, tokeniser_.get());
    editor_->setScrollbarThickness(12);
    editor_->setLineNumbersShown(true);
    editor_->setFont(juce::Font(juce::FontOptions("Menlo", kEditorFontSize, juce::Font::plain)));
    editor_->setColourScheme(makePulseColourScheme());
    editor_->setColour(juce::CodeEditorComponent::backgroundColourId, kCodeBg);
    editor_->setColour(juce::CodeEditorComponent::defaultTextColourId, kCodeText);
    editor_->setColour(juce::CodeEditorComponent::highlightColourId, kAccentSoft);
    editor_->setColour(juce::CodeEditorComponent::lineNumberBackgroundId, kCodeGutter);
    editor_->setColour(juce::CodeEditorComponent::lineNumberTextId, juce::Colour::fromRGB(104, 161, 191));
    addAndMakeVisible(*editor_);

    moduleOverlay_.setInterceptsMouseClicks(false, false);
    editor_->addAndMakeVisible(moduleOverlay_);
    diagnosticOverlay_.setInterceptsMouseClicks(false, false);
    editor_->addAndMakeVisible(diagnosticOverlay_);

    diagnosticsList_.setModel(this);
    addAndMakeVisible(diagnosticsList_);

    diagnosticsSummary_.setMultiLine(true);
    diagnosticsSummary_.setReadOnly(true);
    diagnosticsSummary_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    diagnosticsSummary_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    diagnosticsSummary_.setColour(juce::TextEditor::textColourId, kMuted);
    addAndMakeVisible(diagnosticsSummary_);

    addAndMakeVisible(graphPreview_);
    addAndMakeVisible(sectionPulseOverlay_);
    sectionPulseOverlay_.setInterceptsMouseClicks(false, false);

    refreshFromProcessor();
    setSize(1110, 690);
    startTimer(120);
}

PulsePluginAudioProcessorEditor::~PulsePluginAudioProcessorEditor()
{
    document_.removeListener(this);
}

void PulsePluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    const auto layout = computeLayout(getLocalBounds(), static_cast<int>(sectionControls_.size()), !diagnostics_.empty());
    drawPanel(g, layout.headerPanel, juce::Colour::fromRGBA(255, 255, 255, 90), 22.0f);
    if (!layout.sectionPanel.isEmpty()) {
        drawPanel(g, layout.sectionPanel, juce::Colour::fromRGBA(255, 255, 255, 70), 20.0f);
    }
    drawPanel(g, layout.editorPanel, kCodeBg, 24.0f);
    drawPanel(g, layout.graphPanel, kPanelStrong, 24.0f);
    drawPanel(g, layout.diagnosticsPanel, juce::Colour::fromRGBA(255, 255, 255, 110), 20.0f);

    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    g.drawText("PATCH", layout.editorPanel.withTrimmedBottom(layout.editorPanel.getHeight() - 26).reduced(16, 0), juce::Justification::centredLeft, true);
    g.drawText("FLOW", layout.graphPanel.withTrimmedBottom(layout.graphPanel.getHeight() - 26).reduced(16, 0), juce::Justification::centredLeft, true);
    g.drawText("DIAGNOSTICS", layout.diagnosticsPanel.withTrimmedBottom(layout.diagnosticsPanel.getHeight() - 24).reduced(16, 0), juce::Justification::centredLeft, true);
}

void PulsePluginAudioProcessorEditor::resized()
{
    const auto layout = computeLayout(getLocalBounds(), static_cast<int>(sectionControls_.size()), !diagnostics_.empty());

    auto header = layout.headerPanel.reduced(18, 12);
    titleLabel_.setBounds(header.removeFromLeft(220));
    compileButton_.setBounds(header.removeFromRight(146));
    statusLabel_.setBounds(header.removeFromRight(260));

    auto sectionArea = layout.sectionPanel.reduced(16, 6);
    if (!sectionControls_.empty()) {
        for (int moduleIndex = 0; moduleIndex < static_cast<int>(sectionControls_.size()); ++moduleIndex) {
            auto row = sectionArea.removeFromTop(44);
            if (moduleIndex >= sectionLabels_.size() || moduleIndex >= sectionAdvanceButtons_.size()) {
                continue;
            }

            sectionLabels_[moduleIndex]->setBounds(row.removeFromLeft(160).reduced(0, 10));
            sectionAdvanceButtons_[moduleIndex]->setBounds(row.removeFromRight(96).reduced(2, 8));
            sectionAdvanceButtons_[moduleIndex]->setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGBA(255, 255, 255, 120));
            sectionAdvanceButtons_[moduleIndex]->setColour(juce::TextButton::textColourOffId, kInk);

            auto buttonArea = row.reduced(4, 6);
            int buttonsInRow = 0;
            for (int buttonIndex = 0; buttonIndex < sectionButtons_.size(); ++buttonIndex) {
                if (sectionButtonModuleIndices_[static_cast<std::size_t>(buttonIndex)] == moduleIndex) {
                    ++buttonsInRow;
                }
            }

            const auto buttonWidth = juce::jmin(126, juce::jmax(84, buttonArea.getWidth() / juce::jmax(1, buttonsInRow)));
            for (int buttonIndex = 0; buttonIndex < sectionButtons_.size(); ++buttonIndex) {
                if (sectionButtonModuleIndices_[static_cast<std::size_t>(buttonIndex)] != moduleIndex) {
                    continue;
                }
                sectionButtons_[buttonIndex]->setBounds(buttonArea.removeFromLeft(buttonWidth).reduced(4, 2));
            }
        }
    }

    auto editorBounds = layout.editorPanel.reduced(18, 18);
    editorBounds.removeFromTop(28);
    editor_->setBounds(editorBounds);
    moduleOverlay_.setBounds(editor_->getLocalBounds());
    diagnosticOverlay_.setBounds(editor_->getLocalBounds());
    graphPreview_.setBounds(layout.graphPanel.reduced(10, 28));

    auto diagnosticsContent = layout.diagnosticsPanel.reduced(12, 26);
    diagnosticsContent.removeFromTop(22);
    auto listArea = diagnosticsContent.removeFromTop(diagnostics_.empty() ? 0 : 70);
    diagnosticsList_.setBounds(listArea);
    diagnosticsSummary_.setBounds(diagnosticsContent.withTrimmedTop(diagnostics_.empty() ? 0 : 8));
    sectionPulseOverlay_.setBounds(getLocalBounds());
}

void PulsePluginAudioProcessorEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (pendingDebouncedCompile_ && (now - lastEditTimeMs_) >= kDebounceMs) {
        pendingDebouncedCompile_ = false;
        processor_.requestCompile(document_.getAllContent());
    }

    const auto revision = processor_.getUiRevision();
    if (revision != seenRevision_) {
        refreshFromProcessor();
    } else {
        updateSectionButtonStates();
        bool needsRepaint = false;
        for (auto& amount : pulseAmounts_) {
            if (amount > 0.01) {
                amount = juce::jmax(0.0, amount - 0.12);
                needsRepaint = true;
            } else {
                amount = 0.0;
            }
        }
        if (needsRepaint) {
            sectionPulseOverlay_.repaint();
        }
        if (processor_.isCompileInProgress()) {
            statusLabel_.setText("Compiling...", juce::dontSendNotification);
        }
    }
}

void PulsePluginAudioProcessorEditor::codeDocumentTextInserted(const juce::String&, int)
{
    lastEditTimeMs_ = juce::Time::getMillisecondCounterHiRes();
    pendingDebouncedCompile_ = true;
    statusLabel_.setText("Waiting to recompile...", juce::dontSendNotification);
    moduleOverlay_.repaint();
}

void PulsePluginAudioProcessorEditor::codeDocumentTextDeleted(int, int)
{
    lastEditTimeMs_ = juce::Time::getMillisecondCounterHiRes();
    pendingDebouncedCompile_ = true;
    statusLabel_.setText("Waiting to recompile...", juce::dontSendNotification);
    moduleOverlay_.repaint();
}

int PulsePluginAudioProcessorEditor::getNumRows()
{
    return static_cast<int>(diagnostics_.size());
}

void PulsePluginAudioProcessorEditor::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(diagnostics_.size())) {
        return;
    }

    if (rowIsSelected) {
        g.fillAll(kAccentSoft);
    }

    const auto& diagnostic = diagnostics_[static_cast<std::size_t>(rowNumber)];
    const auto badge = diagnostic.isError ? juce::Colour::fromRGB(170, 78, 67) : juce::Colour::fromRGB(180, 137, 67);
    g.setColour(badge);
    g.fillRoundedRectangle(juce::Rectangle<float>(8.0f, 8.0f, 64.0f, static_cast<float>(height - 16)), 8.0f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText(juce::String(diagnostic.line) + ":" + juce::String(diagnostic.column), 8, 0, 64, height, juce::Justification::centred);

    g.setColour(kInk);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawFittedText(diagnostic.message, 84, 4, width - 92, height - 8, juce::Justification::centredLeft, 2);
}

void PulsePluginAudioProcessorEditor::selectedRowsChanged(int lastRowSelected)
{
    jumpToDiagnostic(lastRowSelected);
}

void PulsePluginAudioProcessorEditor::requestCompileNow()
{
    pendingDebouncedCompile_ = false;
    processor_.requestCompile(document_.getAllContent());
}

void PulsePluginAudioProcessorEditor::refreshFromProcessor()
{
    seenRevision_ = processor_.getUiRevision();
    const auto latestScript = processor_.getScriptText();
    if (document_.getAllContent() != latestScript) {
        document_.replaceAllContent(latestScript);
    }
    diagnostics_ = processor_.getDiagnostics();
    sectionControls_ = processor_.getSectionControls();
    rebuildSectionButtons();
    updateSectionButtonStates();
    diagnosticsList_.updateContent();
    diagnosticsList_.repaint();
    diagnosticsSummary_.setText(processor_.getDiagnosticsText(), juce::dontSendNotification);
    graphPreview_.setSnapshot(processor_.getGraphSnapshot());
    moduleOverlay_.repaint();
    diagnosticOverlay_.repaint();

    if (processor_.isCompileInProgress()) {
        statusLabel_.setText("Compiling...", juce::dontSendNotification);
    } else if (diagnostics_.empty()) {
        statusLabel_.setText("Compiled successfully", juce::dontSendNotification);
    } else {
        statusLabel_.setText("Diagnostics: " + juce::String(static_cast<int>(diagnostics_.size())), juce::dontSendNotification);
    }

    if (!diagnostics_.empty()) {
        diagnosticsList_.selectRow(0);
    }

    resized();
}

void PulsePluginAudioProcessorEditor::jumpToDiagnostic(int row)
{
    if (row < 0 || row >= static_cast<int>(diagnostics_.size()) || editor_ == nullptr) {
        return;
    }

    const auto& diagnostic = diagnostics_[static_cast<std::size_t>(row)];
    const auto line = juce::jmax(0, diagnostic.line - 1);
    editor_->scrollToLine(line);

    juce::CodeDocument::Position start(document_, line, juce::jmax(0, diagnostic.column - 1));
    juce::CodeDocument::Position end(document_, line, juce::jmax(diagnostic.column, diagnostic.column + 1));
    editor_->moveCaretTo(start, false);
    editor_->selectRegion(start, end);
}

void PulsePluginAudioProcessorEditor::rebuildSectionButtons()
{
    for (auto* label : sectionLabels_) {
        removeChildComponent(label);
    }
    for (auto* button : sectionAdvanceButtons_) {
        removeChildComponent(button);
    }
    for (auto* button : sectionButtons_) {
        removeChildComponent(button);
    }
    sectionLabels_.clear(true);
    sectionAdvanceButtons_.clear(true);
    sectionButtons_.clear(true);
    sectionButtonModuleIndices_.clear();
    sectionButtonSectionIndices_.clear();
    seenAdvanceCounts_.assign(sectionControls_.size(), 0);
    pulseAmounts_.assign(sectionControls_.size(), 0.0);

    if (sectionControls_.empty()) {
        return;
    }

    for (int moduleIndex = 0; moduleIndex < static_cast<int>(sectionControls_.size()); ++moduleIndex) {
        const auto& sectionControl = sectionControls_[static_cast<std::size_t>(moduleIndex)];

        auto* label = sectionLabels_.add(new juce::Label());
        label->setText("Form: " + sectionControl.moduleName, juce::dontSendNotification);
        label->setColour(juce::Label::textColourId, kInk);
        label->setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        addAndMakeVisible(label);

        auto* advanceButton = sectionAdvanceButtons_.add(new juce::TextButton("Advance"));
        advanceButton->onClick = [this, moduleName = sectionControl.moduleName] {
            processor_.requestSectionAdvance(moduleName);
        };
        addAndMakeVisible(advanceButton);

        for (int sectionIndex = 0; sectionIndex < sectionControl.sectionNames.size(); ++sectionIndex) {
            auto* button = sectionButtons_.add(new juce::TextButton(sectionControl.sectionNames[sectionIndex]));
            button->setClickingTogglesState(false);
            button->onClick = [this, moduleName = sectionControl.moduleName, sectionIndex] {
                processor_.requestSectionRecall(moduleName, sectionIndex);
            };
            addAndMakeVisible(button);
            sectionButtonModuleIndices_.push_back(moduleIndex);
            sectionButtonSectionIndices_.push_back(sectionIndex);
        }
    }
}

void PulsePluginAudioProcessorEditor::updateSectionButtonStates()
{
    const auto latestControls = processor_.getSectionControls();
    if (latestControls.size() != sectionControls_.size()) {
        sectionControls_ = latestControls;
        rebuildSectionButtons();
        resized();
        return;
    }

    sectionControls_ = latestControls;

    for (int buttonIndex = 0; buttonIndex < sectionButtons_.size(); ++buttonIndex) {
        const auto moduleIndex = sectionButtonModuleIndices_[static_cast<std::size_t>(buttonIndex)];
        const auto sectionIndex = sectionButtonSectionIndices_[static_cast<std::size_t>(buttonIndex)];
        const bool isActive = moduleIndex >= 0
            && moduleIndex < static_cast<int>(sectionControls_.size())
            && sectionControls_[static_cast<std::size_t>(moduleIndex)].activeSectionIndex == sectionIndex;

        auto* button = sectionButtons_[buttonIndex];
        button->setColour(juce::TextButton::buttonColourId,
            isActive ? kAccent : juce::Colour::fromRGBA(255, 255, 255, 140));
        button->setColour(juce::TextButton::textColourOffId, isActive ? juce::Colours::white : kInk);
    }

    for (int moduleIndex = 0; moduleIndex < static_cast<int>(sectionControls_.size()); ++moduleIndex) {
        const auto& control = sectionControls_[static_cast<std::size_t>(moduleIndex)];
        if (moduleIndex >= static_cast<int>(seenAdvanceCounts_.size())) {
            seenAdvanceCounts_.push_back(control.advanceCount);
            pulseAmounts_.push_back(0.0);
            continue;
        }

        if (control.advanceCount != seenAdvanceCounts_[static_cast<std::size_t>(moduleIndex)]) {
            seenAdvanceCounts_[static_cast<std::size_t>(moduleIndex)] = control.advanceCount;
            pulseAmounts_[static_cast<std::size_t>(moduleIndex)] = 1.0;
        }
    }

    sectionPulseOverlay_.repaint();
}

double PulsePluginAudioProcessorEditor::pulseAmountForModule(int moduleIndex) const
{
    if (moduleIndex < 0 || moduleIndex >= static_cast<int>(pulseAmounts_.size())) {
        return 0.0;
    }

    return pulseAmounts_[static_cast<std::size_t>(moduleIndex)];
}
