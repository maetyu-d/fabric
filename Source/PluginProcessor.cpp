#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <utility>

namespace {

struct FactoryPresetData {
    const char* name;
    const char* script;
};

juce::String makeDefaultScript()
{
    return R"pulse(
patch fabric_default

scale D dorian

input midi keys
  channel 1
end

generate clock metro
  every 1/16
end

transform quantize in_key
  scale D dorian
end

transform arp arp1
  gate 70%
end

output midi out
end

connect keys -> in_key
connect in_key -> arp1
connect metro -> arp1.trigger
connect arp1 -> out
end
)pulse";
}

std::vector<FactoryPresetData> makeFactoryPresets()
{
    return {
        { "Default Arp",
            R"pulse(
patch fabric_default

scale D dorian

input midi keys
  channel 1
end

generate clock metro
  every 1/16
end

transform quantize in_key
  scale D dorian
end

transform arp arp1
  gate 70%
end

output midi out
end

connect keys -> in_key
connect in_key -> arp1
connect metro -> arp1.trigger
connect arp1 -> out
end
)pulse" },
        { "Random Walk",
            R"pulse(
patch random_machine

generate clock metro
  every 1/16
end

generate random chance1
  notes D3 F3 A3 C4 E4
  mode walk
  pass 70%
  avoid repeat
  max step 5
  bias center 0.8
  seed 42
end

project to_notes notes1
  quantize D dorian
  gate 55%
end

output midi out
end

connect metro -> chance1.trigger
connect chance1 -> notes1
connect notes1 -> out
end
)pulse" },
        { "Pattern Machine",
            R"pulse(
patch pattern_machine

generate clock metro
  every 1/16
end

generate pattern seq1
  notes C3 D3 F3 A3
  order up_down
end

project to_notes notes1
  quantize C major
  gate 60%
end

output midi out
end

connect metro -> seq1.trigger
connect seq1 -> notes1
connect notes1 -> out
end
)pulse" },
        { "Motion Notes",
            R"pulse(
patch motion_to_notes

input midi keys
end

generate clock metro
  every 1/16
end

analyze motion motion1
  channels 8
  space 0.4
  clocked on
end

project to_notes notes1
  quantize D minor
  range C2..C5
  velocity 100
end

output midi out
end

connect keys -> motion1
connect metro -> motion1.clock
connect motion1.speed -> notes1.values
connect motion1.even -> notes1.gate
connect notes1 -> out
end
)pulse" },
        { "Stage Modulator",
            R"pulse(
patch lists_to_notes

input midi keys
  channel 1
end

generate clock metro
  every 1/8
end

shape stages mod1
  mode loop
  stage 1 level 0.2 time 80ms curve linear
  stage 2 level 0.9 time 120ms curve smooth
  stage 3 level 0.4 time 200ms curve exp
end

shape lists marf1
  pitch C3 E3 G3 Bb3
  time 80ms 140ms 220ms 500ms
  gate 30% 60% 90% 40%

  advance pitch on note
  advance time on threshold 0.7
  advance gate on random

  interpolate pitch on
  interpolate time on
end

project to_notes notes1
  scale D minor
  range C2..C6
  velocity 96
end

output midi out
end

connect keys -> marf1.note
connect metro -> marf1.random
connect mod1 -> marf1.threshold
connect marf1.pitch -> notes1
connect marf1.time -> notes1.time
connect marf1.gate -> notes1.gate
connect notes1 -> out
end
)pulse" },
        { "Quantized Motion Arp",
            R"pulse(
patch tuned_motion

scale D minor

input midi keys
  channel 1
end

transform quantize in_key
  scale D minor
end

transform arp arp1
  gate 65%
end

generate clock metro
  every 1/16
end

output midi out
end

connect keys -> in_key
connect in_key -> arp1
connect metro -> arp1.trigger
connect arp1 -> out
end
)pulse" },
        { "Fibonacci Smear",
            R"pulse(
patch fibonacci_smear

generate clock metro
  every 1/16
end

generate fibonacci fib1
  length 8
  map 0 2 3 5 7
end

project to_notes notes1
  scale D minor
  range C2..C5
  velocity 96
end

memory smear bergson
  keep 3
  weights 0.60 0.25 0.15
  drift weights 0.05
end

output midi out
end

connect metro -> fib1.trigger
connect fib1 -> notes1
connect notes1 -> bergson
connect bergson -> out
end
)pulse" },
        { "Warped Pitch Space",
            R"pulse(
patch warped_space

generate clock metro
  every 1/16
end

generate pattern roots
  notes C3 D3 G3 A3
  order up
end

transform warp warp1
  fold C near F#
  fold G near Db
  wormhole every random 3..6
  wormhole to A2 Eb4 F#4
  amount 1.0
  seed 17
end

project to_notes notes1
  scale D dorian
  range C2..C5
  velocity 100
end

output midi out
end

connect metro -> roots.trigger
connect roots -> warp1.pitch
connect warp1 -> notes1
connect notes1 -> out
end
)pulse" },
        { "Crystal Growth",
            R"pulse(
patch crystal_growth

generate clock metro
  every 1/16
end

generate progression plan
  targets tonic dominant tonic subdominant tonic
  lengths 2 2 2 2 4
end

generate growth crystal
  root C3
  ratios 3/2 5/4 7/4
  family perfect weight 1.4 ratios 3/2 4/3
  family color weight 0.9 ratios 5/4 7/4 9/8
  register mid
  target tonic
  add when stable
  prune when unstable
  prune strength 2
  fold octaves just
  map growth to density
  density drives rate
  max notes 8
end

project to_notes notes1
  scale C lydian
  range C2..C5
  velocity 98
end

output midi out
end

connect metro -> crystal.trigger
connect metro -> plan.trigger
connect plan -> crystal.phrase
connect crystal -> notes1
connect crystal.density -> notes1.velocity
connect crystal.gate -> notes1.gate
connect notes1 -> out
end
)pulse" },
        { "Multi-Agent Swarm",
            R"pulse(
patch swarm_machine

generate clock metro
  every 1/16
end

generate swarm flock
  agents 6
  center D3
  cluster 0.7
  agent 1 anchor
  agent 2 follower
  agent 3 follower
  agent 4 rebel
  agent 5 rebel
  agent 6 follower
end

project to_notes notes1
  scale D dorian
  range C2..C5
  velocity 98
  gate 58%
end

output midi out
end

connect metro -> flock.trigger
connect flock -> notes1
connect notes1 -> out
end
)pulse" },
        { "Constraint Collapse",
            R"pulse(
patch collapse_machine

generate clock metro
  every 1/16
end

generate progression form
  targets tonic dominant tonic subdominant tonic
  lengths 2 2 2 2 4
end

generate section scenes
  start verse
  section verse tonic 2 dominant 2
  section chorus dominant 2 tonic 2
  section bridge subdominant 2 tonic 2
end

generate collapse engine1
  notes C3 D3 Eb3 G3 A3 Bb3
  root C3
  ruleset stable avoid tonic no repeated intervals max 2 semitone steps prefer center 0.65 target tonic reform gentle recover to target follow phrase
  ruleset broken from stable avoid target max 5 semitone steps prefer center 0.2 target dominant reform wild recover to dominant mutate 4
  ruleset release from stable avoid subdominant max 3 semitone steps prefer center 0.8 reform rotate_root recover to tonic
  use stable
  on collapse cycle broken release stable
  on section verse use stable
  on section chorus blend broken 0.65
  on section bridge blend release 0.75
  on collapse mutate 2
  seed 13
end

project to_notes notes1
  scale C minor
  range C2..C5
  velocity 100
  gate 62%
end

output midi out
end

connect metro -> engine1.trigger
connect metro -> form.trigger
connect metro -> scenes.trigger
connect form -> engine1.phrase
connect scenes.section -> engine1.section
connect engine1 -> notes1
connect notes1 -> out
end
)pulse" },
        { "Bands Delay Arp",
            R"pulse(
patch split_delay_arp

tempo 120
scale D minor

input midi keys
  channel 1
end

generate clock metro
  every 1/16
end

transform split bands
  by note
  low below C3
  mid C3..B4
  high above B4
end

transform delay low_late
  time 40ms
end

transform arp arp1
  gate 70%
end

output midi out
end

connect keys -> bands
connect bands.low -> low_late
connect bands.mid -> arp1
connect metro -> arp1.trigger
connect low_late -> out
connect arp1 -> out
connect bands.high -> out
end
)pulse" },
        { "Bouncing Ball",
            R"pulse(
patch bouncing_ball

generate clock metro
  every 1/4
end

generate pattern seed
  notes C3
end

project to_notes notes1
  scale C minor
  gate 85%
  velocity 104
end

transform bounce ball
  count 8
  spacing 180ms -> 25ms
  velocity 112 -> 36
end

output midi out
end

connect metro -> seed.trigger
connect seed -> notes1
connect notes1 -> ball
connect ball -> out
end
)pulse" },
        { "MIDI Loop",
            R"pulse(
patch midi_loop

input midi keys
  channel 1
end

transform loop phrase1
  capture 1/16
  playback on
  overdub off
  quantize to 1/32
end

output midi out
end

connect keys -> phrase1
connect phrase1 -> out
end
)pulse" },
        { "Held Note Random",
            R"pulse(
patch random_held_notes

input midi keys
  channel 1
end

generate clock metro
  every 1/16
end

generate random chance1
  from held notes
  mode walk
  pass 75%
  avoid repeat
  max step 7
  bias center 0.6
  seed 42
end

project to_notes notes1
  quantize D dorian
  gate 60%
end

output midi out
end

connect keys -> chance1.in
connect metro -> chance1.trigger
connect chance1 -> notes1
connect notes1 -> out
end
)pulse" },
        { "Cut-Up Machine",
            R"pulse(
patch cutup_machine

input midi keys
  channel 1
end

generate clock metro
  every 1/16
end

memory cutup burroughs
  capture 1/16
  slice 2
  keep continuity 0.35
  favor harmonic
  seed 42
end

output midi out
end

connect keys -> burroughs.in
connect metro -> burroughs.trigger
connect burroughs -> out
end
)pulse" },
        { "Section Form",
            R"pulse(
patch section_controlled

generate clock metro
  every 1/16
end

generate clock form_clock
  every 1/8
end

generate progression chords
  targets tonic dominant subdominant tonic
  lengths 2 2 2 2
end

generate section form
  start verse
  section verse tonic 2 dominant 2
  section chorus tonic 2 subdominant 2 dominant 2 tonic 2
end

project to_notes mover
  chord scale7
  invert 1
  spread 1 octave
  movement nearest
  arrive on 4
  cadence close
end

output midi out
end

connect metro -> chords.trigger
connect metro -> mover.trigger
connect form_clock -> form.advance
connect form -> mover.phrase
connect chords -> mover
connect mover -> out
end
)pulse" }
    };
}

