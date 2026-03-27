#include "FabricEditor.h"

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

juce::CodeEditorComponent::ColourScheme makeFabricColourScheme()
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
    if (family == "input") return juce::Colour::fromRGB(62, 122, 196);
    if (family == "analyze") return juce::Colour::fromRGB(54, 150, 118);
    if (family == "generate") return juce::Colour::fromRGB(208, 126, 42);
    if (family == "shape") return juce::Colour::fromRGB(181, 88, 126);
    if (family == "transform") return juce::Colour::fromRGB(124, 100, 209);
    if (family == "memory") return juce::Colour::fromRGB(198, 88, 164);
    if (family == "project") return juce::Colour::fromRGB(150, 136, 42);
    if (family == "output") return juce::Colour::fromRGB(186, 82, 64);
    return juce::Colour::fromRGB(138, 136, 130);
}

struct DisplayModule {
    juce::String family;
    juce::String kind;
    juce::String name;
};

std::optional<DisplayModule> inferDisplayModule(const juce::StringArray& tokens)
{
    if (tokens.size() >= 3 && tokens[0] == "midi") {
        if (tokens[1] == "in") return DisplayModule { "input", "midi", tokens[2] };
        if (tokens[1] == "out") return DisplayModule { "output", "midi", tokens[2] };
    }

    if (tokens.size() >= 2) {
        const auto& head = tokens[0];
        const auto& name = tokens[1];

        if (head == "motion") return DisplayModule { "analyze", "motion", name };

        if (head == "clock" || head == "pattern" || head == "fibonacci" || head == "random"
            || head == "phrase" || head == "progression" || head == "growth" || head == "swarm"
            || head == "collapse" || head == "section") {
            return DisplayModule { "generate", head, name };
        }

        if (head == "stages" || head == "lists" || head == "modulator") {
            return DisplayModule { "shape", head, name };
        }

        if (head == "quantize" || head == "split" || head == "delay" || head == "loop"
            || head == "bounce" || head == "arp" || head == "warp" || head == "filter"
            || head == "bits") {
            return DisplayModule { "transform", head, name };
        }

        if (head == "smear" || head == "cutup") {
            return DisplayModule { "memory", head, name };
        }

        if (head == "notes") {
            return DisplayModule { "project", "to_notes", name };
        }
    }

    return std::nullopt;
}

bool isGlobalDirective(const juce::String& token)
{
    return token == "patch" || token == "scale" || token == "key" || token == "tempo";
}

juce::Colour directiveColour(const juce::String& token)
{
    if (token == "patch") return juce::Colour::fromRGB(191, 176, 104);
    if (token == "scale") return juce::Colour::fromRGB(82, 164, 148);
    if (token == "tempo") return juce::Colour::fromRGB(170, 118, 202);
    return juce::Colour::fromRGB(138, 136, 130);
}

void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour fill, float radius = 20.0f)
{
    g.setColour(fill);
    g.fillRoundedRectangle(bounds.toFloat(), radius);
    g.setColour(kOutline);
    g.drawRoundedRectangle(bounds.toFloat(), radius, 1.0f);
}

juce::Colour chipFillColour(juce::Colour base)
{
    return base.interpolatedWith(juce::Colours::white, 0.10f).withAlpha(0.24f);
}

juce::Colour chipTextColour(juce::Colour base)
{
    return base.interpolatedWith(juce::Colours::white, 0.28f).withAlpha(0.98f);
}

void styleSecondaryButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGBA(255, 255, 255, 120));
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGBA(255, 255, 255, 150));
    button.setColour(juce::TextButton::textColourOffId, kInk);
    button.setColour(juce::TextButton::textColourOnId, kInk);
    button.setTriggeredOnMouseDown(false);
}

void stylePrimaryButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, kInk);
    button.setColour(juce::TextButton::buttonOnColourId, kInk.brighter(0.08f));
    button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    button.setTriggeredOnMouseDown(false);
}

void styleHeaderComboBox(juce::ComboBox& combo)
{
    combo.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(255, 255, 255, 120));
    combo.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(24, 24, 26, 55));
    combo.setColour(juce::ComboBox::textColourId, kInk);
    combo.setColour(juce::ComboBox::arrowColourId, kMuted);
}

struct EditorLayout {
    juce::Rectangle<int> headerPanel;
    juce::Rectangle<int> sectionPanel;
    juce::Rectangle<int> editorPanel;
    juce::Rectangle<int> graphPanel;
    juce::Rectangle<int> diagnosticsPanel;
    juce::Rectangle<int> helpPanel;
};

juce::Rectangle<int> contentUnion(const EditorLayout& layout)
{
    auto combined = layout.editorPanel;
    combined = combined.getUnion(layout.graphPanel);
    if (!layout.diagnosticsPanel.isEmpty()) {
        combined = combined.getUnion(layout.diagnosticsPanel);
    }
    if (!layout.helpPanel.isEmpty()) {
        combined = combined.getUnion(layout.helpPanel);
    }
    return combined;
}

EditorLayout computeLayout(juce::Rectangle<int> bounds, int sectionCount, bool hasDiagnostics, bool hasModulatorInspector)
{
    EditorLayout layout;
    auto area = bounds.reduced(18);

    layout.headerPanel = area.removeFromTop(72);

    if (sectionCount > 0) {
        layout.sectionPanel = area.removeFromTop(48 * sectionCount + 12);
        area.removeFromTop(12);
    }

    constexpr int rightColumnWidth = 304;
    constexpr int columnGap = 16;
    constexpr int panelGap = 16;

    auto content = area;
    auto rightColumn = content.removeFromRight(juce::jmin(rightColumnWidth, juce::jmax(280, bounds.getWidth() / 3)));
    content.removeFromRight(columnGap);

    const int helpHeight = hasModulatorInspector ? 220 : 132;
    layout.helpPanel = rightColumn.removeFromBottom(helpHeight);
    rightColumn.removeFromBottom(panelGap);

    if (hasDiagnostics) {
        const int diagnosticsHeight = 152;
        layout.diagnosticsPanel = rightColumn.removeFromBottom(diagnosticsHeight);
        rightColumn.removeFromBottom(panelGap);
    }
    layout.graphPanel = rightColumn;
    layout.editorPanel = content;
    return layout;
}

} // namespace

