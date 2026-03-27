#include "FabricProcessor.h"
#include "FabricEditor.h"

#include <utility>

namespace {

pulse::NodeProcessingMode toPulseNodeMode(FabricAudioProcessor::NodeProcessingMode mode)
{
    switch (mode) {
    case FabricAudioProcessor::NodeProcessingMode::bypass:
        return pulse::NodeProcessingMode::bypass;
    case FabricAudioProcessor::NodeProcessingMode::mute:
        return pulse::NodeProcessingMode::mute;
    case FabricAudioProcessor::NodeProcessingMode::normal:
    default:
        return pulse::NodeProcessingMode::normal;
    }
}

struct FactoryPresetData {
    const char* name;
    const char* script;
};

juce::String makeDefaultScript()
{
    return R"pulse(patch fabric_default

key D dorian

midi in keys
  channel 1
end

clock metro
  every 1/16
end

quantize in_key
  key D dorian
end

arp arp1
  gate 70%
end

midi out out
end

keys -> in_key
in_key -> arp1
metro -> arp1.trigger
arp1 -> out
end
)pulse";
}

std::vector<FactoryPresetData> makeFactoryPresets()
{
    return {
        { "Default Arp",
            R"pulse(patch fabric_default

scale D dorian

midi in keys
  channel 1
end

clock metro
  every 1/16
end

quantize in_key
  scale D dorian
end

arp arp1
  gate 70%
end

midi out out
end

keys -> in_key
in_key -> arp1
metro -> arp1.trigger
arp1 -> out
end
)pulse" },
        { "Random Walk",
            R"pulse(patch random_machine

clock metro
  every 1/16
end

random chance1
  notes D3 F3 A3 C4 E4
  mode walk
  pass 70%
  avoid repeat
  max step 5
  bias center 0.8
  seed 42
end

notes notes1
  quantize D dorian
  gate 55%
end

midi out out
end

metro -> chance1.trigger
chance1 -> notes1
notes1 -> out
end
)pulse" },
        { "Pattern Machine",
            R"pulse(patch pattern_machine

clock metro
  every 1/16
end

pattern seq1
  notes C3 D3 F3 A3
  order up_down
end

notes notes1
  quantize C major
  gate 60%
end

midi out out
end

metro -> seq1.trigger
seq1 -> notes1
notes1 -> out
end
)pulse" },
        { "Groove Ratchet Arp",
            R"pulse(patch groove_ratchet_arp

midi in keys
  channel 1
end

clock metro
  every 1/16
  swing 60%
  ratchet 3
  groove 100 92 108 96
end

quantize in_key
  scale D dorian
end

arp arp1
  gate 62%
end

midi out out
end

keys -> in_key
in_key -> arp1
metro -> arp1.trigger
arp1 -> out
end
)pulse" },
        { "Motion Notes",
            R"pulse(patch motion_to_notes

midi in keys
end

clock metro
  every 1/16
end

motion motion1
  channels 8
  space 0.4
  clocked on
end

notes notes1
  quantize D minor
  range C2..C5
  velocity 100
end

midi out out
end

keys -> motion1
metro -> motion1.clock
motion1.speed -> notes1.values
motion1.even -> notes1.gate
notes1 -> out
end
)pulse" },
        { "Stage Modulator",
            R"pulse(patch lists_to_notes

midi in keys
  channel 1
end

clock metro
  every 1/8
end

stages mod1
  mode loop
  stage 1 level 0.2 time 80ms curve linear
  stage 2 level 0.9 time 120ms curve smooth
  stage 3 level 0.4 time 200ms curve exp
end

lists marf1
  pitch C3 E3 G3 Bb3
  time 80ms 140ms 220ms 500ms
  gate 30% 60% 90% 40%

  advance pitch on note
  advance time on threshold 0.7
  advance gate on random

  interpolate pitch on
  interpolate time on
end

notes notes1
  scale D minor
  range C2..C6
  velocity 96
end

midi out out
end

keys -> marf1.note
metro -> marf1.random
mod1 -> marf1.threshold
marf1.pitch -> notes1
marf1.time -> notes1.time
marf1.gate -> notes1.gate
notes1 -> out
end
)pulse" },
        { "Complex Modulator",
            R"pulse(patch complex_modulator

clock metro
  every 1/16
  swing 57%
  groove 100 94 108 98
end

pattern seq1
  notes C3 D3 F3 A3
  order up_down
end

modulator mod1
  channels 4
  mode loop
  stage 1 channel 1 level 0.15 time 70ms curve linear
  stage 2 channel 1 level 0.90 time 110ms curve smooth
  stage 3 channel 1 level 0.35 time 160ms curve exp
  stage 1 channel 2 level 0.08 time 120ms curve linear
  stage 2 channel 2 level 0.70 time 180ms curve smooth
  stage 3 channel 2 level 0.24 time 260ms curve log
  stage 1 channel 3 level 0.30 time 40ms curve linear
  stage 2 channel 3 level 1.00 time 90ms curve exp
  stage 3 channel 3 level 0.12 time 220ms curve smooth
  stage 1 channel 4 level 0.55 time 150ms curve linear
  stage 2 channel 4 level 0.20 time 200ms curve smooth
  stage 3 channel 4 level 0.82 time 280ms curve exp
end

notes notes1
  scale D dorian
  range C2..C5
end

midi out out
end

metro -> seq1.trigger
seq1 -> notes1
mod1.ch1 -> notes1.velocity
mod1.ch2 -> notes1.time
metro -> mod1.trigger
notes1 -> out
end
)pulse" },
        { "Modulated Modulator",
            R"pulse(patch modulated_modulator

clock metro
  every 1/16
  swing 56%
end

stages mod_depth
  mode loop
  stage 1 level 0.20 time 80ms curve linear
  stage 2 level 0.95 time 120ms curve smooth
  stage 3 level 0.35 time 180ms curve exp
end

stages mod_time
  mode loop
  stage 1 level 0.08 time 90ms curve linear
  stage 2 level 0.22 time 140ms curve smooth
  stage 3 level 0.11 time 200ms curve exp
end

modulator mod1
  channels 2
  mode loop
  stage 1 channel 1 level 0.15 time 70ms curve linear
  stage 2 channel 1 level 0.85 time 110ms curve smooth
  stage 3 channel 1 level 0.30 time 160ms curve exp
  stage 1 channel 2 level 0.10 time 100ms curve linear
  stage 2 channel 2 level 0.70 time 150ms curve smooth
  stage 3 channel 2 level 0.20 time 220ms curve log
end

pattern seq1
  notes C3 D3 F3 A3
  order up_down
end

notes notes1
  scale D dorian
  range C2..C5
end

midi out out
end

metro -> seq1.trigger
metro -> mod1.trigger
seq1 -> notes1
mod_depth -> mod1.ch1_s2_level
mod_time -> mod1.ch2_s1_time
mod1.ch1 -> notes1.velocity
mod1.ch2 -> notes1.time
notes1 -> out
end
)pulse" },
        { "Addressed Overlap Modulator",
            R"pulse(patch addressed_overlap_modulator

clock metro
  every 1/16
  swing 58%
end

clock accents
  every 1/8
end

clock resets
  every 1/2
end

pattern seq1
  notes C3 D3 F3 A3
  order up_down
end

modulator mod1
  channels 2
  mode loop
  overlap on
  stage 1 channel 1 level 0.18 time 60ms overlap 18ms curve linear
  stage 2 channel 1 level 0.95 time 100ms overlap 28ms curve smooth
  stage 3 channel 1 level 0.40 time 170ms overlap 36ms curve exp
  stage 1 channel 2 level 0.12 time 110ms overlap 20ms curve linear
  stage 2 channel 2 level 0.72 time 140ms overlap 34ms curve smooth
  stage 3 channel 2 level 0.25 time 210ms overlap 48ms curve log
end

notes notes1
  scale D dorian
  range C2..C5
end

midi out out
end

metro -> seq1.trigger
seq1 -> notes1
metro -> mod1.ch1_trigger
accents -> mod1.ch1_s2_trigger
accents -> mod1.ch2_s3_trigger
resets -> mod1.ch1_s2_reset
mod1.ch1 -> notes1.velocity
mod1.ch2 -> notes1.time
notes1 -> out
end
)pulse" },
        { "Quantized Motion Arp",
            R"pulse(patch tuned_motion

scale D minor

midi in keys
  channel 1
end

quantize in_key
  scale D minor
end

arp arp1
  gate 65%
end

clock metro
  every 1/16
end

midi out out
end

keys -> in_key
in_key -> arp1
metro -> arp1.trigger
arp1 -> out
end
)pulse" },
        { "Fibonacci Smear",
            R"pulse(patch fibonacci_smear

clock metro
  every 1/16
end

fibonacci fib1
  length 8
  map 0 2 3 5 7
end

notes notes1
  scale D minor
  range C2..C5
  velocity 96
end

smear bergson
  keep 3
  weights 0.60 0.25 0.15
  drift weights 0.05
end

midi out out
end

metro -> fib1.trigger
fib1 -> notes1
notes1 -> bergson
bergson -> out
end
)pulse" },
        { "Warped Pitch Space",
            R"pulse(patch warped_space

clock metro
  every 1/16
end

pattern roots
  notes C3 D3 G3 A3
  order up
end

warp warp1
  fold C near F#
  fold G near Db
  wormhole every random 3..6
  wormhole to A2 Eb4 F#4
  amount 1.0
  seed 17
end

notes notes1
  scale D dorian
  range C2..C5
  velocity 100
end

midi out out
end

metro -> roots.trigger
roots -> warp1.pitch
warp1 -> notes1
notes1 -> out
end
)pulse" },
        { "Crystal Growth",
            R"pulse(patch crystal_growth

clock metro
  every 1/16
end

progression plan
  targets tonic dominant tonic subdominant tonic
  lengths 2 2 2 2 4
end

growth crystal
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

notes notes1
  scale C lydian
  range C2..C5
  velocity 98
end

midi out out
end

metro -> crystal.trigger
metro -> plan.trigger
plan -> crystal.phrase
crystal -> notes1
crystal.density -> notes1.velocity
crystal.gate -> notes1.gate
notes1 -> out
end
)pulse" },
        { "Multi-Agent Swarm",
            R"pulse(patch swarm_machine

clock metro
  every 1/16
end

swarm flock
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

notes notes1
  scale D dorian
  range C2..C5
  velocity 98
  gate 58%
end

midi out out
end

metro -> flock.trigger
flock -> notes1
notes1 -> out
end
)pulse" },
        { "Constraint Collapse",
            R"pulse(patch collapse_machine

clock metro
  every 1/16
end

progression form
  targets tonic dominant tonic subdominant tonic
  lengths 2 2 2 2 4
end

section scenes
  start verse
  section verse tonic 2 dominant 2
  section chorus dominant 2 tonic 2
  section bridge subdominant 2 tonic 2
end

collapse engine1
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

notes notes1
  scale C minor
  range C2..C5
  velocity 100
  gate 62%
end

midi out out
end

metro -> engine1.trigger
metro -> form.trigger
metro -> scenes.trigger
form -> engine1.phrase
scenes.section -> engine1.section
engine1 -> notes1
notes1 -> out
end
)pulse" },
        { "Bands Delay Arp",
            R"pulse(patch split_delay_arp

tempo 120
scale D minor

midi in keys
  channel 1
end

clock metro
  every 1/16
end

split bands
  by note
  low below C3
  mid C3..B4
  high above B4
end

delay low_late
  time 40ms
end

arp arp1
  gate 70%
end

midi out out
end

keys -> bands
bands.low -> low_late
bands.mid -> arp1
metro -> arp1.trigger
low_late -> out
arp1 -> out
bands.high -> out
end
)pulse" },
        { "Bits Crush",
            R"pulse(patch bits_crush

midi in keys
end

bits crush_velocity
  events notes
  target velocity
  and 11110000b
end

midi out out
end

keys -> crush_velocity
crush_velocity -> out
end
)pulse" },
        { "Filtered Bits Crush",
            R"pulse(patch filtered_bits_crush

midi in keys
end

bits crush_velocity
  status notes
  note C3..C5
  velocity 90..127
  target velocity
  and 11110000b
end

midi out out
end

keys -> crush_velocity
crush_velocity -> out
end
)pulse" },
        { "Filtered Bits Exclude",
            R"pulse(patch filtered_bits_exclude

midi in keys
end

bits dodge_hot_notes
  status notes
  note C2..C6
  except note C4..D4
  velocity 70..127
  except velocity 120..127
  target velocity
  and 11110000b
end

midi out out
end

keys -> dodge_hot_notes
dodge_hot_notes -> out
end
)pulse" },
        { "CC Glitch",
            R"pulse(patch cc_glitch

midi in keys
end

bits glitch_modwheel
  status cc
  cc 1
  target data2
  xor 00011111b
end

midi out out
end

keys -> glitch_modwheel
glitch_modwheel -> out
end
)pulse" },
        { "CC Glitch + Arp",
            R"pulse(patch cc_glitch_arp

midi in keys
end

clock metro
  every 1/16
end

quantize in_key
  scale D dorian
end

arp arp1
  gate 65%
end

bits glitch_modwheel
  status cc
  cc 1..8
  except cc 64
  channel 1
  target data2
  xor 00011111b
end

midi out out
end

keys -> in_key
in_key -> arp1
metro -> arp1.trigger
arp1 -> out
keys -> glitch_modwheel
glitch_modwheel -> out
end
)pulse" },
        { "Bouncing Ball",
            R"pulse(patch bouncing_ball

clock metro
  every 1/4
end

pattern seed
  notes C3
end

notes notes1
  scale C minor
  gate 85%
  velocity 104
end

bounce ball
  count 8
  spacing 180ms -> 25ms
  velocity 112 -> 36
end

midi out out
end

metro -> seed.trigger
seed -> notes1
notes1 -> ball
ball -> out
end
)pulse" },
        { "MIDI Loop",
            R"pulse(patch midi_loop

midi in keys
  channel 1
end

loop phrase1
  capture 1/16
  playback on
  overdub off
  quantize to 1/32
end

midi out out
end

keys -> phrase1
phrase1 -> out
end
)pulse" },
        { "Held Note Random",
            R"pulse(patch random_held_notes

midi in keys
  channel 1
end

clock metro
  every 1/16
end

random chance1
  from held notes
  mode walk
  pass 75%
  avoid repeat
  max step 7
  bias center 0.6
  seed 42
end

notes notes1
  quantize D dorian
  gate 60%
end

midi out out
end

keys -> chance1.in
metro -> chance1.trigger
chance1 -> notes1
notes1 -> out
end
)pulse" },
        { "Cut-Up Machine",
            R"pulse(patch cutup_machine

midi in keys
  channel 1
end

clock metro
  every 1/16
end

cutup burroughs
  capture 1/16
  slice 2
  keep continuity 0.35
  favor harmonic
  seed 42
end

midi out out
end

keys -> burroughs.in
metro -> burroughs.trigger
burroughs -> out
end
)pulse" },
        { "Section Form",
            R"pulse(patch section_controlled

clock metro
  every 1/16
end

clock form_clock
  every 1/8
end

progression chords
  targets tonic dominant subdominant tonic
  lengths 2 2 2 2
end

section form
  start verse
  section verse tonic 2 dominant 2
  section chorus tonic 2 subdominant 2 dominant 2 tonic 2
end

notes mover
  chord scale7
  invert 1
  spread 1 octave
  movement nearest
  arrive on 4
  cadence close
end

midi out out
end

metro -> chords.trigger
metro -> mover.trigger
form_clock -> form.advance
form -> mover.phrase
chords -> mover
mover -> out
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

struct FabricAudioProcessor::FactoryPreset {
    juce::String name;
    juce::String script;
};

struct FabricAudioProcessor::TutorialEntry {
    juce::String name;
    juce::String summary;
    juce::String script;
};

class FabricAudioProcessor::CompileJob final : public juce::ThreadPoolJob
{
public:
    CompileJob(FabricAudioProcessor& owner, juce::String scriptText, std::uint64_t requestId)
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
    FabricAudioProcessor& owner_;
    juce::String scriptText_;
    std::uint64_t requestId_ = 0;
};

FabricAudioProcessor::FabricAudioProcessor()
    : juce::AudioProcessor(BusesProperties())
{
    scriptText_ = defaultScript();
    currentProgramIndex_ = 0;
    compileScript(scriptText_);
}

FabricAudioProcessor::~FabricAudioProcessor()
{
    compilePool_.removeAllJobs(true, 2000);
}

void FabricAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_.store(sampleRate);
    blockSize_.store(samplesPerBlock);

    auto state = std::atomic_load(&activeState_);
    if (state != nullptr && state->engine != nullptr) {
        state->engine->reset(sampleRate, static_cast<std::uint32_t>(samplesPerBlock));
    }
}

void FabricAudioProcessor::releaseResources()
{
}

bool FabricAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled() && layouts.getMainOutputChannelSet().isDisabled();
}

void FabricAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
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

    std::unordered_map<juce::String, NodeProcessingMode> nodeModes;
    {
        const juce::ScopedLock lock(uiStateLock_);
        nodeModes = nodeModes_;
    }
    for (const auto& [moduleName, mode] : nodeModes) {
        state->engine->setNodeMode(moduleName.toStdString(), toPulseNodeMode(mode));
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
        ioSnapshot_.nodes = buildNodeIoSnapshots(*state->engine);
    }

    eventsToMidiBuffer(rendered, context.sampleRate, static_cast<int>(context.blockSize), midiMessages);
}