juce::MidiMessage eventToMidiMessage(const pulse::Event& event)
{
    if (event.isNoteOn()) {
        return juce::MidiMessage::noteOn(event.channel(), event.noteNumber(), static_cast<juce::uint8>(event.velocityValue()));
    }

    if (event.isNoteOff()) {
        return juce::MidiMessage::noteOff(event.channel(), event.noteNumber(), static_cast<juce::uint8>(event.velocityValue()));
    }

    if (event.type == pulse::SignalType::midi && event.ints.size() >= 3) {
        const juce::uint8 data[] {
            static_cast<juce::uint8>(event.ints[0]),
            static_cast<juce::uint8>(event.ints[1]),
            static_cast<juce::uint8>(event.ints[2])
        };
        return juce::MidiMessage(data, 3);
    }

    return {};
}

} // namespace

struct PulsePluginAudioProcessor::FactoryPreset {
    juce::String name;
    juce::String script;
};

class PulsePluginAudioProcessor::CompileJob final : public juce::ThreadPoolJob
{
public:
    CompileJob(PulsePluginAudioProcessor& owner, juce::String scriptText, std::uint64_t requestId)
        : juce::ThreadPoolJob("FabricCompile")
        , owner_(owner)
        , scriptText_(std::move(scriptText))
        , requestId_(requestId)
    {
    }