void FabricAudioProcessorEditor::ModuleOverlay::paint(juce::Graphics& g)
{
    if (owner_.editor_ == nullptr) {
        return;
    }

    auto& editor = *owner_.editor_;
    const auto visibleTop = editor.getFirstLineOnScreen();
    const auto visibleBottom = visibleTop + editor.getNumLinesOnScreen() + 1;
    const auto rowHeight = editor.getLineHeight();
    const auto textStart = editor.getCharacterBounds(juce::CodeDocument::Position(owner_.document_, visibleTop, 0)).getX();
    const auto overlayRight = getWidth() - 22;

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

        if (firstConnectLine < 0 && tokens.size() == 3 && tokens[1] == "->") {
            firstConnectLine = lineIndex;
        }

        if (!inModule && isGlobalDirective(tokens[0])) {
            directives.emplace_back(lineIndex, tokens[0]);
        }

        if (!inModule) {
            if (const auto display = inferDisplayModule(tokens); display.has_value()) {
                inModule = true;
                current = {};
                current.startLine = lineIndex;
                current.endLine = lineIndex;
                current.family = display->family;
                current.kind = display->kind;
                current.name = display->name;
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
        auto chipBounds = juce::Rectangle<float>(static_cast<float>(overlayRight - 94),
            static_cast<float>(lineBounds.getY() + 4),
            76.0f,
            16.0f);
        g.setColour(chipFillColour(accent));
        g.fillRoundedRectangle(chipBounds, 8.0f);
        g.setColour(chipTextColour(accent));
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        g.drawText(token.toUpperCase(), chipBounds.toNearestInt().reduced(7, 1), juce::Justification::centredLeft, true);
    }

    if (firstConnectLine >= visibleTop && firstConnectLine <= visibleBottom) {
        const auto connectBounds = editor.getCharacterBounds(juce::CodeDocument::Position(owner_.document_, firstConnectLine, 0));
        const auto separatorY = connectBounds.getY() - 10;
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
        g.drawLine(static_cast<float>(textStart + 12), static_cast<float>(separatorY), static_cast<float>(overlayRight - 16), static_cast<float>(separatorY), 1.0f);

        auto chipBounds = juce::Rectangle<float>(static_cast<float>(overlayRight - 108),
            static_cast<float>(separatorY - 11),
            88.0f,
            16.0f);
        g.setColour(chipFillColour(kAccent));
        g.fillRoundedRectangle(chipBounds, 8.0f);
        g.setColour(chipTextColour(kAccent));
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText("CONNECTIONS", chipBounds.toNearestInt().reduced(7, 1), juce::Justification::centred, true);
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

        g.setColour(accent.withAlpha(0.05f));
        g.fillRoundedRectangle(static_cast<float>(textStart + 14),
            static_cast<float>(blockTop),
            static_cast<float>(overlayRight - textStart - 28),
            static_cast<float>(blockBottom - blockTop),
            10.0f);

        g.setColour(accent.withAlpha(0.40f));
        g.fillRoundedRectangle(static_cast<float>(textStart + 14),
            static_cast<float>(blockTop + 5),
            3.0f,
            static_cast<float>(juce::jmax(14, blockBottom - blockTop - 10)),
            2.0f);

        if (module.startLine >= visibleTop && module.startLine <= visibleBottom) {
            auto chipWidth = static_cast<float>(juce::jmin(184, 66 + (module.kind.length() + module.name.length()) * 5));
            auto chipBounds = juce::Rectangle<float>(static_cast<float>(overlayRight) - chipWidth - 8.0f,
                static_cast<float>(headerTop + 4),
                chipWidth,
                16.0f);
            g.setColour(chipFillColour(accent));
            g.fillRoundedRectangle(chipBounds, 8.0f);
            g.setColour(chipTextColour(accent));
            g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
            const auto chipText = module.name.isNotEmpty()
                ? module.family + " / " + module.kind + " / " + module.name
                : module.family + " / " + module.kind;
            juce::String detail;
            for (const auto& node : owner_.graphSnapshot_.nodes) {
                if (node.name == module.name && node.detail.isNotEmpty()) {
                    detail = node.detail;
                    break;
                }
            }
            g.drawText(detail.isNotEmpty() ? chipText + " / " + detail : chipText,
                chipBounds.toNearestInt().reduced(7, 1),
                juce::Justification::centredLeft,
                true);
        }
    }
}

void FabricAudioProcessorEditor::SectionPulseComponent::paint(juce::Graphics& g)
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

void FabricAudioProcessorEditor::DiagnosticOverlay::paint(juce::Graphics& g)
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

void FabricAudioProcessorEditor::GraphPreviewComponent::setSnapshot(FabricAudioProcessor::GraphSnapshot snapshot)
{
    snapshot_ = std::move(snapshot);
    repaint();
}

void FabricAudioProcessorEditor::GraphPreviewComponent::mouseUp(const juce::MouseEvent& event)
{
    for (const auto& [name, region] : hitRegions_) {
        if (region.bypassButton.contains(event.getPosition())) {
            owner_.cycleNodeMode(name, FabricAudioProcessor::NodeProcessingMode::bypass);
            return;
        }
        if (region.muteButton.contains(event.getPosition())) {
            owner_.cycleNodeMode(name, FabricAudioProcessor::NodeProcessingMode::mute);
            return;
        }
        if (region.body.contains(event.getPosition())) {
            for (const auto& node : snapshot_.nodes) {
                if (node.name == name && node.sourceLine > 0) {
                    owner_.selectedIoModuleName_ = node.name;
                    if (node.kind == "modulator") {
                        owner_.focusModulator(node.name);
                    }
                    owner_.jumpToModuleLine(node.sourceLine);
                    return;
                }
            }
        }
    }
}

void FabricAudioProcessorEditor::IoVisualiserComponent::setSnapshot(FabricAudioProcessor::IoSnapshot snapshot)
{
    snapshot_ = std::move(snapshot);
    repaint();
}

void FabricAudioProcessorEditor::IoVisualiserComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::transparentBlack);

    auto bounds = getLocalBounds().reduced(8);
    drawPanel(g, bounds, kPanelStrong, 24.0f);
    bounds.reduce(20, 18);

    auto header = bounds.removeFromTop(28);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    g.drawText("LIVE I/O", header.removeFromLeft(100), juce::Justification::centredLeft, true);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    const auto focusName = owner_.selectedIoModuleName_.isNotEmpty() ? owner_.selectedIoModuleName_ : juce::String("Whole Patch");
    g.drawText("Focus: " + focusName + "  |  Press Tab to return to the patch.", header, juce::Justification::centredRight, true);

    bounds.removeFromTop(8);
    auto left = bounds.removeFromLeft(bounds.getWidth() / 2);
    bounds.removeFromLeft(14);
    auto right = bounds;

    FabricAudioProcessor::IoSnapshot snapshot = snapshot_;
    if (owner_.selectedIoModuleName_.isNotEmpty()) {
        for (const auto& node : snapshot_.nodes) {
            if (node.moduleName == owner_.selectedIoModuleName_) {
                snapshot.incoming = node.incoming;
                snapshot.outgoing = node.outgoing;
                snapshot.incomingCount = node.incomingCount;
                snapshot.outgoingCount = node.outgoingCount;
                snapshot.incomingActiveCount = node.incomingActiveCount;
                snapshot.outgoingActiveCount = node.outgoingActiveCount;
                snapshot.incomingActive = node.incomingActive;
                snapshot.outgoingActive = node.outgoingActive;
                snapshot.incomingHistory = node.incomingHistory;
                snapshot.outgoingHistory = node.outgoingHistory;
                break;
            }
        }
    }

    const auto drawLane = [&g](juce::Rectangle<int> laneBounds,
                               const juce::String& title,
                               int totalCount,
                               int activeCount,
                               const std::array<std::uint8_t, 32>& history,
                               const std::array<std::uint8_t, 128>& activeNotes,
                               const std::vector<FabricAudioProcessor::IoEventSummary>& events,
                               juce::Colour accent) {
        drawPanel(g, laneBounds, juce::Colour::fromRGBA(255, 255, 255, 112), 20.0f);
        auto area = laneBounds.reduced(16, 14);

        auto laneHeader = area.removeFromTop(26);
        g.setColour(accent);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(title, laneHeader.removeFromLeft(120), juce::Justification::centredLeft, true);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(juce::String(totalCount) + " events", laneHeader, juce::Justification::centredRight, true);

        area.removeFromTop(4);

        auto summary = area.removeFromTop(26);
        auto summaryLeft = summary.removeFromLeft(summary.getWidth() / 2);
        auto chip = [](juce::Graphics& cg, juce::Rectangle<int> chipBounds, juce::String text, juce::Colour base) {
            cg.setColour(chipFillColour(base));
            cg.fillRoundedRectangle(chipBounds.toFloat(), 9.0f);
            cg.setColour(chipTextColour(base));
            cg.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            cg.drawText(text, chipBounds.reduced(8, 2), juce::Justification::centredLeft, true);
        };
        chip(g, summaryLeft.removeFromLeft(98), "Active " + juce::String(activeCount), accent);
        chip(g, summary.removeFromLeft(104), "Now " + juce::String(totalCount), accent.brighter(0.08f));

        area.removeFromTop(6);

        auto meterBounds = area.removeFromTop(76);
        drawPanel(g, meterBounds, juce::Colour::fromRGBA(255, 255, 255, 70), 16.0f);
        auto meter = meterBounds.reduced(12, 10);
        auto meterHeader = meter.removeFromTop(16);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("Activity", meterHeader, juce::Justification::centredLeft, true);
        auto chart = meter.reduced(0, 4);
        const auto maxValue = std::max<std::uint8_t>(1, *std::max_element(history.begin(), history.end()));
        const auto barWidth = juce::jmax(3, chart.getWidth() / static_cast<int>(history.size()));
        for (std::size_t index = 0; index < history.size(); ++index) {
            const auto value = history[index];
            const auto height = static_cast<int>(std::round((static_cast<float>(value) / static_cast<float>(maxValue)) * static_cast<float>(chart.getHeight())));
            auto bar = juce::Rectangle<int>(chart.getX() + static_cast<int>(index) * barWidth,
                chart.getBottom() - height,
                juce::jmax(2, barWidth - 1),
                juce::jmax(2, height));
            g.setColour(accent.withAlpha(index + 1 == history.size() ? 0.95f : 0.35f + (0.45f * static_cast<float>(index) / static_cast<float>(history.size()))));
            g.fillRoundedRectangle(bar.toFloat(), 2.0f);
        }

        area.removeFromTop(10);

        auto keyboardBounds = area.removeFromTop(92);
        drawPanel(g, keyboardBounds, juce::Colour::fromRGBA(255, 255, 255, 70), 16.0f);
        auto keyboard = keyboardBounds.reduced(12, 10);
        auto keyboardHeader = keyboard.removeFromTop(16);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("Active notes", keyboardHeader, juce::Justification::centredLeft, true);
        auto strip = keyboard.reduced(0, 6);
        const auto keyWidth = static_cast<float>(strip.getWidth()) / 128.0f;
        for (int note = 0; note < 128; ++note) {
            const bool isBlack = juce::MidiMessage::isMidiNoteBlack(note);
            auto key = juce::Rectangle<float>(strip.getX() + (static_cast<float>(note) * keyWidth),
                static_cast<float>(strip.getY()),
                std::max(1.0f, keyWidth - 0.5f),
                static_cast<float>(strip.getHeight()));
            g.setColour(isBlack ? juce::Colour::fromRGBA(30, 34, 38, 130) : juce::Colour::fromRGBA(255, 255, 255, 55));
            g.fillRoundedRectangle(key, 1.5f);
            const auto velocity = activeNotes[static_cast<std::size_t>(note)];
            if (velocity > 0) {
                const auto alpha = 0.35f + (0.55f * (static_cast<float>(velocity) / 127.0f));
                g.setColour(accent.withAlpha(alpha));
                g.fillRoundedRectangle(key.reduced(0.2f, 4.0f), 1.5f);
            }
        }
        for (int note = 0; note < 128; note += 12) {
            const auto x = strip.getX() + (static_cast<float>(note) * keyWidth);
            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 28));
            g.drawVerticalLine(static_cast<int>(std::round(x)), static_cast<float>(strip.getY()), static_cast<float>(strip.getBottom()));
        }

        area.removeFromTop(10);

        if (events.empty()) {
            g.setColour(kMuted.withAlpha(0.85f));
            g.setFont(juce::Font(juce::FontOptions(14.0f)));
            g.drawFittedText("No activity yet.", area, juce::Justification::centred, 1);
            return;
        }

        auto eventHeader = area.removeFromTop(16);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("Recent events", eventHeader, juce::Justification::centredLeft, true);
        area.removeFromTop(4);

        constexpr int rowHeight = 24;
        for (std::size_t index = 0; index < events.size(); ++index) {
            auto row = area.removeFromTop(rowHeight);
            if (row.isEmpty()) {
                break;
            }

            const auto& event = events[index];
            auto badge = row.removeFromLeft(10).reduced(0, 4);
            g.setColour(event.isNoteOn ? accent.brighter(0.1f)
                         : event.isNoteOff ? accent.darker(0.15f)
                                           : accent.withAlpha(0.7f));
            g.fillRoundedRectangle(badge.toFloat(), 3.0f);

            g.setColour(kInk);
            g.setFont(juce::Font(juce::FontOptions(15.0f)));
            g.drawText(event.text, row.reduced(10, 0), juce::Justification::centredLeft, true);
        }
    };

    drawLane(left, "Incoming", snapshot.incomingCount, snapshot.incomingActiveCount, snapshot.incomingHistory, snapshot.incomingActive, snapshot.incoming, familyColour("input"));
    drawLane(right, "Outgoing", snapshot.outgoingCount, snapshot.outgoingActiveCount, snapshot.outgoingHistory, snapshot.outgoingActive, snapshot.outgoing, familyColour("output"));
}