juce::AudioProcessorEditor* FabricAudioProcessor::createEditor()
{
    return new FabricAudioProcessorEditor(*this);
}

bool FabricAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String FabricAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FabricAudioProcessor::acceptsMidi() const
{
    return true;
}

bool FabricAudioProcessor::producesMidi() const
{
    return true;
}

bool FabricAudioProcessor::isMidiEffect() const
{
    return true;
}

double FabricAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int FabricAudioProcessor::getNumPrograms()
{
    return static_cast<int>(factoryPresets().size());
}

int FabricAudioProcessor::getCurrentProgram()
{
    return currentProgramIndex_.value_or(0);
}

void FabricAudioProcessor::setCurrentProgram(int index)
{
    const auto& presets = factoryPresets();
    if (index < 0 || index >= static_cast<int>(presets.size())) {
        return;
    }

    currentProgramIndex_ = index;
    compileScript(presets[static_cast<std::size_t>(index)].script);
}

const juce::String FabricAudioProcessor::getProgramName(int index)
{
    const auto& presets = factoryPresets();
    if (index < 0 || index >= static_cast<int>(presets.size())) {
        return {};
    }
    return presets[static_cast<std::size_t>(index)].name;
}

void FabricAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void FabricAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
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

void FabricAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
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

bool FabricAudioProcessor::compileScript(const juce::String& scriptText)
{
    auto result = buildCompileResult(scriptText);
    applyCompileResult(std::move(result), requestedCompileId_.fetch_add(1) + 1);
    return getDiagnosticsText().containsIgnoreCase("successfully");
}