    JobStatus runJob() override
    {
        auto result = owner_.buildCompileResult(scriptText_);
        owner_.applyCompileResult(std::move(result), requestId_);
        return jobHasFinished;
    }

private:
    PulsePluginAudioProcessor& owner_;
    juce::String scriptText_;
    std::uint64_t requestId_ = 0;
};

PulsePluginAudioProcessor::PulsePluginAudioProcessor()
    : juce::AudioProcessor(BusesProperties())
{
    scriptText_ = defaultScript();
    currentProgramIndex_ = 0;
    compileScript(scriptText_);
}

PulsePluginAudioProcessor::~PulsePluginAudioProcessor()
{
    compilePool_.removeAllJobs(true, 2000);
}

void PulsePluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_.store(sampleRate);
    blockSize_.store(samplesPerBlock);

    auto state = std::atomic_load(&activeState_);
    if (state != nullptr && state->engine != nullptr) {
        state->engine->reset(sampleRate, static_cast<std::uint32_t>(samplesPerBlock));
    }
}

void PulsePluginAudioProcessor::releaseResources()
{
}

bool PulsePluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled() && layouts.getMainOutputChannelSet().isDisabled();
}

void PulsePluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    auto state = std::atomic_load(&activeState_);
    if (state == nullptr || state->engine == nullptr) {
        midiMessages.clear();
        return;
    }

    pulse::ProcessContext context;
    context.sampleRate = sampleRate_.load();
    context.blockSize = static_cast<std::uint32_t>(buffer.getNumSamples());
    context.bpm = 120.0;
    context.syncToTransport = syncToTransport_.load();

    if (auto* playHead = getPlayHead()) {
        if (const auto position = playHead->getPosition()) {
            if (const auto bpm = position->getBpm()) {
                context.bpm = *bpm;
            }
            if (const auto ppq = position->getPpqPosition()) {
                context.transportPpq = *ppq;
            }
            context.transportPlaying = position->getIsPlaying();
        }
    }

    const auto incomingEvents = midiBufferToEvents(midiMessages);
    midiMessages.clear();

    for (const auto& moduleName : state->inputModuleNames) {
        state->engine->setInputEvents(moduleName.toStdString(), incomingEvents);
    }

    std::unordered_map<juce::String, int> sectionRecalls;
    std::unordered_map<juce::String, int> sectionAdvances;
    {
        const juce::ScopedLock lock(sectionRequestLock_);
        sectionRecalls.swap(pendingSectionRecalls_);
        sectionAdvances.swap(pendingSectionAdvances_);
    }

    for (const auto& sectionControl : state->sectionControls) {
        std::vector<pulse::Event> controlEvents;
        if (const auto recall = sectionRecalls.find(sectionControl.moduleName); recall != sectionRecalls.end()) {
            controlEvents.push_back(pulse::Event::makeValue(static_cast<double>(recall->second), 0.0));
        }
        if (const auto advance = sectionAdvances.find(sectionControl.moduleName); advance != sectionAdvances.end()) {
            for (int index = 0; index < advance->second; ++index) {
                controlEvents.push_back(pulse::Event::makeTrigger(0.0));
            }
        }
        if (!controlEvents.empty()) {
            state->engine->setInputEvents(sectionControl.moduleName.toStdString(), controlEvents);
        }
    }

    state->engine->process(context);

    {
        const juce::ScopedLock lock(sectionRequestLock_);
        for (const auto& sectionControl : state->sectionControls) {
            if (const auto activeIndex = state->engine->currentSectionIndex(sectionControl.moduleName.toStdString()); activeIndex.has_value()) {
                activeSectionIndices_[sectionControl.moduleName] = *activeIndex;
            }
            if (const auto phase = state->engine->currentSectionPhase(sectionControl.moduleName.toStdString()); phase.has_value()) {
                activeSectionPhases_[sectionControl.moduleName] = *phase;
            }
            if (const auto advanceCount = state->engine->sectionAdvanceCount(sectionControl.moduleName.toStdString()); advanceCount.has_value()) {
                sectionAdvanceCounts_[sectionControl.moduleName] = *advanceCount;
            }
        }
    }

    std::vector<pulse::Event> rendered;
    for (const auto& moduleName : state->outputModuleNames) {
        const auto events = state->engine->outputEvents(moduleName.toStdString());
        rendered.insert(rendered.end(), events.begin(), events.end());
    }

    if (const juce::SpinLock::ScopedTryLockType ioLock(ioSnapshotLock_); ioLock.isLocked()) {
        updateActiveNotes(incomingActiveNotes_, incomingEvents);
        updateActiveNotes(outgoingActiveNotes_, rendered);
        pushHistory(incomingHistory_, static_cast<int>(incomingEvents.size()));
        pushHistory(outgoingHistory_, static_cast<int>(rendered.size()));
        ioSnapshot_ = buildIoSnapshot(incomingEvents, rendered);
        ioSnapshot_.incomingActive = incomingActiveNotes_;
        ioSnapshot_.outgoingActive = outgoingActiveNotes_;
        ioSnapshot_.incomingHistory = incomingHistory_;
        ioSnapshot_.outgoingHistory = outgoingHistory_;
        ioSnapshot_.incomingActiveCount = countActiveNotes(incomingActiveNotes_);
        ioSnapshot_.outgoingActiveCount = countActiveNotes(outgoingActiveNotes_);
    }

    eventsToMidiBuffer(rendered, context.sampleRate, static_cast<int>(context.blockSize), midiMessages);
}