void FabricAudioProcessorEditor::ModulatorInspectorComponent::setSnapshots(std::vector<FabricAudioProcessor::ModulatorSnapshot> snapshots, juce::String inspectedModule)
{
    snapshots_ = std::move(snapshots);
    inspectedModule_ = std::move(inspectedModule);
    repaint();
}

void FabricAudioProcessorEditor::ModulatorInspectorComponent::mouseUp(const juce::MouseEvent& event)
{
    for (const auto& hit : hitRegions_) {
        if (hit.bounds.contains(event.getPosition())) {
            owner_.focusModulator(hit.moduleName);
            owner_.jumpToModulatorStage(hit.moduleName, hit.channel, hit.stage);
            return;
        }
    }
}

void FabricAudioProcessorEditor::ModulatorInspectorComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::transparentBlack);
    hitRegions_.clear();

    if (snapshots_.empty()) {
        g.setColour(kMuted.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(13.5f)));
        g.drawFittedText("No modulator in this patch.", getLocalBounds().reduced(12), juce::Justification::centred, 2);
        return;
    }

    const FabricAudioProcessor::ModulatorSnapshot* snapshot = &snapshots_.front();
    for (const auto& candidate : snapshots_) {
        if (candidate.moduleName == inspectedModule_) {
            snapshot = &candidate;
            break;
        }
    }

    auto bounds = getLocalBounds().reduced(8, 6);
    auto header = bounds.removeFromTop(26);
    const auto accent = familyColour("shape");

    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    g.drawText(snapshot->moduleName.toUpperCase(), header.removeFromLeft(126), juce::Justification::centredLeft, true);

    auto chip = juce::Rectangle<float>(static_cast<float>(header.getRight() - 122), static_cast<float>(header.getY() + 2), 122.0f, 18.0f);
    g.setColour(chipFillColour(accent));
    g.fillRoundedRectangle(chip, 9.0f);
    g.setColour(chipTextColour(accent));
    g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    const auto summary = snapshot->mode + (snapshot->overlapEnabled ? " / overlap" : " / direct");
    g.drawText(summary, chip.toNearestInt().reduced(8, 1), juce::Justification::centredLeft, true);

    bounds.removeFromTop(4);
    for (int channelIndex = 0; channelIndex < static_cast<int>(snapshot->channels.size()); ++channelIndex) {
        if (bounds.getHeight() < 34) {
            break;
        }

        const auto& channel = snapshot->channels[static_cast<std::size_t>(channelIndex)];
        auto row = bounds.removeFromTop(42);
        auto label = row.removeFromLeft(34);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("CH" + juce::String(channelIndex + 1), label, juce::Justification::centredLeft, true);

        auto meterArea = row.removeFromLeft(74).reduced(2, 8);
        drawPanel(g, meterArea, juce::Colour::fromRGBA(255, 255, 255, 70), 7.0f);
        auto fill = meterArea.reduced(2, 2);
        const auto normalized = juce::jlimit(0.0f, 1.0f, channel.level);
        fill.setWidth(static_cast<int>(std::round(fill.getWidth() * normalized)));
        g.setColour(accent.withAlpha(0.82f));
        g.fillRoundedRectangle(fill.toFloat(), 5.0f);

        auto stageArea = row.reduced(2, 6);
        const int gap = 4;
        const int stageWidth = juce::jmax(14, (stageArea.getWidth() - (12 * gap)) / 13);
        for (int stage = 1; stage <= 13; ++stage) {
            auto chipBounds = stageArea.removeFromLeft(stageWidth);
            if (stage < 13) {
                stageArea.removeFromLeft(gap);
            }

            const bool active = std::find(channel.activeStages.begin(), channel.activeStages.end(), stage) != channel.activeStages.end();
            g.setColour(active ? chipFillColour(accent).withAlpha(0.95f) : juce::Colour::fromRGBA(255, 255, 255, 72));
            g.fillRoundedRectangle(chipBounds.toFloat(), 7.0f);
            g.setColour(active ? chipTextColour(accent) : kMuted);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText(juce::String(stage), chipBounds, juce::Justification::centred, true);
            hitRegions_.push_back({ snapshot->moduleName, channelIndex + 1, stage, chipBounds });
        }
    }
}