void FabricAudioProcessor::requestCompile(const juce::String& scriptText)
{
    {
        const juce::ScopedLock lock(uiStateLock_);
        scriptText_ = scriptText;
        compileInProgress_.store(true);
    }

    const auto requestId = requestedCompileId_.fetch_add(1) + 1;
    compilePool_.addJob(new CompileJob(*this, scriptText, requestId), true);
}

juce::String FabricAudioProcessor::getScriptText() const
{
    const juce::ScopedLock lock(uiStateLock_);
    return scriptText_;
}

juce::String FabricAudioProcessor::getDiagnosticsText() const
{
    const juce::ScopedLock lock(uiStateLock_);
    return diagnosticsText_;
}

std::vector<FabricAudioProcessor::UiDiagnostic> FabricAudioProcessor::getDiagnostics() const
{
    const juce::ScopedLock lock(uiStateLock_);
    return diagnostics_;
}

void FabricAudioProcessor::setNodeProcessingMode(const juce::String& moduleName, NodeProcessingMode mode)
{
    if (moduleName.isEmpty()) {
        return;
    }

    const juce::ScopedLock lock(uiStateLock_);
    if (mode == NodeProcessingMode::normal) {
        nodeModes_.erase(moduleName);
    } else {
        nodeModes_[moduleName] = mode;
    }

    for (auto& node : graphSnapshot_.nodes) {
        if (node.name == moduleName) {
            node.mode = mode;
            break;
        }
    }

    uiRevision_.fetch_add(1);
}