juce::AudioProcessorEditor* PulsePluginAudioProcessor::createEditor()
{
    return new PulsePluginAudioProcessorEditor(*this);
}

bool PulsePluginAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String PulsePluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PulsePluginAudioProcessor::acceptsMidi() const
{
    return true;
}

bool PulsePluginAudioProcessor::producesMidi() const
{
    return true;
}

bool PulsePluginAudioProcessor::isMidiEffect() const
{
    return true;
}

double PulsePluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PulsePluginAudioProcessor::getNumPrograms()
{
    return static_cast<int>(factoryPresets().size());
}

int PulsePluginAudioProcessor::getCurrentProgram()
{
    return currentProgramIndex_.value_or(0);
}

void PulsePluginAudioProcessor::setCurrentProgram(int index)
{
    const auto& presets = factoryPresets();
    if (index < 0 || index >= static_cast<int>(presets.size())) {
        return;
    }

    currentProgramIndex_ = index;
    compileScript(presets[static_cast<std::size_t>(index)].script);
}

const juce::String PulsePluginAudioProcessor::getProgramName(int index)
{
    const auto& presets = factoryPresets();
    if (index < 0 || index >= static_cast<int>(presets.size())) {
        return {};
    }
    return presets[static_cast<std::size_t>(index)].name;
}

void PulsePluginAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void PulsePluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("FabricState");
    {
        const juce::ScopedLock lock(uiStateLock_);
        state.setProperty("script", scriptText_, nullptr);
    }
    state.setProperty("syncToTransport", syncToTransport_.load(), nullptr);

    juce::MemoryOutputStream stream(destData, false);
    state.writeToStream(stream);
}

void PulsePluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
    if (const auto state = juce::ValueTree::readFromStream(stream); state.isValid()) {
        syncToTransport_.store(static_cast<bool>(state.getProperty("syncToTransport", true)));
        compileScript(state.getProperty("script", defaultScript()).toString());
        uiRevision_.fetch_add(1);
        return;
    }

    stream.setPosition(0);
    compileScript(stream.readString());
}

bool PulsePluginAudioProcessor::compileScript(const juce::String& scriptText)
{
    auto result = buildCompileResult(scriptText);
    applyCompileResult(std::move(result), requestedCompileId_.fetch_add(1) + 1);
    return getDiagnosticsText().containsIgnoreCase("successfully");
}

void PulsePluginAudioProcessor::requestCompile(const juce::String& scriptText)
{
    {
        const juce::ScopedLock lock(uiStateLock_);
        scriptText_ = scriptText;
        compileInProgress_.store(true);
    }

    const auto requestId = requestedCompileId_.fetch_add(1) + 1;
    compilePool_.addJob(new CompileJob(*this, scriptText, requestId), true);
}

juce::String PulsePluginAudioProcessor::getScriptText() const
{
    const juce::ScopedLock lock(uiStateLock_);
    return scriptText_;
}

juce::String PulsePluginAudioProcessor::getDiagnosticsText() const
{
    const juce::ScopedLock lock(uiStateLock_);
    return diagnosticsText_;
}

std::vector<PulsePluginAudioProcessor::UiDiagnostic> PulsePluginAudioProcessor::getDiagnostics() const
{
    const juce::ScopedLock lock(uiStateLock_);
    return diagnostics_;
}

PulsePluginAudioProcessor::GraphSnapshot PulsePluginAudioProcessor::getGraphSnapshot() const
{
    GraphSnapshot snapshot;
    {
        const juce::ScopedLock lock(uiStateLock_);
        snapshot = graphSnapshot_;
    }

    auto state = std::atomic_load(&activeState_);
    if (state == nullptr || state->engine == nullptr) {
        return snapshot;
    }

    for (auto& node : snapshot.nodes) {
        if (const auto label = state->engine->activeStateLabel(node.name.toStdString()); label.has_value()) {
            node.detail = juce::String(*label);
        }
    }

    return snapshot;
}

std::vector<PulsePluginAudioProcessor::SectionControlSnapshot> PulsePluginAudioProcessor::getSectionControls() const
{
    std::vector<SectionControlSnapshot> controls;
    {
        const juce::ScopedLock lock(uiStateLock_);
        controls = sectionControls_;
    }

    const juce::ScopedLock lock(sectionRequestLock_);
    for (auto& control : controls) {
        if (const auto found = activeSectionIndices_.find(control.moduleName); found != activeSectionIndices_.end()) {
            control.activeSectionIndex = found->second;
        }
        if (const auto found = activeSectionPhases_.find(control.moduleName); found != activeSectionPhases_.end()) {
            control.phase = found->second;
        }
        if (const auto found = sectionAdvanceCounts_.find(control.moduleName); found != sectionAdvanceCounts_.end()) {
            control.advanceCount = found->second;
        }
    }
    return controls;
}