void FabricAudioProcessorEditor::GraphPreviewComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::transparentBlack);
    hitRegions_.clear();

    auto bounds = getLocalBounds().reduced(8);
    drawPanel(g, bounds, kPanel, 22.0f);
    bounds.reduce(18, 18);

    if (snapshot_.nodes.empty()) {
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawFittedText("Graph preview appears here after a successful compile.", bounds, juce::Justification::centred, 2);
        return;
    }

    const int nodeWidth = juce::jmin(204, bounds.getWidth() - 32);
    const int nodeHeight = 60;
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
        const auto fullRect = rect;
        drawPanel(g, rect, juce::Colour::fromRGBA(255, 255, 255, 170), 16.0f);

        auto accent = rect.removeFromLeft(6);
        g.setColour(familyColour(node.family));
        g.fillRoundedRectangle(accent.toFloat(), 3.0f);

        auto text = rect.reduced(12, 8);
        auto controls = juce::Rectangle<int>(fullRect.getRight() - 68, fullRect.getY() + 7, 52, 18);
        auto bypassButton = controls.removeFromLeft(24);
        controls.removeFromLeft(4);
        auto muteButton = controls.removeFromLeft(24);

        const auto drawModeButton = [&](juce::Rectangle<int> buttonBounds, juce::String label, bool active, juce::Colour base) {
            g.setColour(active ? chipFillColour(base).withAlpha(0.9f) : juce::Colour::fromRGBA(255, 255, 255, 108));
            g.fillRoundedRectangle(buttonBounds.toFloat(), 8.0f);
            g.setColour(active ? chipTextColour(base) : kMuted);
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText(label, buttonBounds, juce::Justification::centred, true);
        };
        const auto baseColour = familyColour(node.family);
        drawModeButton(bypassButton, "B", node.mode == FabricAudioProcessor::NodeProcessingMode::bypass, baseColour);
        drawModeButton(muteButton, "M", node.mode == FabricAudioProcessor::NodeProcessingMode::mute, juce::Colour::fromRGB(170, 82, 72));

        g.setColour(kInk);
        g.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
        g.drawText(node.name, text.removeFromTop(22), juce::Justification::centredLeft, true);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText(node.family + " / " + node.kind, text.removeFromTop(16), juce::Justification::centredLeft, true);

        if (node.detail.isNotEmpty()) {
            const auto chipWidth = juce::jmin(132, 42 + static_cast<int>(node.detail.length()) * 7);
            auto chip = juce::Rectangle<float>(static_cast<float>(text.getX()),
                static_cast<float>(text.getY() + 1),
                static_cast<float>(chipWidth),
                16.0f);
            const auto chipColour = familyColour(node.family);
            g.setColour(chipFillColour(chipColour));
            g.fillRoundedRectangle(chip, 8.0f);
            g.setColour(chipTextColour(chipColour));
            g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
            g.drawText(node.detail, chip.toNearestInt().reduced(7, 1), juce::Justification::centredLeft, true);
        }

        hitRegions_[node.name] = { fullRect, bypassButton, muteButton };
    }
}