FabricAudioProcessor::NodeProcessingMode FabricAudioProcessor::getNodeProcessingMode(const juce::String& moduleName) const
{
    const juce::ScopedLock lock(uiStateLock_);
    if (const auto found = nodeModes_.find(moduleName); found != nodeModes_.end()) {
        return found->second;
    }
    return NodeProcessingMode::normal;
}

FabricAudioProcessor::GraphSnapshot FabricAudioProcessor::getGraphSnapshot() const
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

std::vector<FabricAudioProcessor::ModulatorSnapshot> FabricAudioProcessor::getModulatorSnapshots() const
{
    GraphSnapshot graph;
    {
        const juce::ScopedLock lock(uiStateLock_);
        graph = graphSnapshot_;
    }

    auto state = std::atomic_load(&activeState_);
    if (state == nullptr || state->engine == nullptr) {
        return {};
    }

    std::vector<ModulatorSnapshot> snapshots;
    for (const auto& node : graph.nodes) {
        if (node.family != "shape" || node.kind != "modulator") {
            continue;
        }

        const auto runtimeSnapshot = state->engine->modulatorState(node.name.toStdString());
        if (!runtimeSnapshot.has_value()) {
            continue;
        }

        ModulatorSnapshot snapshot;
        snapshot.moduleName = node.name;
        snapshot.mode = runtimeSnapshot->mode;
        snapshot.overlapEnabled = runtimeSnapshot->overlapEnabled;
        snapshot.channels.reserve(runtimeSnapshot->channels.size());
        for (const auto& channel : runtimeSnapshot->channels) {
            ModulatorChannelSnapshot channelSnapshot;
            channelSnapshot.level = static_cast<float>(channel.level);
            channelSnapshot.activeStages = channel.activeStages;
            snapshot.channels.push_back(std::move(channelSnapshot));
        }
        snapshots.push_back(std::move(snapshot));
    }

    return snapshots;
}

std::vector<FabricAudioProcessor::SectionControlSnapshot> FabricAudioProcessor::getSectionControls() const
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

FabricAudioProcessor::IoSnapshot FabricAudioProcessor::getIoSnapshot() const
{
    const juce::SpinLock::ScopedLockType lock(ioSnapshotLock_);
    return ioSnapshot_;
}