PulsePluginAudioProcessor::IoSnapshot PulsePluginAudioProcessor::getIoSnapshot() const
{
    const juce::SpinLock::ScopedLockType lock(ioSnapshotLock_);
    return ioSnapshot_;
}

bool PulsePluginAudioProcessor::isSyncToTransportEnabled() const
{
    return syncToTransport_.load();
}

void PulsePluginAudioProcessor::setSyncToTransportEnabled(bool enabled)
{
    syncToTransport_.store(enabled);
    uiRevision_.fetch_add(1);
}

std::uint64_t PulsePluginAudioProcessor::getUiRevision() const
{
    return uiRevision_.load();
}

bool PulsePluginAudioProcessor::isCompileInProgress() const
{
    return compileInProgress_.load();
}

void PulsePluginAudioProcessor::requestSectionRecall(const juce::String& moduleName, int sectionIndex)
{
    if (moduleName.isEmpty() || sectionIndex < 0) {
        return;
    }

    const juce::ScopedLock lock(sectionRequestLock_);
    pendingSectionRecalls_[moduleName] = sectionIndex;
}

PulsePluginAudioProcessor::IoEventSummary PulsePluginAudioProcessor::summariseEvent(const pulse::Event& event)
{
    IoEventSummary summary;
    if (event.type == pulse::SignalType::midi) {
        const auto noteName = juce::MidiMessage::getMidiNoteName(event.noteNumber(), true, true, 3);
        if (event.isNoteOn()) {
            summary.text = "On  " + noteName + "  vel " + juce::String(event.velocityValue()) + "  ch " + juce::String(event.channel());
            summary.isNoteOn = true;
            return summary;
        }
        if (event.isNoteOff()) {
            summary.text = "Off " + noteName + "  ch " + juce::String(event.channel());
            summary.isNoteOff = true;
            return summary;
        }
        summary.text = "MIDI " + juce::String(event.noteNumber());
        return summary;
    }

    if (event.type == pulse::SignalType::trigger) {
        summary.text = "Trigger";
        return summary;
    }
    if (event.type == pulse::SignalType::gate) {
        summary.text = "Gate " + juce::String(event.valueOr(0.0), 2);
        return summary;
    }
    if (event.type == pulse::SignalType::pitch) {
        summary.text = "Pitch " + juce::String(event.valueOr(0.0), 2);
        return summary;
    }
    if (event.type == pulse::SignalType::value) {
        summary.text = "Value " + juce::String(event.valueOr(0.0), 2);
        return summary;
    }

    summary.text = "Event";
    return summary;
}

PulsePluginAudioProcessor::IoSnapshot PulsePluginAudioProcessor::buildIoSnapshot(const std::vector<pulse::Event>& incoming,
    const std::vector<pulse::Event>& outgoing)
{
    IoSnapshot snapshot;
    snapshot.incomingCount = static_cast<int>(incoming.size());
    snapshot.outgoingCount = static_cast<int>(outgoing.size());

    constexpr std::size_t maxShown = 12;
    snapshot.incoming.reserve(std::min(maxShown, incoming.size()));
    snapshot.outgoing.reserve(std::min(maxShown, outgoing.size()));

    for (std::size_t index = 0; index < incoming.size() && index < maxShown; ++index) {
        snapshot.incoming.push_back(summariseEvent(incoming[index]));
    }
    for (std::size_t index = 0; index < outgoing.size() && index < maxShown; ++index) {
        snapshot.outgoing.push_back(summariseEvent(outgoing[index]));
    }

    return snapshot;
}

void PulsePluginAudioProcessor::updateActiveNotes(std::array<std::uint8_t, 128>& activeNotes, const std::vector<pulse::Event>& events)
{
    for (const auto& event : events) {
        if (event.type != pulse::SignalType::midi) {
            continue;
        }
        const auto note = juce::jlimit(0, 127, event.noteNumber());
        if (event.isNoteOn() && event.velocityValue() > 0) {
            activeNotes[static_cast<std::size_t>(note)] = static_cast<std::uint8_t>(juce::jlimit(1, 127, event.velocityValue()));
        } else if (event.isNoteOff() || (event.isNoteOn() && event.velocityValue() == 0)) {
            activeNotes[static_cast<std::size_t>(note)] = 0;
        }
    }
}

void PulsePluginAudioProcessor::pushHistory(std::array<std::uint8_t, 32>& history, int count)
{
    for (std::size_t index = 0; index + 1 < history.size(); ++index) {
        history[index] = history[index + 1];
    }
    history.back() = static_cast<std::uint8_t>(juce::jlimit(0, 255, count));
}