FabricAudioProcessorEditor::FabricAudioProcessorEditor(FabricAudioProcessor& audioProcessor)
    : AudioProcessorEditor(&audioProcessor)
    , processor_(audioProcessor)
    , tokeniser_(std::make_unique<juce::CPlusPlusCodeTokeniser>())
{
    titleLabel_.setText("Fabric", juce::dontSendNotification);
    titleLabel_.setColour(juce::Label::textColourId, kInk);
    titleLabel_.setFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    stateSummaryLabel_.setColour(juce::Label::textColourId, kMuted.withAlpha(0.95f));
    stateSummaryLabel_.setJustificationType(juce::Justification::centredRight);
    stateSummaryLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(stateSummaryLabel_);

    tutorialBox_.setTextWhenNothingSelected("Lessons");
    tutorialBox_.setJustificationType(juce::Justification::centredLeft);
    styleHeaderComboBox(tutorialBox_);
    tutorialBox_.onChange = [this] {
        selectedTutorialIndex_ = juce::jmax(0, tutorialBox_.getSelectedItemIndex());
        updateTutorialSummary();
    };
    addAndMakeVisible(tutorialBox_);

    ioModuleBox_.setTextWhenNothingSelected("Whole Patch");
    ioModuleBox_.setJustificationType(juce::Justification::centredLeft);
    styleHeaderComboBox(ioModuleBox_);
    ioModuleBox_.onChange = [this] {
        const auto selected = ioModuleBox_.getSelectedItemIndex();
        if (selected <= 0) {
            selectedIoModuleName_.clear();
        } else if (selected - 1 < static_cast<int>(ioSnapshot_.nodes.size())) {
            selectedIoModuleName_ = ioSnapshot_.nodes[static_cast<std::size_t>(selected - 1)].moduleName;
        }
        ioVisualiser_.repaint();
    };
    addAndMakeVisible(ioModuleBox_);

    tutorialLoadButton_.setButtonText("Open Lesson");
    tutorialLoadButton_.onClick = [this] { loadSelectedTutorial(); };
    styleSecondaryButton(tutorialLoadButton_);
    addAndMakeVisible(tutorialLoadButton_);

    loadButton_.onClick = [this] { loadPatchFromFile(); };
    styleSecondaryButton(loadButton_);
    addAndMakeVisible(loadButton_);

    saveButton_.onClick = [this] { savePatchToFile(); };
    styleSecondaryButton(saveButton_);
    addAndMakeVisible(saveButton_);

    clockModeButton_.onClick = [this] {
        processor_.setSyncToTransportEnabled(!processor_.isSyncToTransportEnabled());
        refreshFromProcessor();
    };
    styleSecondaryButton(clockModeButton_);
    addAndMakeVisible(clockModeButton_);

    compileButton_.onClick = [this] { requestCompileNow(); };
    stylePrimaryButton(compileButton_);
    addAndMakeVisible(compileButton_);

    document_.replaceAllContent(processor_.getScriptText());
    document_.addListener(this);

    editor_ = std::make_unique<juce::CodeEditorComponent>(document_, tokeniser_.get());
    editor_->setScrollbarThickness(12);
    editor_->setLineNumbersShown(true);
    editor_->setFont(juce::Font(juce::FontOptions("Menlo", kEditorFontSize, juce::Font::plain)));
    editor_->setColourScheme(makeFabricColourScheme());
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

    helpSummary_.setMultiLine(true);
    helpSummary_.setReadOnly(true);
    helpSummary_.setText(R"(bits examples
status cc
cc 1..8
except cc 64
channel 1

status notes
note C3..C5
except note D4
velocity 90..127
except velocity 120..127)", juce::dontSendNotification);
    helpSummary_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    helpSummary_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    helpSummary_.setColour(juce::TextEditor::textColourId, kMuted.interpolatedWith(juce::Colours::black, 0.12f));
    helpSummary_.setFont(juce::Font(juce::FontOptions("Menlo", 12.5f, juce::Font::plain)));
    addAndMakeVisible(helpSummary_);

    lessonSummary_.setMultiLine(true);
    lessonSummary_.setReadOnly(true);
    lessonSummary_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    lessonSummary_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    lessonSummary_.setColour(juce::TextEditor::textColourId, kMuted.interpolatedWith(juce::Colours::black, 0.08f));
    lessonSummary_.setFont(juce::Font(juce::FontOptions("Menlo", 13.5f, juce::Font::plain)));
    addAndMakeVisible(lessonSummary_);

    graphViewport_.setViewedComponent(&graphPreview_, false);
    graphViewport_.setScrollBarsShown(true, false);
    graphViewport_.setScrollBarThickness(10);
    graphViewport_.setColour(juce::ScrollBar::thumbColourId, juce::Colour::fromRGB(76, 184, 235));
    graphViewport_.setColour(juce::ScrollBar::trackColourId, juce::Colour::fromRGBA(255, 255, 255, 40));
    addAndMakeVisible(graphViewport_);
    addAndMakeVisible(ioVisualiser_);
    addAndMakeVisible(modulatorInspector_);
    addAndMakeVisible(sectionPulseOverlay_);
    sectionPulseOverlay_.setInterceptsMouseClicks(false, false);
    ioVisualiser_.setVisible(false);
    modulatorInspector_.setVisible(false);
    lessonSummary_.setVisible(false);

    setWantsKeyboardFocus(true);
    addKeyListener(this);
    editor_->addKeyListener(this);
    diagnosticsList_.addKeyListener(this);
    tutorialBox_.addKeyListener(this);
    ioModuleBox_.addKeyListener(this);
    tutorialLoadButton_.addKeyListener(this);
    compileButton_.addKeyListener(this);
    clockModeButton_.addKeyListener(this);
    loadButton_.addKeyListener(this);
    saveButton_.addKeyListener(this);

    rebuildTutorialBrowser();
    rebuildIoModuleBrowser();
    refreshFromProcessor();
    applyViewMode();
    setSize(1110, 690);
    startTimer(120);
}