bool FabricAudioProcessor::isSyncToTransportEnabled() const
{
    return syncToTransport_.load();
}

void FabricAudioProcessor::setSyncToTransportEnabled(bool enabled)
{
    syncToTransport_.store(enabled);
    uiRevision_.fetch_add(1);
}

std::uint64_t FabricAudioProcessor::getUiRevision() const
{
    return uiRevision_.load();
}

bool FabricAudioProcessor::isCompileInProgress() const
{
    return compileInProgress_.load();
}

void FabricAudioProcessor::requestSectionRecall(const juce::String& moduleName, int sectionIndex)
{
    if (moduleName.isEmpty() || sectionIndex < 0) {
        return;
    }

    const juce::ScopedLock lock(sectionRequestLock_);
    pendingSectionRecalls_[moduleName] = sectionIndex;
}

FabricAudioProcessor::IoEventSummary FabricAudioProcessor::summariseEvent(const pulse::Event& event)
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

FabricAudioProcessor::IoSnapshot FabricAudioProcessor::buildIoSnapshot(const std::vector<pulse::Event>& incoming,
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

std::vector<FabricAudioProcessor::NodeIoSnapshot> FabricAudioProcessor::buildNodeIoSnapshots(const pulse::Engine& engine)
{
    const auto* graph = engine.graph();
    if (graph == nullptr) {
        return {};
    }

    const auto& patch = graph->patch();
    std::vector<NodeIoSnapshot> snapshots;
    snapshots.reserve(patch.nodes.size());

    for (std::size_t nodeIndex = 0; nodeIndex < patch.nodes.size(); ++nodeIndex) {
        const auto& node = patch.nodes[nodeIndex];

        std::vector<pulse::Event> incomingEvents;
        std::vector<pulse::Event> outgoingEvents;
        for (const auto& input : node.inputs) {
            if (const auto* buffer = graph->inputBuffer(nodeIndex, input.name)) {
                const auto& events = buffer->events();
                incomingEvents.insert(incomingEvents.end(), events.begin(), events.end());
            }
        }
        for (const auto& output : node.outputs) {
            if (const auto* buffer = graph->outputBuffer(nodeIndex, output.name)) {
                const auto& events = buffer->events();
                outgoingEvents.insert(outgoingEvents.end(), events.begin(), events.end());
            }
        }

        auto byTime = [](const pulse::Event& a, const pulse::Event& b) { return a.time < b.time; };
        std::sort(incomingEvents.begin(), incomingEvents.end(), byTime);
        std::sort(outgoingEvents.begin(), outgoingEvents.end(), byTime);

        NodeIoSnapshot snapshot;
        snapshot.moduleName = node.name;
        snapshot.family = juce::String(pulse::toString(node.family));
        snapshot.kind = node.kind;
        snapshot.incomingCount = static_cast<int>(incomingEvents.size());
        snapshot.outgoingCount = static_cast<int>(outgoingEvents.size());

        constexpr std::size_t maxShown = 12;
        snapshot.incoming.reserve(std::min(maxShown, incomingEvents.size()));
        snapshot.outgoing.reserve(std::min(maxShown, outgoingEvents.size()));
        for (std::size_t index = 0; index < incomingEvents.size() && index < maxShown; ++index) {
            snapshot.incoming.push_back(summariseEvent(incomingEvents[index]));
        }
        for (std::size_t index = 0; index < outgoingEvents.size() && index < maxShown; ++index) {
            snapshot.outgoing.push_back(summariseEvent(outgoingEvents[index]));
        }

        updateActiveNotes(snapshot.incomingActive, incomingEvents);
        updateActiveNotes(snapshot.outgoingActive, outgoingEvents);
        snapshot.incomingActiveCount = countActiveNotes(snapshot.incomingActive);
        snapshot.outgoingActiveCount = countActiveNotes(snapshot.outgoingActive);

        std::array<std::uint8_t, 32> incomingHistory {};
        std::array<std::uint8_t, 32> outgoingHistory {};
        incomingHistory.back() = static_cast<std::uint8_t>(juce::jlimit(0, 255, snapshot.incomingCount));
        outgoingHistory.back() = static_cast<std::uint8_t>(juce::jlimit(0, 255, snapshot.outgoingCount));
        snapshot.incomingHistory = incomingHistory;
        snapshot.outgoingHistory = outgoingHistory;

        snapshots.push_back(std::move(snapshot));
    }

    return snapshots;
}

void FabricAudioProcessor::updateActiveNotes(std::array<std::uint8_t, 128>& activeNotes, const std::vector<pulse::Event>& events)
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

void FabricAudioProcessor::pushHistory(std::array<std::uint8_t, 32>& history, int count)
{
    for (std::size_t index = 0; index + 1 < history.size(); ++index) {
        history[index] = history[index + 1];
    }
    history.back() = static_cast<std::uint8_t>(juce::jlimit(0, 255, count));
}

int FabricAudioProcessor::countActiveNotes(const std::array<std::uint8_t, 128>& activeNotes)
{
    int total = 0;
    for (const auto value : activeNotes) {
        if (value > 0) {
            ++total;
        }
    }
    return total;
}

void FabricAudioProcessor::requestSectionAdvance(const juce::String& moduleName)
{
    if (moduleName.isEmpty()) {
        return;
    }

    const juce::ScopedLock lock(sectionRequestLock_);
    pendingSectionAdvances_[moduleName] += 1;
}

juce::String FabricAudioProcessor::defaultScript()
{
    return makeDefaultScript();
}

const std::vector<FabricAudioProcessor::FactoryPreset>& FabricAudioProcessor::factoryPresets()
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

const std::vector<FabricAudioProcessor::TutorialEntry>& FabricAudioProcessor::tutorials()
{
    static const std::vector<TutorialEntry> entries {
        { "01 First Pattern", "Generate notes with a clock, a pattern, and a note projector.", R"pulse(patch pattern_machine

clock metro
  every 1/16
end

pattern riff
  notes D3 F3 A3 C4
  order up_down
end

notes notes1
  scale D dorian
  range C2..C5
  velocity 100
end

midi out out
end

metro -> riff.trigger
riff -> notes1
notes1 -> out
end
)pulse" },
        { "02 Live Quantize And Arp", "Feed live MIDI through a scale quantizer and arpeggiator.", R"pulse(patch tutorial_live_quantize_arp

midi in keys
  channel 1
end

clock metro
  every 1/16
end

quantize in_key
  scale D dorian
end

arp arp1
  gate 68%
end

midi out out
end

keys -> in_key
in_key -> arp1
metro -> arp1.trigger
arp1 -> out
end
)pulse" },
        { "03 Random Walk", "Shape random melody so it moves locally instead of jumping everywhere.", R"pulse(patch random_machine

clock metro
  every 1/16
end

random chance1
  notes D3 F3 A3 C4 E4
  mode walk
  pass 70%
  avoid repeat
  max step 5
  bias center 0.8
  seed 42
end

notes notes1
  quantize D dorian
  gate 55%
end

midi out out
end

metro -> chance1.trigger
chance1 -> notes1
notes1 -> out
end
)pulse" },
        { "04 Split Delay And Arp", "Split incoming MIDI into note bands and process each branch differently.", R"pulse(patch split_delay_arp

tempo 120
scale D minor

midi in keys
  channel 1
end

clock metro
  every 1/16
end

split bands
  by note
  low below C3
  mid C3..B4
  high above B4
end

delay low_late
  time 40ms
end

arp arp1
  gate 70%
end

midi out out
end

keys -> bands
bands.low -> low_late
bands.mid -> arp1
metro -> arp1.trigger
low_late -> out
arp1 -> out
bands.high -> out
end
)pulse" },
        { "05 MIDI Loop", "Capture a phrase and replay it as a loop.", R"pulse(patch midi_loop

midi in keys
  channel 1
end

loop phrase1
  capture 1/16
  playback on
  overdub off
  quantize to 1/32
end

midi out out
end

keys -> phrase1
phrase1 -> out
end
)pulse" },
        { "06 Bouncing Ball", "Turn a note into a decaying rhythmic bounce gesture.", R"pulse(patch bouncing_ball

clock metro
  every 1/4
end

pattern seed
  notes C3
end

notes notes1
  scale C minor
  gate 85%
  velocity 104
end

bounce ball
  count 8
  spacing 180ms -> 25ms
  velocity 112 -> 36
end

midi out out
end

metro -> seed.trigger
seed -> notes1
notes1 -> ball
ball -> out
end
)pulse" },
        { "07 Motion To Notes", "Extract motion from live MIDI and reuse it as musical control.", R"pulse(patch motion_to_notes

midi in keys
end

clock metro
  every 1/16
end

motion motion1
  channels 8
  space 0.4
  clocked on
end

notes notes1
  quantize D minor
  range C2..C5
  velocity 100
end

midi out out
end

keys -> motion1
metro -> motion1.clock
motion1.speed -> notes1.values
motion1.even -> notes1.gate
notes1 -> out
end
)pulse" },
        { "08 Lists To Notes", "Use list traversal for pitch, time, and gate to build evolving phrases.", R"pulse(patch lists_to_notes

midi in keys
  channel 1
end

clock metro
  every 1/8
end

stages mod1
  mode loop
  stage 1 level 0.2 time 80ms curve linear
  stage 2 level 0.9 time 120ms curve smooth
  stage 3 level 0.4 time 200ms curve exp
end

lists marf1
  pitch C3 E3 G3 Bb3
  time 80ms 140ms 220ms 500ms
  gate 30% 60% 90% 40%
  advance pitch on note
  advance time on threshold 0.7
  advance gate on random
  interpolate pitch on
  interpolate time on
end

notes notes1
  scale D minor
  range C2..C6
  velocity 96
end

midi out out
end

keys -> marf1.note
metro -> marf1.random
mod1 -> marf1.threshold
marf1.pitch -> notes1
marf1.time -> notes1.time
marf1.gate -> notes1.gate
notes1 -> out
end
)pulse" },
        { "09 Groove Swing Ratchet", "Make the clock feel alive with swing, ratchets, and groove weighting.", R"pulse(patch groove_ratchet_arp

midi in keys
  channel 1
end

clock metro
  every 1/16
  swing 60%
  ratchet 3
  groove 100 92 108 96
end

quantize in_key
  scale D dorian
end

arp arp1
  gate 62%
end

midi out out
end

keys -> in_key
in_key -> arp1
metro -> arp1.trigger
arp1 -> out
end
)pulse" },
        { "10 Bitwise MIDI Filters", "Apply low-level bit operations only to the MIDI events you care about.", R"pulse(patch tutorial_filtered_bits

midi in keys
end

bits sculpt_notes
  status notes
  note C3..C5
  velocity 85..127
  target velocity
  and 11110000b
end

midi out out
end

keys -> sculpt_notes
sculpt_notes -> out
end
)pulse" },
        { "11 Temporal Smear", "Blend the present note with recent history for harmonic afterimages.", R"pulse(patch fibonacci_smear

clock metro
  every 1/16
end

fibonacci fib1
  length 8
  map C3 D3 F3 G3 A3
end

notes notes1
  scale D dorian
  gate 52%
end

smear bergson
  keep 3 notes
  weights 0.60 0.25 0.15
  drift weights 0.05
end

midi out out
end

metro -> fib1.trigger
fib1 -> notes1
notes1 -> bergson
bergson -> out
end
)pulse" },
        { "12 Warped Pitch Space", "Fold pitch neighborhoods and add wormholes before converting back to notes.", R"pulse(patch warped_pitch_space

clock metro
  every 1/16
end

pattern roots
  notes C3 D3 E3 G3
  order up_down
end

warp warp1
  fold C near F#
  wormhole every random 3..6
  wormhole to A2 Eb4 F#4
  amount 0.72
  seed 19
end

notes notes1
  scale C lydian
  range C2..C5
  velocity 102
end

midi out out
end

metro -> roots.trigger
roots -> warp1.pitch
warp1 -> notes1
notes1 -> out
end
)pulse" },
        { "13 Crystal Growth", "Grow harmony from ratio families and steer it with density and phrase targets.", R"pulse(patch crystal_growth

clock metro
  every 1/16
end

progression plan
  targets tonic dominant tonic subdominant tonic
  lengths 2 2 2 2 4
end

growth crystal
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

notes notes1
  scale C lydian
  range C2..C5
  velocity 98
end

midi out out
end

metro -> crystal.trigger
metro -> plan.trigger
plan -> crystal.phrase
crystal -> notes1
crystal.density -> notes1.velocity
crystal.gate -> notes1.gate
notes1 -> out
end
)pulse" },
        { "14 Constraint Collapse", "Write named rule sets and let the melody break, reform, and follow section changes.", R"pulse(patch constraint_collapse

clock metro
  every 1/16
end

progression form
  targets tonic dominant tonic subdominant tonic
  lengths 2 2 2 2 4
end

section scenes
  start verse
  section verse tonic 2 dominant 2
  section chorus dominant 2 tonic 2
  section bridge subdominant 2 tonic 2
end

collapse engine1
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

notes notes1
  scale C minor
  range C2..C5
  velocity 100
  gate 62%
end

midi out out
end

metro -> engine1.trigger
metro -> form.trigger
metro -> scenes.trigger
form -> engine1.phrase
scenes.section -> engine1.section
engine1 -> notes1
notes1 -> out
end
)pulse" },
        { "15 Complex Modulator", "Use Fabric's modulator for multi-channel staged control, per-stage modulation, and stage activity routing.", R"pulse(patch modulated_modulator

clock metro
  every 1/16
  swing 56%
end

stages mod_depth
  mode loop
  stage 1 level 0.20 time 80ms curve linear
  stage 2 level 0.95 time 120ms curve smooth
  stage 3 level 0.35 time 180ms curve exp
end

stages mod_time
  mode loop
  stage 1 level 0.08 time 90ms curve linear
  stage 2 level 0.22 time 140ms curve smooth
  stage 3 level 0.11 time 200ms curve exp
end

modulator mod1
  channels 2
  mode loop
  overlap on
  stage 1 channel 1 level 0.15 time 70ms overlap 14ms curve linear
  stage 2 channel 1 level 0.85 time 110ms overlap 20ms curve smooth
  stage 3 channel 1 level 0.30 time 160ms overlap 24ms curve exp
  stage 1 channel 2 level 0.10 time 100ms overlap 18ms curve linear
  stage 2 channel 2 level 0.70 time 150ms overlap 28ms curve smooth
  stage 3 channel 2 level 0.20 time 220ms overlap 32ms curve log
end

pattern seq1
  notes C3 D3 F3 A3
  order up_down
end

notes notes1
  scale D dorian
  range C2..C5
end

midi out out
end

metro -> seq1.trigger
metro -> mod1.trigger
seq1 -> notes1
mod_depth -> mod1.ch1_s2_level
mod_time -> mod1.ch2_s1_time
mod1.ch1 -> notes1.velocity
mod1.ch2 -> notes1.time
mod1.ch1_s2_gate -> notes1.gate
notes1 -> out
end
)pulse" },
        { "16 Held Note Random", "Use the notes you are holding as the source for a random walk.", R"pulse(patch random_held_notes

midi in keys
  channel 1
end

clock metro
  every 1/16
end

random chance1
  from held notes
  mode walk
  pass 75%
  avoid repeat
  max step 7
  bias center 0.6
  seed 42
end

notes notes1
  quantize D dorian
  gate 60%
end

midi out out
end

keys -> chance1.in
metro -> chance1.trigger
chance1 -> notes1
notes1 -> out
)pulse" },
        { "17 Section Form Control", "Drive harmony with named sections like verse, chorus, and bridge.", R"pulse(patch section_controlled

clock metro
  every 1/16
end

clock form_clock
  every 1/8
end

progression chords
  targets tonic dominant subdominant tonic
  lengths 2 2 2 2
end

section form
  start verse
  section verse tonic 2 dominant 2
  section chorus tonic 2 subdominant 2 dominant 2 tonic 2
end

notes mover
  chord scale7
  invert 1
  spread 1 octave
  movement nearest
  arrive on 4
  cadence close
end

midi out out
end

metro -> chords.trigger
form_clock -> form.advance
form -> mover.phrase
chords -> mover
mover -> out
)pulse" },
        { "18 Phrase Guided Harmony", "Feed phrase targets into the projector so arrivals feel intentional.", R"pulse(patch phrase_guided_projector

clock metro
  every 1/16
end

phrase plan
  targets tonic dominant tonic subdominant tonic
end

pattern roots
  notes C3 D3 G3 Bb3
  order up
end

notes mover
  chord scale7
  invert 1
  spread 1 octave
  movement nearest
  arrive on 4
  cadence close
end

midi out out
end

metro -> plan.trigger
metro -> roots.trigger
roots -> mover
plan -> mover.phrase
mover -> out
)pulse" },
        { "19 Multi-Agent Swarm", "Let multiple simple agent roles negotiate the melodic output.", R"pulse(patch swarm_machine

clock metro
  every 1/16
end

swarm flock
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

notes notes1
  scale D dorian
  range C2..C5
  velocity 98
  gate 58%
end

midi out out
end

metro -> flock.trigger
flock -> notes1
notes1 -> out
)pulse" },
        { "20 Growth And Modulator", "Combine harmonic growth with staged modulation for dynamics and phrasing.", R"pulse(patch growth_modulator

clock metro
  every 1/16
end

progression plan
  targets tonic dominant tonic subdominant tonic
  lengths 2 2 2 2 4
end

growth crystal
  root C3
  family perfect weight 1.3 ratios 3/2 4/3
  family color weight 0.9 ratios 5/4 7/4 9/8
  register mid
  target tonic
  add when stable
  prune when unstable
  prune strength 2
  fold octaves just
  density drives rate
  max notes 8
end

modulator mod1
  channels 2
  mode loop
  overlap on
  stage 1 channel 1 level 0.18 time 90ms overlap 16ms curve linear
  stage 2 channel 1 level 0.92 time 130ms overlap 22ms curve smooth
  stage 3 channel 1 level 0.36 time 190ms overlap 28ms curve exp
  stage 1 channel 2 level 0.08 time 110ms overlap 18ms curve linear
  stage 2 channel 2 level 0.62 time 170ms overlap 28ms curve smooth
  stage 3 channel 2 level 0.14 time 240ms overlap 34ms curve log
end

notes notes1
  scale C lydian
  range C2..C5
end

midi out out
end

metro -> crystal.trigger
metro -> plan.trigger
metro -> mod1.trigger
plan -> crystal.phrase
crystal -> notes1
crystal.density -> notes1.gate
mod1.ch1 -> notes1.velocity
mod1.ch2 -> notes1.time
notes1 -> out
)pulse" },
        { "21 Modulator Stage Gates", "Use stage gate and end outputs as patch logic for other modules.", R"pulse(patch modulator_stage_gates

clock metro
  every 1/16
end

modulator mod1
  channels 1
  mode loop
  overlap on
  stage 1 channel 1 level 0.18 time 80ms overlap 12ms curve linear
  stage 2 channel 1 level 0.92 time 120ms overlap 18ms curve smooth
  stage 3 channel 1 level 0.30 time 180ms overlap 24ms curve exp
end

pattern riff
  notes C3 D3 F3 A3
  order up_down
end

notes notes1
  scale D dorian
  range C2..C5
  velocity 104
end

midi out out
end

metro -> mod1.trigger
mod1.ch1_s1_end -> riff.trigger
riff -> notes1
mod1.ch1_s2_gate -> notes1.gate
mod1.ch1 -> notes1.time
notes1 -> out
)pulse" },
        { "22 CC Bit Glitch", "Target a controller lane with scoped bitwise processing.", R"pulse(patch cc_glitch

midi in keys
end

bits glitch_modwheel
  status cc
  cc 1
  channel 1
  target data2
  xor 00011111b
end

midi out out
end

keys -> glitch_modwheel
glitch_modwheel -> out
)pulse" },
        { "23 Loop Into Cut-Up", "Capture a phrase with a looper and recombine it with cut-up logic.", R"pulse(patch loop_into_cutup

midi in keys
  channel 1
end

clock metro
  every 1/16
end

loop phrase1
  capture 1/8
  playback on
  overdub off
  quantize to 1/32
end

cutup burroughs
  capture 1/16
  slice 2
  keep continuity 0.35
  favor harmonic
  seed 42
end

midi out out
end

keys -> phrase1
phrase1 -> burroughs.in
metro -> burroughs.trigger
burroughs -> out
)pulse" }
    };
    return entries;
}

juce::StringArray FabricAudioProcessor::getTutorialNames() const
{
    juce::StringArray names;
    for (const auto& tutorial : tutorials()) {
        names.add(tutorial.name);
    }
    return names;
}

std::optional<FabricAudioProcessor::TutorialInfo> FabricAudioProcessor::getTutorial(int index) const
{
    const auto& entries = tutorials();
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        return std::nullopt;
    }
    const auto& entry = entries[static_cast<std::size_t>(index)];
    return TutorialInfo { entry.name, entry.summary, entry.script };
}