int PulsePluginAudioProcessor::countActiveNotes(const std::array<std::uint8_t, 128>& activeNotes)
{
    int total = 0;
    for (const auto value : activeNotes) {
        if (value > 0) {
            ++total;
        }
    }
    return total;
}

void PulsePluginAudioProcessor::requestSectionAdvance(const juce::String& moduleName)
{
    if (moduleName.isEmpty()) {
        return;
    }

    const juce::ScopedLock lock(sectionRequestLock_);
    pendingSectionAdvances_[moduleName] += 1;
}

juce::String PulsePluginAudioProcessor::defaultScript()
{
    return makeDefaultScript();
}

const std::vector<PulsePluginAudioProcessor::FactoryPreset>& PulsePluginAudioProcessor::factoryPresets()
{
    static const std::vector<FactoryPreset> presets = [] {
        std::vector<FactoryPreset> result;
        for (const auto& preset : makeFactoryPresets()) {
            result.push_back({ preset.name, preset.script });
        }
        return result;
    }();
    return presets;
}

PulsePluginAudioProcessor::CompileResult PulsePluginAudioProcessor::buildCompileResult(const juce::String& scriptText) const
{
    CompileResult result;
    result.scriptText = scriptText;

    auto nextEngine = std::make_shared<pulse::Engine>();
    const auto success = nextEngine->loadPatchText(scriptText.toStdString());
    result.diagnostics = diagnosticsFromPulse(nextEngine->diagnostics());
    result.diagnosticsText = diagnosticsToText(result.diagnostics);
    result.success = success;

    if (!success) {
        return result;
    }

    nextEngine->reset(sampleRate_.load(), static_cast<std::uint32_t>(blockSize_.load()));

    auto engineState = std::make_shared<EngineState>();
    engineState->engine = std::move(nextEngine);
    engineState->graph = graphFromEngine(*engineState->engine);

    if (engineState->engine->graph() != nullptr) {
        for (const auto& node : engineState->engine->graph()->patch().nodes) {
            if (node.family == pulse::ModuleFamily::input && node.kind == "midi") {
                engineState->inputModuleNames.add(node.name);
            }
            if (node.family == pulse::ModuleFamily::output && node.kind == "midi") {
                engineState->outputModuleNames.add(node.name);
            }
        }

        for (const auto& module : engineState->engine->graph()->patch().source.modules) {
            if (module.family != pulse::ModuleFamily::generate || module.kind != "section") {
                continue;
            }

            SectionControlSnapshot snapshot;
            snapshot.moduleName = module.name;

            for (const auto& property : module.properties) {
                if (property.key == "section" && !property.values.empty()) {
                    snapshot.sectionNames.add(property.values.front());
                }
            }

            if (snapshot.sectionNames.isEmpty()) {
                snapshot.sectionNames.add("default");
            }

            if (const auto activeIndex = engineState->engine->currentSectionIndex(snapshot.moduleName.toStdString()); activeIndex.has_value()) {
                snapshot.activeSectionIndex = *activeIndex;
            }
            if (const auto phase = engineState->engine->currentSectionPhase(snapshot.moduleName.toStdString()); phase.has_value()) {
                snapshot.phase = *phase;
            }
            if (const auto advanceCount = engineState->engine->sectionAdvanceCount(snapshot.moduleName.toStdString()); advanceCount.has_value()) {
                snapshot.advanceCount = *advanceCount;
            }

            engineState->sectionControls.push_back(std::move(snapshot));
        }
    }

    result.engineState = std::move(engineState);
    if (result.diagnosticsText.isEmpty()) {
        result.diagnosticsText = "Compiled successfully.";
    }
    return result;
}

void PulsePluginAudioProcessor::applyCompileResult(CompileResult result, std::uint64_t requestId)
{
    if (requestId != requestedCompileId_.load()) {
        return;
    }

    if (result.success && result.engineState != nullptr) {
        std::atomic_store(&activeState_, result.engineState);
    }

    {
        const juce::ScopedLock lock(uiStateLock_);
        scriptText_ = result.scriptText;
        diagnostics_ = result.diagnostics;
        diagnosticsText_ = result.diagnosticsText;
        if (result.success && result.engineState != nullptr) {
            graphSnapshot_ = result.engineState->graph;
            sectionControls_ = result.engineState->sectionControls;
            const juce::ScopedLock sectionLock(sectionRequestLock_);
            activeSectionIndices_.clear();
            activeSectionPhases_.clear();
            sectionAdvanceCounts_.clear();
            for (const auto& sectionControl : sectionControls_) {
                activeSectionIndices_[sectionControl.moduleName] = sectionControl.activeSectionIndex;
                activeSectionPhases_[sectionControl.moduleName] = sectionControl.phase;
                sectionAdvanceCounts_[sectionControl.moduleName] = sectionControl.advanceCount;
            }
        }
        compileInProgress_.store(false);
    }

    const auto& presets = factoryPresets();
    currentProgramIndex_.reset();
    for (int index = 0; index < static_cast<int>(presets.size()); ++index) {
        if (result.scriptText == presets[static_cast<std::size_t>(index)].script) {
            currentProgramIndex_ = index;
            break;
        }
    }

    uiRevision_.fetch_add(1);
}