FabricAudioProcessorEditor::~FabricAudioProcessorEditor()
{
    document_.removeListener(this);
}

void FabricAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    const auto layout = computeLayout(getLocalBounds(), static_cast<int>(sectionControls_.size()), !diagnostics_.empty(), !modulatorSnapshots_.empty());
    drawPanel(g, layout.headerPanel, juce::Colour::fromRGBA(255, 255, 255, 90), 22.0f);
    if (!layout.sectionPanel.isEmpty()) {
        drawPanel(g, layout.sectionPanel, juce::Colour::fromRGBA(255, 255, 255, 70), 20.0f);
    }
    if (viewMode_ == ViewMode::io) {
        drawPanel(g, contentUnion(layout), juce::Colour::fromRGBA(255, 255, 255, 70), 24.0f);
    } else if (viewMode_ == ViewMode::lessons) {
        drawPanel(g, contentUnion(layout), juce::Colour::fromRGBA(255, 255, 255, 70), 24.0f);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
        g.drawText("LESSONS", contentUnion(layout).withTrimmedBottom(contentUnion(layout).getHeight() - 26).reduced(20, 0), juce::Justification::centredLeft, true);
    } else {
        drawPanel(g, layout.editorPanel, kCodeBg, 24.0f);
        drawPanel(g, layout.graphPanel, kPanelStrong, 24.0f);
        if (!layout.diagnosticsPanel.isEmpty()) {
            drawPanel(g, layout.diagnosticsPanel, juce::Colour::fromRGBA(255, 255, 255, 110), 20.0f);
        }
        drawPanel(g, layout.helpPanel, juce::Colour::fromRGBA(255, 255, 255, 110), 20.0f);

        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
        g.drawText("PATCH", layout.editorPanel.withTrimmedBottom(layout.editorPanel.getHeight() - 26).reduced(16, 0), juce::Justification::centredLeft, true);
        g.drawText("FLOW", layout.graphPanel.withTrimmedBottom(layout.graphPanel.getHeight() - 26).reduced(16, 0), juce::Justification::centredLeft, true);
        if (!layout.diagnosticsPanel.isEmpty()) {
            g.drawText("DIAGNOSTICS", layout.diagnosticsPanel.withTrimmedBottom(layout.diagnosticsPanel.getHeight() - 24).reduced(16, 0), juce::Justification::centredLeft, true);
        }
        g.drawText(modulatorSnapshots_.empty() ? "CHEATSHEET" : "MODULATOR",
            layout.helpPanel.withTrimmedBottom(layout.helpPanel.getHeight() - 24).reduced(16, 0),
            juce::Justification::centredLeft,
            true);
    }
}

void FabricAudioProcessorEditor::resized()
{
    const auto layout = computeLayout(getLocalBounds(), static_cast<int>(sectionControls_.size()), !diagnostics_.empty(), !modulatorSnapshots_.empty());

    auto header = layout.headerPanel.reduced(22, 14);
    const auto controlHeight = 40;
    titleLabel_.setBounds(header.removeFromLeft(168));

    auto controls = header.removeFromRight(804);
    controls = controls.withTrimmedTop((controls.getHeight() - controlHeight) / 2).withHeight(controlHeight);

    if (viewMode_ == ViewMode::io) {
        ioModuleBox_.setBounds(controls.removeFromLeft(224));
        controls.removeFromLeft(14);
        tutorialBox_.setBounds({});
        tutorialLoadButton_.setBounds({});
    } else {
        tutorialBox_.setBounds(controls.removeFromLeft(224));
        controls.removeFromLeft(10);
        tutorialLoadButton_.setBounds(controls.removeFromLeft(132));
        controls.removeFromLeft(14);
        ioModuleBox_.setBounds({});
    }
    loadButton_.setBounds(controls.removeFromLeft(82));
    controls.removeFromLeft(8);
    saveButton_.setBounds(controls.removeFromLeft(82));
    controls.removeFromLeft(14);
    clockModeButton_.setBounds(controls.removeFromLeft(156));
    controls.removeFromLeft(12);
    compileButton_.setBounds(controls.removeFromLeft(170));

    if (stateSummaryLabel_.isVisible()) {
        auto summary = header.reduced(10, 8);
        stateSummaryLabel_.setBounds(summary);
    } else {
        stateSummaryLabel_.setBounds({});
    }

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

    auto editorBounds = layout.editorPanel.reduced(20, 20);
    editorBounds.removeFromTop(30);
    editor_->setBounds(editorBounds);
    moduleOverlay_.setBounds(editor_->getLocalBounds());
    diagnosticOverlay_.setBounds(editor_->getLocalBounds());
    auto graphViewportBounds = layout.graphPanel.reduced(12, 30);
    graphViewport_.setBounds(graphViewportBounds);
    const auto graphContentHeight = juce::jmax(graphViewportBounds.getHeight(),
        56 + static_cast<int>(graphSnapshot_.nodes.size()) * 80);
    graphPreview_.setBounds(0, 0, graphViewportBounds.getWidth() - 10, graphContentHeight);
    ioVisualiser_.setBounds(contentUnion(layout).reduced(10, 10));
    lessonSummary_.setBounds(contentUnion(layout).reduced(22, 32));

    if (!layout.diagnosticsPanel.isEmpty()) {
        auto diagnosticsContent = layout.diagnosticsPanel.reduced(12, 26);
        diagnosticsContent.removeFromTop(22);
        auto listArea = diagnosticsContent.removeFromTop(70);
        diagnosticsList_.setBounds(listArea);
        diagnosticsSummary_.setBounds(diagnosticsContent.withTrimmedTop(8));
    } else {
        diagnosticsList_.setBounds({});
        diagnosticsSummary_.setBounds({});
    }

    auto helpContent = layout.helpPanel.reduced(12, 26);
    helpContent.removeFromTop(22);
    helpSummary_.setBounds(helpContent);
    modulatorInspector_.setBounds(helpContent);

    sectionPulseOverlay_.setBounds(getLocalBounds());
    applyViewMode();
}

void FabricAudioProcessorEditor::timerCallback()
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
        graphSnapshot_ = processor_.getGraphSnapshot();
        ioSnapshot_ = processor_.getIoSnapshot();
        rebuildIoModuleBrowser();
        modulatorSnapshots_ = processor_.getModulatorSnapshots();
        updateStateTracking(graphSnapshot_);
        graphPreview_.setSnapshot(graphSnapshot_);
        ioVisualiser_.setSnapshot(ioSnapshot_);
        modulatorInspector_.setSnapshots(modulatorSnapshots_, inspectedModulatorName_);
        updateTutorialSummary();
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
    }
}