FabricAudioProcessor::CompileResult FabricAudioProcessor::buildCompileResult(const juce::String& scriptText) const
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

void FabricAudioProcessor::applyCompileResult(CompileResult result, std::uint64_t requestId)
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
            for (auto& node : graphSnapshot_.nodes) {
                if (const auto found = nodeModes_.find(node.name); found != nodeModes_.end()) {
                    node.mode = found->second;
                }
            }
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

FabricAudioProcessor::GraphSnapshot FabricAudioProcessor::graphFromEngine(const pulse::Engine& engine)
{
    GraphSnapshot snapshot;
    if (engine.graph() == nullptr) {
        return snapshot;
    }

    const auto& patch = engine.graph()->patch();
    snapshot.nodes.reserve(patch.nodes.size());
    for (std::size_t index = 0; index < patch.nodes.size(); ++index) {
        const auto& node = patch.nodes[index];
        snapshot.nodes.push_back({
            node.name,
            juce::String(pulse::toString(node.family)),
            node.kind,
            {},
            static_cast<int>(patch.source.modules[index].location.line),
            NodeProcessingMode::normal
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

std::vector<FabricAudioProcessor::UiDiagnostic> FabricAudioProcessor::diagnosticsFromPulse(const std::vector<pulse::Diagnostic>& diagnostics)
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

juce::String FabricAudioProcessor::diagnosticsToText(const std::vector<UiDiagnostic>& diagnostics)
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

std::vector<pulse::Event> FabricAudioProcessor::midiBufferToEvents(const juce::MidiBuffer& midi)
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

void FabricAudioProcessor::eventsToMidiBuffer(const std::vector<pulse::Event>& events, double sampleRate, int blockSize, juce::MidiBuffer& midi)
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
    return new FabricAudioProcessor();
}