PulsePluginAudioProcessor::GraphSnapshot PulsePluginAudioProcessor::graphFromEngine(const pulse::Engine& engine)
{
    GraphSnapshot snapshot;
    if (engine.graph() == nullptr) {
        return snapshot;
    }

    const auto& patch = engine.graph()->patch();
    snapshot.nodes.reserve(patch.nodes.size());
    for (const auto& node : patch.nodes) {
        snapshot.nodes.push_back({
            node.name,
            juce::String(pulse::toString(node.family)),
            node.kind,
            {}
        });
    }

    snapshot.connections.reserve(patch.connections.size());
    for (const auto& connection : patch.connections) {
        snapshot.connections.push_back({
            patch.nodes[connection.fromNode].name,
            connection.fromPort,
            patch.nodes[connection.toNode].name,
            connection.toPort
        });
    }

    return snapshot;
}

std::vector<PulsePluginAudioProcessor::UiDiagnostic> PulsePluginAudioProcessor::diagnosticsFromPulse(const std::vector<pulse::Diagnostic>& diagnostics)
{
    std::vector<UiDiagnostic> result;
    result.reserve(diagnostics.size());
    for (const auto& diagnostic : diagnostics) {
        result.push_back({
            diagnostic.message,
            static_cast<int>(diagnostic.location.line),
            static_cast<int>(diagnostic.location.column),
            diagnostic.severity == pulse::Diagnostic::Severity::error
        });
    }
    return result;
}

juce::String PulsePluginAudioProcessor::diagnosticsToText(const std::vector<UiDiagnostic>& diagnostics)
{
    if (diagnostics.empty()) {
        return {};
    }

    juce::StringArray lines;
    for (const auto& diagnostic : diagnostics) {
        lines.add(juce::String(diagnostic.isError ? "Error" : "Warning") + " "
            + juce::String(diagnostic.line) + ":" + juce::String(diagnostic.column)
            + "  " + diagnostic.message);
    }
    return lines.joinIntoString("\n");
}

std::vector<pulse::Event> PulsePluginAudioProcessor::midiBufferToEvents(const juce::MidiBuffer& midi)
{
    std::vector<pulse::Event> events;
    for (const auto metadata : midi) {
        const auto& message = metadata.getMessage();
        const auto time = static_cast<double>(metadata.samplePosition);

        if (message.isNoteOn()) {
            events.push_back(pulse::Event::makeNoteOn(message.getNoteNumber(), message.getVelocity(), message.getChannel(), time));
            continue;
        }

        if (message.isNoteOff()) {
            events.push_back(pulse::Event::makeNoteOff(message.getNoteNumber(), static_cast<int>(message.getVelocity()), message.getChannel(), time));
            continue;
        }

        if (message.isController()) {
            pulse::Event event;
            event.type = pulse::SignalType::midi;
            event.time = time;
            event.ints = { 0xB0, message.getControllerNumber(), message.getControllerValue(), message.getChannel() };
            events.push_back(std::move(event));
        }
    }

    return events;
}

void PulsePluginAudioProcessor::eventsToMidiBuffer(const std::vector<pulse::Event>& events, double sampleRate, int blockSize, juce::MidiBuffer& midi)
{
    for (const auto& event : events) {
        auto message = eventToMidiMessage(event);
        if (message.getRawDataSize() <= 0) {
            continue;
        }

        int samplePosition = 0;
        if (event.time <= 1.0 && sampleRate > 1.0) {
            samplePosition = static_cast<int>(juce::jlimit(0.0, static_cast<double>(blockSize - 1), event.time * sampleRate));
        } else {
            samplePosition = static_cast<int>(juce::jlimit(0.0, static_cast<double>(blockSize - 1), event.time));
        }

        midi.addEvent(message, samplePosition);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PulsePluginAudioProcessor();
}