void FabricAudioProcessorEditor::codeDocumentTextInserted(const juce::String&, int)
{
    lastEditTimeMs_ = juce::Time::getMillisecondCounterHiRes();
    pendingDebouncedCompile_ = true;
    moduleOverlay_.repaint();
}

void FabricAudioProcessorEditor::codeDocumentTextDeleted(int, int)
{
    lastEditTimeMs_ = juce::Time::getMillisecondCounterHiRes();
    pendingDebouncedCompile_ = true;
    moduleOverlay_.repaint();
}

bool FabricAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::tabKey) {
        toggleViewMode();
        return true;
    }

    return AudioProcessorEditor::keyPressed(key);
}

bool FabricAudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key.getKeyCode() == juce::KeyPress::tabKey) {
        toggleViewMode();
        return true;
    }

    return false;
}

int FabricAudioProcessorEditor::getNumRows()
{
    return static_cast<int>(diagnostics_.size());
}

void FabricAudioProcessorEditor::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
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

void FabricAudioProcessorEditor::selectedRowsChanged(int lastRowSelected)
{
    jumpToDiagnostic(lastRowSelected);
}

void FabricAudioProcessorEditor::requestCompileNow()
{
    pendingDebouncedCompile_ = false;
    processor_.requestCompile(document_.getAllContent());
}

void FabricAudioProcessorEditor::rebuildTutorialBrowser()
{
    tutorialNames_ = processor_.getTutorialNames();
    tutorialBox_.clear(juce::dontSendNotification);
    for (int index = 0; index < tutorialNames_.size(); ++index) {
        tutorialBox_.addItem(tutorialNames_[index], index + 1);
    }
    if (tutorialNames_.isEmpty()) {
        selectedTutorialIndex_ = -1;
        tutorialBox_.setText({}, juce::dontSendNotification);
        return;
    }

    selectedTutorialIndex_ = juce::jlimit(0, tutorialNames_.size() - 1, selectedTutorialIndex_);
    tutorialBox_.setSelectedItemIndex(selectedTutorialIndex_, juce::dontSendNotification);
}

void FabricAudioProcessorEditor::rebuildIoModuleBrowser()
{
    ioModuleBox_.clear(juce::dontSendNotification);
    ioModuleBox_.addItem("Whole Patch", 1);

    int selectedIndex = 0;
    for (int index = 0; index < static_cast<int>(ioSnapshot_.nodes.size()); ++index) {
        const auto& node = ioSnapshot_.nodes[static_cast<std::size_t>(index)];
        ioModuleBox_.addItem(node.moduleName, index + 2);
        if (node.moduleName == selectedIoModuleName_) {
            selectedIndex = index + 1;
        }
    }

    if (selectedIndex == 0 && selectedIoModuleName_.isNotEmpty()) {
        selectedIoModuleName_.clear();
    }

    ioModuleBox_.setSelectedItemIndex(selectedIndex, juce::dontSendNotification);
}

void FabricAudioProcessorEditor::updateTutorialSummary()
{
    auto tutorial = processor_.getTutorial(selectedTutorialIndex_);
    if (!tutorial.has_value()) {
        lessonSummary_.setText({}, juce::dontSendNotification);
        return;
    }

    juce::String summary;
    summary << tutorial->name << "\n\n";
    summary << tutorial->summary << "\n\n";
    summary << "Load Lesson replaces the current patch with this tutorial.\n";
    summary << "Press Tab to cycle editor -> I/O -> lessons.\n";
    summary << "Click graph nodes to jump to code.\n";
    summary << "Use the Lessons dropdown in the header to choose a tutorial.\n";
    lessonSummary_.setText(summary, juce::dontSendNotification);
}

void FabricAudioProcessorEditor::loadSelectedTutorial()
{
    auto tutorial = processor_.getTutorial(selectedTutorialIndex_);
    if (!tutorial.has_value()) {
        return;
    }

    document_.replaceAllContent(tutorial->script);
    pendingDebouncedCompile_ = false;
    processor_.requestCompile(tutorial->script);
    updateTutorialSummary();
}

void FabricAudioProcessorEditor::refreshFromProcessor()
{
    seenRevision_ = processor_.getUiRevision();
    const auto latestScript = processor_.getScriptText();
    for (int index = 0; index < tutorialNames_.size(); ++index) {
        const auto tutorial = processor_.getTutorial(index);
        if (tutorial.has_value() && tutorial->script == latestScript) {
            selectedTutorialIndex_ = index;
            tutorialBox_.setSelectedItemIndex(index, juce::dontSendNotification);
            break;
        }
    }
    if (document_.getAllContent() != latestScript) {
        document_.replaceAllContent(latestScript);
    }
    diagnostics_ = processor_.getDiagnostics();
    diagnosticsList_.setVisible(!diagnostics_.empty());
    diagnosticsSummary_.setVisible(!diagnostics_.empty());
    graphSnapshot_ = processor_.getGraphSnapshot();
    ioSnapshot_ = processor_.getIoSnapshot();
    rebuildIoModuleBrowser();
    modulatorSnapshots_ = processor_.getModulatorSnapshots();
    const auto inspectedStillExists = std::any_of(modulatorSnapshots_.begin(), modulatorSnapshots_.end(), [this](const auto& snapshot) {
        return snapshot.moduleName == inspectedModulatorName_;
    });
    if ((!inspectedStillExists || inspectedModulatorName_.isEmpty()) && !modulatorSnapshots_.empty()) {
        inspectedModulatorName_ = modulatorSnapshots_.front().moduleName;
    } else if (modulatorSnapshots_.empty()) {
        inspectedModulatorName_.clear();
    }
    updateStateTracking(graphSnapshot_);
    sectionControls_ = processor_.getSectionControls();
    rebuildSectionButtons();
    updateSectionButtonStates();
    diagnosticsList_.updateContent();
    diagnosticsList_.repaint();
    diagnosticsSummary_.setText(diagnostics_.empty() ? juce::String() : processor_.getDiagnosticsText(), juce::dontSendNotification);
    graphPreview_.setSnapshot(graphSnapshot_);
    ioVisualiser_.setSnapshot(ioSnapshot_);
    modulatorInspector_.setSnapshots(modulatorSnapshots_, inspectedModulatorName_);
    updateTutorialSummary();
    moduleOverlay_.repaint();
    diagnosticOverlay_.repaint();

    const auto summaryText = stateSummaryText();
    stateSummaryLabel_.setVisible(summaryText.isNotEmpty());
    stateSummaryLabel_.setText(summaryText, juce::dontSendNotification);
    clockModeButton_.setButtonText(processor_.isSyncToTransportEnabled() ? "Logic Sync" : "Free Clock");

    if (!diagnostics_.empty()) {
        diagnosticsList_.selectRow(0);
    }

    resized();
}

