#include "pulse/JuceIntegration.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace {

std::string readFile(const std::string& path)
{
    std::ifstream input(path);
    if (!input) return {};
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

void printEvent(const pulse::Event& event)
{
    if (event.isNoteOn()) {
        std::cout << "note_on"
                  << " note=" << event.noteNumber()
                  << " velocity=" << event.velocityValue()
                  << " channel=" << event.channel()
                  << " time=" << event.time
                  << '\n';
        return;
    }

    if (event.isNoteOff()) {
        std::cout << "note_off"
                  << " note=" << event.noteNumber()
                  << " velocity=" << event.velocityValue()
                  << " channel=" << event.channel()
                  << " time=" << event.time
                  << '\n';
        return;
    }

    std::cout << pulse::toString(event.type)
              << " time=" << event.time;
    if (!event.floats.empty()) {
        std::cout << " value=" << event.floats.front();
    }
    std::cout << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    const std::string path = argc > 1 ? argv[1] : "examples/live_memory_machine.pulse";
    const auto source = readFile(path);
    if (source.empty()) {
        std::cerr << "Could not read patch: " << path << '\n';
        return 1;
    }

    pulse::Engine engine;
    if (!engine.loadPatchText(source)) {
        for (const auto& diagnostic : engine.diagnostics()) {
            std::cerr << diagnostic.message << " at " << diagnostic.location.line << ":" << diagnostic.location.column << '\n';
        }
        return 1;
    }

    engine.reset(48000.0, 256);

    pulse::ProcessContext context;
    context.sampleRate = 48000.0;
    context.blockSize = 256;
    context.bpm = 120.0;

    const bool usesKeysInput = source.find("input midi keys") != std::string::npos;
    const bool usesMotion = source.find("analyze motion") != std::string::npos;
    const bool usesLoop = source.find("transform loop") != std::string::npos;
    const bool usesBounce = source.find("transform bounce") != std::string::npos;
    const bool usesCutup = source.find("memory cutup") != std::string::npos;

    for (int block = 0; block < 40; ++block) {
        if (usesKeysInput) {
            if (usesCutup) {
                if (block == 0) {
                    engine.setInputEvents("keys", {
                        pulse::Event::makeNoteOn(60, 100, 1, 0.0)
                    });
                } else if (block == 1) {
                    engine.setInputEvents("keys", {
                        pulse::Event::makeNoteOff(60, 0, 1, 0.0),
                        pulse::Event::makeNoteOn(64, 100, 1, 0.004)
                    });
                } else if (block == 2) {
                    engine.setInputEvents("keys", {
                        pulse::Event::makeNoteOff(64, 0, 1, 0.0),
                        pulse::Event::makeNoteOn(67, 100, 1, 0.003)
                    });
                } else if (block == 3) {
                    engine.setInputEvents("keys", {
                        pulse::Event::makeNoteOff(67, 0, 1, 0.0),
                        pulse::Event::makeNoteOn(62, 100, 1, 0.002)
                    });
                } else if (block == 4) {
                    engine.setInputEvents("keys", {
                        pulse::Event::makeNoteOff(62, 0, 1, 0.0)
                    });
                } else {
                    engine.setInputEvents("keys", {});
                }
            } else if (usesBounce) {
                if (block == 0) {
                    engine.setInputEvents("keys", {
                        pulse::Event::makeNoteOn(60, 100, 1, 0.0)
                    });
                } else {
                    engine.setInputEvents("keys", {});
                }
            } else if (usesLoop) {
                if (block == 0) {
                    engine.setInputEvents("keys", {
                        pulse::Event::makeNoteOn(60, 100, 1, 0.0)
                    });
                } else if (block == 2) {
                    engine.setInputEvents("keys", {
                        pulse::Event::makeNoteOff(60, 0, 1, 0.0),
                        pulse::Event::makeNoteOn(64, 100, 1, 0.004)
                    });
                } else if (block == 4) {
                    engine.setInputEvents("keys", {
                        pulse::Event::makeNoteOff(64, 0, 1, 0.0),
                        pulse::Event::makeNoteOn(67, 100, 1, 0.003)
                    });
                } else if (block == 6) {
                    engine.setInputEvents("keys", {
                        pulse::Event::makeNoteOff(67, 0, 1, 0.0)
                    });
                } else {
                    engine.setInputEvents("keys", {});
                }
            } else if (usesMotion && (block == 0 || block == 8 || block == 16 || block == 24)) {
                const int note = (block == 0) ? 36 : (block == 8) ? 48
                    : (block == 16) ? 60
                                     : 72;
                engine.setInputEvents("keys", {
                    pulse::Event::makeNoteOn(note, 100, 1, 0.0)
                });
            } else if (block == 0) {
                engine.setInputEvents("keys", {
                    pulse::Event::makeNoteOn(36, 96, 1, 0.0),
                    pulse::Event::makeNoteOn(61, 100, 1, 0.0),
                    pulse::Event::makeNoteOn(64, 100, 1, 0.0),
                    pulse::Event::makeNoteOn(67, 100, 1, 0.0),
                    pulse::Event::makeNoteOn(84, 110, 1, 0.0)
                });
            } else if (block == 28) {
                engine.setInputEvents("keys", {
                    pulse::Event::makeNoteOff(36, 0, 1, 0.0),
                    pulse::Event::makeNoteOff(61, 0, 1, 0.0),
                    pulse::Event::makeNoteOff(64, 0, 1, 0.0),
                    pulse::Event::makeNoteOff(67, 0, 1, 0.0),
                    pulse::Event::makeNoteOff(84, 0, 1, 0.0)
                });
            } else {
                engine.setInputEvents("keys", {});
            }
        }

        engine.process(context);
        const auto output = engine.outputEvents("out");
        if (!output.empty()) {
            std::cout << "Block " << block << " output events: " << output.size() << '\n';
            for (const auto& event : output) {
                printEvent(event);
            }
        }
    }

    return 0;
}
