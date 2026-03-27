#include "pulse/JuceIntegration.hpp"

namespace pulse {

bool Engine::loadPatchText(const std::string& source)
{
    diagnostics_.clear();
    runtime_.reset();
    compiled_.reset();
    patch_.reset();

    patch_ = parser_.parse(source, diagnostics_);
    if (!patch_.has_value()) return false;

    compiled_ = compiler_.compile(*patch_, diagnostics_);
    if (!compiled_.has_value()) return false;

    runtime_ = std::make_unique<RuntimeGraph>(*compiled_);
    return true;
}

void Engine::reset(double sampleRate, std::uint32_t blockSize)
{
    (void)blockSize;
    if (runtime_) {
        runtime_->reset(sampleRate);
    }
}

void Engine::process(const ProcessContext& context)
{
    if (runtime_) {
        runtime_->process(context);
    }
}

void Engine::setInputEvents(const std::string& moduleName, const std::vector<Event>& events)
{
    if (runtime_) {
        runtime_->setInputEvents(moduleName, events);
    }
}

void Engine::setNodeMode(const std::string& moduleName, NodeProcessingMode mode)
{
    if (runtime_) {
        runtime_->setNodeMode(moduleName, mode);
    }
}

NodeProcessingMode Engine::nodeMode(const std::string& moduleName) const
{
    if (!runtime_) return NodeProcessingMode::normal;
    return runtime_->nodeMode(moduleName);
}

const std::vector<Diagnostic>& Engine::diagnostics() const
{
    return diagnostics_;
}

const RuntimeGraph* Engine::graph() const
{
    return runtime_.get();
}

std::vector<Event> Engine::outputEvents(const std::string& moduleName) const
{
    if (!runtime_) return {};
    return runtime_->outputEvents(moduleName);
}

std::optional<int> Engine::currentSectionIndex(const std::string& moduleName) const
{
    if (!runtime_) return std::nullopt;
    return runtime_->currentSectionIndex(moduleName);
}

std::optional<double> Engine::currentSectionPhase(const std::string& moduleName) const
{
    if (!runtime_) return std::nullopt;
    return runtime_->currentSectionPhase(moduleName);
}

std::optional<std::uint64_t> Engine::sectionAdvanceCount(const std::string& moduleName) const
{
    if (!runtime_) return std::nullopt;
    return runtime_->sectionAdvanceCount(moduleName);
}

std::optional<std::string> Engine::activeStateLabel(const std::string& moduleName) const
{
    if (!runtime_) return std::nullopt;
    return runtime_->activeStateLabel(moduleName);
}

std::optional<ModulatorStateSnapshot> Engine::modulatorState(const std::string& moduleName) const
{
    if (!runtime_) return std::nullopt;
    return runtime_->modulatorState(moduleName);
}

} // namespace pulse