void FabricAudioProcessorEditor::applyViewMode()
{
    const auto showingEditor = viewMode_ == ViewMode::editor;
    const auto showingIo = viewMode_ == ViewMode::io;
    const auto showingLessons = viewMode_ == ViewMode::lessons;
    if (editor_ != nullptr) {
        editor_->setVisible(showingEditor);
    }
    tutorialBox_.setVisible(!showingIo);
    tutorialLoadButton_.setVisible(!showingIo);
    ioModuleBox_.setVisible(showingIo);
    graphViewport_.setVisible(showingEditor);
    diagnosticsList_.setVisible(showingEditor && !diagnostics_.empty());
    diagnosticsSummary_.setVisible(showingEditor && !diagnostics_.empty());
    helpSummary_.setVisible(showingEditor && modulatorSnapshots_.empty());
    modulatorInspector_.setVisible(showingEditor && !modulatorSnapshots_.empty());
    ioVisualiser_.setVisible(showingIo);
    lessonSummary_.setVisible(showingLessons);
}

void FabricAudioProcessorEditor::toggleViewMode()
{
    switch (viewMode_) {
    case ViewMode::editor:
        viewMode_ = ViewMode::io;
        break;
    case ViewMode::io:
        viewMode_ = ViewMode::lessons;
        break;
    case ViewMode::lessons:
    default:
        viewMode_ = ViewMode::editor;
        break;
    }
    applyViewMode();
    repaint();
}

void FabricAudioProcessorEditor::loadPatchFromFile()
{
    auto startFile = lastPatchFile_.exists() ? lastPatchFile_ : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    fileChooser_ = std::make_unique<juce::FileChooser>("Load Fabric patch", startFile, "*.pulse");
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (!file.existsAsFile()) {
                return;
            }

            const auto content = file.loadFileAsString();
            if (content.isEmpty()) {
                return;
            }

            lastPatchFile_ = file;
            document_.replaceAllContent(content);
            pendingDebouncedCompile_ = false;
            processor_.requestCompile(content);
        });
}

void FabricAudioProcessorEditor::savePatchToFile()
{
    auto startFile = lastPatchFile_.exists()
        ? lastPatchFile_
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("patch.pulse");
    fileChooser_ = std::make_unique<juce::FileChooser>("Save Fabric patch", startFile, "*.pulse");
    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file == juce::File()) {
                return;
            }
            if (!file.hasFileExtension(".pulse")) {
                file = file.withFileExtension(".pulse");
            }

            if (file.replaceWithText(document_.getAllContent())) {
                lastPatchFile_ = file;
            }
        });
}

void FabricAudioProcessorEditor::jumpToDiagnostic(int row)
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

void FabricAudioProcessorEditor::rebuildSectionButtons()
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

void FabricAudioProcessorEditor::updateSectionButtonStates()
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

double FabricAudioProcessorEditor::pulseAmountForModule(int moduleIndex) const
{
    if (moduleIndex < 0 || moduleIndex >= static_cast<int>(pulseAmounts_.size())) {
        return 0.0;
    }

    return pulseAmounts_[static_cast<std::size_t>(moduleIndex)];
}

void FabricAudioProcessorEditor::updateStateTracking(const FabricAudioProcessor::GraphSnapshot& snapshot)
{
    for (const auto& node : snapshot.nodes) {
        if (node.detail.isEmpty()) {
            continue;
        }

        const auto previous = lastNodeDetails_.find(node.name);
        if (previous == lastNodeDetails_.end() || previous->second != node.detail) {
            auto& history = nodeStateHistory_[node.name];
            if (history.isEmpty() || history[history.size() - 1] != node.detail) {
                history.add(node.detail);
                while (history.size() > 4) {
                    history.remove(0);
                }
            }
            lastNodeDetails_[node.name] = node.detail;
        }
    }
}

juce::String FabricAudioProcessorEditor::stateSummaryText() const
{
    juce::StringArray parts;
    for (const auto& node : graphSnapshot_.nodes) {
        if (node.detail.isEmpty()) {
            continue;
        }
        juce::String part = node.name + ": " + node.detail;
        if (const auto history = nodeStateHistory_.find(node.name); history != nodeStateHistory_.end() && history->second.size() > 1) {
            juce::StringArray tail = history->second;
            part << "  [" << tail.joinIntoString(" > ") << "]";
        }
        parts.add(part);
    }
    return parts.isEmpty() ? juce::String() : parts.joinIntoString("   ");
}

void FabricAudioProcessorEditor::jumpToModuleLine(int line)
{
    if (editor_ == nullptr || line <= 0) {
        return;
    }

    const auto lineIndex = juce::jmax(0, line - 1);
    juce::CodeDocument::Position start(document_, lineIndex, 0);
    juce::CodeDocument::Position end(document_, lineIndex, document_.getLine(lineIndex).length());
    editor_->grabKeyboardFocus();
    editor_->moveCaretTo(start, false);
    editor_->selectRegion(start, end);
    editor_->scrollToKeepCaretOnScreen();
}

void FabricAudioProcessorEditor::jumpToModulatorStage(const juce::String& moduleName, int channel, int stage)
{
    if (editor_ == nullptr || moduleName.isEmpty() || channel <= 0 || stage <= 0) {
        return;
    }

    bool inTargetModule = false;
    const auto lineCount = document_.getNumLines();
    for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
        const auto trimmed = document_.getLine(lineIndex).trim();
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

        if (!inTargetModule) {
            if (tokens.size() >= 3 && tokens[0] == "shape" && tokens[1] == "modulator" && tokens[2] == moduleName) {
                inTargetModule = true;
            }
            continue;
        }

        if (trimmed == "end") {
            break;
        }

        if (tokens[0] != "stage") {
            continue;
        }

        const bool stageMatches = tokens.size() >= 2 && tokens[1].getIntValue() == stage;
        const bool channelMatches = trimmed.containsWholeWord("channel") && trimmed.containsWholeWord(juce::String(channel));
        if (stageMatches && channelMatches) {
            jumpToModuleLine(lineIndex + 1);
            return;
        }
    }
}

void FabricAudioProcessorEditor::focusModulator(const juce::String& moduleName)
{
    inspectedModulatorName_ = moduleName;
    modulatorInspector_.setSnapshots(modulatorSnapshots_, inspectedModulatorName_);
}

void FabricAudioProcessorEditor::cycleNodeMode(const juce::String& moduleName, FabricAudioProcessor::NodeProcessingMode mode)
{
    const auto current = processor_.getNodeProcessingMode(moduleName);
    const auto next = (current == mode) ? FabricAudioProcessor::NodeProcessingMode::normal : mode;
    processor_.setNodeProcessingMode(moduleName, next);
}
