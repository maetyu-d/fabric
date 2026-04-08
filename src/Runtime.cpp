#include "pulse/Runtime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <string_view>
#include <tuple>

namespace pulse {

namespace {

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

constexpr double kPi = 3.14159265358979323846;

enum class DistributionKind {
    uniform,
    gaussian,
    poisson,
    brownian,
    burst
};

struct DistributionConfig {
    DistributionKind kind = DistributionKind::uniform;
    double lambda = 3.0;
    double burst = 0.6;
};

std::optional<std::string> findPropertyValue(const Module& module, const std::string& key);

DistributionKind parseDistributionKind(const std::string& value)
{
    if (value == "gaussian") return DistributionKind::gaussian;
    if (value == "poisson") return DistributionKind::poisson;
    if (value == "brownian") return DistributionKind::brownian;
    if (value == "burst") return DistributionKind::burst;
    return DistributionKind::uniform;
}

DistributionConfig parseDistributionConfig(const Module& module)
{
    DistributionConfig config;
    if (const auto distribution = findPropertyValue(module, "distribution")) {
        config.kind = parseDistributionKind(*distribution);
    }
    if (const auto lambda = findPropertyValue(module, "lambda")) {
        config.lambda = std::max(0.1, std::stod(*lambda));
    }
    if (const auto burst = findPropertyValue(module, "burst")) {
        config.burst = std::clamp(std::stod(*burst), 0.0, 0.98);
    }
    return config;
}

template <typename NextUnitFn>
double gaussianSample(NextUnitFn&& nextUnit)
{
    const auto u1 = std::max(1.0e-6, nextUnit());
    const auto u2 = std::max(1.0e-6, nextUnit());
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
}

template <typename NextUnitFn>
int poissonSample(double lambda, NextUnitFn&& nextUnit)
{
    const auto limit = std::exp(-std::max(0.1, lambda));
    double product = 1.0;
    int count = 0;
    do {
        ++count;
        product *= std::max(1.0e-6, nextUnit());
    } while (product > limit && count < 64);
    return std::max(0, count - 1);
}

template <typename NextUnitFn>
double distributionPhase01(const DistributionConfig& config, double& memory, NextUnitFn&& nextUnit)
{
    switch (config.kind) {
    case DistributionKind::gaussian:
        return clamp01(0.5 + (gaussianSample(nextUnit) * 0.18));
    case DistributionKind::poisson: {
        const auto sample = static_cast<double>(poissonSample(config.lambda, nextUnit));
        return clamp01(sample / std::max(1.0, config.lambda * 3.0));
    }
    case DistributionKind::brownian:
        memory = clamp01(memory + (gaussianSample(nextUnit) * 0.12));
        return memory;
    case DistributionKind::burst:
        memory = clamp01((memory * config.burst) + 0.5 + (gaussianSample(nextUnit) * 0.10));
        return memory;
    case DistributionKind::uniform:
    default:
        memory = nextUnit();
        return memory;
    }
}

template <typename NextUnitFn>
double distributionOffset(const DistributionConfig& config, double spread, double& memory, NextUnitFn&& nextUnit)
{
    switch (config.kind) {
    case DistributionKind::gaussian:
        return std::clamp(gaussianSample(nextUnit) * std::max(0.5, spread * 0.33), -spread, spread);
    case DistributionKind::poisson: {
        const auto centered = static_cast<double>(poissonSample(config.lambda, nextUnit)) - config.lambda;
        return std::clamp(centered / std::max(1.0, config.lambda) * std::max(0.5, spread * 0.45), -spread, spread);
    }
    case DistributionKind::brownian:
        memory = std::clamp(memory + (gaussianSample(nextUnit) * std::max(0.35, spread * 0.12)), -spread, spread);
        return memory;
    case DistributionKind::burst:
        memory = std::clamp((memory * config.burst) + (gaussianSample(nextUnit) * std::max(0.25, spread * 0.10)), -spread, spread);
        return memory;
    case DistributionKind::uniform:
    default:
        memory = (nextUnit() * 2.0 - 1.0) * spread;
        return memory;
    }
}

double applyCurve(const std::string& curve, double t)
{
    t = clamp01(t);
    if (curve == "exp") return t * t;
    if (curve == "log") return std::sqrt(t);
    if (curve == "smooth") return t * t * (3.0 - 2.0 * t);
    return t;
}

double parseTimeToken(const std::string& token, double bpm)
{
    if (token.ends_with("ms")) {
        return std::stod(token.substr(0, token.size() - 2)) / 1000.0;
    }

    if (token.ends_with('s') && !token.ends_with("ms")) {
        return std::stod(token.substr(0, token.size() - 1));
    }

    if (token.ends_with('m')) {
        return std::stod(token.substr(0, token.size() - 1)) * 60.0;
    }

    const auto slash = token.find('/');
    if (slash != std::string::npos) {
        const auto numerator = std::stod(token.substr(0, slash));
        const auto denominator = std::stod(token.substr(slash + 1));
        const auto beatLengthSeconds = 60.0 / bpm;
        return (numerator * 4.0 / denominator) * beatLengthSeconds;
    }

    return std::stod(token);
}

int clampMidiNote(int note)
{
    return std::clamp(note, 0, 127);
}

int clampMidiData(int value)
{
    return std::clamp(value, 0, 127);
}

int clampMidiChannel(int value)
{
    return std::clamp(value, 1, 16);
}

int parseBitValue(std::string_view token)
{
    if (token.empty()) return 0;

    if (token.ends_with("b") || token.ends_with("B")) {
        int value = 0;
        for (const auto ch : token.substr(0, token.size() - 1)) {
            if (ch != '0' && ch != '1') continue;
            value = (value << 1) | (ch - '0');
        }
        return value;
    }

    return std::stoi(std::string(token), nullptr, 0);
}

int parseNoteName(std::string_view token)
{
    if (token.empty()) return 60;
    if (std::isdigit(static_cast<unsigned char>(token.front())) != 0 || token.front() == '-' || token.front() == '+') {
        return clampMidiNote(std::stoi(std::string(token)));
    }

    static constexpr std::pair<std::string_view, int> names[] = {
        { "C", 0 }, { "C#", 1 }, { "Db", 1 }, { "D", 2 }, { "D#", 3 }, { "Eb", 3 },
        { "E", 4 }, { "F", 5 }, { "F#", 6 }, { "Gb", 6 }, { "G", 7 }, { "G#", 8 },
        { "Ab", 8 }, { "A", 9 }, { "A#", 10 }, { "Bb", 10 }, { "B", 11 }
    };

    std::string notePart;
    std::string octavePart;
    for (const auto ch : token) {
        if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '#' || ch == 'b') {
            notePart.push_back(ch);
        } else {
            octavePart.push_back(ch);
        }
    }

    int semitone = 0;
    for (const auto& [name, value] : names) {
        if (name == notePart) {
            semitone = value;
            break;
        }
    }

    const int octave = octavePart.empty() ? 4 : std::stoi(octavePart);
    return clampMidiNote((octave + 1) * 12 + semitone);
}

std::optional<std::string> findPropertyValue(const Module& module, const std::string& key)
{
    for (const auto& property : module.properties) {
        if (property.key == key && !property.values.empty()) {
            return property.values.front();
        }
    }
    return std::nullopt;
}

std::vector<std::string> findPropertyValues(const Module& module, const std::string& key)
{
    for (const auto& property : module.properties) {
        if (property.key == key) {
            return property.values;
        }
    }
    return {};
}

std::vector<int> scaleSemitones(const std::vector<std::string>& values)
{
    if (values.size() < 2) return { 0, 2, 4, 5, 7, 9, 11 };

    const auto tonic = values[0];
    const auto quality = values[1];

    static const std::unordered_map<std::string, std::vector<int>> scales {
        { "major", { 0, 2, 4, 5, 7, 9, 11 } },
        { "minor", { 0, 2, 3, 5, 7, 8, 10 } },
        { "dorian", { 0, 2, 3, 5, 7, 9, 10 } },
        { "lydian", { 0, 2, 4, 6, 7, 9, 11 } }
    };

    auto found = scales.find(quality);
    auto semitones = found != scales.end() ? found->second : scales.at("major");
    const auto root = parseNoteName(tonic) % 12;
    for (auto& semitone : semitones) {
        semitone = (semitone + root) % 12;
    }
    std::sort(semitones.begin(), semitones.end());
    return semitones;
}

int quantizeToScale(int note, const std::vector<int>& scale)
{
    if (scale.empty()) return clampMidiNote(note);

    const int octave = note / 12;
    const int pitchClass = ((note % 12) + 12) % 12;

    int bestNote = note;
    int bestDistance = std::numeric_limits<int>::max();
    for (int octaveOffset = -1; octaveOffset <= 1; ++octaveOffset) {
        for (const auto scalePitchClass : scale) {
            const int candidate = (octave + octaveOffset) * 12 + scalePitchClass;
            const int distance = std::abs(candidate - note);
            if (distance < bestDistance || (distance == bestDistance && candidate >= note)) {
                bestDistance = distance;
                bestNote = candidate;
            }
        }
    }

    if (bestDistance == std::numeric_limits<int>::max()) {
        return clampMidiNote(note - pitchClass + scale.front());
    }

    return clampMidiNote(bestNote);
}

struct Stage {
    double level = 0.0;
    double durationSeconds = 0.25;
    double overlapSeconds = 0.0;
    std::string curve = "linear";
};

std::vector<int> parsePitchList(const std::vector<std::string>& values)
{
    std::vector<int> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(parseNoteName(value));
    }
    return result;
}

double parseChoiceScalar(const std::string& token)
{
    if (token.empty()) return 0.0;
    if (std::isdigit(static_cast<unsigned char>(token.front())) != 0 || token.front() == '-' || token.front() == '+') {
        return std::stod(token);
    }
    return static_cast<double>(parseNoteName(token));
}

double parseGateScalar(const std::string& token)
{
    if (token == "on" || token == "yes" || token == "true") return 1.0;
    if (token == "off" || token == "no" || token == "false") return 0.0;
    if (!token.empty() && token.back() == '%') {
        return clamp01(std::stod(token.substr(0, token.size() - 1)) / 100.0);
    }
    return clamp01(parseChoiceScalar(token));
}

template <typename NextUnitFn>
std::size_t weightedIndex(const std::vector<double>& weights, NextUnitFn&& nextUnit)
{
    if (weights.empty()) {
        return 0;
    }

    double total = 0.0;
    for (const auto weight : weights) {
        total += std::max(0.01, weight);
    }

    auto choice = nextUnit() * total;
    for (std::size_t index = 0; index < weights.size(); ++index) {
        choice -= std::max(0.01, weights[index]);
        if (choice <= 0.0) {
            return index;
        }
    }

    return weights.size() - 1;
}

std::vector<double> parseGroovePattern(const std::vector<std::string>& values)
{
    if (values.size() == 1) {
        const auto name = values.front();
        if (name == "straight") return { 1.0, 1.0, 1.0, 1.0 };
        if (name == "shuffle") return { 1.14, 0.86, 1.14, 0.86 };
        if (name == "push") return { 0.92, 1.08, 0.94, 1.06 };
        if (name == "lag") return { 1.08, 0.92, 1.06, 0.94 };
        if (name == "machine") return { 1.00, 0.94, 1.10, 0.96 };
    }

    std::vector<double> pattern;
    for (const auto& value : values) {
        double parsed = std::stod(value);
        if (parsed > 4.0) {
            parsed /= 100.0;
        }
        pattern.push_back(std::max(0.05, parsed));
    }
    return pattern;
}

std::string joinTokens(const std::vector<std::string>& values, std::size_t startIndex = 0)
{
    std::string result;
    for (std::size_t i = startIndex; i < values.size(); ++i) {
        if (!result.empty()) {
            result += ' ';
        }
        result += values[i];
    }
    return result;
}

struct ExprNode {
    enum class Kind {
        number,
        variable,
        unary,
        binary,
        call
    };

    Kind kind = Kind::number;
    double number = 0.0;
    std::string text;
    std::vector<std::unique_ptr<ExprNode>> args;
};

class ExpressionParser {
public:
    explicit ExpressionParser(std::string text)
        : text_(std::move(text))
    {
    }

    std::unique_ptr<ExprNode> parse()
    {
        auto trimmed = text_;
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        if (!trimmed.empty() && trimmed.front() == '=') {
            trimmed.erase(trimmed.begin());
        }
        text_ = trimmed;
        pos_ = 0;
        auto node = parseExpression();
        skipSpace();
        if (pos_ != text_.size()) {
            return {};
        }
        return node;
    }

private:
    std::unique_ptr<ExprNode> parseExpression()
    {
        auto node = parseTerm();
        while (node) {
            skipSpace();
            if (match('+') || match('-')) {
                const auto op = text_[pos_ - 1];
                auto rhs = parseTerm();
                if (!rhs) return {};
                auto combined = std::make_unique<ExprNode>();
                combined->kind = ExprNode::Kind::binary;
                combined->text = std::string(1, op);
                combined->args.push_back(std::move(node));
                combined->args.push_back(std::move(rhs));
                node = std::move(combined);
            } else {
                break;
            }
        }
        return node;
    }

    std::unique_ptr<ExprNode> parseTerm()
    {
        auto node = parsePower();
        while (node) {
            skipSpace();
            if (match('*') || match('/') || match('%')) {
                const auto op = text_[pos_ - 1];
                auto rhs = parsePower();
                if (!rhs) return {};
                auto combined = std::make_unique<ExprNode>();
                combined->kind = ExprNode::Kind::binary;
                combined->text = std::string(1, op);
                combined->args.push_back(std::move(node));
                combined->args.push_back(std::move(rhs));
                node = std::move(combined);
            } else {
                break;
            }
        }
        return node;
    }

    std::unique_ptr<ExprNode> parsePower()
    {
        auto node = parseUnary();
        while (node) {
            skipSpace();
            if (!match('^')) break;
            auto rhs = parseUnary();
            if (!rhs) return {};
            auto combined = std::make_unique<ExprNode>();
            combined->kind = ExprNode::Kind::binary;
            combined->text = "^";
            combined->args.push_back(std::move(node));
            combined->args.push_back(std::move(rhs));
            node = std::move(combined);
        }
        return node;
    }

    std::unique_ptr<ExprNode> parseUnary()
    {
        skipSpace();
        if (match('+') || match('-')) {
            const auto op = text_[pos_ - 1];
            auto node = parseUnary();
            if (!node) return {};
            auto unary = std::make_unique<ExprNode>();
            unary->kind = ExprNode::Kind::unary;
            unary->text = std::string(1, op);
            unary->args.push_back(std::move(node));
            return unary;
        }
        return parsePrimary();
    }

    std::unique_ptr<ExprNode> parsePrimary()
    {
        skipSpace();
        if (match('(')) {
            auto node = parseExpression();
            skipSpace();
            if (!match(')')) return {};
            return node;
        }

        if (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.')) {
            return parseNumber();
        }

        if (pos_ < text_.size() && (std::isalpha(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_')) {
            auto ident = parseIdentifier();
            if (ident.empty()) return {};
            skipSpace();
            if (!match('(')) {
                auto variable = std::make_unique<ExprNode>();
                variable->kind = ExprNode::Kind::variable;
                variable->text = ident;
                return variable;
            }

            auto call = std::make_unique<ExprNode>();
            call->kind = ExprNode::Kind::call;
            call->text = ident;
            skipSpace();
            if (match(')')) {
                return call;
            }

            while (true) {
                auto arg = parseExpression();
                if (!arg) return {};
                call->args.push_back(std::move(arg));
                skipSpace();
                if (match(')')) {
                    break;
                }
                if (!match(',')) return {};
            }
            return call;
        }

        return {};
    }

    std::unique_ptr<ExprNode> parseNumber()
    {
        const auto start = pos_;
        bool seenDot = false;
        while (pos_ < text_.size()) {
            const auto ch = text_[pos_];
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                ++pos_;
                continue;
            }
            if (ch == '.' && !seenDot) {
                seenDot = true;
                ++pos_;
                continue;
            }
            break;
        }

        auto node = std::make_unique<ExprNode>();
        node->kind = ExprNode::Kind::number;
        node->number = std::stod(text_.substr(start, pos_ - start));
        return node;
    }

    std::string parseIdentifier()
    {
        const auto start = pos_;
        while (pos_ < text_.size()) {
            const auto ch = text_[pos_];
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
                ++pos_;
            } else {
                break;
            }
        }
        return text_.substr(start, pos_ - start);
    }

    void skipSpace()
    {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) {
            ++pos_;
        }
    }

    bool match(char ch)
    {
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == ch) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::string text_;
    std::size_t pos_ = 0;
};

double evaluateExpression(const ExprNode* node, const std::unordered_map<std::string, double>& vars)
{
    if (!node) return 0.0;

    switch (node->kind) {
    case ExprNode::Kind::number:
        return node->number;
    case ExprNode::Kind::variable: {
        if (node->text == "pi") return kPi;
        const auto it = vars.find(node->text);
        return it != vars.end() ? it->second : 0.0;
    }
    case ExprNode::Kind::unary: {
        const auto value = evaluateExpression(node->args.front().get(), vars);
        return node->text == "-" ? -value : value;
    }
    case ExprNode::Kind::binary: {
        const auto left = evaluateExpression(node->args[0].get(), vars);
        const auto right = evaluateExpression(node->args[1].get(), vars);
        if (node->text == "+") return left + right;
        if (node->text == "-") return left - right;
        if (node->text == "*") return left * right;
        if (node->text == "/") return std::abs(right) < 1.0e-9 ? 0.0 : left / right;
        if (node->text == "%") return std::abs(right) < 1.0e-9 ? 0.0 : std::fmod(left, right);
        if (node->text == "^") return std::pow(left, right);
        return 0.0;
    }
    case ExprNode::Kind::call: {
        const auto arg = [&](std::size_t index, double fallback = 0.0) {
            return index < node->args.size() ? evaluateExpression(node->args[index].get(), vars) : fallback;
        };
        if (node->text == "sin") return std::sin(arg(0));
        if (node->text == "cos") return std::cos(arg(0));
        if (node->text == "tan") return std::tan(arg(0));
        if (node->text == "abs") return std::abs(arg(0));
        if (node->text == "sqrt") return std::sqrt(std::max(0.0, arg(0)));
        if (node->text == "floor") return std::floor(arg(0));
        if (node->text == "ceil") return std::ceil(arg(0));
        if (node->text == "round") return std::round(arg(0));
        if (node->text == "min") return std::min(arg(0), arg(1));
        if (node->text == "max") return std::max(arg(0), arg(1));
        if (node->text == "clamp") return std::clamp(arg(0), arg(1), arg(2));
        return 0.0;
    }
    }

    return 0.0;
}

std::unique_ptr<ExprNode> parseEquationExpression(const std::vector<std::string>& values)
{
    auto parser = ExpressionParser(joinTokens(values));
    return parser.parse();
}

class InputMidiNode final : public RuntimeNode {
public:
    void reset(double) override
    {
        pending_.clear();
    }

    void setExternalEvents(const std::vector<Event>& events) override
    {
        pending_ = events;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>&,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) return;
        out->second->append(pending_);
        pending_.clear();
    }

private:
    std::vector<Event> pending_;
};

class MotionNode final : public RuntimeNode {
public:
    explicit MotionNode(const Module& module)
    {
        if (const auto channels = findPropertyValue(module, "channels")) {
            channelCount_ = std::clamp(std::stoi(*channels), 1, 8);
        }
        if (const auto space = findPropertyValue(module, "space")) {
            space_ = std::clamp(std::stod(*space), 0.05, 1.0);
        }
        if (const auto clocked = findPropertyValue(module, "clocked")) {
            clocked_ = (*clocked == "on");
        }
    }

    void reset(double) override
    {
        lastNoteTime_.reset();
        pendingZone_.reset();
        pendingSpeed_.reset();
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        bool clockPulse = false;
        if (const auto clock = inputs.find("clock"); clock != inputs.end()) {
            clockPulse = !clock->second->events().empty();
        }

        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        if (const auto input = inputs.find("in"); input != inputs.end()) {
            for (const auto& event : input->second->events()) {
                if (!event.isNoteOn()) continue;

                const auto zone = zoneForNote(event.noteNumber());
                const auto speed = speedForEvent(event.time, blockDuration);

                if (clocked_) {
                    pendingZone_ = zone;
                    pendingSpeed_ = speed;
                } else {
                    emitZone(zone, event.time, outputs);
                    emitSpeed(speed, event.time, outputs);
                }

                lastNoteTime_ = absoluteTime_ + event.time;
            }
        }

        if (clocked_ && clockPulse && pendingZone_.has_value()) {
            emitZone(*pendingZone_, 0.0, outputs);
            emitSpeed(pendingSpeed_.value_or(0.0), 0.0, outputs);
            pendingZone_.reset();
            pendingSpeed_.reset();
        }

        absoluteTime_ += blockDuration;
    }

private:
    [[nodiscard]] int zoneForNote(int note) const
    {
        const auto normalized = std::clamp(note / 128.0, 0.0, 0.999);
        return std::min(channelCount_ - 1, static_cast<int>(normalized * channelCount_));
    }

    [[nodiscard]] double speedForEvent(double eventTime, double blockDuration) const
    {
        if (!lastNoteTime_.has_value()) {
            return 0.5;
        }

        const auto currentAbsoluteTime = absoluteTime_ + std::clamp(eventTime, 0.0, blockDuration);
        const auto delta = std::max(currentAbsoluteTime - *lastNoteTime_, 1.0e-4);
        return std::clamp((space_ / delta) * 0.05, 0.0, 1.0);
    }

    static void emitSpeed(double speed, double time, std::unordered_map<std::string, EventBuffer*>& outputs)
    {
        if (const auto out = outputs.find("speed"); out != outputs.end()) {
            out->second->push(Event::makeValue(speed, time));
        }
    }

    void emitZone(int zone, double time, std::unordered_map<std::string, EventBuffer*>& outputs) const
    {
        const auto pulsePort = "pulse" + std::to_string(zone + 1);
        if (const auto pulse = outputs.find(pulsePort); pulse != outputs.end()) {
            pulse->second->push(Event::makeTrigger(time));
        }

        Event evenOdd;
        evenOdd.type = SignalType::gate;
        evenOdd.time = time;
        evenOdd.floats.push_back(1.0);

        if ((zone % 2) == 0) {
            if (const auto even = outputs.find("even"); even != outputs.end()) {
                even->second->push(evenOdd);
            }
        } else {
            if (const auto odd = outputs.find("odd"); odd != outputs.end()) {
                odd->second->push(evenOdd);
            }
        }
    }

    int channelCount_ = 8;
    double space_ = 0.4;
    bool clocked_ = false;
    double absoluteTime_ = 0.0;
    std::optional<double> lastNoteTime_;
    std::optional<int> pendingZone_;
    std::optional<double> pendingSpeed_;
};

class ClockNode final : public RuntimeNode {
public:
    explicit ClockNode(const Module& module)
    {
        if (const auto every = findPropertyValue(module, "every")) {
            everyToken_ = *every;
        }
        if (const auto swing = findPropertyValue(module, "swing")) {
            auto text = *swing;
            if (!text.empty() && text.back() == '%') {
                text.pop_back();
                swing_ = std::clamp(std::stod(text) / 100.0, 0.5, 0.8);
            } else {
                swing_ = std::clamp(std::stod(text), 0.5, 0.8);
            }
        }
        if (const auto ratchet = findPropertyValue(module, "ratchet")) {
            ratchet_ = std::clamp(std::stoi(*ratchet), 1, 8);
        }
        groovePattern_ = parseGroovePattern(findPropertyValues(module, "groove"));
        if (const auto tuplet = findPropertyValue(module, "tuplet")) {
            const auto separator = tuplet->find(':');
            if (separator != std::string::npos) {
                const auto a = std::max(1, std::stoi(tuplet->substr(0, separator)));
                const auto b = std::max(1, std::stoi(tuplet->substr(separator + 1)));
                tupletRatio_ = static_cast<double>(b) / static_cast<double>(a);
            } else {
                const auto n = std::max(1, std::stoi(*tuplet));
                tupletRatio_ = 2.0 / static_cast<double>(n);
            }
        }
        if (const auto chance = findPropertyValue(module, "chance")) {
            stepChance_ = parseProbability(*chance);
        }
        if (const auto pulseChance = findPropertyValues(module, "pulse"); pulseChance.size() >= 2 && pulseChance[0] == "chance") {
            pulseChance_ = parseProbability(pulseChance[1]);
        }
        if (const auto bars = findPropertyValue(module, "bars")) {
            beatsPerBar_ = std::clamp(std::stoi(*bars), 1, 16);
        }
        accentPattern_ = parseAccentPattern(findPropertyValues(module, "accents"));
        if (const auto accent = findPropertyValues(module, "accent"); !accent.empty()) {
            const auto parsed = parseAccentPattern(accent);
            if (!parsed.empty()) {
                accentPattern_ = parsed;
            }
        }
        if (const auto ratchetPattern = findPropertyValues(module, "ratchet"); ratchetPattern.size() >= 2) {
            ratchetPattern_.reserve(ratchetPattern.size());
            for (const auto& token : ratchetPattern) {
                ratchetPattern_.push_back(std::clamp(std::stoi(token), 1, 8));
            }
            if (!ratchetPattern_.empty()) {
                ratchet_ = ratchetPattern_.front();
            }
        }
        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = state_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }
    }

    void reset(double sampleRate) override
    {
        sampleRate_ = sampleRate;
        elapsedSeconds_ = 0.0;
        nextTriggerSeconds_ = 0.0;
        nextTriggerBeats_ = 0.0;
        stepIndex_ = 0;
        syncArmed_ = false;
        state_ = seed_;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>&,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) return;

        const auto interval = parseTimeToken(everyToken_, context.bpm);
        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;

        const auto start = elapsedSeconds_;
        const auto end = elapsedSeconds_ + blockDuration;

        if (nextTriggerSeconds_ < start) {
            nextTriggerSeconds_ = start;
        }

        while (nextTriggerSeconds_ <= end + 1.0e-9) {
            const auto stepDuration = stepDurationSeconds(interval, context.bpm);
            emitStep(outputs, nextTriggerSeconds_, start, 1.0, stepDuration, false, 60.0 / std::max(context.bpm, 1.0));
            nextTriggerSeconds_ += stepDuration;
            ++stepIndex_;
        }

        elapsedSeconds_ = end;
    }

private:
    [[nodiscard]] double grooveMultiplier() const
    {
        if (groovePattern_.empty()) {
            return 1.0;
        }

        const auto index = stepIndex_ % groovePattern_.size();
        const auto average = std::accumulate(groovePattern_.begin(), groovePattern_.end(), 0.0) / static_cast<double>(groovePattern_.size());
        if (average <= 1.0e-6) {
            return 1.0;
        }
        return groovePattern_[index] / average;
    }

    [[nodiscard]] double swingMultiplier() const
    {
        if (!swing_.has_value()) {
            return 1.0;
        }
        return (stepIndex_ % 2 == 0) ? (2.0 * *swing_) : (2.0 * (1.0 - *swing_));
    }

    [[nodiscard]] double stepDurationSeconds(double baseInterval, double bpm) const
    {
        (void)bpm;
        return std::max(1.0e-6, baseInterval * swingMultiplier() * grooveMultiplier() * tupletRatio_);
    }

    void emitStep(std::unordered_map<std::string, EventBuffer*>& outputs,
        double stepStart,
        double blockStart,
        double unitSeconds,
        double stepDuration,
        bool beatSpace,
        double beatLengthSeconds)
    {
        if (!passesChance(stepChance_)) {
            return;
        }

        const auto ratchetCount = currentRatchetCount();
        const auto spacing = stepDuration / static_cast<double>(ratchetCount);
        const auto accentValue = currentAccent(stepStart, beatSpace, beatLengthSeconds);
        const auto out = outputs.find("out");
        const auto beatOut = outputs.find("beat");
        const auto barOut = outputs.find("bar");
        const auto accentOut = outputs.find("accent");
        const auto stepRelative = std::max(0.0, (stepStart - blockStart) * unitSeconds);

        if (beatOut != outputs.end() && isBeatBoundary(stepStart, beatSpace, beatLengthSeconds)) {
            beatOut->second->push(Event::makeTrigger(stepRelative));
        }
        if (barOut != outputs.end() && isBarBoundary(stepStart, beatSpace, beatLengthSeconds)) {
            barOut->second->push(Event::makeTrigger(stepRelative));
        }

        for (int index = 0; index < ratchetCount; ++index) {
            if (!passesChance(pulseChance_)) {
                continue;
            }
            const auto absoluteTime = stepStart + (spacing * static_cast<double>(index));
            const auto relative = (absoluteTime - blockStart) * unitSeconds;
            if (relative >= -1.0e-9) {
                if (out != outputs.end()) {
                    out->second->push(Event::makeTrigger(std::max(0.0, relative)));
                }
                if (accentOut != outputs.end()) {
                    accentOut->second->push(Event::makeValue(accentValue, std::max(0.0, relative)));
                }
            }
        }
    }

    [[nodiscard]] static double parseProbability(std::string token)
    {
        if (!token.empty() && token.back() == '%') {
            token.pop_back();
            return std::clamp(std::stod(token) / 100.0, 0.0, 1.0);
        }
        return std::clamp(std::stod(token), 0.0, 1.0);
    }

    [[nodiscard]] static std::vector<double> parseAccentPattern(const std::vector<std::string>& values)
    {
        std::vector<double> accents;
        for (const auto& token : values) {
            try {
                accents.push_back(std::clamp(std::stod(token), 0.0, 2.0));
            } catch (...) {
            }
        }
        return accents;
    }

    [[nodiscard]] int currentRatchetCount() const
    {
        if (ratchetPattern_.empty()) {
            return ratchet_;
        }
        return ratchetPattern_[stepIndex_ % ratchetPattern_.size()];
    }

    [[nodiscard]] double currentAccent(double stepStart, bool beatSpace, double beatLengthSeconds) const
    {
        if (accentPattern_.empty()) {
            return 1.0;
        }
        const auto beats = beatSpace ? stepStart : (stepStart / std::max(beatLengthSeconds, 1.0e-6));
        const auto beatIndex = static_cast<int>(std::floor(beats + 1.0e-6)) % beatsPerBar_;
        return accentPattern_[static_cast<std::size_t>(beatIndex) % accentPattern_.size()];
    }

    [[nodiscard]] bool isBeatBoundary(double stepStart, bool beatSpace, double beatLengthSeconds) const
    {
        const auto beats = beatSpace ? stepStart : (stepStart / std::max(beatLengthSeconds, 1.0e-6));
        const auto whole = std::round(beats);
        return std::abs(beats - whole) < 1.0e-6;
    }

    [[nodiscard]] bool isBarBoundary(double stepStart, bool beatSpace, double beatLengthSeconds) const
    {
        const auto beats = beatSpace ? stepStart : (stepStart / std::max(beatLengthSeconds, 1.0e-6));
        const auto whole = std::round(beats);
        if (std::abs(beats - whole) >= 1.0e-6) {
            return false;
        }
        const auto beat = static_cast<int>(whole);
        return beat % beatsPerBar_ == 0;
    }

    [[nodiscard]] bool passesChance(double chance)
    {
        if (chance >= 1.0) return true;
        if (chance <= 0.0) return false;
        return nextUnit() <= chance;
    }

    [[nodiscard]] double nextUnit()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<double>(state_ & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    double sampleRate_ = 44100.0;
    std::string everyToken_ = "1/8";
    double elapsedSeconds_ = 0.0;
    double nextTriggerSeconds_ = 0.0;
    double nextTriggerBeats_ = 0.0;
    std::size_t stepIndex_ = 0;
    bool syncArmed_ = false;
    int ratchet_ = 1;
    std::vector<int> ratchetPattern_;
    std::optional<double> swing_;
    std::vector<double> groovePattern_;
    std::vector<double> accentPattern_;
    double tupletRatio_ = 1.0;
    double stepChance_ = 1.0;
    double pulseChance_ = 1.0;
    int beatsPerBar_ = 4;
    std::uint32_t seed_ = 97;
    std::uint32_t state_ = 97;
};

class PatternNode final : public RuntimeNode {
public:
    explicit PatternNode(const Module& module)
        : notes_(parsePitchList(findPropertyValues(module, "notes")))
    {
        if (notes_.empty()) {
            notes_ = { 60, 64, 67, 71 };
        }

        if (const auto order = findPropertyValue(module, "order")) {
            order_ = *order;
        }
    }

    void reset(double) override
    {
        step_ = 0;
        ascending_ = true;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) return;

        for (const auto& event : trigger->second->events()) {
            out->second->push(Event::makePitch(static_cast<double>(currentNote()), event.time));
            advance();
        }
    }

private:
    [[nodiscard]] int currentNote() const
    {
        return notes_[step_ % notes_.size()];
    }

    void advance()
    {
        if (notes_.empty()) return;

        if (order_ == "up_down" && notes_.size() > 1) {
            if (ascending_) {
                if (step_ + 1 >= notes_.size()) {
                    ascending_ = false;
                    step_ = notes_.size() - 2;
                } else {
                    ++step_;
                }
            } else {
                if (step_ == 0) {
                    ascending_ = true;
                    step_ = 1;
                } else {
                    --step_;
                }
            }
            return;
        }

        ++step_;
    }

    std::vector<int> notes_;
    std::string order_ = "up";
    std::size_t step_ = 0;
    bool ascending_ = true;
};

class PhraseNode final : public RuntimeNode {
public:
    explicit PhraseNode(const Module& module)
        : targets_(parseTargets(findPropertyValues(module, "targets")))
    {
        if (targets_.empty()) {
            targets_ = { 0.0, 4.0, 0.0 };
        }
    }

    void reset(double) override
    {
        step_ = 0;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) return;

        for (const auto& event : trigger->second->events()) {
            out->second->push(Event::makeValue(currentTarget(), event.time));
            advance();
        }
    }

private:
    static std::vector<double> parseTargets(const std::vector<std::string>& values)
    {
        std::vector<double> result;
        result.reserve(values.size());
        for (const auto& value : values) {
            if (value == "tonic") result.push_back(0.0);
            else if (value == "supertonic") result.push_back(1.0);
            else if (value == "mediant") result.push_back(2.0);
            else if (value == "subdominant") result.push_back(3.0);
            else if (value == "dominant") result.push_back(4.0);
            else if (value == "submediant") result.push_back(5.0);
            else if (value == "leading") result.push_back(6.0);
            else result.push_back(std::stod(value));
        }
        return result;
    }

    [[nodiscard]] double currentTarget() const
    {
        return targets_[step_ % targets_.size()];
    }

    void advance()
    {
        if (!targets_.empty()) {
            ++step_;
        }
    }

    std::vector<double> targets_;
    std::size_t step_ = 0;
};

class ProgressionNode final : public RuntimeNode {
public:
    explicit ProgressionNode(const Module& module)
        : targets_(parseTargets(findPropertyValues(module, "targets")))
        , lengths_(parseLengths(findPropertyValues(module, "lengths")))
    {
        if (targets_.empty()) {
            targets_ = { 0.0, 4.0, 0.0 };
        }
        if (lengths_.empty()) {
            lengths_.assign(targets_.size(), 4);
        }
        while (lengths_.size() < targets_.size()) {
            lengths_.push_back(lengths_.back());
        }
    }

    void reset(double) override
    {
        step_ = 0;
        countWithinStep_ = 0;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) return;

        for (const auto& event : trigger->second->events()) {
            out->second->push(Event::makeValue(currentTarget(), event.time));
            advance();
        }
    }

private:
    static std::vector<double> parseTargets(const std::vector<std::string>& values)
    {
        std::vector<double> result;
        result.reserve(values.size());
        for (const auto& value : values) {
            if (value == "tonic") result.push_back(0.0);
            else if (value == "supertonic") result.push_back(1.0);
            else if (value == "mediant") result.push_back(2.0);
            else if (value == "subdominant") result.push_back(3.0);
            else if (value == "dominant") result.push_back(4.0);
            else if (value == "submediant") result.push_back(5.0);
            else if (value == "leading") result.push_back(6.0);
            else result.push_back(std::stod(value));
        }
        return result;
    }

    static std::vector<int> parseLengths(const std::vector<std::string>& values)
    {
        std::vector<int> result;
        result.reserve(values.size());
        for (const auto& value : values) {
            result.push_back(std::max(1, std::stoi(value)));
        }
        return result;
    }

    [[nodiscard]] double currentTarget() const
    {
        return targets_[step_ % targets_.size()];
    }

    void advance()
    {
        if (targets_.empty()) return;

        ++countWithinStep_;
        if (countWithinStep_ >= lengths_[step_ % lengths_.size()]) {
            countWithinStep_ = 0;
            step_ = (step_ + 1) % targets_.size();
        }
    }

    std::vector<double> targets_;
    std::vector<int> lengths_;
    std::size_t step_ = 0;
    int countWithinStep_ = 0;
};

class SectionNode final : public RuntimeNode {
public:
    explicit SectionNode(const Module& module)
    {
        for (const auto& property : module.properties) {
            if (property.key != "section" || property.values.size() < 3) {
                continue;
            }

            SectionPlan plan;
            plan.name = property.values[0];
            for (std::size_t index = 1; index + 1 < property.values.size(); index += 2) {
                const auto& target = property.values[index];
                const auto& length = property.values[index + 1];
                plan.targets.push_back(parseTarget(target));
                plan.lengths.push_back(std::max(1, std::stoi(length)));
            }

            if (!plan.targets.empty()) {
                sections_.push_back(std::move(plan));
            }
        }

        for (const auto& property : module.properties) {
            if (property.key == "start" && !property.values.empty()) {
                startSectionName_ = property.values[0];
            }

            if (property.key == "select" && property.values.size() >= 2 && property.values[0] == "by" && property.values[1] == "note") {
                midiNoteSelect_ = true;
            }

            if (property.key == "select" && property.values.size() >= 2 && property.values[0] == "by"
                && (property.values[1] == "value" || property.values[1] == "cc")) {
                valueSelect_ = true;
            }

            if (property.key == "base" && !property.values.empty()) {
                baseNote_ = parseNoteName(property.values[0]);
            }

            if (property.key == "recall" && property.values.size() >= 2) {
                for (std::size_t index = 0; index + 1 < property.values.size(); index += 2) {
                    const auto& sectionName = property.values[index];
                    const auto recallValue = static_cast<int>(std::lround(std::stod(property.values[index + 1])));
                    const auto sectionIndex = findSectionIndexByName(sectionName);
                    if (sectionIndex.has_value()) {
                        recallMap_[recallValue] = *sectionIndex;
                    }
                }
            }
        }

        if (sections_.empty()) {
            sections_.push_back({ "default", { 0.0, 4.0, 0.0 }, { 4, 4, 4 } });
        }
    }

    void reset(double) override
    {
        currentSection_ = startingSectionIndex();
        resetSectionProgress();
        pendingExternal_.clear();
    }

    void setExternalEvents(const std::vector<Event>& events) override
    {
        pendingExternal_ = events;
    }

    [[nodiscard]] std::optional<int> currentSectionIndex() const override
    {
        return static_cast<int>(currentSection_);
    }

    [[nodiscard]] std::optional<double> currentSectionPhase() const override
    {
        const auto& section = sections_[currentSection_ % sections_.size()];
        if (section.lengths.empty()) {
            return 0.0;
        }

        const auto length = std::max(1, section.lengths[step_ % section.lengths.size()]);
        return std::clamp(static_cast<double>(countWithinStep_) / static_cast<double>(length), 0.0, 1.0);
    }

    [[nodiscard]] std::optional<std::uint64_t> sectionAdvanceCount() const override
    {
        return advanceCount_;
    }

    [[nodiscard]] std::optional<std::string> activeStateLabel() const override
    {
        if (sections_.empty()) {
            return std::nullopt;
        }
        return sections_[currentSection_ % sections_.size()].name;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) return;

        applySelect(inputs);
        applyAdvance(inputs);
        applyMidiSelect(inputs);
        applyExternalEvents();

        for (const auto& event : trigger->second->events()) {
            out->second->push(Event::makeValue(currentTarget(), event.time));
            if (const auto sectionOut = outputs.find("section"); sectionOut != outputs.end()) {
                sectionOut->second->push(Event::makeValue(static_cast<double>(currentSection_), event.time));
            }
            advance();
        }
    }

private:
    struct SectionPlan {
        std::string name;
        std::vector<double> targets;
        std::vector<int> lengths;
    };

    static double parseTarget(const std::string& value)
    {
        if (value == "tonic") return 0.0;
        if (value == "supertonic") return 1.0;
        if (value == "mediant") return 2.0;
        if (value == "subdominant") return 3.0;
        if (value == "dominant") return 4.0;
        if (value == "submediant") return 5.0;
        if (value == "leading") return 6.0;
        return std::stod(value);
    }

    [[nodiscard]] double currentTarget() const
    {
        const auto& section = sections_[currentSection_ % sections_.size()];
        return section.targets[step_ % section.targets.size()];
    }

    void advance()
    {
        const auto& section = sections_[currentSection_ % sections_.size()];
        if (section.targets.empty()) return;

        ++countWithinStep_;
        if (countWithinStep_ >= section.lengths[step_ % section.lengths.size()]) {
            countWithinStep_ = 0;
            step_ = (step_ + 1) % section.targets.size();
            ++advanceCount_;
        }
    }

    void applySelect(const std::unordered_map<std::string, const EventBuffer*>& inputs)
    {
        const auto select = inputs.find("select");
        if (select == inputs.end() || select->second->events().empty()) {
            return;
        }

        applySelectValue(select->second->events().back().valueOr(0.0));
    }

    void applyAdvance(const std::unordered_map<std::string, const EventBuffer*>& inputs)
    {
        const auto advanceInput = inputs.find("advance");
        if (advanceInput == inputs.end() || advanceInput->second->events().empty() || sections_.empty()) {
            return;
        }

        for (const auto& event : advanceInput->second->events()) {
            (void) event;
            currentSection_ = (currentSection_ + 1) % sections_.size();
            resetSectionProgress();
        }
    }

    void applyMidiSelect(const std::unordered_map<std::string, const EventBuffer*>& inputs)
    {
        if (!midiNoteSelect_) {
            return;
        }

        const auto midi = inputs.find("in");
        if (midi == inputs.end() || midi->second->events().empty() || sections_.empty()) {
            return;
        }

        for (const auto& event : midi->second->events()) {
            if (!event.isNoteOn()) {
                continue;
            }

            const auto rawIndex = event.noteNumber() - baseNote_;
            if (rawIndex < 0) {
                continue;
            }

            const auto next = static_cast<std::size_t>(std::min<int>(rawIndex, static_cast<int>(sections_.size()) - 1));
            if (next != currentSection_) {
                currentSection_ = next;
                resetSectionProgress();
            }
        }
    }

    void applyExternalEvents()
    {
        if (pendingExternal_.empty()) {
            return;
        }

        for (const auto& event : pendingExternal_) {
            if (event.type == SignalType::trigger) {
                if (!sections_.empty()) {
                    currentSection_ = (currentSection_ + 1) % sections_.size();
                    resetSectionProgress();
                }
                continue;
            }

            if (event.type == SignalType::value) {
                applySelectValue(event.valueOr(0.0));
                continue;
            }

            if (event.type == SignalType::midi && midiNoteSelect_ && event.isNoteOn()) {
                const auto rawIndex = event.noteNumber() - baseNote_;
                if (rawIndex < 0) {
                    continue;
                }

                const auto next = static_cast<std::size_t>(std::min<int>(rawIndex, static_cast<int>(sections_.size()) - 1));
                if (next != currentSection_) {
                    currentSection_ = next;
                    resetSectionProgress();
                }
            }
        }

        pendingExternal_.clear();
    }

    void applySelectValue(double raw)
    {
        const auto count = static_cast<int>(sections_.size());
        if (count <= 0) return;

        int next = 0;
        if (!recallMap_.empty()) {
            const auto key = static_cast<int>(std::lround(raw));
            const auto found = recallMap_.find(key);
            if (found != recallMap_.end()) {
                next = static_cast<int>(found->second);
            } else {
                return;
            }
        } else if (valueSelect_) {
            if (raw >= 0.0 && raw <= 1.0) {
                next = static_cast<int>(std::lround(raw * static_cast<double>(count - 1)));
            } else {
                next = static_cast<int>(std::lround((std::clamp(raw, 0.0, 127.0) / 127.0) * static_cast<double>(count - 1)));
            }
        } else {
            next = static_cast<int>(std::lround(raw));
        }

        next = ((next % count) + count) % count;
        if (static_cast<std::size_t>(next) != currentSection_) {
            currentSection_ = static_cast<std::size_t>(next);
            resetSectionProgress();
        }
    }

    void resetSectionProgress()
    {
        step_ = 0;
        countWithinStep_ = 0;
    }

    [[nodiscard]] std::size_t startingSectionIndex() const
    {
        if (!startSectionName_.empty()) {
            for (std::size_t index = 0; index < sections_.size(); ++index) {
                if (sections_[index].name == startSectionName_) {
                    return index;
                }
            }
        }

        return 0;
    }

    [[nodiscard]] std::optional<std::size_t> findSectionIndexByName(const std::string& name) const
    {
        for (std::size_t index = 0; index < sections_.size(); ++index) {
            if (sections_[index].name == name) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::vector<SectionPlan> sections_;
    std::string startSectionName_;
    std::size_t currentSection_ = 0;
    bool midiNoteSelect_ = false;
    bool valueSelect_ = false;
    int baseNote_ = 36;
    std::unordered_map<int, std::size_t> recallMap_;
    std::size_t step_ = 0;
    int countWithinStep_ = 0;
    std::vector<Event> pendingExternal_;
    std::uint64_t advanceCount_ = 0;
};

class FibonacciNode final : public RuntimeNode {
public:
    explicit FibonacciNode(const Module& module)
        : map_(parseNumericMap(findPropertyValues(module, "map")))
    {
        if (const auto length = findPropertyValue(module, "length")) {
            length_ = std::max(1, std::stoi(*length));
        }
        if (map_.empty()) {
            map_ = { 0.0, 2.0, 3.0, 5.0, 7.0 };
        }
    }

    void reset(double) override
    {
        a_ = 1;
        b_ = 1;
        emitted_ = 0;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) return;

        for (const auto& event : trigger->second->events()) {
            const auto fib = currentValue();
            const auto mapped = map_[static_cast<std::size_t>(fib % static_cast<long long>(map_.size()))];
            out->second->push(Event::makePitch(60.0 + mapped, event.time));
            advance();
        }
    }

private:
    static std::vector<double> parseNumericMap(const std::vector<std::string>& values)
    {
        std::vector<double> result;
        result.reserve(values.size());
        for (const auto& value : values) {
            result.push_back(std::stod(value));
        }
        return result;
    }

    [[nodiscard]] long long currentValue() const
    {
        return emitted_ == 0 ? 1LL : b_;
    }

    void advance()
    {
        ++emitted_;
        if (emitted_ >= length_) {
            a_ = 1;
            b_ = 1;
            emitted_ = 0;
            return;
        }

        const auto next = a_ + b_;
        a_ = b_;
        b_ = next;
    }

    std::vector<double> map_;
    int length_ = 8;
    long long a_ = 1;
    long long b_ = 1;
    int emitted_ = 0;
};

class GrowthNode final : public RuntimeNode {
public:
    explicit GrowthNode(const Module& module)
        : root_(parseNoteName(findPropertyValue(module, "root").value_or("C3")))
        , distribution_(parseDistributionConfig(module))
    {
        addRatioFamily("default", parseRatioValues(findPropertyValues(module, "ratios")), 1.0);

        if (const auto prune = findPropertyValues(module, "prune"); prune.size() >= 2 && prune[0] == "when") {
            pruneUnstable_ = (prune[1] == "unstable");
        }

        if (const auto add = findPropertyValues(module, "add"); add.size() >= 2 && add[0] == "when") {
            requireStable_ = (add[1] == "stable");
        }

        if (const auto map = findPropertyValues(module, "map"); map.size() >= 3 && map[0] == "growth" && map[1] == "to") {
            densityMode_ = map[2];
        }

        if (const auto maxNotes = findPropertyValues(module, "max"); maxNotes.size() >= 2 && maxNotes[0] == "notes") {
            maxNotes_ = std::max(1, std::stoi(maxNotes[1]));
        }

        for (const auto& property : module.properties) {
            if (property.key == "fold" && !property.values.empty()) {
                const auto joined = joinValues(property.values);
                if (joined == "just" || joined == "octaves just" || joined == "octave just") {
                    justFoldOctaves_ = true;
                }
            }

            if (property.key == "family" && !property.values.empty()) {
                parseFamily(property.values);
            }

            if (property.key == "prune" && property.values.size() >= 2 && property.values[0] == "strength") {
                pruneStrength_ = std::clamp(std::stoi(property.values[1]), 1, 3);
            }

            if (property.key == "density" && property.values.size() >= 2 && property.values[0] == "drives") {
                densityMode_ = property.values[1];
            }

            if (property.key == "register" && !property.values.empty()) {
                registerMode_ = property.values[0];
            }

            if (property.key == "target" && !property.values.empty()) {
                staticFunctionalTarget_ = parseFunctionalTarget(property.values[0]);
            }
        }

        if (ratioChoices_.empty()) {
            addRatioFamily("default", { "3/2", "5/4", "7/4" }, 1.0);
        }
    }

    void reset(double) override
    {
        structure_.clear();
        structure_.push_back(root_);
        step_ = 0;
        distributionMemory_ = 0.5;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) {
            return;
        }

        for (const auto& event : trigger->second->events()) {
            activeFunctionalTarget_ = phraseTargetAtTime(inputs, event.time).value_or(staticFunctionalTarget_);
            const auto emissions = noteRateFromDensity();
            const auto blockDuration = context.sampleRate > 0.0
                ? static_cast<double>(context.blockSize) / context.sampleRate
                : 0.0;
            const auto spacing = emissions > 1 ? (blockDuration * 0.8) / static_cast<double>(emissions) : 0.0;

            for (int emission = 0; emission < emissions; ++emission) {
                const auto note = advanceGrowth();
                const auto eventTime = std::min(event.time + (spacing * static_cast<double>(emission)), std::max(0.0, blockDuration - 1.0e-4));
                out->second->push(Event::makePitch(static_cast<double>(note), eventTime));
            }
            if (const auto density = outputs.find("density"); density != outputs.end()) {
                density->second->push(Event::makeValue(currentDensity(), event.time));
            }
            if (const auto gate = outputs.find("gate"); gate != outputs.end()) {
                Event gateEvent;
                gateEvent.type = SignalType::gate;
                gateEvent.time = event.time;
                gateEvent.floats.push_back(gateFromDensity());
                gate->second->push(std::move(gateEvent));
            }
        }
    }

private:
    struct Candidate {
        int note = 60;
        double score = 0.0;
    };

    struct RatioChoice {
        int semitones = 0;
        double weight = 1.0;
        int family = 0;
    };

    static std::vector<std::string> parseRatioValues(const std::vector<std::string>& values)
    {
        std::vector<std::string> parsed;
        parsed.reserve(values.size());
        for (const auto& value : values) {
            if (value == "weight" || value == "ratios") {
                continue;
            }
            parsed.push_back(value);
        }
        return parsed;
    }

    [[nodiscard]] int advanceGrowth()
    {
        auto candidate = chooseCandidate();
        if (candidate.has_value()) {
            const auto alreadyPresent = std::find(structure_.begin(), structure_.end(), candidate->note) != structure_.end();
            if (!alreadyPresent) {
                structure_.push_back(candidate->note);
            }

            if (static_cast<int>(structure_.size()) > maxNotes_) {
                removeWeakestNote(candidate->note);
            }

            ++step_;
            return candidate->note;
        }

        if (pruneUnstable_ && structure_.size() > 1) {
            removeWeakestNote(root_);
        }

        const auto fallback = structure_[step_ % structure_.size()];
        ++step_;
        return fallback;
    }

    [[nodiscard]] std::optional<Candidate> chooseCandidate()
    {
        const auto family = chooseFamilyIndex();
        std::vector<Candidate> candidates;

        for (const auto anchor : structure_) {
            for (const auto& ratio : ratioChoices_) {
                if (ratio.family != family) {
                    continue;
                }

                auto note = clampMidiNote(anchor + ratio.semitones);
                note = foldCandidate(note);

                const auto score = candidateWeight(note, ratio);
                if (requireStable_ && score < stabilityThreshold()) {
                    continue;
                }

                candidates.push_back(Candidate { note, score });
            }
        }

        if (candidates.empty()) {
            return std::nullopt;
        }

        auto totalWeight = 0.0;
        for (const auto& candidate : candidates) {
            totalWeight += std::max(0.01, candidate.score);
        }

        auto choice = distributionPhase01(distribution_, distributionMemory_, [this]() { return randomUnit(); }) * totalWeight;
        for (const auto& candidate : candidates) {
            choice -= std::max(0.01, candidate.score);
            if (choice <= 0.0) {
                return candidate;
            }
        }

        return candidates.back();
    }

    [[nodiscard]] double reinforcementScore(int note) const
    {
        double score = 0.0;
        const auto candidateClass = ((note % 12) + 12) % 12;

        for (const auto existing : structure_) {
            const auto distance = std::abs(note - existing);
            const auto intervalClass = distance % 12;
            const bool ratioMatch = std::any_of(ratioChoices_.begin(), ratioChoices_.end(), [&](const RatioChoice& ratio) {
                return (std::abs(ratio.semitones) % 12) == intervalClass;
            });

            if (ratioMatch) {
                score += 1.4;
            }

            if ((((existing % 12) + 12) % 12) == candidateClass) {
                score += 0.28;
            }

            score += std::max(0.0, 1.0 - (static_cast<double>(distance) / 18.0)) * 0.22;
        }

        score -= std::abs(note - root_) / 48.0;
        score += octaveClassSupport(candidateClass) * 0.45;
        return score;
    }

    [[nodiscard]] double stabilityThreshold() const
    {
        return std::max(0.9, static_cast<double>(structure_.size()) * 0.45);
    }

    void removeWeakestNote(int protectedNote)
    {
        if (structure_.size() <= 1) {
            return;
        }

        for (int round = 0; round < pruneStrength_; ++round) {
            auto weakest = structure_.end();
            auto weakestScore = std::numeric_limits<double>::max();
            for (auto it = structure_.begin(); it != structure_.end(); ++it) {
                if (*it == protectedNote) {
                    continue;
                }

                const auto score = structuralFitness(*it);
                if (score < weakestScore) {
                    weakestScore = score;
                    weakest = it;
                }
            }

            if (weakest == structure_.end()) {
                return;
            }

            structure_.erase(weakest);
            if (structure_.size() <= 1) {
                return;
            }
        }
    }

    [[nodiscard]] double currentDensity() const
    {
        if (densityMode_ != "density") {
            return std::clamp(static_cast<double>(structure_.size()) / static_cast<double>(maxNotes_), 0.0, 1.0);
        }
        return std::clamp(static_cast<double>(structure_.size()) / static_cast<double>(maxNotes_), 0.0, 1.0);
    }

    [[nodiscard]] int chooseFamilyIndex()
    {
        if (familyWeights_.empty()) {
            return 0;
        }

        auto total = 0.0;
        for (std::size_t index = 0; index < familyWeights_.size(); ++index) {
            total += effectiveFamilyWeight(static_cast<int>(index));
        }

        auto choice = distributionPhase01(distribution_, distributionMemory_, [this]() { return randomUnit(); }) * total;
        for (std::size_t index = 0; index < familyWeights_.size(); ++index) {
            choice -= effectiveFamilyWeight(static_cast<int>(index));
            if (choice <= 0.0) {
                return static_cast<int>(index);
            }
        }

        return static_cast<int>(familyWeights_.size() - 1);
    }

    void parseFamily(const std::vector<std::string>& values)
    {
        if (values.empty()) {
            return;
        }

        const auto name = values[0];
        double weight = 1.0;
        std::vector<std::string> ratios;
        bool readingRatios = false;

        for (std::size_t i = 1; i < values.size(); ++i) {
            if (values[i] == "weight" && (i + 1) < values.size()) {
                weight = std::max(0.1, std::stod(values[i + 1]));
                ++i;
                continue;
            }
            if (values[i] == "ratios") {
                readingRatios = true;
                continue;
            }
            if (readingRatios) {
                ratios.push_back(values[i]);
            }
        }

        if (!ratios.empty()) {
            addRatioFamily(name, ratios, weight);
        }
    }

    void addRatioFamily(const std::string& name, const std::vector<std::string>& values, double weight)
    {
        const auto familyIndex = familyCount_++;
        familyNames_.push_back(name);
        familyWeights_.push_back(weight);
        for (const auto& value : values) {
            const auto semitones = ratioToSemitones(value);
            ratioChoices_.push_back(RatioChoice { semitones, weight, familyIndex });
        }
    }

    [[nodiscard]] static int ratioToSemitones(const std::string& value)
    {
        const auto slash = value.find('/');
        if (slash == std::string::npos) {
            return std::stoi(value);
        }

        const auto numerator = std::stod(value.substr(0, slash));
        const auto denominator = std::stod(value.substr(slash + 1));
        if (denominator == 0.0) {
            return 0;
        }

        return static_cast<int>(std::lround(12.0 * std::log2(numerator / denominator)));
    }

    [[nodiscard]] int foldCandidate(int note) const
    {
        const auto [rangeLow, rangeHigh, targetCenter] = registerWindow();

        if (justFoldOctaves_) {
            const auto foldedCenter = targetCenter.value_or(static_cast<int>(std::lround(averagePitch())));
            while (note - foldedCenter > 6) note -= 12;
            while (foldedCenter - note > 6) note += 12;
        } else {
            while (note > rangeHigh) note -= 12;
            while (note < rangeLow) note += 12;
        }

        while (note > rangeHigh) note -= 12;
        while (note < rangeLow) note += 12;
        return std::clamp(note, rangeLow, rangeHigh);
    }

    [[nodiscard]] double octaveClassSupport(int candidateClass) const
    {
        double support = 0.0;
        for (const auto existing : structure_) {
            if ((((existing % 12) + 12) % 12) == candidateClass) {
                support += 1.0;
            }
        }
        return support / std::max(1.0, static_cast<double>(structure_.size()));
    }

    [[nodiscard]] double averagePitch() const
    {
        if (structure_.empty()) {
            return static_cast<double>(root_);
        }

        double total = 0.0;
        for (const auto note : structure_) {
            total += static_cast<double>(note);
        }
        return total / static_cast<double>(structure_.size());
    }

    [[nodiscard]] double structuralFitness(int note) const
    {
        auto score = reinforcementScore(note);
        score += std::max(0.0, 1.0 - (std::abs(note - root_) / 24.0)) * 0.35;
        score += std::max(0.0, 1.0 - (std::abs(note - static_cast<int>(std::lround(averagePitch()))) / 18.0)) * 0.25;
        return score;
    }

    [[nodiscard]] int noteRateFromDensity() const
    {
        if (densityMode_ != "rate") {
            return 1;
        }

        const auto density = currentDensity();
        if (density > 0.85) return 4;
        if (density > 0.6) return 3;
        if (density > 0.35) return 2;
        return 1;
    }

    [[nodiscard]] double gateFromDensity() const
    {
        return std::clamp(0.35 + (currentDensity() * 0.55), 0.15, 0.95);
    }

    [[nodiscard]] double effectiveFamilyWeight(int familyIndex) const
    {
        auto weight = familyWeights_[static_cast<std::size_t>(familyIndex)];
        if (activeFunctionalTarget_ == 4 && familyNames_[static_cast<std::size_t>(familyIndex)] == "perfect") {
            weight *= 1.6;
        }
        if (activeFunctionalTarget_ == 0 && familyNames_[static_cast<std::size_t>(familyIndex)] == "color") {
            weight *= 0.85;
        }
        if (activeFunctionalTarget_ == 3 && familyNames_[static_cast<std::size_t>(familyIndex)] == "color") {
            weight *= 1.3;
        }
        return std::max(0.05, weight);
    }

    [[nodiscard]] double candidateWeight(int note, const RatioChoice& ratio) const
    {
        auto score = reinforcementScore(note) * std::max(0.1, ratio.weight);
        score += functionalPull(note) * 0.75;
        return score;
    }

    [[nodiscard]] double functionalPull(int note) const
    {
        const auto targetNote = targetPitchNearAverage(activeFunctionalTarget_);
        const auto distance = std::abs(note - targetNote);
        return std::max(0.0, 1.0 - (static_cast<double>(distance) / 18.0));
    }

    [[nodiscard]] int targetPitchNearAverage(int degree) const
    {
        const auto anchor = static_cast<int>(std::lround(averagePitch()));
        const int pitchClass = rootPitchClass() + functionalSemitoneOffset(degree);
        int best = anchor;
        int bestDistance = std::numeric_limits<int>::max();
        for (int octave = -4; octave <= 4; ++octave) {
            const auto candidate = ((anchor / 12) + octave) * 12 + ((pitchClass % 12 + 12) % 12);
            const auto distance = std::abs(candidate - anchor);
            if (distance < bestDistance) {
                best = candidate;
                bestDistance = distance;
            }
        }
        return best;
    }

    [[nodiscard]] int rootPitchClass() const
    {
        return ((root_ % 12) + 12) % 12;
    }

    [[nodiscard]] static int parseFunctionalTarget(const std::string& value)
    {
        if (value == "dominant") return 4;
        if (value == "subdominant") return 3;
        return 0;
    }

    [[nodiscard]] static int functionalSemitoneOffset(int degree)
    {
        if (degree == 4) return 7;
        if (degree == 3) return 5;
        return 0;
    }

    [[nodiscard]] static std::optional<int> phraseTargetAtTime(const std::unordered_map<std::string, const EventBuffer*>& inputs, double eventTime)
    {
        const auto input = inputs.find("phrase");
        if (input == inputs.end() || input->second->events().empty()) {
            return std::nullopt;
        }

        double selected = input->second->events().front().valueOr(0.0);
        for (const auto& event : input->second->events()) {
            if (event.time > eventTime + 1.0e-9) {
                break;
            }
            selected = event.valueOr(selected);
        }

        return static_cast<int>(std::lround(selected));
    }

    [[nodiscard]] std::tuple<int, int, std::optional<int>> registerWindow() const
    {
        if (registerMode_ == "low") {
            return { 24, 60, 42 };
        }
        if (registerMode_ == "mid") {
            return { 48, 78, 63 };
        }
        if (registerMode_ == "wide") {
            return { 36, 96, std::nullopt };
        }
        return { 36, 84, 60 };
    }

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double randomUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    [[nodiscard]] static std::string joinValues(const std::vector<std::string>& values)
    {
        std::string result;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i != 0) {
                result += ' ';
            }
            result += values[i];
        }
        return result;
    }

    int root_ = 48;
    std::vector<RatioChoice> ratioChoices_;
    std::vector<std::string> familyNames_;
    std::vector<double> familyWeights_;
    std::vector<int> structure_;
    std::string densityMode_ = "density";
    std::string registerMode_ = "mid";
    int maxNotes_ = 8;
    bool pruneUnstable_ = true;
    bool requireStable_ = true;
    bool justFoldOctaves_ = false;
    int pruneStrength_ = 1;
    int familyCount_ = 0;
    int staticFunctionalTarget_ = 0;
    int activeFunctionalTarget_ = 0;
    DistributionConfig distribution_;
    double distributionMemory_ = 0.5;
    std::uint32_t state_ = 131;
    std::size_t step_ = 0;
};

class SwarmNode final : public RuntimeNode {
public:
    explicit SwarmNode(const Module& module)
        : center_(parseNoteName(findPropertyValue(module, "center").value_or("D3")))
        , distribution_(parseDistributionConfig(module))
    {
        if (const auto agents = findPropertyValue(module, "agents")) {
            voiceCount_ = std::max(1, std::stoi(*agents));
        }

        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }

        if (const auto cluster = findPropertyValue(module, "cluster")) {
            cluster_ = std::clamp(std::stod(*cluster), 0.0, 1.0);
        }

        for (const auto& property : module.properties) {
            if (property.key == "agent" && property.values.size() >= 2) {
                if (property.values.size() >= 3 && property.values[1] == "type") {
                    roles_.push_back(property.values[2]);
                } else {
                    roles_.push_back(property.values[1]);
                }
            }
        }
    }

    void reset(double) override
    {
        voices_.clear();
        if (roles_.empty()) {
            roles_ = { "anchor", "follower", "rebel", "follower" };
        }

        for (int i = 0; i < voiceCount_; ++i) {
            Voice voice;
            voice.role = roles_[static_cast<std::size_t>(i % static_cast<int>(roles_.size()))];
            voice.note = clampMidiNote(center_ + ((i % 3) * 3) - 3);
            voices_.push_back(voice);
        }

        rotateIndex_ = 0;
        state_ = seed_;
        distributionMemory_ = 0.5;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) {
            return;
        }

        for (const auto& event : trigger->second->events()) {
            updateVoices();
            rotateIndex_ = chooseVoiceIndex();
            const auto& voice = voices_[rotateIndex_ % voices_.size()];
            out->second->push(Event::makePitch(static_cast<double>(voice.note), event.time));
            if (const auto density = outputs.find("density"); density != outputs.end()) {
                density->second->push(Event::makeValue(clusteredness(), event.time));
            }
        }
    }

private:
    struct Voice {
        std::string role;
        int note = 60;
    };

    void updateVoices()
    {
        const auto average = averagePitch();
        for (auto& voice : voices_) {
            if (voice.role == "anchor") {
                voice.note = nudgeToward(voice.note, center_, 1);
            } else if (voice.role == "follower") {
                const auto target = static_cast<int>(std::lround((average * cluster_) + (center_ * (1.0 - cluster_))));
                voice.note = nudgeToward(voice.note, target, 2);
            } else if (voice.role == "rebel") {
                const auto direction = (voice.note >= average) ? 1 : -1;
                voice.note = clampMidiNote(voice.note + (direction * 2));
                if (std::abs(voice.note - center_) > 16) {
                    voice.note = nudgeToward(voice.note, center_, 3);
                }
            } else {
                voice.note = nudgeToward(voice.note, center_, 1);
            }
        }
    }

    [[nodiscard]] double averagePitch() const
    {
        if (voices_.empty()) {
            return static_cast<double>(center_);
        }

        double total = 0.0;
        for (const auto& voice : voices_) {
            total += static_cast<double>(voice.note);
        }
        return total / static_cast<double>(voices_.size());
    }

    [[nodiscard]] static int nudgeToward(int note, int target, int step)
    {
        if (note == target) {
            return note;
        }
        return clampMidiNote(note + ((note < target) ? step : -step));
    }

    [[nodiscard]] double clusteredness() const
    {
        if (voices_.size() < 2) {
            return 1.0;
        }

        double spread = 0.0;
        const auto average = averagePitch();
        for (const auto& voice : voices_) {
            spread += std::abs(static_cast<double>(voice.note) - average);
        }
        spread /= static_cast<double>(voices_.size());
        return std::clamp(1.0 - (spread / 18.0), 0.0, 1.0);
    }

    [[nodiscard]] std::size_t chooseVoiceIndex()
    {
        if (voices_.empty()) {
            return 0;
        }
        const auto phase = distributionPhase01(distribution_, distributionMemory_, [this]() { return nextUnit(); });
        const auto index = static_cast<std::size_t>(std::clamp<int>(static_cast<int>(std::floor(phase * static_cast<double>(voices_.size()))), 0, static_cast<int>(voices_.size() - 1)));
        return index;
    }

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double nextUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    int center_ = 50;
    int voiceCount_ = 4;
    double cluster_ = 0.7;
    std::vector<std::string> roles_;
    std::vector<Voice> voices_;
    std::size_t rotateIndex_ = 0;
    DistributionConfig distribution_;
    double distributionMemory_ = 0.5;
    std::uint32_t seed_ = 0x19283746u;
    std::uint32_t state_ = seed_;
};

class CollapseNode final : public RuntimeNode {
public:
    explicit CollapseNode(const Module& module)
        : notes_(parsePitchList(findPropertyValues(module, "notes")))
        , root_(parseNoteName(findPropertyValue(module, "root").value_or("C3")))
    {
        if (notes_.empty()) {
            notes_ = { 48, 50, 52, 53, 55, 57, 59 };
        }

        for (const auto& property : module.properties) {
            if (property.key == "ruleset" && property.values.size() >= 2) {
                ruleSets_[property.values[0]] = parseRuleSet(property.values);
                continue;
            }

            if (property.key == "use" && !property.values.empty()) {
                initialRuleSetName_ = property.values[0];
                continue;
            }

            if (property.key == "avoid" && !property.values.empty()) {
                if (property.values[0] == "tonic") avoidTonic_ = true;
                if (property.values[0] == "dominant") avoidDegree_ = 4;
                if (property.values[0] == "subdominant") avoidDegree_ = 3;
                if (property.values[0] == "target") avoidTarget_ = true;
                if (property.values[0] == "repeated_intervals") avoidRepeatedIntervals_ = true;
            }

            if (property.key == "no" && property.values.size() >= 2) {
                if (property.values[0] == "repeated" && property.values[1] == "intervals") {
                    avoidRepeatedIntervals_ = true;
                }
            }

            if (property.key == "max" && property.values.size() >= 2 && property.values[0] == "step") {
                maxStep_ = std::max(1, std::stoi(property.values[1]));
            }
            if (property.key == "max" && property.values.size() >= 3 && property.values[1] == "semitone") {
                maxStep_ = std::max(1, std::stoi(property.values[0]));
            }

            if (property.key == "on" && property.values.size() >= 3 && property.values[0] == "collapse" && property.values[1] == "mutate") {
                mutationAmount_ = std::max(1, std::stoi(property.values[2]));
            }
            if (property.key == "on" && property.values.size() >= 3 && property.values[0] == "collapse" && property.values[1] == "use") {
                collapseUseRuleSet_ = property.values[2];
            }
            if (property.key == "on" && property.values.size() >= 4 && property.values[0] == "collapse" && property.values[1] == "cycle") {
                collapseCycleRuleSets_.assign(property.values.begin() + 2, property.values.end());
            }
            if (property.key == "on" && property.values.size() >= 4 && property.values[0] == "section" && property.values[2] == "use") {
                sectionRuleSets_[property.values[1]] = property.values[3];
                if (std::find(sectionNames_.begin(), sectionNames_.end(), property.values[1]) == sectionNames_.end()) {
                    sectionNames_.push_back(property.values[1]);
                }
            }
            if (property.key == "on" && property.values.size() >= 5 && property.values[0] == "section" && property.values[2] == "blend") {
                sectionBlends_[property.values[1]] = SectionBlend { property.values[3], std::clamp(std::stod(property.values[4]), 0.0, 1.0) };
                if (std::find(sectionNames_.begin(), sectionNames_.end(), property.values[1]) == sectionNames_.end()) {
                    sectionNames_.push_back(property.values[1]);
                }
            }
            if (property.key == "on" && property.values.size() >= 4 && property.values[0] == "collapse" && property.values[1] == "reform") {
                reformMode_ = property.values[2];
                reformStrength_ = std::max(1, std::stoi(property.values[3]));
            } else if (property.key == "on" && property.values.size() >= 3 && property.values[0] == "collapse" && property.values[1] == "reform") {
                reformMode_ = property.values[2];
            }

            if (property.key == "reform" && !property.values.empty()) {
                reformMode_ = property.values[0];
            }

            if (property.key == "recover" && property.values.size() >= 2 && property.values[0] == "to") {
                recoverTarget_ = property.values[1];
            }

            if (property.key == "prefer" && property.values.size() >= 2 && property.values[0] == "center") {
                preferCenter_ = std::clamp(std::stod(property.values[1]), 0.0, 1.0);
            }

            if (property.key == "follow" && !property.values.empty() && property.values[0] == "phrase") {
                followPhrase_ = true;
            }

            if (property.key == "target" && !property.values.empty()) {
                staticTargetDegree_ = parseFunctionalTarget(property.values[0]);
            }

            if (property.key == "seed" && !property.values.empty()) {
                seed_ = static_cast<std::uint32_t>(std::stoul(property.values[0]));
                state_ = seed_;
            }
        }

        defaultRuleSet_ = captureCurrentRuleSet();
    }

    void reset(double) override
    {
        state_ = seed_;
        applyRuleSet(defaultRuleSet_, true);
        if (!initialRuleSetName_.empty()) {
            applyNamedRuleSet(initialRuleSetName_, true);
        }
        lastNote_ = notes_.front();
        lastInterval_.reset();
        collapsePulse_ = 0.0;
        activePhraseTarget_ = 0;
        collapseCycleIndex_ = 0;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) {
            return;
        }

        for (const auto& event : trigger->second->events()) {
            activePhraseTarget_ = phraseTargetAtTime(inputs, event.time).value_or(activePhraseTarget_);
            updateSectionRuleSet(inputs, event.time);
            const auto next = chooseNextNote();
            if (next.has_value()) {
                const auto interval = *next - lastNote_;
                lastInterval_ = interval;
                lastNote_ = *next;
                collapsePulse_ = 0.0;
                out->second->push(Event::makePitch(static_cast<double>(*next), event.time));
            } else {
                reformOnCollapse();
                collapsePulse_ = 1.0;
                out->second->push(Event::makePitch(static_cast<double>(lastNote_), event.time));
            }

            if (const auto state = outputs.find("state"); state != outputs.end()) {
                state->second->push(Event::makeValue(collapsePulse_, event.time));
            }
        }
    }

private:
    struct RuleSet {
        std::optional<bool> avoidTonic;
        std::optional<int> avoidDegree;
        std::optional<bool> avoidTarget;
        std::optional<bool> avoidRepeatedIntervals;
        std::optional<int> maxStep;
        std::optional<double> preferCenter;
        std::optional<int> targetDegree;
        std::optional<std::string> reformMode;
        std::optional<std::string> recoverTarget;
        std::optional<int> mutationAmount;
        std::optional<bool> followPhrase;
    };

    struct SectionBlend {
        std::string ruleSetName;
        double amount = 0.5;
    };

    struct Candidate {
        int note = 60;
        double weight = 1.0;
    };

    [[nodiscard]] std::optional<int> chooseNextNote()
    {
        std::vector<Candidate> candidates;
        const auto targetDegree = resolvedTargetDegree();
        const auto resolvedMaxStep = effectiveMaxStep();
        for (const auto note : notes_) {
            if (avoidTonic_ && (((note - root_) % 12) + 12) % 12 == 0) {
                continue;
            }
            if (avoidDegree_.has_value() && scaleDegreeClass(note) == *avoidDegree_) {
                continue;
            }
            if (avoidTarget_ && scaleDegreeClass(note) == targetDegree) {
                continue;
            }

            const auto step = std::abs(note - lastNote_);
            if (step > resolvedMaxStep) {
                continue;
            }

            const auto interval = note - lastNote_;
            if (avoidRepeatedIntervals_ && lastInterval_.has_value() && interval == *lastInterval_) {
                continue;
            }

            candidates.push_back({ note, candidateWeight(note, targetDegree) });
        }

        if (candidates.empty()) {
            return std::nullopt;
        }

        auto totalWeight = 0.0;
        for (const auto& candidate : candidates) {
            totalWeight += std::max(0.01, candidate.weight);
        }

        auto choice = randomUnit() * totalWeight;
        for (const auto& candidate : candidates) {
            choice -= std::max(0.01, candidate.weight);
            if (choice <= 0.0) {
                return candidate.note;
            }
        }

        return candidates.back().note;
    }

    void reformOnCollapse()
    {
        if (!collapseCycleRuleSets_.empty()) {
            applyNamedRuleSet(collapseCycleRuleSets_[collapseCycleIndex_ % collapseCycleRuleSets_.size()], true);
            collapseCycleIndex_ = (collapseCycleIndex_ + 1) % collapseCycleRuleSets_.size();
        } else if (!collapseUseRuleSet_.empty()) {
            applyNamedRuleSet(collapseUseRuleSet_, true);
        }

        if (reformMode_ == "rotate_pool") {
            rotatePool();
        } else if (reformMode_ == "rotate_root") {
            rotateRoot();
        } else if (reformMode_ == "wild") {
            mutateRules(mutationAmount_ + reformStrength_);
            rotatePool();
            rotateRoot();
        } else {
            mutateRules(std::max(1, mutationAmount_));
        }

        applyRecoveryTarget();
        lastInterval_.reset();
    }

    [[nodiscard]] RuleSet parseRuleSet(const std::vector<std::string>& values) const
    {
        RuleSet ruleSet;
        std::size_t index = 1;
        if (values.size() >= 3 && values[1] == "from") {
            if (const auto found = ruleSets_.find(values[2]); found != ruleSets_.end()) {
                ruleSet = found->second;
            }
            index = 3;
        }

        for (; index < values.size(); ++index) {
            const auto& token = values[index];
            if (token == "avoid" && (index + 1) < values.size()) {
                const auto& next = values[index + 1];
                if (next == "tonic") ruleSet.avoidTonic = true;
                if (next == "dominant") ruleSet.avoidDegree = 4;
                if (next == "subdominant") ruleSet.avoidDegree = 3;
                if (next == "target") ruleSet.avoidTarget = true;
                ++index;
                continue;
            }
            if (token == "no" && (index + 2) < values.size() && values[index + 1] == "repeated" && values[index + 2] == "intervals") {
                ruleSet.avoidRepeatedIntervals = true;
                index += 2;
                continue;
            }
            if (token == "max" && (index + 3) < values.size() && values[index + 2] == "semitone" && values[index + 3] == "steps") {
                ruleSet.maxStep = std::max(1, std::stoi(values[index + 1]));
                index += 3;
                continue;
            }
            if (token == "max" && (index + 2) < values.size() && values[index + 1] == "step") {
                ruleSet.maxStep = std::max(1, std::stoi(values[index + 2]));
                index += 2;
                continue;
            }
            if (token == "prefer" && (index + 2) < values.size() && values[index + 1] == "center") {
                ruleSet.preferCenter = std::clamp(std::stod(values[index + 2]), 0.0, 1.0);
                index += 2;
                continue;
            }
            if (token == "target" && (index + 1) < values.size()) {
                ruleSet.targetDegree = parseFunctionalTarget(values[index + 1]);
                ++index;
                continue;
            }
            if (token == "reform" && (index + 1) < values.size()) {
                ruleSet.reformMode = values[index + 1];
                ++index;
                continue;
            }
            if (token == "recover" && (index + 2) < values.size() && values[index + 1] == "to") {
                ruleSet.recoverTarget = values[index + 2];
                index += 2;
                continue;
            }
            if (token == "mutate" && (index + 1) < values.size()) {
                ruleSet.mutationAmount = std::max(1, std::stoi(values[index + 1]));
                ++index;
                continue;
            }
            if (token == "follow" && (index + 1) < values.size() && values[index + 1] == "phrase") {
                ruleSet.followPhrase = true;
                ++index;
                continue;
            }
        }
        return ruleSet;
    }

    [[nodiscard]] RuleSet captureCurrentRuleSet() const
    {
        RuleSet ruleSet;
        ruleSet.avoidTonic = avoidTonic_;
        ruleSet.avoidDegree = avoidDegree_;
        ruleSet.avoidTarget = avoidTarget_;
        ruleSet.avoidRepeatedIntervals = avoidRepeatedIntervals_;
        ruleSet.maxStep = maxStep_;
        ruleSet.preferCenter = preferCenter_;
        ruleSet.targetDegree = staticTargetDegree_;
        ruleSet.reformMode = reformMode_;
        ruleSet.recoverTarget = recoverTarget_;
        ruleSet.mutationAmount = mutationAmount_;
        ruleSet.followPhrase = followPhrase_;
        return ruleSet;
    }

    void applyRuleSet(const RuleSet& ruleSet, bool replace)
    {
        if (replace) {
            avoidTonic_ = false;
            avoidDegree_.reset();
            avoidTarget_ = false;
            avoidRepeatedIntervals_ = false;
            maxStep_ = 2;
            preferCenter_ = 0.45;
            staticTargetDegree_ = 0;
            reformMode_ = "gentle";
            recoverTarget_ = "target";
            mutationAmount_ = 2;
            followPhrase_ = false;
        }

        if (ruleSet.avoidTonic.has_value()) avoidTonic_ = *ruleSet.avoidTonic;
        if (ruleSet.avoidDegree.has_value()) avoidDegree_ = *ruleSet.avoidDegree;
        if (ruleSet.avoidTarget.has_value()) avoidTarget_ = *ruleSet.avoidTarget;
        if (ruleSet.avoidRepeatedIntervals.has_value()) avoidRepeatedIntervals_ = *ruleSet.avoidRepeatedIntervals;
        if (ruleSet.maxStep.has_value()) maxStep_ = *ruleSet.maxStep;
        if (ruleSet.preferCenter.has_value()) preferCenter_ = *ruleSet.preferCenter;
        if (ruleSet.targetDegree.has_value()) staticTargetDegree_ = *ruleSet.targetDegree;
        if (ruleSet.reformMode.has_value()) reformMode_ = *ruleSet.reformMode;
        if (ruleSet.recoverTarget.has_value()) recoverTarget_ = *ruleSet.recoverTarget;
        if (ruleSet.mutationAmount.has_value()) mutationAmount_ = *ruleSet.mutationAmount;
        if (ruleSet.followPhrase.has_value()) followPhrase_ = *ruleSet.followPhrase;
    }

    void applyNamedRuleSet(const std::string& name, bool replace)
    {
        if (const auto found = ruleSets_.find(name); found != ruleSets_.end()) {
            applyRuleSet(found->second, replace);
            activeRuleSetName_ = name;
            activeStateLabel_ = name;
        }
    }

    void applyBlendedRuleSet(const std::string& name, double amount)
    {
        if (const auto found = ruleSets_.find(name); found != ruleSets_.end()) {
            const auto blended = blendRuleSets(captureCurrentRuleSet(), found->second, amount);
            applyRuleSet(blended, true);
            activeRuleSetName_ = name;
            activeStateLabel_ = name + " " + std::to_string(static_cast<int>(std::lround(amount * 100.0))) + "%";
        }
    }

    void updateSectionRuleSet(const std::unordered_map<std::string, const EventBuffer*>& inputs, double eventTime)
    {
        const auto sectionInput = inputs.find("section");
        if (sectionInput == inputs.end() || sectionInput->second->events().empty()) {
            return;
        }

        double selected = sectionInput->second->events().front().valueOr(0.0);
        for (const auto& event : sectionInput->second->events()) {
            if (event.time > eventTime + 1.0e-9) {
                break;
            }
            selected = event.valueOr(selected);
        }

        const auto index = std::clamp(static_cast<int>(std::lround(selected)), 0, static_cast<int>(sectionNames_.size()) - 1);
        if (sectionNames_.empty()) {
            return;
        }
        const auto& sectionName = sectionNames_[static_cast<std::size_t>(index)];
        if (sectionName == activeSectionName_) {
            return;
        }
        activeSectionName_ = sectionName;

        if (const auto blend = sectionBlends_.find(sectionName); blend != sectionBlends_.end()) {
            applyBlendedRuleSet(blend->second.ruleSetName, blend->second.amount);
            return;
        }

        if (const auto found = sectionRuleSets_.find(sectionName); found != sectionRuleSets_.end() && found->second != activeRuleSetName_) {
            applyNamedRuleSet(found->second, true);
        }
    }

    [[nodiscard]] std::optional<std::string> activeStateLabel() const override
    {
        if (activeRuleSetName_.empty() && activeStateLabel_.empty()) {
            return std::nullopt;
        }
        return activeStateLabel_.empty() ? std::optional<std::string>(activeRuleSetName_) : std::optional<std::string>(activeStateLabel_);
    }

    [[nodiscard]] static RuleSet blendRuleSets(const RuleSet& from, const RuleSet& to, double amount)
    {
        RuleSet result = from;
        auto mixOptionalInt = [amount](const std::optional<int>& a, const std::optional<int>& b) -> std::optional<int> {
            if (!a.has_value()) return b;
            if (!b.has_value()) return a;
            return static_cast<int>(std::lround((*a * (1.0 - amount)) + (*b * amount)));
        };
        auto mixOptionalDouble = [amount](const std::optional<double>& a, const std::optional<double>& b) -> std::optional<double> {
            if (!a.has_value()) return b;
            if (!b.has_value()) return a;
            return (*a * (1.0 - amount)) + (*b * amount);
        };
        auto chooseOptionalBool = [amount](const std::optional<bool>& a, const std::optional<bool>& b) -> std::optional<bool> {
            if (!a.has_value()) return b;
            if (!b.has_value()) return a;
            return amount >= 0.5 ? b : a;
        };
        auto chooseOptionalString = [amount](const std::optional<std::string>& a, const std::optional<std::string>& b) -> std::optional<std::string> {
            if (!a.has_value()) return b;
            if (!b.has_value()) return a;
            return amount >= 0.5 ? b : a;
        };

        result.avoidTonic = chooseOptionalBool(from.avoidTonic, to.avoidTonic);
        result.avoidDegree = mixOptionalInt(from.avoidDegree, to.avoidDegree);
        result.avoidTarget = chooseOptionalBool(from.avoidTarget, to.avoidTarget);
        result.avoidRepeatedIntervals = chooseOptionalBool(from.avoidRepeatedIntervals, to.avoidRepeatedIntervals);
        result.maxStep = mixOptionalInt(from.maxStep, to.maxStep);
        result.preferCenter = mixOptionalDouble(from.preferCenter, to.preferCenter);
        result.targetDegree = mixOptionalInt(from.targetDegree, to.targetDegree);
        result.reformMode = chooseOptionalString(from.reformMode, to.reformMode);
        result.recoverTarget = chooseOptionalString(from.recoverTarget, to.recoverTarget);
        result.mutationAmount = mixOptionalInt(from.mutationAmount, to.mutationAmount);
        result.followPhrase = chooseOptionalBool(from.followPhrase, to.followPhrase);
        return result;
    }

    void mutateRules(int amount)
    {
        for (int i = 0; i < amount; ++i) {
            const auto choice = nextBits() % 5u;
            if (choice == 0u) {
                avoidTonic_ = !avoidTonic_;
            } else if (choice == 1u) {
                avoidRepeatedIntervals_ = !avoidRepeatedIntervals_;
            } else if (choice == 2u) {
                maxStep_ = std::clamp(maxStep_ + ((nextBits() & 1u) == 0u ? -1 : 1), 1, 12);
            } else if (choice == 3u) {
                preferCenter_ = std::clamp(preferCenter_ + (((nextBits() & 1u) == 0u) ? -0.15 : 0.15), 0.0, 1.0);
            } else {
                avoidTarget_ = !avoidTarget_;
            }
        }
    }

    void rotatePool()
    {
        if (notes_.size() > 1) {
            const auto distance = static_cast<std::size_t>(1 + (nextBits() % static_cast<std::uint32_t>(notes_.size() - 1)));
            std::rotate(notes_.begin(), notes_.begin() + static_cast<std::ptrdiff_t>(distance), notes_.end());
        }
    }

    void rotateRoot()
    {
        const auto target = targetPitch();
        auto best = root_;
        auto bestDistance = std::numeric_limits<int>::max();
        for (const auto note : notes_) {
            const auto distance = std::abs(note - target);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = note;
            }
        }
        root_ = best;
    }

    void applyRecoveryTarget()
    {
        const auto target = targetPitch();
        if (reformMode_ == "gentle" || reformMode_ == "wild" || reformMode_ == "rotate_root") {
            lastNote_ = nearestNoteTo(target);
        }
    }

    [[nodiscard]] int nearestNoteTo(int target) const
    {
        auto best = notes_.front();
        auto bestDistance = std::numeric_limits<int>::max();
        for (const auto note : notes_) {
            const auto distance = std::abs(note - target);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = note;
            }
        }
        return best;
    }

    [[nodiscard]] int targetPitch() const
    {
        const auto degree = recoverTarget_ == "target"
            ? resolvedTargetDegree()
            : parseFunctionalTarget(recoverTarget_);
        return targetPitchNear(lastNote_, degree);
    }

    [[nodiscard]] double candidateWeight(int note, int targetDegree) const
    {
        auto weight = 1.0;
        weight += std::max(0.0, 1.0 - (std::abs(note - lastNote_) / 8.0)) * 0.45;
        weight += std::max(0.0, 1.0 - (std::abs(note - root_) / 18.0)) * preferCenter_ * 0.55;
        weight += std::max(0.0, 1.0 - (std::abs(note - targetPitchNear(lastNote_, targetDegree)) / 14.0)) * 0.65;
        return weight;
    }

    [[nodiscard]] int effectiveMaxStep() const
    {
        if (!followPhrase_) {
            return maxStep_;
        }
        if (activePhraseTarget_ == 4) {
            return std::min(12, maxStep_ + 2);
        }
        if (activePhraseTarget_ == 0) {
            return std::max(1, maxStep_ - 1);
        }
        return maxStep_;
    }

    [[nodiscard]] int resolvedTargetDegree() const
    {
        return followPhrase_ ? activePhraseTarget_ : staticTargetDegree_;
    }

    [[nodiscard]] int scaleDegreeClass(int note) const
    {
        const auto interval = (((note - root_) % 12) + 12) % 12;
        if (interval == 7) return 4;
        if (interval == 5) return 3;
        return 0;
    }

    [[nodiscard]] int targetPitchNear(int anchor, int degree) const
    {
        const auto pitchClass = rootPitchClass() + functionalSemitoneOffset(degree);
        auto best = anchor;
        auto bestDistance = std::numeric_limits<int>::max();
        for (int octave = -4; octave <= 4; ++octave) {
            const auto candidate = ((anchor / 12) + octave) * 12 + ((pitchClass % 12 + 12) % 12);
            const auto distance = std::abs(candidate - anchor);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = candidate;
            }
        }
        return best;
    }

    [[nodiscard]] int rootPitchClass() const
    {
        return ((root_ % 12) + 12) % 12;
    }

    [[nodiscard]] static int parseFunctionalTarget(const std::string& value)
    {
        if (value == "dominant") return 4;
        if (value == "subdominant") return 3;
        return 0;
    }

    [[nodiscard]] static int functionalSemitoneOffset(int degree)
    {
        if (degree == 4) return 7;
        if (degree == 3) return 5;
        return 0;
    }

    [[nodiscard]] static std::optional<int> phraseTargetAtTime(const std::unordered_map<std::string, const EventBuffer*>& inputs, double eventTime)
    {
        const auto input = inputs.find("phrase");
        if (input == inputs.end() || input->second->events().empty()) {
            return std::nullopt;
        }

        double selected = input->second->events().front().valueOr(0.0);
        for (const auto& event : input->second->events()) {
            if (event.time > eventTime + 1.0e-9) {
                break;
            }
            selected = event.valueOr(selected);
        }

        return static_cast<int>(std::lround(selected));
    }

    [[nodiscard]] std::uint32_t nextBits() const
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double randomUnit() const
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    std::vector<int> notes_;
    int root_ = 48;
    mutable std::uint32_t seed_ = 91;
    mutable std::uint32_t state_ = 91;
    std::unordered_map<std::string, RuleSet> ruleSets_;
    std::unordered_map<std::string, std::string> sectionRuleSets_;
    std::unordered_map<std::string, SectionBlend> sectionBlends_;
    std::vector<std::string> sectionNames_;
    RuleSet defaultRuleSet_;
    std::string initialRuleSetName_;
    std::string activeRuleSetName_;
    std::string activeStateLabel_;
    std::string activeSectionName_;
    std::string collapseUseRuleSet_;
    std::vector<std::string> collapseCycleRuleSets_;
    std::size_t collapseCycleIndex_ = 0;
    int lastNote_ = 48;
    std::optional<int> lastInterval_;
    int maxStep_ = 2;
    int mutationAmount_ = 2;
    bool avoidTonic_ = true;
    bool avoidRepeatedIntervals_ = true;
    bool avoidTarget_ = false;
    bool followPhrase_ = false;
    std::optional<int> avoidDegree_;
    int staticTargetDegree_ = 0;
    int activePhraseTarget_ = 0;
    double preferCenter_ = 0.45;
    std::string reformMode_ = "gentle";
    std::string recoverTarget_ = "target";
    int reformStrength_ = 1;
    double collapsePulse_ = 0.0;
};

class RandomNode final : public RuntimeNode {
public:
    explicit RandomNode(const Module& module)
        : notes_(parsePitchList(findPropertyValues(module, "notes")))
        , distribution_(parseDistributionConfig(module))
    {
        if (const auto pass = findPropertyValue(module, "pass")) {
            passChance_ = std::clamp(parseProbability(*pass), 0.0, 1.0);
        }

        if (const auto range = findPropertyValue(module, "range")) {
            const auto dots = range->find("..");
            if (dots != std::string::npos) {
                lowNote_ = parseNoteName(range->substr(0, dots));
                highNote_ = parseNoteName(range->substr(dots + 2));
            }
        }

        if (const auto step = findPropertyValue(module, "step")) {
            stepSize_ = std::max(1, std::stoi(*step));
        }

        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }

        if (const auto mode = findPropertyValue(module, "mode")) {
            mode_ = *mode;
        }

        if (const auto from = findPropertyValues(module, "from"); from.size() >= 2 && from[0] == "held" && from[1] == "notes") {
            useHeldNotes_ = true;
        }

        if (const auto avoidRepeat = findPropertyValue(module, "avoid")) {
            avoidRepeat_ = (*avoidRepeat == "repeat");
        }

        if (const auto maxStep = findPropertyValues(module, "max"); maxStep.size() >= 2 && maxStep[0] == "step") {
            maxStep_ = std::max(1, std::stoi(maxStep[1]));
        }

        if (const auto bias = findPropertyValues(module, "bias"); bias.size() >= 2 && bias[0] == "center") {
            biasCenter_ = std::clamp(std::stod(bias[1]), 0.0, 1.0);
        }

        if (notes_.empty()) {
            notes_ = { 48, 50, 53, 55, 57, 60, 62 };
        }
    }

    void reset(double) override
    {
        state_ = seed_;
        lastNote_ = notes_.front();
        heldNotes_.clear();
        distributionMemory_ = 0.5;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        ingestHeldNotes(inputs);

        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) return;

        for (const auto& event : trigger->second->events()) {
            if (nextUnit() > passChance_) {
                continue;
            }

            out->second->push(Event::makePitch(static_cast<double>(chooseNote()), event.time));
        }
    }

private:
    static double parseProbability(const std::string& value)
    {
        auto text = value;
        if (!text.empty() && text.back() == '%') {
            text.pop_back();
            return std::stod(text) / 100.0;
        }
        return std::stod(text);
    }

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double nextUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    [[nodiscard]] int chooseNote()
    {
        if (mode_ == "walk") {
            lastNote_ = chooseWalkNote();
            return lastNote_;
        }

        lastNote_ = choosePoolNote(lastNote_);
        return lastNote_;
    }

    [[nodiscard]] int chooseWalkNote()
    {
        const auto sourceNotes = activeSourceNotes();
        if (!sourceNotes.empty()) {
            std::vector<int> candidates;
            for (const auto note : sourceNotes) {
                if (avoidRepeat_ && note == lastNote_) {
                    continue;
                }
                if (std::abs(note - lastNote_) <= maxStep_) {
                    candidates.push_back(note);
                }
            }

            if (candidates.empty()) {
                candidates = sourceNotes;
            }

            return chooseBiased(candidates, lastNote_);
        }

        std::vector<int> candidates;
        for (int note = lowNote_; note <= highNote_; note += stepSize_) {
            if (avoidRepeat_ && note == lastNote_) {
                continue;
            }
            if (std::abs(note - lastNote_) <= maxStep_) {
                candidates.push_back(note);
            }
        }

        if (candidates.empty()) {
            for (int note = lowNote_; note <= highNote_; note += stepSize_) {
                candidates.push_back(note);
            }
        }

        return chooseBiased(candidates, lowNote_ + ((highNote_ - lowNote_) / 2));
    }

    [[nodiscard]] int choosePoolNote(int centerReference)
    {
        const auto sourceNotes = activeSourceNotes();
        if (!sourceNotes.empty()) {
            std::vector<int> candidates = sourceNotes;
            if (avoidRepeat_ && candidates.size() > 1) {
                candidates.erase(std::remove(candidates.begin(), candidates.end(), lastNote_), candidates.end());
            }
            if (candidates.empty()) {
                candidates = sourceNotes;
            }
            return chooseBiased(candidates, centerReference);
        }

        std::vector<int> candidates;
        for (int note = lowNote_; note <= highNote_; note += stepSize_) {
            if (avoidRepeat_ && note == lastNote_) {
                continue;
            }
            candidates.push_back(note);
        }
        if (candidates.empty()) {
            candidates.push_back(lastNote_);
        }
        return chooseBiased(candidates, lowNote_ + ((highNote_ - lowNote_) / 2));
    }

    [[nodiscard]] int chooseBiased(const std::vector<int>& candidates, int centerReference)
    {
        if (candidates.empty()) {
            return lastNote_;
        }

        std::vector<double> weights;
        weights.reserve(candidates.size());
        double total = 0.0;
        for (const auto note : candidates) {
            const auto distance = static_cast<double>(std::abs(note - centerReference));
            auto weight = 1.0;
            if (biasCenter_ > 0.0) {
                weight = 1.0 / (1.0 + (distance * biasCenter_));
            }
            weights.push_back(weight);
            total += weight;
        }

        const auto target = distributionPhase01(distribution_, distributionMemory_, [this]() { return nextUnit(); }) * total;
        double running = 0.0;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            running += weights[i];
            if (target <= running) {
                return candidates[i];
            }
        }

        return candidates.back();
    }

    void ingestHeldNotes(const std::unordered_map<std::string, const EventBuffer*>& inputs)
    {
        const auto input = inputs.find("in");
        if (input == inputs.end()) {
            return;
        }

        for (const auto& event : input->second->events()) {
            if (event.isNoteOn()) {
                const auto note = event.noteNumber();
                if (std::find(heldNotes_.begin(), heldNotes_.end(), note) == heldNotes_.end()) {
                    heldNotes_.push_back(note);
                    std::sort(heldNotes_.begin(), heldNotes_.end());
                }
            } else if (event.isNoteOff()) {
                const auto note = event.noteNumber();
                heldNotes_.erase(std::remove(heldNotes_.begin(), heldNotes_.end(), note), heldNotes_.end());
            }
        }
    }

    [[nodiscard]] std::vector<int> activeSourceNotes() const
    {
        if (useHeldNotes_ && !heldNotes_.empty()) {
            return heldNotes_;
        }
        return notes_;
    }

    std::vector<int> notes_;
    std::vector<int> heldNotes_;
    double passChance_ = 1.0;
    int lowNote_ = 48;
    int highNote_ = 72;
    int stepSize_ = 1;
    std::uint32_t seed_ = 0x12345678u;
    std::uint32_t state_ = seed_;
    int lastNote_ = 60;
    std::string mode_ = "pick";
    bool useHeldNotes_ = false;
    bool avoidRepeat_ = false;
    int maxStep_ = 7;
    double biasCenter_ = 0.0;
    DistributionConfig distribution_;
    double distributionMemory_ = 0.5;
};

class SieveNode final : public RuntimeNode {
public:
    explicit SieveNode(const Module& module)
    {
        if (const auto mode = findPropertyValue(module, "mode")) {
            mode_ = *mode;
        }

        if (const auto base = findPropertyValue(module, "base")) {
            if (!base->empty() && (std::isdigit(static_cast<unsigned char>(base->front())) != 0 || base->front() == '-' || base->front() == '+')) {
                base_ = std::stoi(*base);
            } else {
                base_ = parseNoteName(*base);
            }
        }

        for (const auto& property : module.properties) {
            if (property.key != "mod" || property.values.size() < 3) {
                continue;
            }

            Rule rule;
            rule.modulus = std::max(1, std::stoi(property.values[0]));
            std::size_t index = 1;
            if (property.values[index] == "keep") {
                ++index;
            }
            for (; index < property.values.size(); ++index) {
                rule.keep.push_back(normalizedRemainder(std::stoi(property.values[index]), rule.modulus));
            }
            std::sort(rule.keep.begin(), rule.keep.end());
            rule.keep.erase(std::unique(rule.keep.begin(), rule.keep.end()), rule.keep.end());
            if (!rule.keep.empty()) {
                rules_.push_back(std::move(rule));
            }
        }
    }

    void reset(double) override
    {
        stepIndex_ = 0;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        if (const auto pitchIn = inputs.find("pitch"); pitchIn != inputs.end()) {
            if (auto* out = findOutput(outputs, "out")) {
                for (const auto& event : pitchIn->second->events()) {
                    const auto raw = static_cast<int>(std::lround(event.valueOr(60.0)));
                    if (mode_ == "pass") {
                        if (passes(raw)) {
                            out->push(Event::makePitch(static_cast<double>(raw), event.time));
                        }
                    } else {
                        out->push(Event::makePitch(static_cast<double>(nearest(raw)), event.time));
                    }
                }
            }
        }

        if (const auto triggerIn = inputs.find("trigger"); triggerIn != inputs.end()) {
            if (auto* triggerOut = findOutput(outputs, "trigger")) {
                for (const auto& event : triggerIn->second->events()) {
                    const auto step = base_ + stepIndex_;
                    if (passes(step)) {
                        triggerOut->push(Event::makeTrigger(event.time));
                    }
                    ++stepIndex_;
                }
            }
        }
    }

private:
    struct Rule {
        int modulus = 12;
        std::vector<int> keep;
    };

    static EventBuffer* findOutput(std::unordered_map<std::string, EventBuffer*>& outputs, const std::string& name)
    {
        if (const auto found = outputs.find(name); found != outputs.end()) {
            return found->second;
        }
        return nullptr;
    }

    static int normalizedRemainder(int value, int modulus)
    {
        return ((value % modulus) + modulus) % modulus;
    }

    [[nodiscard]] bool passes(int value) const
    {
        if (rules_.empty()) {
            return true;
        }

        for (const auto& rule : rules_) {
            const auto remainder = normalizedRemainder(value, rule.modulus);
            if (std::find(rule.keep.begin(), rule.keep.end(), remainder) == rule.keep.end()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] int nearest(int raw) const
    {
        if (passes(raw)) {
            return clampMidiNote(raw);
        }

        for (int distance = 1; distance <= 64; ++distance) {
            const auto above = raw + distance;
            if (passes(above)) {
                return clampMidiNote(above);
            }

            const auto below = raw - distance;
            if (passes(below)) {
                return clampMidiNote(below);
            }
        }

        return clampMidiNote(raw);
    }

    std::vector<Rule> rules_;
    std::string mode_ = "nearest";
    int base_ = 0;
    int stepIndex_ = 0;
};

class FieldNode final : public RuntimeNode {
public:
    explicit FieldNode(const Module& module)
        : center_(parseNoteName(findPropertyValue(module, "center").value_or("D3")))
        , distribution_(parseDistributionConfig(module))
    {
        if (const auto density = findPropertyValue(module, "density")) {
            density_ = std::clamp(std::stod(*density), 0.0, 1.0);
        }
        if (const auto spread = findPropertyValue(module, "spread")) {
            spread_ = std::max(1.0, std::stod(*spread));
        }
        if (const auto drift = findPropertyValue(module, "drift")) {
            drift_ = std::max(0.0, std::stod(*drift));
        }
        if (const auto emit = findPropertyValue(module, "emit")) {
            emitCount_ = std::max(1, std::stoi(*emit));
        }
        if (const auto reg = findPropertyValue(module, "register")) {
            registerMode_ = *reg;
        }
        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }
        if (const auto mode = findPropertyValue(module, "mode")) {
            mode_ = *mode;
        }
    }

    void reset(double) override
    {
        state_ = seed_;
        currentCenter_ = static_cast<double>(center_);
        distributionMemory_ = 0.0;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) {
            return;
        }

        for (const auto& event : trigger->second->events()) {
            advanceCenter();
            const auto count = std::max(1, emitCount_);
            for (int index = 0; index < count; ++index) {
                const auto relative = count > 1 ? (0.8 * static_cast<double>(index) / static_cast<double>(count)) : 0.0;
                const auto note = clampToRegister(static_cast<int>(std::lround(currentCenter_ + sampleOffset())));
                out->second->push(Event::makePitch(static_cast<double>(note), std::min(0.99, event.time + relative * 0.01)));
            }
            if (const auto densityOut = outputs.find("density"); densityOut != outputs.end()) {
                densityOut->second->push(Event::makeValue(density_, event.time));
            }
        }
    }

private:
    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double nextUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    void advanceCenter()
    {
        if (drift_ <= 0.0) {
            return;
        }
        const auto delta = (nextUnit() * 2.0 - 1.0) * drift_;
        currentCenter_ = std::clamp(currentCenter_ + delta, 0.0, 127.0);
    }

    [[nodiscard]] double sampleOffset()
    {
        auto spread = spread_;
        if (mode_ == "cluster") {
            spread *= density_ * 0.35;
        }
        return distributionOffset(distribution_, spread, distributionMemory_, [this]() { return nextUnit(); });
    }

    [[nodiscard]] int clampToRegister(int note) const
    {
        if (registerMode_ == "low") {
            return std::clamp(note, 24, 60);
        }
        if (registerMode_ == "mid") {
            return std::clamp(note, 48, 84);
        }
        if (registerMode_ == "high") {
            return std::clamp(note, 72, 108);
        }
        if (registerMode_ == "wide") {
            return std::clamp(note, 24, 108);
        }
        return clampMidiNote(note);
    }

    int center_ = 50;
    double currentCenter_ = 50.0;
    double density_ = 0.6;
    double spread_ = 12.0;
    double drift_ = 0.0;
    std::string mode_ = "cloud";
    std::string registerMode_ = "mid";
    int emitCount_ = 1;
    DistributionConfig distribution_;
    double distributionMemory_ = 0.0;
    std::uint32_t seed_ = 0x31415926u;
    std::uint32_t state_ = seed_;
};

class FormulaNode final : public RuntimeNode {
public:
    explicit FormulaNode(const Module& module)
        : notes_(parsePitchList(findPropertyValues(module, "notes")))
    {
        if (notes_.empty()) {
            notes_ = { 60, 63, 67, 70 };
        }
        if (const auto pitchValues = findPropertyValues(module, "pitch"); !pitchValues.empty()) {
            notes_ = parsePitchList(pitchValues);
        }
        transpositions_ = parseNumericValues(findPropertyValues(module, "transpose"));
        lengths_ = parseLengths(findPropertyValues(module, "lengths"));
        if (lengths_.empty()) {
            lengths_.assign(notes_.size(), 1);
        }
        while (lengths_.size() < notes_.size()) {
            lengths_.push_back(lengths_.back());
        }
        timeValues_ = parseTimes(findPropertyValues(module, "rhythm"));
        if (timeValues_.empty()) {
            timeValues_ = parseTimes(findPropertyValues(module, "time"));
        }
        if (timeValues_.empty()) {
            timeValues_.assign(notes_.size(), 0.125);
        }
        while (timeValues_.size() < notes_.size()) {
            timeValues_.push_back(timeValues_.back());
        }

        gateValues_ = parseGateValues(findPropertyValues(module, "gate"));
        if (gateValues_.empty()) {
            gateValues_.assign(notes_.size(), 0.6);
        }
        while (gateValues_.size() < notes_.size()) {
            gateValues_.push_back(gateValues_.back());
        }

        if (const auto rotate = findPropertyValues(module, "rotate"); rotate.size() >= 2 && rotate[0] == "every") {
            rotateEvery_ = std::max(1, std::stoi(rotate[1]));
        }
        if (const auto reverse = findPropertyValue(module, "reverse")) {
            reverse_ = (*reverse == "on");
        }
    }

    void reset(double) override
    {
        step_ = 0;
        countWithinStep_ = 0;
        rotationOffset_ = 0;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) {
            return;
        }

        for (const auto& event : trigger->second->events()) {
            out->second->push(Event::makePitch(static_cast<double>(currentNote()), event.time));
            if (const auto gateOut = outputs.find("gate"); gateOut != outputs.end()) {
                Event gateEvent;
                gateEvent.type = SignalType::gate;
                gateEvent.time = event.time;
                gateEvent.floats.push_back(currentGate());
                gateOut->second->push(std::move(gateEvent));
            }
            if (const auto timeOut = outputs.find("time"); timeOut != outputs.end()) {
                timeOut->second->push(Event::makeValue(currentTime(), event.time));
            }
            if (const auto stepOut = outputs.find("step"); stepOut != outputs.end()) {
                stepOut->second->push(Event::makeValue(static_cast<double>(step_), event.time));
            }
            advance();
        }
    }

private:
    static std::vector<int> parseLengths(const std::vector<std::string>& values)
    {
        std::vector<int> result;
        for (const auto& value : values) {
            result.push_back(std::max(1, std::stoi(value)));
        }
        return result;
    }

    static std::vector<int> parseNumericValues(const std::vector<std::string>& values)
    {
        std::vector<int> result;
        for (const auto& value : values) {
            result.push_back(std::stoi(value));
        }
        return result;
    }

    static std::vector<double> parseTimes(const std::vector<std::string>& values)
    {
        std::vector<double> result;
        for (const auto& value : values) {
            result.push_back(parseTimeToken(value, 120.0));
        }
        return result;
    }

    static std::vector<double> parseGateValues(const std::vector<std::string>& values)
    {
        std::vector<double> result;
        for (auto value : values) {
            if (!value.empty() && value.back() == '%') {
                value.pop_back();
                result.push_back(std::stod(value) / 100.0);
            } else {
                result.push_back(std::stod(value));
            }
        }
        return result;
    }

    [[nodiscard]] std::size_t activeIndex() const
    {
        if (notes_.empty()) {
            return 0;
        }
        auto index = (step_ + rotationOffset_) % notes_.size();
        if (reverse_) {
            index = (notes_.size() - 1 - index) % notes_.size();
        }
        return index;
    }

    [[nodiscard]] int currentNote() const
    {
        const auto index = activeIndex();
        const auto base = notes_[index];
        const auto transposition = transpositions_.empty() ? 0 : transpositions_[index % transpositions_.size()];
        return clampMidiNote(base + transposition);
    }

    [[nodiscard]] double currentTime() const
    {
        const auto index = activeIndex();
        return timeValues_[index % timeValues_.size()];
    }

    [[nodiscard]] double currentGate() const
    {
        const auto index = activeIndex();
        return std::clamp(gateValues_[index % gateValues_.size()], 0.0, 1.0);
    }

    void advance()
    {
        ++countWithinStep_;
        const auto index = activeIndex();
        if (countWithinStep_ >= lengths_[index % lengths_.size()]) {
            countWithinStep_ = 0;
            step_ = (step_ + 1) % notes_.size();
            if (rotateEvery_ > 0 && step_ != 0 && (step_ % static_cast<std::size_t>(rotateEvery_)) == 0) {
                rotationOffset_ = (rotationOffset_ + 1) % notes_.size();
            }
        }
    }

    std::vector<int> notes_;
    std::vector<int> transpositions_;
    std::vector<int> lengths_;
    std::vector<double> timeValues_;
    std::vector<double> gateValues_;
    std::size_t step_ = 0;
    int countWithinStep_ = 0;
    std::size_t rotationOffset_ = 0;
    int rotateEvery_ = 0;
    bool reverse_ = false;
};

class MomentNode final : public RuntimeNode {
public:
    explicit MomentNode(const Module& module)
    {
        for (const auto& property : module.properties) {
            if (property.key != "moment" || property.values.size() < 3) {
                continue;
            }

            Moment moment;
            moment.name = property.values[0];
            moment.length = std::max(1, std::stoi(property.values[1]));
            for (std::size_t index = 2; index < property.values.size(); ++index) {
                moment.notes.push_back(parseNoteName(property.values[index]));
            }
            if (!moment.notes.empty()) {
                moments_.push_back(std::move(moment));
            }
        }

        if (const auto start = findPropertyValue(module, "start")) {
            startMoment_ = *start;
        }

        if (const auto jump = findPropertyValue(module, "jump")) {
            jumpMode_ = *jump;
        }

        if (const auto transition = findPropertyValue(module, "transition")) {
            transitionMode_ = *transition;
        }

        for (const auto& property : module.properties) {
            if (property.key == "chance" && property.values.size() >= 4 && property.values[1] == "->") {
                transitions_[property.values[0]].push_back(Transition {
                    property.values[2],
                    std::max(0.0, std::stod(property.values[3]))
                });
            }
        }

        if (moments_.empty()) {
            moments_.push_back(Moment { "default", 4, { 60, 64, 67 } });
        }
    }

    void reset(double) override
    {
        currentMoment_ = startingMomentIndex();
        stepWithinMoment_ = 0;
        noteIndex_ = 0;
        transitionPulse_ = 0.0;
    }

    [[nodiscard]] std::optional<std::string> activeStateLabel() const override
    {
        if (moments_.empty()) {
            return std::nullopt;
        }
        return moments_[currentMoment_ % moments_.size()].name;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger == inputs.end() || out == outputs.end()) {
            return;
        }

        for (const auto& event : trigger->second->events()) {
            const auto& moment = moments_[currentMoment_ % moments_.size()];
            const auto note = moment.notes[noteIndex_ % moment.notes.size()];
            out->second->push(Event::makePitch(static_cast<double>(note), event.time));
            if (const auto stateOut = outputs.find("state"); stateOut != outputs.end()) {
                stateOut->second->push(Event::makeValue(static_cast<double>(currentMoment_) + transitionPulse_, event.time));
            }
            ++noteIndex_;
            ++stepWithinMoment_;
            if (stepWithinMoment_ >= moment.length) {
                chooseNextMoment();
            }
            transitionPulse_ = std::max(0.0, transitionPulse_ - 0.25);
        }
    }

private:
    struct Moment {
        std::string name;
        int length = 4;
        std::vector<int> notes;
    };

    struct Transition {
        std::string to;
        double weight = 1.0;
    };

    [[nodiscard]] std::size_t startingMomentIndex() const
    {
        for (std::size_t index = 0; index < moments_.size(); ++index) {
            if (moments_[index].name == startMoment_) {
                return index;
            }
        }
        return 0;
    }

    void chooseNextMoment()
    {
        stepWithinMoment_ = 0;
        if (transitionMode_ != "carry") {
            noteIndex_ = 0;
        }

        if (jumpMode_ == "weighted") {
            if (const auto next = weightedTransitionIndex(); next.has_value()) {
                currentMoment_ = *next;
                transitionPulse_ = 0.75;
                return;
            }
        }

        currentMoment_ = (currentMoment_ + 1) % moments_.size();
        transitionPulse_ = 0.35;
    }

    [[nodiscard]] std::optional<std::size_t> weightedTransitionIndex()
    {
        const auto& current = moments_[currentMoment_ % moments_.size()];
        const auto found = transitions_.find(current.name);
        if (found == transitions_.end() || found->second.empty()) {
            return std::nullopt;
        }

        double total = 0.0;
        for (const auto& transition : found->second) {
            total += std::max(0.01, transition.weight);
        }

        auto choice = randomUnit() * total;
        for (const auto& transition : found->second) {
            choice -= std::max(0.01, transition.weight);
            if (choice <= 0.0) {
                for (std::size_t index = 0; index < moments_.size(); ++index) {
                    if (moments_[index].name == transition.to) {
                        return index;
                    }
                }
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] double randomUnit()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<double>(state_ & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    std::vector<Moment> moments_;
    std::unordered_map<std::string, std::vector<Transition>> transitions_;
    std::string startMoment_;
    std::string jumpMode_ = "cycle";
    std::string transitionMode_ = "reset";
    std::size_t currentMoment_ = 0;
    int stepWithinMoment_ = 0;
    int noteIndex_ = 0;
    double transitionPulse_ = 0.0;
    std::uint32_t state_ = 0x10293847u;
};

class ChanceNode final : public RuntimeNode {
public:
    explicit ChanceNode(const Module& module)
    {
        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }

        if (const auto mode = findPropertyValue(module, "mode")) {
            mode_ = *mode;
        }

        if (const auto choose = findPropertyValues(module, "choose"); !choose.empty()) {
            choices_ = parseChoices(choose);
        }

        if (const auto coin = findPropertyValues(module, "coin"); !coin.empty()) {
            mode_ = "coin";
            choices_ = parseChoices(coin);
        }

        if (const auto dice = findPropertyValues(module, "dice"); !dice.empty()) {
            mode_ = "dice";
            choices_ = parseChoices(dice);
        }

        auto iChing = findPropertyValues(module, "i_ching");
        if (iChing.empty()) {
            iChing = findPropertyValues(module, "iching");
        }
        if (!iChing.empty()) {
            mode_ = "i_ching";
            choices_ = parseChoices(iChing);
        }

        if (const auto weights = findPropertyValues(module, "weights"); !weights.empty()) {
            for (const auto& value : weights) {
                weights_.push_back(std::max(0.01, std::stod(value)));
            }
        }

        for (const auto& property : module.properties) {
            if (property.key == "rule" && !property.values.empty()) {
                const auto ruleValue = parseChoiceToken(property.values[0]);
                auto weight = 1.0;
                for (std::size_t index = 1; index + 1 < property.values.size(); ++index) {
                    if (property.values[index] == "weight") {
                        weight = std::max(0.01, std::stod(property.values[index + 1]));
                        break;
                    }
                }
                weightedRules_.push_back(Choice { ruleValue });
                weightedRuleWeights_.push_back(weight);
            }
        }

        if (choices_.empty()) {
            if (mode_ == "coin") {
                choices_ = { Choice { 0.0 }, Choice { 1.0 } };
            } else if (mode_ == "dice") {
                choices_ = { Choice { 1.0 }, Choice { 2.0 }, Choice { 3.0 }, Choice { 4.0 }, Choice { 5.0 }, Choice { 6.0 } };
            } else {
                for (int index = 0; index < 8; ++index) {
                    choices_.push_back(Choice { 48.0 + static_cast<double>(index * 2) });
                }
            }
        }
    }

    void reset(double) override
    {
        state_ = seed_;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto triggerIn = inputs.find("trigger");
        if (triggerIn == inputs.end()) return;

        auto* out = findOutput(outputs, "out");
        auto* pitch = findOutput(outputs, "pitch");
        auto* triggerOut = findOutput(outputs, "trigger");
        auto* indexOut = findOutput(outputs, "index");

        for (const auto& event : triggerIn->second->events()) {
            const auto selection = selectChoice();
            if (!selection.has_value()) {
                continue;
            }

            if (out != nullptr) {
                out->push(Event::makeValue(selection->choice.value, event.time));
            }
            if (pitch != nullptr) {
                pitch->push(Event::makePitch(selection->choice.value, event.time));
            }
            if (triggerOut != nullptr) {
                triggerOut->push(Event::makeTrigger(event.time));
            }
            if (indexOut != nullptr) {
                indexOut->push(Event::makeValue(selection->indexValue, event.time));
            }
        }
    }

private:
    struct Choice {
        double value = 0.0;
    };

    struct Selection {
        Choice choice;
        double indexValue = 0.0;
    };

    static std::vector<Choice> parseChoices(const std::vector<std::string>& tokens)
    {
        std::vector<Choice> result;
        result.reserve(tokens.size());
        for (const auto& token : tokens) {
            result.push_back({ parseChoiceToken(token) });
        }
        return result;
    }

    static double parseChoiceToken(const std::string& token)
    {
        return parseChoiceScalar(token);
    }

    static EventBuffer* findOutput(std::unordered_map<std::string, EventBuffer*>& outputs, const std::string& name)
    {
        if (const auto found = outputs.find(name); found != outputs.end()) {
            return found->second;
        }
        return nullptr;
    }

    [[nodiscard]] std::optional<Selection> selectChoice()
    {
        if (choices_.empty()) {
            return std::nullopt;
        }

        if (!weightedRules_.empty()) {
            const auto index = weightedIndex(weightedRuleWeights_, [this]() { return nextUnit(); });
            return Selection { weightedRules_[index], static_cast<double>(index) };
        }

        if (!weights_.empty() && weights_.size() == choices_.size() && mode_ != "coin" && mode_ != "i_ching") {
            const auto index = weightedIndex(weights_, [this]() { return nextUnit(); });
            return Selection { choices_[index], static_cast<double>(index) };
        }

        if (mode_ == "coin") {
            const auto heads = nextUnit() >= 0.5;
            const auto index = heads ? 1U : 0U;
            return Selection { choices_[index % choices_.size()], static_cast<double>(index) };
        }

        if (mode_ == "dice") {
            const auto faceCount = std::max(2, static_cast<int>(choices_.size()));
            const auto face = 1 + static_cast<int>(nextBits() % static_cast<std::uint32_t>(faceCount));
            const auto index = static_cast<std::size_t>((face - 1) % static_cast<int>(choices_.size()));
            return Selection { choices_[index], static_cast<double>(face) };
        }

        const auto hexagram = rollHexagram();
        const auto index = static_cast<std::size_t>((hexagram - 1) % static_cast<int>(choices_.size()));
        return Selection { choices_[index], static_cast<double>(hexagram) };
    }

    [[nodiscard]] int rollHexagram()
    {
        int bits = 0;
        for (int line = 0; line < 6; ++line) {
            int sum = 0;
            for (int coin = 0; coin < 3; ++coin) {
                sum += (nextUnit() < 0.5) ? 2 : 3;
            }
            const auto yang = (sum == 7 || sum == 9);
            if (yang) {
                bits |= (1 << line);
            }
        }
        return bits + 1;
    }

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double nextUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    std::string mode_ = "coin";
    std::vector<Choice> choices_;
    std::vector<double> weights_;
    std::vector<Choice> weightedRules_;
    std::vector<double> weightedRuleWeights_;
    std::uint32_t seed_ = 0x2468ace1u;
    std::uint32_t state_ = seed_;
};

class TableNode final : public RuntimeNode {
public:
    explicit TableNode(const Module& module)
    {
        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }

        for (const auto& property : module.properties) {
            if (property.key != "rule" || property.values.empty()) {
                continue;
            }
            rawChoices_.push_back(property.values[0]);
            choices_.push_back(parseChoiceScalar(property.values[0]));
            auto weight = 1.0;
            for (std::size_t index = 1; index + 1 < property.values.size(); ++index) {
                if (property.values[index] == "weight") {
                    weight = std::max(0.01, std::stod(property.values[index + 1]));
                    break;
                }
            }
            weights_.push_back(weight);
        }

        if (choices_.empty()) {
            rawChoices_ = { "C4", "E4", "G4" };
            choices_ = { 60.0, 64.0, 67.0 };
            weights_ = { 1.0, 1.0, 1.0 };
        }
    }

    void reset(double) override
    {
        state_ = seed_;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        if (trigger == inputs.end()) {
            return;
        }
        for (const auto& event : trigger->second->events()) {
            const auto index = weightedIndex(weights_, [this]() { return nextUnit(); });
            emit(outputs, rawChoices_[index], choices_[index], static_cast<double>(index), event.time);
        }
    }

private:
    void emit(std::unordered_map<std::string, EventBuffer*>& outputs, const std::string& raw, double value, double index, double time)
    {
        if (const auto out = outputs.find("out"); out != outputs.end()) {
            out->second->push(Event::makeValue(value, time));
        }
        if (const auto pitch = outputs.find("pitch"); pitch != outputs.end()) {
            pitch->second->push(Event::makePitch(value, time));
        }
        if (const auto timeOut = outputs.find("time"); timeOut != outputs.end()) {
            timeOut->second->push(Event::makeValue(value, time));
        }
        if (const auto gate = outputs.find("gate"); gate != outputs.end()) {
            gate->second->push(Event::makeValue(parseGateScalar(raw), time));
        }
        if (const auto trigger = outputs.find("trigger"); trigger != outputs.end()) {
            trigger->second->push(Event::makeTrigger(time));
        }
        if (const auto indexOut = outputs.find("index"); indexOut != outputs.end()) {
            indexOut->second->push(Event::makeValue(index, time));
        }
    }

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double nextUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    std::vector<std::string> rawChoices_;
    std::vector<double> choices_;
    std::vector<double> weights_;
    std::uint32_t seed_ = 0x55112233u;
    std::uint32_t state_ = seed_;
};

class MarkovNode final : public RuntimeNode {
public:
    explicit MarkovNode(const Module& module)
    {
        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }
        if (const auto start = findPropertyValue(module, "start")) {
            startState_ = *start;
        }

        for (const auto& property : module.properties) {
            if (property.key != "state" || property.values.size() < 4 || property.values[1] != "->") {
                continue;
            }

            TransitionSet set;
            set.from = property.values[0];
            for (std::size_t index = 2; index + 1 < property.values.size(); index += 2) {
                set.targets.push_back(property.values[index]);
                set.weights.push_back(std::max(0.01, std::stod(property.values[index + 1])));
            }
            if (!set.targets.empty()) {
                transitions_[set.from] = std::move(set);
            }
        }

        if (startState_.empty() && !transitions_.empty()) {
            startState_ = transitions_.begin()->first;
        }
    }

    void reset(double) override
    {
        state_ = seed_;
        currentState_ = startState_;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        if (trigger == inputs.end()) {
            return;
        }

        for (const auto& event : trigger->second->events()) {
            if (!currentState_.empty()) {
                if (const auto valueOut = outputs.find("value"); valueOut != outputs.end()) {
                    valueOut->second->push(Event::makeValue(parseChoiceScalar(currentState_), event.time));
                }
                if (const auto out = outputs.find("out"); out != outputs.end()) {
                    out->second->push(Event::makePitch(parseChoiceScalar(currentState_), event.time));
                }
                if (const auto timeOut = outputs.find("time"); timeOut != outputs.end()) {
                    timeOut->second->push(Event::makeValue(parseChoiceScalar(currentState_), event.time));
                }
                if (const auto gateOut = outputs.find("gate"); gateOut != outputs.end()) {
                    gateOut->second->push(Event::makeValue(parseGateScalar(currentState_), event.time));
                }
                if (const auto stateOut = outputs.find("state"); stateOut != outputs.end()) {
                    stateOut->second->push(Event::makeValue(static_cast<double>(stateIndex(currentState_)), event.time));
                }
            }
            advance();
        }
    }

private:
    struct TransitionSet {
        std::string from;
        std::vector<std::string> targets;
        std::vector<double> weights;
    };

    void advance()
    {
        const auto found = transitions_.find(currentState_);
        if (found == transitions_.end() || found->second.targets.empty()) {
            return;
        }
        const auto index = weightedIndex(found->second.weights, [this]() { return nextUnit(); });
        currentState_ = found->second.targets[index];
    }

    [[nodiscard]] int stateIndex(const std::string& name) const
    {
        int index = 0;
        for (const auto& [key, _] : transitions_) {
            if (key == name) {
                return index;
            }
            ++index;
        }
        return index;
    }

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double nextUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    std::unordered_map<std::string, TransitionSet> transitions_;
    std::string startState_;
    std::string currentState_;
    std::uint32_t seed_ = 0x22446688u;
    std::uint32_t state_ = seed_;
};

class TreeNode final : public RuntimeNode {
public:
    explicit TreeNode(const Module& module)
    {
        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }
        if (const auto root = findPropertyValue(module, "root")) {
            root_ = *root;
        }

        for (const auto& property : module.properties) {
            if (property.key != "node" || property.values.size() < 4 || property.values[1] != "->") {
                continue;
            }
            Node node;
            node.name = property.values[0];
            for (std::size_t index = 2; index + 1 < property.values.size(); index += 2) {
                node.targets.push_back(property.values[index]);
                node.weights.push_back(std::max(0.01, std::stod(property.values[index + 1])));
            }
            if (!node.targets.empty()) {
                nodes_[node.name] = std::move(node);
            }
        }

        if (root_.empty() && !nodes_.empty()) {
            root_ = nodes_.begin()->first;
        }
    }

    void reset(double) override
    {
        state_ = seed_;
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto trigger = inputs.find("trigger");
        if (trigger == inputs.end()) {
            return;
        }

        for (const auto& event : trigger->second->events()) {
            auto nodeName = root_;
            int depth = 0;
            while (depth < 16) {
                const auto found = nodes_.find(nodeName);
                if (found == nodes_.end() || found->second.targets.empty()) {
                    break;
                }
                const auto index = weightedIndex(found->second.weights, [this]() { return nextUnit(); });
                nodeName = found->second.targets[index];
                ++depth;
            }

            const auto value = parseChoiceScalar(nodeName);
            if (const auto out = outputs.find("out"); out != outputs.end()) {
                out->second->push(Event::makeValue(value, event.time));
            }
            if (const auto pitch = outputs.find("pitch"); pitch != outputs.end()) {
                pitch->second->push(Event::makePitch(value, event.time));
            }
            if (const auto timeOut = outputs.find("time"); timeOut != outputs.end()) {
                timeOut->second->push(Event::makeValue(value, event.time));
            }
            if (const auto gateOut = outputs.find("gate"); gateOut != outputs.end()) {
                gateOut->second->push(Event::makeValue(parseGateScalar(nodeName), event.time));
            }
            if (const auto triggerOut = outputs.find("trigger"); triggerOut != outputs.end()) {
                triggerOut->second->push(Event::makeTrigger(event.time));
            }
            if (const auto indexOut = outputs.find("index"); indexOut != outputs.end()) {
                indexOut->second->push(Event::makeValue(static_cast<double>(depth), event.time));
            }
        }
    }

private:
    struct Node {
        std::string name;
        std::vector<std::string> targets;
        std::vector<double> weights;
    };

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double nextUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    std::unordered_map<std::string, Node> nodes_;
    std::string root_;
    std::uint32_t seed_ = 0x8899aabbu;
    std::uint32_t state_ = seed_;
};

class StagesNode final : public RuntimeNode {
public:
    explicit StagesNode(const Module& module)
    {
        const auto mode = findPropertyValue(module, "mode");
        const auto run = findPropertyValues(module, "run");
        freeRunning_ = (mode.has_value() && *mode == "loop") || (!run.empty() && run.front() == "free");

        for (const auto& property : module.properties) {
            if (property.key != "stage" || property.values.size() < 7) continue;

            Stage stage;
            for (std::size_t i = 1; i + 1 < property.values.size(); i += 2) {
                const auto& key = property.values[i];
                const auto& value = property.values[i + 1];
                if (key == "level") stage.level = std::stod(value);
                if (key == "time") stage.durationSeconds = parseTimeToken(value, 120.0);
                if (key == "overlap") stage.overlapSeconds = std::max(0.0, parseTimeToken(value, 120.0));
                if (key == "curve") stage.curve = value;
            }
            stages_.push_back(stage);
        }

        if (stages_.empty()) {
            stages_.push_back(Stage { 0.0, 0.25, 0.0, "linear" });
            stages_.push_back(Stage { 1.0, 0.25, 0.0, "linear" });
        }
    }

    void reset(double sampleRate) override
    {
        sampleRate_ = sampleRate;
        active_ = freeRunning_;
        stageIndex_ = 0;
        stageElapsed_ = 0.0;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) return;

        if (const auto trigger = inputs.find("trigger"); trigger != inputs.end() && !trigger->second->events().empty()) {
            active_ = true;
            stageIndex_ = 0;
            stageElapsed_ = 0.0;
        }

        if (!active_ || stages_.empty()) {
            out->second->push(Event::makeValue(currentLevel_, 0.0));
            return;
        }

        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        advance(blockDuration);
        out->second->push(Event::makeValue(currentLevel_, 0.0));
    }

private:
    void advance(double deltaSeconds)
    {
        double remaining = deltaSeconds;
        while (remaining > 0.0 && active_) {
            auto& stage = stages_[stageIndex_];
            const auto duration = std::max(stage.durationSeconds, 1.0e-6);
            const auto before = stageElapsed_;
            const auto consume = std::min(remaining, duration - stageElapsed_);
            stageElapsed_ += consume;
            remaining -= consume;

            const auto t = applyCurve(stage.curve, stageElapsed_ / duration);
            const auto startLevel = (stageIndex_ == 0) ? currentLevel_ : stages_[stageIndex_ - 1].level;
            currentLevel_ = startLevel + (stage.level - startLevel) * t;

            if (before < duration && stageElapsed_ >= duration - 1.0e-9) {
                stageElapsed_ = 0.0;
                ++stageIndex_;
                if (stageIndex_ >= stages_.size()) {
                    if (freeRunning_) {
                        stageIndex_ = 0;
                    } else {
                        stageIndex_ = stages_.size() - 1;
                        active_ = false;
                    }
                }
            }
        }
    }

    std::vector<Stage> stages_;
    double sampleRate_ = 44100.0;
    std::size_t stageIndex_ = 0;
    double stageElapsed_ = 0.0;
    double currentLevel_ = 0.0;
    bool active_ = false;
    bool freeRunning_ = false;
};

class ModulatorNode final : public RuntimeNode {
public:
    explicit ModulatorNode(const Module& module)
    {
        if (const auto channels = findPropertyValue(module, "channels")) {
            channelCount_ = std::clamp(std::stoi(*channels), 1, 4);
        }
        if (const auto mode = findPropertyValue(module, "mode")) {
            mode_ = *mode;
        }
        if (const auto overlap = findPropertyValue(module, "overlap")) {
            overlapEnabled_ = (*overlap == "on");
        }

        freeRunning_ = (mode_ == "loop" || mode_ == "lfo" || mode_ == "sequence");
        baseChannelStages_.assign(static_cast<std::size_t>(channelCount_), {});
        channelStates_.assign(static_cast<std::size_t>(channelCount_), {});

        for (const auto& property : module.properties) {
            if (property.key != "stage" || property.values.size() < 2) continue;

            std::optional<int> channel;
            Stage stage;
            std::size_t startIndex = 0;
            if (!property.values.empty() && std::all_of(property.values.front().begin(), property.values.front().end(), [](unsigned char c) {
                    return std::isdigit(c) != 0;
                })) {
                startIndex = 1;
            }

            for (std::size_t i = startIndex; i + 1 < property.values.size(); i += 2) {
                const auto& key = property.values[i];
                const auto& value = property.values[i + 1];
                if (key == "channel") channel = std::clamp(std::stoi(value), 1, 4);
                if (key == "level") stage.level = std::stod(value);
                if (key == "time") stage.durationSeconds = parseTimeToken(value, 120.0);
                if (key == "overlap") stage.overlapSeconds = std::max(0.0, parseTimeToken(value, 120.0));
                if (key == "curve") stage.curve = value;
            }

            if (channel.has_value()) {
                ensureChannel(*channel);
                baseChannelStages_[static_cast<std::size_t>(*channel - 1)].push_back(stage);
            } else {
                for (int index = 0; index < channelCount_; ++index) {
                    baseChannelStages_[static_cast<std::size_t>(index)].push_back(stage);
                }
            }
        }

        for (auto& stages : baseChannelStages_) {
            if (stages.empty()) {
                stages.push_back(Stage { 0.0, 0.25, 0.0, "linear" });
                stages.push_back(Stage { 1.0, 0.25, 0.0, "smooth" });
                stages.push_back(Stage { 0.0, 0.25, 0.0, "exp" });
            }
        }
    }

    void reset(double sampleRate) override
    {
        sampleRate_ = sampleRate;
        for (auto& state : channelStates_) {
            state.selectedStage = 0;
            state.sequenceStageIndex = 0;
            state.sequenceElapsed = 0.0;
            state.sequenceBaseLevel = 0.0;
            state.sequenceLevel = 0.0;
            state.currentLevel = 0.0;
            state.sequenceActive = freeRunning_;
            state.manualStages.clear();
        }
    }

    [[nodiscard]] std::optional<std::string> activeStateLabel() const override
    {
        return std::to_string(channelCount_) + "ch " + mode_ + (overlapEnabled_ ? " overlap" : "");
    }

    [[nodiscard]] std::optional<ModulatorStateSnapshot> modulatorState() const override
    {
        ModulatorStateSnapshot snapshot;
        snapshot.channelCount = channelCount_;
        snapshot.mode = mode_;
        snapshot.overlapEnabled = overlapEnabled_;
        snapshot.channels.reserve(channelStates_.size());

        for (std::size_t channelIndex = 0; channelIndex < channelStates_.size(); ++channelIndex) {
            const auto& state = channelStates_[channelIndex];
            ModulatorChannelStateSnapshot channelSnapshot;
            channelSnapshot.level = state.currentLevel;
            channelSnapshot.activeStages = currentActiveStages(state);
            snapshot.channels.push_back(std::move(channelSnapshot));
        }

        return snapshot;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto rateScale = rateAtInput(inputs);
        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate * rateScale;

        for (int channel = 0; channel < channelCount_; ++channel) {
            auto& state = channelStates_[static_cast<std::size_t>(channel)];
            const auto channelNumber = channel + 1;
            auto stages = modulatedStages(inputs, channelNumber);
            std::vector<std::size_t> endedStages;
            handleSelections(inputs, state, channelNumber, stages.size());
            handleResets(inputs, state, channelNumber);
            handleGlobalTriggers(inputs, state, channelNumber, stages);
            handleStageTriggers(inputs, state, channelNumber, stages);
            advanceSequence(state, stages, blockDuration, endedStages);
            advanceManualStages(state, stages, blockDuration, endedStages);
            state.currentLevel = state.sequenceLevel + sumManualLevels(state);

            const auto valueEvent = Event::makeValue(state.currentLevel, 0.0);
            const auto portName = "ch" + std::to_string(channelNumber);
            if (const auto out = outputs.find(portName); out != outputs.end()) {
                out->second->push(valueEvent);
            }
            if (channel == 0) {
                if (const auto out = outputs.find("out"); out != outputs.end()) {
                    out->second->push(valueEvent);
                }
            }

            emitStageOutputs(outputs, channelNumber, state, endedStages);
        }
    }

private:
    struct ManualStage {
        std::size_t stageIndex = 0;
        double elapsed = 0.0;
        double startLevel = 0.0;
        double currentLevel = 0.0;
    };

    struct ChannelState {
        std::size_t selectedStage = 0;
        std::size_t sequenceStageIndex = 0;
        double sequenceElapsed = 0.0;
        double sequenceBaseLevel = 0.0;
        double sequenceLevel = 0.0;
        double currentLevel = 0.0;
        bool sequenceActive = false;
        std::vector<ManualStage> manualStages;
    };

    void ensureChannel(int channel)
    {
        channelCount_ = std::max(channelCount_, channel);
        baseChannelStages_.resize(static_cast<std::size_t>(channelCount_));
        channelStates_.resize(static_cast<std::size_t>(channelCount_));
    }

    [[nodiscard]] std::vector<Stage> modulatedStages(const std::unordered_map<std::string, const EventBuffer*>& inputs, int channel) const
    {
        auto stages = baseChannelStages_[static_cast<std::size_t>(channel - 1)];
        for (std::size_t stageIndex = 0; stageIndex < stages.size(); ++stageIndex) {
            const auto stageNumber = static_cast<int>(stageIndex) + 1;
            const auto prefix = "ch" + std::to_string(channel) + "_s" + std::to_string(stageNumber) + "_";
            if (const auto level = inputValue(inputs, prefix + "level"); level.has_value()) {
                stages[stageIndex].level = *level;
            }
            if (const auto time = inputValue(inputs, prefix + "time"); time.has_value()) {
                stages[stageIndex].durationSeconds = std::clamp(*time, 0.001, 600.0);
            }
            if (const auto curve = inputValue(inputs, prefix + "curve"); curve.has_value()) {
                stages[stageIndex].curve = curveName(*curve);
            }
        }
        return stages;
    }

    void handleSelections(const std::unordered_map<std::string, const EventBuffer*>& inputs,
        ChannelState& state,
        int channel,
        std::size_t stageCount) const
    {
        if (stageCount == 0) return;
        const auto port = "ch" + std::to_string(channel) + "_select";
        if (const auto selected = inputValue(inputs, port); selected.has_value()) {
            const auto index = std::clamp(static_cast<int>(std::lround(*selected)) - 1, 0, static_cast<int>(stageCount) - 1);
            state.selectedStage = static_cast<std::size_t>(index);
        }
    }

    void handleResets(const std::unordered_map<std::string, const EventBuffer*>& inputs,
        ChannelState& state,
        int channel)
    {
        const auto resetPort = "ch" + std::to_string(channel) + "_reset";
        if (hasTrigger(inputs, resetPort)) {
            resetChannel(state);
        }

        for (int stage = 1; stage <= 13; ++stage) {
            const auto port = "ch" + std::to_string(channel) + "_s" + std::to_string(stage) + "_reset";
            if (hasTrigger(inputs, port)) {
                removeManualStage(state, static_cast<std::size_t>(stage - 1));
            }
        }
    }

    void handleGlobalTriggers(const std::unordered_map<std::string, const EventBuffer*>& inputs,
        ChannelState& state,
        int channel,
        const std::vector<Stage>& stages)
    {
        if (stages.empty()) return;
        if (hasTrigger(inputs, "trigger") || hasTrigger(inputs, "ch" + std::to_string(channel) + "_trigger")) {
            startSequence(state, state.selectedStage, stages);
        }
    }

    void handleStageTriggers(const std::unordered_map<std::string, const EventBuffer*>& inputs,
        ChannelState& state,
        int channel,
        const std::vector<Stage>& stages)
    {
        for (int stage = 1; stage <= std::min<int>(13, static_cast<int>(stages.size())); ++stage) {
            const auto port = "ch" + std::to_string(channel) + "_s" + std::to_string(stage) + "_trigger";
            if (hasTrigger(inputs, port)) {
                startManualStage(state, static_cast<std::size_t>(stage - 1), stages);
            }
        }
    }

    [[nodiscard]] double rateAtInput(const std::unordered_map<std::string, const EventBuffer*>& inputs) const
    {
        if (const auto rate = inputs.find("rate"); rate != inputs.end() && !rate->second->events().empty()) {
            return std::clamp(rate->second->events().back().valueOr(1.0), 0.125, 8.0);
        }
        return 1.0;
    }

    [[nodiscard]] static std::optional<double> inputValue(const std::unordered_map<std::string, const EventBuffer*>& inputs, const std::string& port)
    {
        if (const auto found = inputs.find(port); found != inputs.end() && !found->second->events().empty()) {
            return found->second->events().back().valueOr(0.0);
        }
        return std::nullopt;
    }

    [[nodiscard]] static bool hasTrigger(const std::unordered_map<std::string, const EventBuffer*>& inputs, const std::string& port)
    {
        if (const auto found = inputs.find(port); found != inputs.end()) {
            return !found->second->events().empty();
        }
        return false;
    }

    [[nodiscard]] static std::string curveName(double value)
    {
        const auto normalized = std::clamp(value, 0.0, 1.0);
        if (normalized < 0.25) return "linear";
        if (normalized < 0.5) return "smooth";
        if (normalized < 0.75) return "exp";
        return "log";
    }

    void resetChannel(ChannelState& state) const
    {
        state.sequenceActive = freeRunning_;
        state.sequenceStageIndex = state.selectedStage;
        state.sequenceElapsed = 0.0;
        state.sequenceBaseLevel = 0.0;
        state.sequenceLevel = 0.0;
        state.currentLevel = 0.0;
        state.manualStages.clear();
    }

    void startSequence(ChannelState& state, std::size_t stageIndex, const std::vector<Stage>& stages) const
    {
        if (stages.empty()) return;
        if (!overlapEnabled_) {
            state.manualStages.clear();
        }
        state.selectedStage = std::min(stageIndex, stages.size() - 1);
        state.sequenceStageIndex = state.selectedStage;
        state.sequenceElapsed = 0.0;
        state.sequenceBaseLevel = overlapEnabled_ ? state.currentLevel : 0.0;
        state.sequenceLevel = state.sequenceBaseLevel;
        state.sequenceActive = true;
    }

    void startManualStage(ChannelState& state, std::size_t stageIndex, const std::vector<Stage>& stages) const
    {
        if (stageIndex >= stages.size()) return;
        if (!overlapEnabled_) {
            state.manualStages.clear();
        }
        state.manualStages.push_back({ stageIndex, 0.0, state.currentLevel, state.currentLevel });
    }

    void removeManualStage(ChannelState& state, std::size_t stageIndex) const
    {
        state.manualStages.erase(std::remove_if(state.manualStages.begin(), state.manualStages.end(), [stageIndex](const ManualStage& active) {
            return active.stageIndex == stageIndex;
        }), state.manualStages.end());
    }

    void advanceSequence(ChannelState& state, const std::vector<Stage>& stages, double deltaSeconds, std::vector<std::size_t>& endedStages)
    {
        if (!state.sequenceActive || stages.empty()) return;

        double remaining = deltaSeconds;
        while (remaining > 0.0 && state.sequenceActive) {
            const auto& stage = stages[state.sequenceStageIndex];
            const auto duration = std::max(stage.durationSeconds, 1.0e-6);
            const auto overlapPoint = overlapEnabled_ && state.sequenceStageIndex + 1 < stages.size()
                ? std::clamp(duration - std::min(stage.overlapSeconds, duration - 1.0e-6), 0.0, duration)
                : duration;
            const auto nextBoundary = (overlapEnabled_ && stage.overlapSeconds > 1.0e-6 && state.sequenceStageIndex + 1 < stages.size() && state.sequenceElapsed < overlapPoint - 1.0e-9)
                ? overlapPoint
                : duration;
            const auto before = state.sequenceElapsed;
            const auto consume = std::min(remaining, nextBoundary - state.sequenceElapsed);
            state.sequenceElapsed += consume;
            remaining -= consume;

            const auto t = applyCurve(stage.curve, state.sequenceElapsed / duration);
            state.sequenceLevel = state.sequenceBaseLevel + (stage.level - state.sequenceBaseLevel) * t;

            if (nextBoundary == overlapPoint && before < overlapPoint && state.sequenceElapsed >= overlapPoint - 1.0e-9) {
                state.manualStages.push_back({ state.sequenceStageIndex, state.sequenceElapsed, state.sequenceBaseLevel, state.sequenceLevel });
                state.sequenceBaseLevel = state.sequenceLevel;
                state.sequenceElapsed = 0.0;
                ++state.sequenceStageIndex;
                if (state.sequenceStageIndex >= stages.size()) {
                    if (freeRunning_) {
                        state.sequenceStageIndex = 0;
                    } else {
                        state.sequenceStageIndex = stages.size() - 1;
                        state.sequenceActive = false;
                    }
                }
                continue;
            }

            if (before < duration && state.sequenceElapsed >= duration - 1.0e-9) {
                endedStages.push_back(state.sequenceStageIndex);
                state.sequenceBaseLevel = stage.level;
                state.sequenceElapsed = 0.0;
                ++state.sequenceStageIndex;
                if (state.sequenceStageIndex >= stages.size()) {
                    if (freeRunning_) {
                        state.sequenceStageIndex = 0;
                    } else {
                        state.sequenceStageIndex = stages.size() - 1;
                        state.sequenceActive = false;
                    }
                }
            }
        }
    }

    void advanceManualStages(ChannelState& state, const std::vector<Stage>& stages, double deltaSeconds, std::vector<std::size_t>& endedStages) const
    {
        for (auto& active : state.manualStages) {
            const auto& stage = stages[active.stageIndex];
            const auto duration = std::max(stage.durationSeconds, 1.0e-6);
            active.elapsed = std::min(duration, active.elapsed + deltaSeconds);
            const auto t = applyCurve(stage.curve, active.elapsed / duration);
            active.currentLevel = active.startLevel + (stage.level - active.startLevel) * t;
        }

        state.manualStages.erase(std::remove_if(state.manualStages.begin(), state.manualStages.end(), [&stages, &endedStages](const ManualStage& active) {
            const auto duration = std::max(stages[active.stageIndex].durationSeconds, 1.0e-6);
            const auto finished = active.elapsed >= duration - 1.0e-9;
            if (finished) {
                endedStages.push_back(active.stageIndex);
            }
            return finished;
        }), state.manualStages.end());
    }

    void emitStageOutputs(std::unordered_map<std::string, EventBuffer*>& outputs,
        int channel,
        const ChannelState& state,
        const std::vector<std::size_t>& endedStages) const
    {
        std::array<bool, 13> active {};
        if (state.sequenceActive && state.sequenceStageIndex < active.size()) {
            active[state.sequenceStageIndex] = true;
        }
        for (const auto& manual : state.manualStages) {
            if (manual.stageIndex < active.size()) {
                active[manual.stageIndex] = true;
            }
        }

        std::array<bool, 13> ended {};
        for (const auto stageIndex : endedStages) {
            if (stageIndex < ended.size()) {
                ended[stageIndex] = true;
            }
        }

        for (int stage = 1; stage <= 13; ++stage) {
            const auto prefix = "ch" + std::to_string(channel) + "_s" + std::to_string(stage) + "_";
            if (const auto gateOut = outputs.find(prefix + "gate"); gateOut != outputs.end()) {
                gateOut->second->push(Event { SignalType::gate, 0.0, {}, { active[static_cast<std::size_t>(stage - 1)] ? 1.0 : 0.0 } });
            }
            if (ended[static_cast<std::size_t>(stage - 1)]) {
                if (const auto endOut = outputs.find(prefix + "end"); endOut != outputs.end()) {
                    endOut->second->push(Event::makeTrigger(0.0));
                }
            }
        }
    }

    [[nodiscard]] static std::vector<int> currentActiveStages(const ChannelState& state)
    {
        std::vector<int> stages;
        if (state.sequenceActive) {
            stages.push_back(static_cast<int>(state.sequenceStageIndex) + 1);
        }
        for (const auto& manual : state.manualStages) {
            stages.push_back(static_cast<int>(manual.stageIndex) + 1);
        }
        std::sort(stages.begin(), stages.end());
        stages.erase(std::unique(stages.begin(), stages.end()), stages.end());
        return stages;
    }

    [[nodiscard]] static double sumManualLevels(const ChannelState& state)
    {
        double total = 0.0;
        for (const auto& active : state.manualStages) {
            total += active.currentLevel;
        }
        return total;
    }

    double sampleRate_ = 44100.0;
    int channelCount_ = 1;
    std::string mode_ = "loop";
    bool freeRunning_ = true;
    bool overlapEnabled_ = false;
    std::vector<std::vector<Stage>> baseChannelStages_;
    std::vector<ChannelState> channelStates_;
};

class QuantizeNode final : public RuntimeNode {
public:
    explicit QuantizeNode(const Module& module)
        : scale_(scaleSemitones(findPropertyValues(module, "scale")))
    {
    }

    void reset(double) override {}

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        if (const auto midiInput = inputs.find("in"); midiInput != inputs.end()) {
            for (const auto& event : midiInput->second->events()) {
                if (!event.isNoteOn() && !event.isNoteOff()) {
                    if (const auto out = outputs.find("out"); out != outputs.end()) {
                        out->second->push(event);
                    }
                    continue;
                }

                auto quantized = event;
                quantized.ints[1] = quantizeToScale(event.noteNumber(), scale_);
                if (const auto out = outputs.find("out"); out != outputs.end()) {
                    out->second->push(quantized);
                }
                if (const auto pitchOut = outputs.find("pitch"); pitchOut != outputs.end()) {
                    pitchOut->second->push(Event::makePitch(static_cast<double>(quantized.noteNumber()), event.time));
                }
            }
        }

        if (const auto pitchInput = inputs.find("pitch"); pitchInput != inputs.end()) {
            for (const auto& event : pitchInput->second->events()) {
                const auto note = quantizeToScale(static_cast<int>(std::lround(event.valueOr(60.0))), scale_);
                if (const auto pitchOut = outputs.find("pitch"); pitchOut != outputs.end()) {
                    pitchOut->second->push(Event::makePitch(static_cast<double>(note), event.time));
                }
            }
        }

        if (const auto valueInput = inputs.find("values"); valueInput != inputs.end()) {
            for (const auto& event : valueInput->second->events()) {
                const auto note = quantizeToScale(static_cast<int>(std::lround(event.valueOr(60.0))), scale_);
                if (const auto pitchOut = outputs.find("pitch"); pitchOut != outputs.end()) {
                    pitchOut->second->push(Event::makePitch(static_cast<double>(note), event.time));
                }
            }
        }
    }

private:
    std::vector<int> scale_;
};

class WarpNode final : public RuntimeNode {
public:
    explicit WarpNode(const Module& module)
    {
        for (const auto& property : module.properties) {
            if (property.key == "fold" && property.values.size() >= 3) {
                FoldRule rule;
                rule.fromPitchClass = parseNoteName(property.values[0]) % 12;
                if (property.values[1] == "near" && property.values.size() >= 3) {
                    rule.toPitchClass = parseNoteName(property.values[2]) % 12;
                } else {
                    rule.toPitchClass = parseNoteName(property.values[1]) % 12;
                }
                folds_.push_back(rule);
            }

            if (property.key == "amount" && !property.values.empty()) {
                amount_ = std::clamp(std::stod(property.values[0]), 0.0, 1.0);
            }

            if (property.key == "seed" && !property.values.empty()) {
                state_ = static_cast<std::uint32_t>(std::stoul(property.values[0]));
                seed_ = state_;
            }

            if (property.key == "wormhole" && property.values.size() >= 3 && property.values[0] == "every") {
                wormholeEnabled_ = true;
                if (property.values[1] == "random") {
                    const auto dots = property.values[2].find("..");
                    if (dots != std::string::npos) {
                        wormholeMin_ = std::max(1, std::stoi(property.values[2].substr(0, dots)));
                        wormholeMax_ = std::max(wormholeMin_, std::stoi(property.values[2].substr(dots + 2)));
                    } else {
                        wormholeMin_ = wormholeMax_ = std::max(1, std::stoi(property.values[2]));
                    }
                } else {
                    wormholeMin_ = wormholeMax_ = std::max(1, std::stoi(property.values[1]));
                }
            }

            if (property.key == "wormhole" && property.values.size() >= 2 && property.values[0] == "to") {
                wormholeTargets_ = parsePitchList({ property.values.begin() + 1, property.values.end() });
            }
        }

        if (wormholeTargets_.empty() && !folds_.empty()) {
            for (const auto& fold : folds_) {
                wormholeTargets_.push_back(60 + fold.toPitchClass);
            }
        }
        if (wormholeTargets_.empty()) {
            wormholeTargets_ = { 54, 60, 66, 72 };
        }
    }

    void reset(double) override
    {
        state_ = seed_;
        eventCount_ = 0;
        nextWormholeAt_ = chooseNextWormholeInterval();
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) {
            return;
        }

        if (const auto pitchInput = inputs.find("pitch"); pitchInput != inputs.end()) {
            for (const auto& event : pitchInput->second->events()) {
                out->second->push(Event::makePitch(warpPitch(event.valueOr(60.0)), event.time));
            }
        }

        if (const auto valueInput = inputs.find("values"); valueInput != inputs.end()) {
            for (const auto& event : valueInput->second->events()) {
                out->second->push(Event::makePitch(warpPitch(event.valueOr(60.0)), event.time));
            }
        }
    }

private:
    struct FoldRule {
        int fromPitchClass = 0;
        int toPitchClass = 0;
    };

    [[nodiscard]] double warpPitch(double rawPitch)
    {
        double warped = rawPitch;
        for (const auto& fold : folds_) {
            warped = applyFoldRule(warped, fold);
        }

        ++eventCount_;
        if (wormholeEnabled_ && eventCount_ >= nextWormholeAt_) {
            warped = applyWormhole(warped);
            eventCount_ = 0;
            nextWormholeAt_ = chooseNextWormholeInterval();
        }

        return warped;
    }

    [[nodiscard]] double applyFoldRule(double pitch, const FoldRule& rule) const
    {
        const int note = static_cast<int>(std::lround(pitch));
        const int pitchClass = ((note % 12) + 12) % 12;
        if (pitchClass != rule.fromPitchClass) {
            return pitch;
        }

        const int octave = note / 12;
        int target = octave * 12 + rule.toPitchClass;
        while ((target - note) > 6) target -= 12;
        while ((note - target) > 6) target += 12;
        return pitch + ((static_cast<double>(target) - pitch) * amount_);
    }

    [[nodiscard]] double applyWormhole(double pitch)
    {
        const auto index = static_cast<std::size_t>(nextBits() % wormholeTargets_.size());
        const auto target = wormholeTargets_[index];
        return pitch + (static_cast<double>(target) - pitch);
    }

    [[nodiscard]] int chooseNextWormholeInterval()
    {
        if (!wormholeEnabled_) {
            return std::numeric_limits<int>::max();
        }
        if (wormholeMin_ >= wormholeMax_) {
            return wormholeMin_;
        }
        const auto span = static_cast<std::uint32_t>(wormholeMax_ - wormholeMin_ + 1);
        return wormholeMin_ + static_cast<int>(nextBits() % span);
    }

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    std::vector<FoldRule> folds_;
    std::vector<int> wormholeTargets_;
    double amount_ = 1.0;
    bool wormholeEnabled_ = false;
    int wormholeMin_ = 4;
    int wormholeMax_ = 4;
    int eventCount_ = 0;
    int nextWormholeAt_ = std::numeric_limits<int>::max();
    std::uint32_t seed_ = 97;
    std::uint32_t state_ = 97;
};

class BitsNode final : public RuntimeNode {
public:
    explicit BitsNode(const Module& module)
    {
        if (const auto target = findPropertyValue(module, "target")) {
            target_ = *target;
        } else if (const auto on = findPropertyValue(module, "on")) {
            target_ = *on;
        }
        if (const auto channel = findPropertyValue(module, "channel")) {
            inputChannel_ = clampMidiChannel(std::stoi(*channel));
        }

        for (const auto& property : module.properties) {
            if (property.key == "events" && !property.values.empty()) {
                eventScope_ = property.values.front();
            } else if (property.key == "only" && !property.values.empty()) {
                eventScope_ = property.values.front();
            } else if (property.key == "status" && !property.values.empty()) {
                eventScope_ = property.values.front();
            } else if (property.key == "cc" && !property.values.empty()) {
                parseCcSelector(property.values.front(), ccFilter_);
            } else if (property.key == "except" && property.values.size() >= 2 && property.values.front() == "cc") {
                parseCcSelector(property.values[1], excludedCcFilter_);
            } else if (property.key == "except" && property.values.size() >= 2 && property.values.front() == "note") {
                parseNoteSelector(property.values[1], excludedNoteFilter_);
            } else if (property.key == "except" && property.values.size() >= 2 && property.values.front() == "velocity") {
                parseDataSelector(property.values[1], excludedVelocityFilter_);
            } else if (property.key == "note" && !property.values.empty()) {
                parseNoteSelector(property.values.front(), noteFilter_);
            } else if (property.key == "velocity" && !property.values.empty()) {
                parseDataSelector(property.values.front(), velocityFilter_);
            }

            if (property.key == "and" && !property.values.empty()) {
                operations_.push_back({ OperationKind::bitAnd, parseBitValue(property.values.front()) });
            } else if (property.key == "or" && !property.values.empty()) {
                operations_.push_back({ OperationKind::bitOr, parseBitValue(property.values.front()) });
            } else if (property.key == "xor" && !property.values.empty()) {
                operations_.push_back({ OperationKind::bitXor, parseBitValue(property.values.front()) });
            } else if (property.key == "left" && !property.values.empty()) {
                operations_.push_back({ OperationKind::shiftLeft, parseBitValue(property.values.front()) });
            } else if (property.key == "right" && !property.values.empty()) {
                operations_.push_back({ OperationKind::shiftRight, parseBitValue(property.values.front()) });
            } else if (property.key == "not") {
                operations_.push_back({ OperationKind::bitNot, 0 });
            } else if (property.key == "mask" && property.values.size() >= 2 && property.values.front() == "with") {
                operations_.push_back({ OperationKind::bitAnd, parseBitValue(property.values[1]) });
            }
        }
    }

    void reset(double) override {}

    [[nodiscard]] std::optional<std::string> activeStateLabel() const override
    {
        std::vector<std::string> parts;
        if (eventScope_ != "all") {
            parts.push_back(eventScope_);
        }
        if (inputChannel_.has_value()) {
            parts.push_back("ch" + std::to_string(*inputChannel_));
        }
        if (hasActiveFilter(ccFilter_)) {
            parts.push_back("cc " + describeFilter(ccFilter_));
        }
        if (hasActiveFilter(excludedCcFilter_)) {
            parts.push_back("!cc " + describeFilter(excludedCcFilter_));
        }
        if (hasActiveFilter(noteFilter_)) {
            parts.push_back("note " + describeFilter(noteFilter_));
        }
        if (hasActiveFilter(excludedNoteFilter_)) {
            parts.push_back("!note " + describeFilter(excludedNoteFilter_));
        }
        if (hasActiveFilter(velocityFilter_)) {
            parts.push_back("vel " + describeFilter(velocityFilter_));
        }
        if (hasActiveFilter(excludedVelocityFilter_)) {
            parts.push_back("!vel " + describeFilter(excludedVelocityFilter_));
        }
        if (parts.empty()) {
            parts.push_back(target_);
        }
        if (parts.size() > 3) {
            parts.resize(3);
            parts.push_back("...");
        }

        std::ostringstream stream;
        for (std::size_t index = 0; index < parts.size(); ++index) {
            if (index > 0) stream << " ";
            stream << parts[index];
        }
        return stream.str();
    }

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) return;

        if (const auto input = inputs.find("in"); input != inputs.end()) {
            for (const auto& event : input->second->events()) {
                out->second->push(apply(event));
            }
        }
    }

private:
    enum class OperationKind {
        bitAnd,
        bitOr,
        bitXor,
        bitNot,
        shiftLeft,
        shiftRight
    };

    struct Operation {
        OperationKind kind = OperationKind::bitAnd;
        int argument = 0;
    };

    struct IntFilter {
        std::optional<int> exact;
        std::optional<std::pair<int, int>> range;
    };

    [[nodiscard]] Event apply(const Event& event) const
    {
        if (event.type != SignalType::midi || operations_.empty() || !matchesScope(event)) {
            return event;
        }

        auto transformed = event;
        if (transformed.ints.size() < 4) {
            return transformed;
        }

        int* field = fieldPointer(transformed);
        if (field == nullptr) {
            return transformed;
        }

        int value = *field;
        for (const auto& operation : operations_) {
            switch (operation.kind) {
            case OperationKind::bitAnd:
                value &= operation.argument;
                break;
            case OperationKind::bitOr:
                value |= operation.argument;
                break;
            case OperationKind::bitXor:
                value ^= operation.argument;
                break;
            case OperationKind::bitNot:
                value = ~value;
                break;
            case OperationKind::shiftLeft:
                value <<= std::max(0, operation.argument);
                break;
            case OperationKind::shiftRight:
                value >>= std::max(0, operation.argument);
                break;
            }
        }

        *field = clampForTarget(value);

        if ((target_ == "note" || target_ == "velocity") && (transformed.isNoteOn() || transformed.isNoteOff())) {
            transformed.ints[1] = clampMidiData(transformed.ints[1]);
            transformed.ints[2] = clampMidiData(transformed.ints[2]);
        }

        return transformed;
    }

    [[nodiscard]] int* fieldPointer(Event& event) const
    {
        if (target_ == "status") return &event.ints[0];
        if (target_ == "note") {
            if (event.isNoteOn() || event.isNoteOff()) return &event.ints[1];
            return nullptr;
        }
        if (target_ == "velocity") {
            if (event.isNoteOn() || event.isNoteOff()) return &event.ints[2];
            return nullptr;
        }
        if (target_ == "channel") return &event.ints[3];
        if (target_ == "data1") return &event.ints[1];
        if (target_ == "data2") return &event.ints[2];
        return &event.ints[2];
    }

    [[nodiscard]] int clampForTarget(int value) const
    {
        if (target_ == "status") return std::clamp(value, 0, 255);
        if (target_ == "channel") return clampMidiChannel(value);
        return clampMidiData(value);
    }

    [[nodiscard]] bool matchesScope(const Event& event) const
    {
        if (event.ints.empty()) return false;
        if (inputChannel_.has_value()) {
            if (event.ints.size() < 4 || event.ints[3] != *inputChannel_) {
                return false;
            }
        }

        const int status = event.ints[0] & 0xF0;
        const bool isNoteEvent = event.isNoteOn() || event.isNoteOff() || status == 0x80 || status == 0x90;
        if (hasActiveFilter(noteFilter_) || hasActiveFilter(excludedNoteFilter_) || hasActiveFilter(velocityFilter_) || hasActiveFilter(excludedVelocityFilter_)) {
            if (!isNoteEvent || event.ints.size() < 3) {
                return false;
            }
            if (!matchesIntFilter(event.ints[1], noteFilter_)) {
                return false;
            }
            if (matchesIntFilter(event.ints[1], excludedNoteFilter_)) {
                return false;
            }
            if (!matchesIntFilter(event.ints[2], velocityFilter_)) {
                return false;
            }
            if (matchesIntFilter(event.ints[2], excludedVelocityFilter_)) {
                return false;
            }
        }

        if (eventScope_ == "all") return true;
        if (eventScope_ == "note" || eventScope_ == "notes") {
            return isNoteEvent;
        }
        if (eventScope_ == "note_on" || eventScope_ == "noteon") {
            return status == 0x90 && event.ints.size() >= 3 && event.ints[2] > 0;
        }
        if (eventScope_ == "note_off" || eventScope_ == "noteoff") {
            return status == 0x80 || (status == 0x90 && event.ints.size() >= 3 && event.ints[2] == 0);
        }
        if (eventScope_ == "cc" || eventScope_ == "controller" || eventScope_ == "controllers") {
            if (status != 0xB0) {
                return false;
            }
            if (event.ints.size() < 2) {
                return false;
            }
            const int cc = event.ints[1];
            if (!matchesIntFilter(cc, ccFilter_)) {
                return false;
            }
            if (matchesIntFilter(cc, excludedCcFilter_)) {
                return false;
            }
            return true;
        }
        if (eventScope_ == "pitch" || eventScope_ == "pitchbend") {
            return status == 0xE0;
        }
        if (eventScope_ == "program" || eventScope_ == "program_change") {
            return status == 0xC0;
        }
        if (eventScope_ == "channel_aftertouch" || eventScope_ == "aftertouch") {
            return status == 0xD0;
        }
        if (eventScope_ == "poly_aftertouch" || eventScope_ == "poly_pressure") {
            return status == 0xA0;
        }
        return true;
    }

    static void parseCcSelector(const std::string& token, IntFilter& filter)
    {
        parseDataSelector(token, filter);
    }

    static void parseDataSelector(const std::string& token, IntFilter& filter)
    {
        const auto dots = token.find("..");
        if (dots != std::string::npos) {
            const int low = clampMidiData(std::stoi(token.substr(0, dots)));
            const int high = clampMidiData(std::stoi(token.substr(dots + 2)));
            filter.range = std::pair { std::min(low, high), std::max(low, high) };
            filter.exact.reset();
            return;
        }

        filter.exact = clampMidiData(std::stoi(token));
        filter.range.reset();
    }

    static void parseNoteSelector(const std::string& token, IntFilter& filter)
    {
        const auto dots = token.find("..");
        if (dots != std::string::npos) {
            const int low = clampMidiNote(parseNoteName(token.substr(0, dots)));
            const int high = clampMidiNote(parseNoteName(token.substr(dots + 2)));
            filter.range = std::pair { std::min(low, high), std::max(low, high) };
            filter.exact.reset();
            return;
        }

        filter.exact = clampMidiNote(parseNoteName(token));
        filter.range.reset();
    }

    [[nodiscard]] static bool matchesIntFilter(int value, const IntFilter& filter)
    {
        if (filter.exact.has_value()) {
            return value == *filter.exact;
        }
        if (filter.range.has_value()) {
            return value >= filter.range->first && value <= filter.range->second;
        }
        return true;
    }

    [[nodiscard]] static bool hasActiveFilter(const IntFilter& filter)
    {
        return filter.exact.has_value() || filter.range.has_value();
    }

    [[nodiscard]] static std::string describeFilter(const IntFilter& filter)
    {
        if (filter.exact.has_value()) {
            return std::to_string(*filter.exact);
        }
        if (filter.range.has_value()) {
            return std::to_string(filter.range->first) + ".." + std::to_string(filter.range->second);
        }
        return "*";
    }

    std::string target_ = "velocity";
    std::string eventScope_ = "all";
    std::optional<int> inputChannel_;
    IntFilter ccFilter_;
    IntFilter excludedCcFilter_;
    IntFilter noteFilter_;
    IntFilter excludedNoteFilter_;
    IntFilter velocityFilter_;
    IntFilter excludedVelocityFilter_;
    std::vector<Operation> operations_;
};

class EquationNode final : public RuntimeNode {
public:
    explicit EquationNode(const Module& module)
    {
        for (const auto& property : module.properties) {
            if (property.key == "value") {
                valueExpr_ = parseEquationExpression(property.values);
            } else if (property.key == "pitch") {
                pitchExpr_ = parseEquationExpression(property.values);
            } else if (property.key == "note") {
                noteExpr_ = parseEquationExpression(property.values);
            } else if (property.key == "velocity") {
                velocityExpr_ = parseEquationExpression(property.values);
            } else if (property.key == "channel") {
                channelExpr_ = parseEquationExpression(property.values);
            }
        }
    }

    void reset(double sampleRate) override
    {
        sampleRate_ = sampleRate;
        runningTimeSeconds_ = 0.0;
        lastValueInput_ = 0.0;
        lastPitchInput_ = 60.0;
        activeNoteRemap_.clear();
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto blockDuration = static_cast<double>(context.blockSize) / std::max(context.sampleRate, 1.0);
        captureLatestScalarInputs(inputs);

        const auto midiIn = findEvents(inputs, "in");
        const auto triggerIn = findEvents(inputs, "trigger");
        const auto valueIn = findEvents(inputs, "value");
        const auto pitchIn = findEvents(inputs, "pitch");

        if (midiIn != nullptr && !midiIn->empty()) {
            processMidiStream(context, *midiIn, inputs, outputs);
        } else {
            std::vector<double> evaluationTimes;
            if (triggerIn != nullptr && !triggerIn->empty()) {
                for (const auto& event : *triggerIn) {
                    evaluationTimes.push_back(event.time);
                }
            } else if (valueIn != nullptr && !valueIn->empty()) {
                for (const auto& event : *valueIn) {
                    evaluationTimes.push_back(event.time);
                }
            } else if (pitchIn != nullptr && !pitchIn->empty()) {
                for (const auto& event : *pitchIn) {
                    evaluationTimes.push_back(event.time);
                }
            } else {
                evaluationTimes.push_back(0.0);
            }

            for (const auto eventTime : evaluationTimes) {
                emitDerivedSignals(context, outputs, buildVariables(context, nullptr, eventTime, inputs), eventTime);
            }
        }

        runningTimeSeconds_ += blockDuration;
    }

private:
    using Vars = std::unordered_map<std::string, double>;

    [[nodiscard]] static const std::vector<Event>* findEvents(const std::unordered_map<std::string, const EventBuffer*>& inputs, const std::string& port)
    {
        const auto it = inputs.find(port);
        if (it == inputs.end() || it->second == nullptr) {
            return nullptr;
        }
        return &it->second->events();
    }

    void captureLatestScalarInputs(const std::unordered_map<std::string, const EventBuffer*>& inputs)
    {
        if (const auto valueEvents = findEvents(inputs, "value"); valueEvents != nullptr && !valueEvents->empty()) {
            lastValueInput_ = valueEvents->back().valueOr(lastValueInput_);
        }
        if (const auto pitchEvents = findEvents(inputs, "pitch"); pitchEvents != nullptr && !pitchEvents->empty()) {
            lastPitchInput_ = pitchEvents->back().valueOr(lastPitchInput_);
        }
    }

    void processMidiStream(const ProcessContext& context,
        const std::vector<Event>& midiEvents,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs)
    {
        const auto out = outputs.find("out");
        for (const auto& event : midiEvents) {
            auto vars = buildVariables(context, &event, event.time, inputs);
            emitDerivedSignals(context, outputs, vars, event.time);

            if (out == outputs.end() || event.type != SignalType::midi) {
                continue;
            }

            auto transformed = event;
            if (transformed.ints.size() >= 4) {
                if (noteExpr_ && (transformed.isNoteOn() || transformed.isNoteOff())) {
                    transformed.ints[1] = remapNote(transformed, vars);
                }
                if (velocityExpr_ && (transformed.isNoteOn() || transformed.isNoteOff())) {
                    transformed.ints[2] = clampMidiData(static_cast<int>(std::lround(evaluateExpression(velocityExpr_.get(), vars))));
                }
                if (channelExpr_) {
                    transformed.ints[3] = clampMidiChannel(static_cast<int>(std::lround(evaluateExpression(channelExpr_.get(), vars))));
                }
            }
            out->second->push(transformed);
        }
    }

    void emitDerivedSignals(const ProcessContext&,
        std::unordered_map<std::string, EventBuffer*>& outputs,
        const Vars& vars,
        double eventTime)
    {
        if (valueExpr_) {
            if (const auto out = outputs.find("value"); out != outputs.end()) {
                out->second->push(Event::makeValue(evaluateExpression(valueExpr_.get(), vars), eventTime));
            }
        }

        if (pitchExpr_) {
            if (const auto out = outputs.find("pitch"); out != outputs.end()) {
                out->second->push(Event::makePitch(evaluateExpression(pitchExpr_.get(), vars), eventTime));
            }
        }
    }

    [[nodiscard]] Vars buildVariables(const ProcessContext& context,
        const Event* event,
        double eventTime,
        const std::unordered_map<std::string, const EventBuffer*>& inputs) const
    {
        Vars vars;
        const auto absoluteTime = runningTimeSeconds_ + eventTime;
        vars["t"] = absoluteTime;
        vars["time"] = absoluteTime;
        vars["beat"] = absoluteTime * context.bpm / 60.0;
        vars["bpm"] = context.bpm;
        vars["value"] = currentScalarAtTime(inputs, "value", eventTime, lastValueInput_);
        vars["pitch"] = currentScalarAtTime(inputs, "pitch", eventTime, lastPitchInput_);
        vars["x"] = vars["value"];
        vars["note"] = vars["pitch"];
        vars["velocity"] = 100.0;
        vars["channel"] = 1.0;
        vars["status"] = 0.0;

        if (event != nullptr) {
            if (event->type == SignalType::midi) {
                vars["note"] = static_cast<double>(event->noteNumber());
                vars["pitch"] = static_cast<double>(event->noteNumber());
                vars["velocity"] = static_cast<double>(event->velocityValue());
                vars["channel"] = static_cast<double>(event->channel());
                vars["status"] = !event->ints.empty() ? static_cast<double>(event->ints[0]) : 0.0;
                vars["x"] = vars["note"];
            } else {
                vars["x"] = event->valueOr(vars["value"]);
                if (event->type == SignalType::pitch) {
                    vars["pitch"] = event->valueOr(vars["pitch"]);
                    vars["note"] = vars["pitch"];
                    vars["x"] = vars["pitch"];
                }
                if (event->type == SignalType::value) {
                    vars["value"] = event->valueOr(vars["value"]);
                    vars["x"] = vars["value"];
                }
            }
        }

        return vars;
    }

    [[nodiscard]] static double currentScalarAtTime(const std::unordered_map<std::string, const EventBuffer*>& inputs,
        const std::string& port,
        double eventTime,
        double fallback)
    {
        const auto it = inputs.find(port);
        if (it == inputs.end() || it->second == nullptr) {
            return fallback;
        }

        double current = fallback;
        for (const auto& event : it->second->events()) {
            if (event.time > eventTime + 1.0e-9) {
                break;
            }
            current = event.valueOr(current);
        }
        return current;
    }

    double sampleRate_ = 44100.0;
    double runningTimeSeconds_ = 0.0;
    double lastValueInput_ = 0.0;
    double lastPitchInput_ = 60.0;
    std::unordered_map<int, std::vector<int>> activeNoteRemap_;
    std::unique_ptr<ExprNode> valueExpr_;
    std::unique_ptr<ExprNode> pitchExpr_;
    std::unique_ptr<ExprNode> noteExpr_;
    std::unique_ptr<ExprNode> velocityExpr_;
    std::unique_ptr<ExprNode> channelExpr_;

    int remapNote(const Event& event, const Vars& vars)
    {
        const auto originalNote = event.noteNumber();
        const auto key = event.channel() * 128 + originalNote;
        if (event.isNoteOn()) {
            const auto mapped = clampMidiNote(static_cast<int>(std::lround(evaluateExpression(noteExpr_.get(), vars))));
            activeNoteRemap_[key].push_back(mapped);
            return mapped;
        }

        if (event.isNoteOff()) {
            const auto it = activeNoteRemap_.find(key);
            if (it != activeNoteRemap_.end() && !it->second.empty()) {
                const auto mapped = it->second.back();
                it->second.pop_back();
                if (it->second.empty()) {
                    activeNoteRemap_.erase(it);
                }
                return mapped;
            }
        }

        return clampMidiNote(static_cast<int>(std::lround(evaluateExpression(noteExpr_.get(), vars))));
    }
};

class ListsNode final : public RuntimeNode {
public:
    explicit ListsNode(const Module& module)
        : pitchValues_(parsePitchList(findPropertyValues(module, "pitch")))
        , timeValues_(parseNumericListOrTimes(findPropertyValues(module, "time")))
        , gateValues_(parseGateList(findPropertyValues(module, "gate")))
    {
        if (pitchValues_.empty()) pitchValues_ = { 60, 64, 67, 70 };
        if (timeValues_.empty()) timeValues_ = { 0.125, 0.25, 0.5, 1.0 };
        if (gateValues_.empty()) gateValues_ = { 0.3, 0.6, 0.9, 0.4 };

        for (const auto& property : module.properties) {
            if (property.key == "advance" && property.values.size() >= 3) {
                if (property.values[0] == "pitch") pitchAdvance_ = property.values[2];
                if (property.values[0] == "time") timeAdvance_ = property.values[2];
                if (property.values[0] == "gate") gateAdvance_ = property.values[2];
            }

            if (property.key == "interpolate" && property.values.size() >= 2) {
                if (property.values[0] == "pitch") interpolatePitch_ = property.values[1] == "on";
                if (property.values[0] == "time") interpolateTime_ = property.values[1] == "on";
            }
        }
    }

    void reset(double) override
    {
        pitchIndex_ = 0;
        timeIndex_ = 0;
        gateIndex_ = 0;
        phase_ = 0.0;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        maybeAdvance(inputs, "note", pitchAdvance_, pitchIndex_, pitchValues_.size());
        maybeAdvance(inputs, "threshold", timeAdvance_, timeIndex_, timeValues_.size());
        maybeAdvance(inputs, "random", gateAdvance_, gateIndex_, gateValues_.size());

        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        phase_ += blockDuration;

        if (const auto pitchOut = outputs.find("pitch"); pitchOut != outputs.end()) {
            pitchOut->second->push(Event::makePitch(currentPitch(), 0.0));
        }

        if (const auto timeOut = outputs.find("time"); timeOut != outputs.end()) {
            timeOut->second->push(Event::makeValue(currentTime(), 0.0));
        }

        if (const auto gateOut = outputs.find("gate"); gateOut != outputs.end()) {
            Event gateEvent;
            gateEvent.type = SignalType::gate;
            gateEvent.time = 0.0;
            gateEvent.floats.push_back(currentGate());
            gateOut->second->push(std::move(gateEvent));
        }
    }

private:
    static std::vector<double> parseNumericListOrTimes(const std::vector<std::string>& values)
    {
        std::vector<double> result;
        result.reserve(values.size());
        for (const auto& value : values) {
            result.push_back(parseTimeToken(value, 120.0));
        }
        return result;
    }

    static std::vector<double> parseGateList(const std::vector<std::string>& values)
    {
        std::vector<double> result;
        result.reserve(values.size());
        for (auto value : values) {
            if (!value.empty() && value.back() == '%') {
                value.pop_back();
                result.push_back(std::stod(value) / 100.0);
            } else {
                result.push_back(std::stod(value));
            }
        }
        return result;
    }

    static void maybeAdvance(const std::unordered_map<std::string, const EventBuffer*>& inputs,
        const std::string& portName,
        const std::string& mode,
        std::size_t& index,
        std::size_t size)
    {
        if (size == 0) return;
        const auto port = inputs.find(portName);
        if (port == inputs.end()) return;

        bool shouldAdvance = false;
        if (mode == "note") {
            shouldAdvance = std::any_of(port->second->events().begin(), port->second->events().end(), [](const Event& event) {
                return event.isNoteOn();
            });
        } else if (mode == "threshold") {
            shouldAdvance = std::any_of(port->second->events().begin(), port->second->events().end(), [](const Event& event) {
                return event.valueOr(0.0) >= 0.7;
            });
        } else if (mode == "random") {
            shouldAdvance = !port->second->events().empty();
        }

        if (shouldAdvance) {
            index = (index + 1) % size;
        }
    }

    [[nodiscard]] double interpolationMix() const
    {
        const auto period = std::max(baseTime(), 1.0e-6);
        const auto local = std::fmod(phase_, period);
        return local / period;
    }

    [[nodiscard]] double currentPitch() const
    {
        if (!interpolatePitch_ || pitchValues_.size() < 2) {
            return static_cast<double>(pitchValues_[pitchIndex_ % pitchValues_.size()]);
        }

        const auto current = pitchValues_[pitchIndex_ % pitchValues_.size()];
        const auto next = pitchValues_[(pitchIndex_ + 1) % pitchValues_.size()];
        return static_cast<double>(current) + (static_cast<double>(next - current) * interpolationMix());
    }

    [[nodiscard]] double currentTime() const
    {
        if (!interpolateTime_ || timeValues_.size() < 2) {
            return baseTime();
        }

        const auto current = baseTime();
        const auto next = timeValues_[(timeIndex_ + 1) % timeValues_.size()];
        return current + ((next - current) * interpolationMix());
    }

    [[nodiscard]] double currentGate() const
    {
        return gateValues_[gateIndex_ % gateValues_.size()];
    }

    [[nodiscard]] double baseTime() const
    {
        return timeValues_[timeIndex_ % timeValues_.size()];
    }

    std::vector<int> pitchValues_;
    std::vector<double> timeValues_;
    std::vector<double> gateValues_;
    std::string pitchAdvance_ = "note";
    std::string timeAdvance_ = "threshold";
    std::string gateAdvance_ = "random";
    std::size_t pitchIndex_ = 0;
    std::size_t timeIndex_ = 0;
    std::size_t gateIndex_ = 0;
    double phase_ = 0.0;
    bool interpolatePitch_ = false;
    bool interpolateTime_ = false;
};

class ToNotesNode final : public RuntimeNode {
public:
    explicit ToNotesNode(const Module& module)
        : scale_(scaleSemitones(findPropertyValues(module, "scale")))
    {
        if (const auto mode = findPropertyValue(module, "mode")) {
            polyphonic_ = (*mode == "poly");
        }
        if (const auto voices = findPropertyValue(module, "voices")) {
            voiceLimit_ = std::max(1, std::stoi(*voices));
        }
        if (const auto steal = findPropertyValue(module, "steal")) {
            stealMode_ = *steal;
        }
        if (const auto range = findPropertyValue(module, "range")) {
            const auto dots = range->find("..");
            if (dots != std::string::npos) {
                lowNote_ = parseNoteName(range->substr(0, dots));
                highNote_ = parseNoteName(range->substr(dots + 2));
            }
        }
        if (const auto center = findPropertyValue(module, "center")) {
            centerNote_ = parseNoteName(*center);
        } else {
            centerNote_ = (lowNote_ + highNote_) / 2;
        }
        if (const auto velocity = findPropertyValue(module, "velocity")) {
            velocity_ = std::clamp(std::stoi(*velocity), 1, 127);
        }
        if (const auto chord = findPropertyValue(module, "chord")) {
            const auto normalized = *chord;
            if (normalized == "scale_triad" || normalized == "diatonic_triad") {
                scaleChordSize_ = 3;
            } else if (normalized == "scale7" || normalized == "diatonic7") {
                scaleChordSize_ = 4;
            } else {
                intervals_ = intervalsForChord(normalized);
            }
        }
        const auto intervalValues = findPropertyValues(module, "intervals");
        if (!intervalValues.empty()) {
            intervals_ = parseIntervals(intervalValues);
        }
        if (const auto invert = findPropertyValue(module, "invert")) {
            inversion_ = std::max(0, std::stoi(*invert));
        }
        if (const auto drop = findPropertyValue(module, "drop")) {
            dropVoice_ = std::max(0, std::stoi(*drop));
        }
        if (const auto spread = findPropertyValues(module, "spread"); !spread.empty()) {
            spreadSemitones_ = parseSpread(spread);
        }
        if (const auto movement = findPropertyValue(module, "movement")) {
            movementMode_ = *movement;
        }
        for (const auto& property : module.properties) {
            if (property.key == "arrive" && property.values.size() >= 2 && property.values[0] == "on") {
                arriveEvery_ = std::max(1, std::stoi(property.values[1]));
            }

            if (property.key == "cadence" && !property.values.empty()) {
                if (property.values[0] == "to" && property.values.size() >= 2) {
                    cadenceTarget_ = property.values[1];
                } else {
                    cadenceMode_ = property.values[0];
                }
            }

            if (property.key == "keep" && !property.values.empty()) {
                const auto enabled = property.values.size() < 2 || property.values[1] != "off";
                if (property.values[0] == "bass") keepBass_ = enabled;
                if (property.values[0] == "top") keepTop_ = enabled;
            }

            if (property.key == "avoid" && !property.values.empty()) {
                const auto enabled = property.values.size() < 2 || property.values[1] != "off";
                if (property.values[0] == "crossing") avoidCrossing_ = enabled;
            }
        }
        if (intervals_.empty()) {
            intervals_ = { 0 };
        }
    }

    void reset(double) override
    {
        activeVoices_.clear();
        queuedNoteOffs_.clear();
        nextVoiceSerial_ = 1;
        previousVoicing_.clear();
        previousRootNote_.reset();
        phraseStep_ = 0;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) return;

        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        emitQueuedNoteOffs(blockDuration, *out->second);

        if (const auto pitchInput = inputs.find("in"); pitchInput != inputs.end()) {
            for (const auto& event : pitchInput->second->events()) {
                emitFromValue(event.valueOr(60.0), event.time, blockDuration, inputs, *out->second);
            }
        }

        if (const auto valuesInput = inputs.find("values"); valuesInput != inputs.end()) {
            for (const auto& event : valuesInput->second->events()) {
                emitFromValue(event.valueOr(60.0), event.time, blockDuration, inputs, *out->second);
            }
        }
    }

private:
    struct ActiveVoice {
        int note = 60;
        int velocity = 100;
        std::size_t serial = 0;
        double scheduledOffTime = 0.0;
    };

    struct ScheduledNoteOff {
        double timeUntilEmit = 0.0;
        Event event;
    };

    static std::vector<int> parseIntervals(const std::vector<std::string>& values)
    {
        std::vector<int> result;
        result.reserve(values.size());
        for (const auto& value : values) {
            result.push_back(std::stoi(value));
        }
        return result;
    }

    static std::vector<int> intervalsForChord(const std::string& chord)
    {
        static const std::unordered_map<std::string, std::vector<int>> chords {
            { "single", { 0 } },
            { "power", { 0, 7 } },
            { "major", { 0, 4, 7 } },
            { "minor", { 0, 3, 7 } },
            { "sus2", { 0, 2, 7 } },
            { "sus4", { 0, 5, 7 } },
            { "dim", { 0, 3, 6 } },
            { "aug", { 0, 4, 8 } },
            { "7", { 0, 4, 7, 10 } },
            { "maj7", { 0, 4, 7, 11 } },
            { "m7", { 0, 3, 7, 10 } },
            { "min7", { 0, 3, 7, 10 } },
            { "add9", { 0, 4, 7, 14 } }
        };

        if (const auto found = chords.find(chord); found != chords.end()) {
            return found->second;
        }

        return { 0 };
    }

    static int parseSpread(const std::vector<std::string>& values)
    {
        if (values.empty()) {
            return 0;
        }

        if (values.size() >= 2 && (values[1] == "octave" || values[1] == "octaves")) {
            return std::stoi(values[0]) * 12;
        }

        return std::stoi(values[0]);
    }

    void emitQueuedNoteOffs(double blockDuration, EventBuffer& output)
    {
        std::vector<ScheduledNoteOff> remaining;
        remaining.reserve(queuedNoteOffs_.size());

        for (auto& scheduled : queuedNoteOffs_) {
            scheduled.timeUntilEmit -= blockDuration;
            if (scheduled.timeUntilEmit <= 0.0) {
                scheduled.event.time = std::max(0.0, blockDuration + scheduled.timeUntilEmit);
                output.push(scheduled.event);

                removeActiveVoice(scheduled.event.noteNumber());
            } else {
                remaining.push_back(scheduled);
            }
        }

        queuedNoteOffs_ = std::move(remaining);
    }

    void emitFromValue(double rawValue,
        double time,
        double blockDuration,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        EventBuffer& output)
    {
        const auto note = clampIntoRange(quantizeToScale(static_cast<int>(std::lround(rawValue)), scale_));
        const auto gateFraction = valueAtTime(inputs, "gate", time, 0.75, 0.05, 1.0);
        const auto durationSeconds = valueAtTime(inputs, "time", time, blockDuration, 0.0, 60.0);
        const auto baseVelocity = static_cast<int>(std::lround(valueAtTime(inputs, "velocity", time, static_cast<double>(velocity_), 1.0, 127.0)));
        const auto phraseTarget = phraseTargetAtTime(inputs, time);
        const auto noteOffTime = std::max(time, time + (durationSeconds * gateFraction));
        const auto notes = projectedNotesFor(note, phraseTarget);

        if (!polyphonic_ && sameProjectedChordActive(notes)) {
            for (const auto projectedNote : notes) {
                extendQueuedNoteOff(projectedNote, noteOffTime, blockDuration);
            }
            return;
        }

        if (!polyphonic_ && !activeVoices_.empty()) {
            releaseAllVoices(output, time);
        }

        for (std::size_t index = 0; index < notes.size(); ++index) {
            const auto projectedNote = notes[index];
            const auto projectedVelocity = velocityForVoice(baseVelocity, index);

            if (isActiveNote(projectedNote)) {
                extendQueuedNoteOff(projectedNote, noteOffTime, blockDuration);
                continue;
            }

            if (polyphonic_ && activeVoices_.size() >= static_cast<std::size_t>(voiceLimit_)) {
                const auto stolenNote = chooseNoteToSteal();
                output.push(Event::makeNoteOff(stolenNote, 0, 1, time));
                cancelQueuedNoteOff(stolenNote);
                removeActiveVoice(stolenNote);
            }

            output.push(Event::makeNoteOn(projectedNote, projectedVelocity, 1, time));

            if (noteOffTime <= blockDuration) {
                output.push(Event::makeNoteOff(projectedNote, 0, 1, noteOffTime));
                continue;
            }

            activeVoices_.push_back({ projectedNote, projectedVelocity, nextVoiceSerial_++, noteOffTime });
            extendQueuedNoteOff(projectedNote, noteOffTime, blockDuration);
        }

        previousVoicing_ = notes;
        previousRootNote_ = note;
        ++phraseStep_;
    }

    [[nodiscard]] int clampIntoRange(int note) const
    {
        return std::clamp(note, lowNote_, highNote_);
    }

    void cancelQueuedNoteOff(int note)
    {
        queuedNoteOffs_.erase(std::remove_if(queuedNoteOffs_.begin(), queuedNoteOffs_.end(), [&](const ScheduledNoteOff& scheduled) {
            return scheduled.event.isNoteOff() && scheduled.event.noteNumber() == note;
        }), queuedNoteOffs_.end());
    }

    [[nodiscard]] bool isActiveNote(int note) const
    {
        return std::any_of(activeVoices_.begin(), activeVoices_.end(), [&](const ActiveVoice& voice) {
            return voice.note == note;
        });
    }

    void removeActiveVoice(int note)
    {
        activeVoices_.erase(std::remove_if(activeVoices_.begin(), activeVoices_.end(), [&](const ActiveVoice& voice) {
            return voice.note == note;
        }), activeVoices_.end());
    }

    [[nodiscard]] int chooseNoteToSteal() const
    {
        if (activeVoices_.empty()) {
            return 60;
        }

        if (stealMode_ == "quietest") {
            return std::min_element(activeVoices_.begin(), activeVoices_.end(), [](const ActiveVoice& left, const ActiveVoice& right) {
                return left.velocity < right.velocity;
            })->note;
        }

        if (stealMode_ == "shortest_remaining") {
            return std::min_element(activeVoices_.begin(), activeVoices_.end(), [](const ActiveVoice& left, const ActiveVoice& right) {
                return left.scheduledOffTime < right.scheduledOffTime;
            })->note;
        }

        if (stealMode_ == "farthest_from_center") {
            return std::max_element(activeVoices_.begin(), activeVoices_.end(), [&](const ActiveVoice& left, const ActiveVoice& right) {
                return std::abs(left.note - centerNote_) < std::abs(right.note - centerNote_);
            })->note;
        }

        if (stealMode_ == "highest") {
            return std::max_element(activeVoices_.begin(), activeVoices_.end(), [](const ActiveVoice& left, const ActiveVoice& right) {
                return left.note < right.note;
            })->note;
        }

        if (stealMode_ == "lowest") {
            return std::min_element(activeVoices_.begin(), activeVoices_.end(), [](const ActiveVoice& left, const ActiveVoice& right) {
                return left.note < right.note;
            })->note;
        }

        return std::min_element(activeVoices_.begin(), activeVoices_.end(), [](const ActiveVoice& left, const ActiveVoice& right) {
            return left.serial < right.serial;
        })->note;
    }

    void releaseAllVoices(EventBuffer& output, double time)
    {
        for (const auto& voice : activeVoices_) {
            output.push(Event::makeNoteOff(voice.note, 0, 1, time));
            cancelQueuedNoteOff(voice.note);
        }
        activeVoices_.clear();
    }

    void extendQueuedNoteOff(int note, double noteOffTime, double blockDuration)
    {
        if (noteOffTime <= blockDuration) {
            return;
        }

        for (auto& scheduled : queuedNoteOffs_) {
            if (scheduled.event.isNoteOff() && scheduled.event.noteNumber() == note) {
                if (noteOffTime > scheduled.timeUntilEmit) {
                    scheduled.timeUntilEmit = noteOffTime;
                    scheduled.event.time = noteOffTime;
                }
                updateVoiceOffTime(note, noteOffTime);
                return;
            }
        }

        queuedNoteOffs_.push_back({
            noteOffTime,
            Event::makeNoteOff(note, 0, 1, noteOffTime)
        });
        updateVoiceOffTime(note, noteOffTime);
    }

    void updateVoiceOffTime(int note, double noteOffTime)
    {
        for (auto& voice : activeVoices_) {
            if (voice.note == note) {
                voice.scheduledOffTime = std::max(voice.scheduledOffTime, noteOffTime);
                break;
            }
        }
    }

    [[nodiscard]] bool sameProjectedChordActive(const std::vector<int>& notes) const
    {
        if (activeVoices_.size() != notes.size()) {
            return false;
        }

        return std::all_of(notes.begin(), notes.end(), [&](int note) {
            return isActiveNote(note);
        });
    }

    [[nodiscard]] std::vector<int> projectedNotesFor(int rootNote, std::optional<int> phraseTarget) const
    {
        auto notes = baseProjectedNotes(rootNote);
        applyInversion(notes);
        applyDropVoicing(notes);
        applySpread(notes);
        applyMovement(notes, rootNote);
        applyConstraints(notes);
        applyCadence(notes, rootNote, phraseTarget);

        std::sort(notes.begin(), notes.end());
        notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
        for (auto& note : notes) {
            note = clampIntoRange(note);
        }
        std::sort(notes.begin(), notes.end());
        notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
        return notes;
    }

    void applyMovement(std::vector<int>& notes, int rootNote) const
    {
        if (notes.empty() || previousVoicing_.empty() || previousVoicing_.size() != notes.size()) {
            return;
        }

        if (movementMode_ == "nearest") {
            for (std::size_t index = 0; index < notes.size(); ++index) {
                notes[index] = octaveShiftNear(notes[index], previousVoicing_[index]);
            }
            return;
        }

        if (movementMode_ == "parallel") {
            const auto delta = rootNote - previousRootNote_.value_or(rootNote);
            for (std::size_t index = 0; index < notes.size(); ++index) {
                notes[index] = octaveShiftNear(notes[index], previousVoicing_[index] + delta);
            }
            return;
        }

        if (movementMode_ == "contrary") {
            const auto delta = rootNote - previousRootNote_.value_or(rootNote);
            for (std::size_t index = 0; index < notes.size(); ++index) {
                notes[index] = octaveShiftNear(notes[index], previousVoicing_[index] - delta);
            }
        }
    }

    void applyConstraints(std::vector<int>& notes) const
    {
        if (notes.empty()) {
            return;
        }

        std::sort(notes.begin(), notes.end());

        if (!previousVoicing_.empty() && previousVoicing_.size() == notes.size()) {
            if (keepBass_) {
                notes.front() = octaveShiftNear(notes.front(), previousVoicing_.front());
            }

            if (keepTop_) {
                notes.back() = octaveShiftNear(notes.back(), previousVoicing_.back());
            }
        }

        if (avoidCrossing_) {
            enforceAscendingVoices(notes);
        }
    }

    void applyCadence(std::vector<int>& notes, int rootNote, std::optional<int> phraseTarget) const
    {
        if (!isArrivalStep() || notes.empty()) {
            return;
        }

        const auto cadenceRoot = cadenceRootNote(rootNote, phraseTarget);

        if (cadenceMode_ == "none" && cadenceTarget_ != "none") {
            const auto targetVoicing = projectedCadenceTarget(cadenceRoot, notes.size());
            if (!targetVoicing.empty()) {
                notes = targetVoicing;
            }
            return;
        }

        if (cadenceMode_ == "root_position") {
            notes = projectedCadenceTarget(cadenceRoot, notes.size());
            std::sort(notes.begin(), notes.end());
            return;
        }

        if (cadenceMode_ == "close") {
            const auto anchor = octaveShiftNear(cadenceRoot, centerNote_);
            for (std::size_t index = 0; index < notes.size(); ++index) {
                notes[index] = octaveShiftNear(notes[index], anchor + static_cast<int>(index) * 3);
            }
            enforceAscendingVoices(notes);
            return;
        }

        if (cadenceMode_ == "open") {
            const auto anchor = octaveShiftNear(cadenceRoot, centerNote_);
            for (std::size_t index = 0; index < notes.size(); ++index) {
                notes[index] = octaveShiftNear(notes[index], anchor + static_cast<int>(index) * 7);
            }
            enforceAscendingVoices(notes);
        }
    }

    [[nodiscard]] int cadenceRootNote(int currentRoot, std::optional<int> phraseTarget) const
    {
        if (phraseTarget.has_value()) {
            return nearestScaleDegreeAround(centerNote_, *phraseTarget);
        }

        if (cadenceTarget_ == "tonic") {
            return nearestScaleDegreeAround(centerNote_, 0);
        }

        if (cadenceTarget_ == "dominant") {
            return nearestScaleDegreeAround(centerNote_, 4);
        }

        if (cadenceTarget_ == "subdominant") {
            return nearestScaleDegreeAround(centerNote_, 3);
        }

        return currentRoot;
    }

    [[nodiscard]] static std::optional<int> phraseTargetAtTime(const std::unordered_map<std::string, const EventBuffer*>& inputs, double eventTime)
    {
        const auto input = inputs.find("phrase");
        if (input == inputs.end() || input->second->events().empty()) {
            return std::nullopt;
        }

        double selected = input->second->events().front().valueOr(0.0);
        for (const auto& event : input->second->events()) {
            if (event.time > eventTime + 1.0e-9) {
                break;
            }
            selected = event.valueOr(selected);
        }

        return static_cast<int>(std::lround(selected));
    }

    [[nodiscard]] std::vector<int> projectedCadenceTarget(int targetRoot, std::size_t voiceCount) const
    {
        auto target = baseProjectedNotes(targetRoot);
        applyInversion(target);
        applyDropVoicing(target);
        applySpread(target);
        if (target.size() > voiceCount) {
            target.resize(voiceCount);
        }
        return target;
    }

    [[nodiscard]] int nearestScaleDegreeAround(int anchor, int degreeIndex) const
    {
        if (scale_.empty()) {
            return anchor;
        }

        const auto normalizedDegree = ((degreeIndex % static_cast<int>(scale_.size())) + static_cast<int>(scale_.size())) % static_cast<int>(scale_.size());
        int best = anchor;
        int bestDistance = std::numeric_limits<int>::max();

        for (int octave = -3; octave <= 3; ++octave) {
            const auto candidate = ((anchor / 12) + octave) * 12 + scale_[static_cast<std::size_t>(normalizedDegree)];
            const auto distance = std::abs(candidate - anchor);
            if (distance < bestDistance) {
                best = candidate;
                bestDistance = distance;
            }
        }

        return clampIntoRange(best);
    }

    [[nodiscard]] bool isArrivalStep() const
    {
        return arriveEvery_ > 0 && ((phraseStep_ + 1) % arriveEvery_ == 0);
    }

    static void enforceAscendingVoices(std::vector<int>& notes)
    {
        if (notes.size() < 2) {
            return;
        }

        std::sort(notes.begin(), notes.end());
        for (std::size_t index = 1; index < notes.size(); ++index) {
            while (notes[index] <= notes[index - 1]) {
                notes[index] += 12;
            }
        }
    }

    [[nodiscard]] static int octaveShiftNear(int note, int target)
    {
        int best = note;
        int bestDistance = std::abs(note - target);

        for (int octave = -3; octave <= 3; ++octave) {
            const auto candidate = note + (octave * 12);
            const auto distance = std::abs(candidate - target);
            if (distance < bestDistance) {
                best = candidate;
                bestDistance = distance;
            }
        }

        return best;
    }

    [[nodiscard]] std::vector<int> baseProjectedNotes(int rootNote) const
    {
        if (scaleChordSize_ > 0) {
            return buildScaleChord(rootNote, scaleChordSize_);
        }

        std::vector<int> notes;
        notes.reserve(intervals_.size());
        for (const auto interval : intervals_) {
            notes.push_back(rootNote + interval);
        }
        return notes;
    }

    [[nodiscard]] std::vector<int> buildScaleChord(int rootNote, int voiceCount) const
    {
        std::vector<int> notes;
        notes.reserve(static_cast<std::size_t>(voiceCount));
        notes.push_back(rootNote);

        auto current = rootNote;
        for (int index = 1; index < voiceCount; ++index) {
            current = advanceScaleSteps(current, 2);
            notes.push_back(current);
        }

        return notes;
    }

    [[nodiscard]] int advanceScaleSteps(int note, int steps) const
    {
        auto current = note;
        for (int index = 0; index < steps; ++index) {
            current = nextScaleToneAbove(current);
        }
        return current;
    }

    [[nodiscard]] int nextScaleToneAbove(int note) const
    {
        for (int candidate = note + 1; candidate <= highNote_ + 24; ++candidate) {
            const auto pitchClass = ((candidate % 12) + 12) % 12;
            if (std::find(scale_.begin(), scale_.end(), pitchClass) != scale_.end()) {
                return candidate;
            }
        }
        return note + 2;
    }

    void applyInversion(std::vector<int>& notes) const
    {
        if (notes.size() < 2 || inversion_ <= 0) {
            return;
        }

        const auto rotations = std::min<std::size_t>(static_cast<std::size_t>(inversion_), notes.size());
        for (std::size_t index = 0; index < rotations; ++index) {
            std::sort(notes.begin(), notes.end());
            notes.front() += 12;
        }
    }

    void applyDropVoicing(std::vector<int>& notes) const
    {
        if (notes.size() < 2 || dropVoice_ <= 0 || dropVoice_ > static_cast<int>(notes.size())) {
            return;
        }

        std::sort(notes.begin(), notes.end());
        const auto dropIndex = notes.size() - static_cast<std::size_t>(dropVoice_);
        notes[dropIndex] -= 12;
    }

    void applySpread(std::vector<int>& notes) const
    {
        if (notes.size() < 2 || spreadSemitones_ == 0) {
            return;
        }

        const auto voiceCount = notes.size() - 1;
        for (std::size_t index = 1; index < notes.size(); ++index) {
            const auto mix = static_cast<double>(index) / static_cast<double>(voiceCount);
            notes[index] += static_cast<int>(std::lround(spreadSemitones_ * mix));
        }
    }

    [[nodiscard]] int velocityForVoice(int baseVelocity, std::size_t index) const
    {
        const auto attenuation = static_cast<int>(index) * 10;
        return std::clamp(baseVelocity - attenuation, 1, 127);
    }

    [[nodiscard]] static double valueAtTime(const std::unordered_map<std::string, const EventBuffer*>& inputs,
        const std::string& portName,
        double eventTime,
        double fallback,
        double minimum,
        double maximum)
    {
        const auto input = inputs.find(portName);
        if (input == inputs.end() || input->second->events().empty()) {
            return fallback;
        }

        double selected = input->second->events().front().valueOr(fallback);
        for (const auto& event : input->second->events()) {
            if (event.time > eventTime + 1.0e-9) {
                break;
            }
            selected = event.valueOr(selected);
        }

        return std::clamp(selected, minimum, maximum);
    }

    std::vector<int> scale_;
    std::vector<int> intervals_;
    std::vector<ActiveVoice> activeVoices_;
    std::vector<ScheduledNoteOff> queuedNoteOffs_;
    bool polyphonic_ = false;
    int voiceLimit_ = 8;
    int centerNote_ = 60;
    int scaleChordSize_ = 0;
    int inversion_ = 0;
    int dropVoice_ = 0;
    int spreadSemitones_ = 0;
    std::string movementMode_ = "static";
    int arriveEvery_ = 0;
    std::string cadenceMode_ = "none";
    std::string cadenceTarget_ = "none";
    bool keepBass_ = false;
    bool keepTop_ = false;
    bool avoidCrossing_ = false;
    std::string stealMode_ = "oldest";
    std::size_t nextVoiceSerial_ = 1;
    std::size_t phraseStep_ = 0;
    std::vector<int> previousVoicing_;
    std::optional<int> previousRootNote_;
    int velocity_ = 100;
    int lowNote_ = 0;
    int highNote_ = 127;
};

class SmearNode final : public RuntimeNode {
public:
    explicit SmearNode(const Module& module)
    {
        if (const auto keep = findPropertyValue(module, "keep")) {
            keepCount_ = std::max(1, std::stoi(*keep));
        }

        weights_ = parseWeights(findPropertyValues(module, "weights"));
        if (weights_.empty()) {
            weights_ = { 0.60, 0.25, 0.15 };
        }

        if (const auto drift = findPropertyValues(module, "drift"); drift.size() >= 2 && drift[0] == "weights") {
            driftAmount_ = std::abs(std::stod(drift[1]));
        }
    }

    void reset(double) override
    {
        history_.clear();
        phase_ = 0.0;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto in = inputs.find("in");
        if (in == inputs.end()) return;

        phase_ += static_cast<double>(context.blockSize) / context.sampleRate;
        const auto driftedWeights = currentWeights();

        for (const auto& event : in->second->events()) {
            if (!event.isNoteOn() && !event.isNoteOff()) {
                if (const auto out = outputs.find("out"); out != outputs.end()) {
                    out->second->push(event);
                }
                continue;
            }

            if (event.isNoteOn()) {
                remember(event.noteNumber());
            }

            const auto smeared = clampMidiNote(static_cast<int>(std::lround(smearNote(event.noteNumber(), driftedWeights))));

            if (const auto pitch = outputs.find("pitch"); pitch != outputs.end()) {
                pitch->second->push(Event::makePitch(static_cast<double>(smeared), event.time));
            }

            if (const auto out = outputs.find("out"); out != outputs.end()) {
                auto transformed = event;
                if (transformed.ints.size() >= 2) {
                    transformed.ints[1] = smeared;
                }
                out->second->push(transformed);
            }
        }
    }

private:
    static std::vector<double> parseWeights(const std::vector<std::string>& values)
    {
        std::vector<double> result;
        result.reserve(values.size());
        for (const auto& value : values) {
            result.push_back(std::stod(value));
        }
        return result;
    }

    void remember(int note)
    {
        history_.insert(history_.begin(), note);
        if (history_.size() > static_cast<std::size_t>(keepCount_)) {
            history_.resize(static_cast<std::size_t>(keepCount_));
        }
    }

    [[nodiscard]] double smearNote(int current, const std::vector<double>& weights) const
    {
        if (history_.empty()) {
            return static_cast<double>(current);
        }

        double totalWeight = 0.0;
        double total = 0.0;

        for (std::size_t i = 0; i < history_.size() && i < weights.size(); ++i) {
            total += static_cast<double>(history_[i]) * weights[i];
            totalWeight += weights[i];
        }

        if (totalWeight <= 0.0) {
            return static_cast<double>(current);
        }

        return total / totalWeight;
    }

    [[nodiscard]] std::vector<double> currentWeights() const
    {
        if (driftAmount_ <= 0.0) {
            return weights_;
        }

        std::vector<double> drifted = weights_;
        for (std::size_t i = 0; i < drifted.size(); ++i) {
            const auto offset = std::sin(phase_ * (0.7 + (0.31 * static_cast<double>(i)))) * driftAmount_;
            drifted[i] = std::max(0.01, drifted[i] + offset);
        }
        return drifted;
    }

    int keepCount_ = 3;
    std::vector<double> weights_;
    std::vector<int> history_;
    double driftAmount_ = 0.0;
    double phase_ = 0.0;
};

class CutupNode final : public RuntimeNode {
public:
    explicit CutupNode(const Module& module)
    {
        if (const auto capture = findPropertyValues(module, "capture"); !capture.empty()) {
            captureSeconds_ = parseCaptureTime(capture);
        }

        if (const auto slice = findPropertyValue(module, "slice")) {
            sliceSize_ = std::max(1, std::stoi(*slice));
        }

        if (const auto keep = findPropertyValues(module, "keep"); keep.size() >= 2 && keep[0] == "continuity") {
            continuity_ = std::clamp(std::stod(keep[1]), 0.0, 1.0);
        }

        if (const auto favor = findPropertyValue(module, "favor")) {
            favorHarmonic_ = (*favor == "harmonic");
        }

        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }
    }

    void reset(double) override
    {
        absoluteTime_ = 0.0;
        captureStart_.reset();
        activeNotes_.clear();
        spans_.clear();
        fragments_.clear();
        queued_.clear();
        state_ = seed_;
        lastFragmentIndex_.reset();
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        const auto blockStart = absoluteTime_;
        const auto blockEnd = blockStart + blockDuration;

        captureInput(inputs, blockStart);
        finalizeOpenSpans(blockEnd);
        rebuildFragmentsIfNeeded();

        const auto trigger = inputs.find("trigger");
        const auto out = outputs.find("out");
        if (trigger != inputs.end() && out != outputs.end() && !fragments_.empty()) {
            for (const auto& event : trigger->second->events()) {
                scheduleFragment(event.time);
            }
        }

        emitQueued(blockDuration, outputs);
        absoluteTime_ = blockEnd;
    }

private:
    struct NoteSpan {
        int note = 60;
        int velocity = 100;
        int channel = 1;
        double start = 0.0;
        double duration = 0.1;
    };

    struct ActiveNote {
        int velocity = 100;
        int channel = 1;
        double start = 0.0;
    };

    struct Fragment {
        std::vector<NoteSpan> spans;
        double averagePitch = 60.0;
    };

    struct ScheduledEvent {
        double timeUntilEmit = 0.0;
        Event event;
    };

    static double parseCaptureTime(const std::vector<std::string>& values)
    {
        if (values.empty()) return parseTimeToken("1/16", 120.0);
        if (values.size() == 1) return parseTimeToken(values[0], 120.0);
        if (values.size() == 2) return parseTimeToken(values[0], 120.0);
        return parseTimeToken(values[0], 120.0);
    }

    void captureInput(const std::unordered_map<std::string, const EventBuffer*>& inputs, double blockStart)
    {
        const auto input = inputs.find("in");
        if (input == inputs.end()) return;

        if (!captureStart_.has_value() && !input->second->events().empty()) {
            captureStart_ = blockStart;
        }

        for (const auto& event : input->second->events()) {
            if (!captureStart_.has_value()) break;

            const auto absoluteEventTime = blockStart + event.time;
            if ((absoluteEventTime - *captureStart_) > captureSeconds_) {
                continue;
            }

            if (event.isNoteOn()) {
                activeNotes_[event.noteNumber()] = ActiveNote {
                    event.velocityValue(),
                    event.channel(),
                    absoluteEventTime - *captureStart_
                };
            } else if (event.isNoteOff()) {
                const auto it = activeNotes_.find(event.noteNumber());
                if (it == activeNotes_.end()) {
                    continue;
                }

                spans_.push_back({
                    event.noteNumber(),
                    it->second.velocity,
                    it->second.channel,
                    it->second.start,
                    std::max(0.01, absoluteEventTime - *captureStart_ - it->second.start)
                });
                activeNotes_.erase(it);
                fragmentsDirty_ = true;
            }
        }
    }

    void finalizeOpenSpans(double blockEnd)
    {
        if (!captureStart_.has_value()) return;
        if ((blockEnd - *captureStart_) < captureSeconds_) return;

        for (const auto& [note, active] : activeNotes_) {
            spans_.push_back({
                note,
                active.velocity,
                active.channel,
                active.start,
                std::max(0.05, captureSeconds_ - active.start)
            });
        }

        activeNotes_.clear();
        fragmentsDirty_ = true;
    }

    void rebuildFragmentsIfNeeded()
    {
        if (!fragmentsDirty_) return;

        std::sort(spans_.begin(), spans_.end(), [](const NoteSpan& left, const NoteSpan& right) {
            return left.start < right.start;
        });

        fragments_.clear();
        for (std::size_t index = 0; index < spans_.size(); index += static_cast<std::size_t>(sliceSize_)) {
            Fragment fragment;
            const auto end = std::min(spans_.size(), index + static_cast<std::size_t>(sliceSize_));
            fragment.spans.insert(fragment.spans.end(), spans_.begin() + static_cast<long>(index), spans_.begin() + static_cast<long>(end));
            double totalPitch = 0.0;
            for (const auto& span : fragment.spans) {
                totalPitch += static_cast<double>(span.note);
            }
            fragment.averagePitch = fragment.spans.empty() ? 60.0 : (totalPitch / static_cast<double>(fragment.spans.size()));
            fragments_.push_back(std::move(fragment));
        }

        fragmentsDirty_ = false;
    }

    void scheduleFragment(double triggerTime)
    {
        const auto index = chooseFragmentIndex();
        if (index >= fragments_.size()) return;

        const auto& fragment = fragments_[index];
        for (const auto& span : fragment.spans) {
            queued_.push_back({
                triggerTime + span.start,
                Event::makeNoteOn(span.note, span.velocity, span.channel, triggerTime + span.start)
            });
            queued_.push_back({
                triggerTime + span.start + span.duration,
                Event::makeNoteOff(span.note, 0, span.channel, triggerTime + span.start + span.duration)
            });
        }

        lastFragmentIndex_ = index;
    }

    [[nodiscard]] std::size_t chooseFragmentIndex()
    {
        if (fragments_.empty()) return 0;
        if (!lastFragmentIndex_.has_value()) {
            return static_cast<std::size_t>(nextBits() % static_cast<std::uint32_t>(fragments_.size()));
        }

        if (nextUnit() < continuity_) {
            const auto current = static_cast<int>(*lastFragmentIndex_);
            const auto minIndex = std::max(0, current - 1);
            const auto maxIndex = std::min(static_cast<int>(fragments_.size()) - 1, current + 1);
            return static_cast<std::size_t>(minIndex + static_cast<int>(nextBits() % static_cast<std::uint32_t>((maxIndex - minIndex) + 1)));
        }

        if (favorHarmonic_) {
            const auto referencePitch = fragments_[*lastFragmentIndex_].averagePitch;
            std::size_t bestIndex = 0;
            double bestScore = std::numeric_limits<double>::max();
            for (std::size_t i = 0; i < fragments_.size(); ++i) {
                if (i == *lastFragmentIndex_) continue;
                const auto score = std::abs(fragments_[i].averagePitch - referencePitch) + (nextUnit() * 0.1);
                if (score < bestScore) {
                    bestScore = score;
                    bestIndex = i;
                }
            }
            return bestIndex;
        }

        return static_cast<std::size_t>(nextBits() % static_cast<std::uint32_t>(fragments_.size()));
    }

    void emitQueued(double blockDuration, std::unordered_map<std::string, EventBuffer*>& outputs)
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) return;

        std::vector<ScheduledEvent> remaining;
        remaining.reserve(queued_.size());
        for (auto& scheduled : queued_) {
            scheduled.timeUntilEmit -= blockDuration;
            if (scheduled.timeUntilEmit <= 0.0) {
                scheduled.event.time = std::max(0.0, blockDuration + scheduled.timeUntilEmit);
                out->second->push(scheduled.event);
            } else {
                remaining.push_back(scheduled);
            }
        }
        queued_ = std::move(remaining);
    }

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double nextUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    double captureSeconds_ = 0.25;
    int sliceSize_ = 2;
    double continuity_ = 0.35;
    bool favorHarmonic_ = false;
    std::uint32_t seed_ = 0xabcdef12u;
    std::uint32_t state_ = seed_;
    double absoluteTime_ = 0.0;
    bool fragmentsDirty_ = false;
    std::optional<double> captureStart_;
    std::unordered_map<int, ActiveNote> activeNotes_;
    std::vector<NoteSpan> spans_;
    std::vector<Fragment> fragments_;
    std::vector<ScheduledEvent> queued_;
    std::optional<std::size_t> lastFragmentIndex_;
};

class SlicerNode final : public RuntimeNode {
public:
    explicit SlicerNode(const Module& module)
    {
        if (const auto capture = findPropertyValues(module, "capture"); !capture.empty()) {
            captureSeconds_ = parseCaptureTime(capture);
        }
        if (const auto slices = findPropertyValue(module, "slices")) {
            sliceCount_ = std::max(1, std::stoi(*slices));
        }
        if (const auto order = findPropertyValue(module, "order")) {
            randomOrder_ = (*order == "random");
        }
        for (const auto& property : module.properties) {
            if (property.key == "drift" && property.values.size() >= 2) {
                if (property.values[0] == "start") {
                    startDriftSeconds_ = std::max(0.0, parseTimeToken(property.values[1], 120.0));
                } else if (property.values[0] == "size") {
                    sizeDriftSeconds_ = std::max(0.0, parseTimeToken(property.values[1], 120.0));
                }
            }
        }
        if (const auto reverse = findPropertyValue(module, "reverse")) {
            reverseChance_ = parseChance(*reverse);
        }
        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }
    }

    void reset(double) override
    {
        absoluteTime_ = 0.0;
        captureStart_.reset();
        activeNotes_.clear();
        spans_.clear();
        queued_.clear();
        state_ = seed_;
        nextSliceIndex_ = 0;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        const auto blockStart = absoluteTime_;
        const auto blockEnd = blockStart + blockDuration;

        captureInput(inputs, blockStart);
        finalizeOpenSpans(blockEnd);

        if (const auto trigger = inputs.find("trigger"); trigger != inputs.end() && !spans_.empty()) {
            for (const auto& event : trigger->second->events()) {
                const auto sliceIndex = chooseSliceIndex();
                scheduleSlice(sliceIndex, event.time, outputs);
            }
        }

        emitQueued(blockDuration, outputs);
        absoluteTime_ = blockEnd;
    }

private:
    struct NoteSpan {
        int note = 60;
        int velocity = 100;
        int channel = 1;
        double start = 0.0;
        double duration = 0.1;
    };

    struct ActiveNote {
        int velocity = 100;
        int channel = 1;
        double start = 0.0;
    };

    struct ScheduledEvent {
        double timeUntilEmit = 0.0;
        Event event;
    };

    static double parseCaptureTime(const std::vector<std::string>& values)
    {
        if (values.empty()) return parseTimeToken("1/8", 120.0);
        return parseTimeToken(values.front(), 120.0);
    }

    static double parseChance(const std::string& text)
    {
        auto value = text;
        if (!value.empty() && value.back() == '%') {
            value.pop_back();
            return std::clamp(std::stod(value) / 100.0, 0.0, 1.0);
        }
        if (value == "on") return 1.0;
        if (value == "off") return 0.0;
        return std::clamp(std::stod(value), 0.0, 1.0);
    }

    void captureInput(const std::unordered_map<std::string, const EventBuffer*>& inputs, double blockStart)
    {
        const auto input = inputs.find("in");
        if (input == inputs.end()) return;

        if (!captureStart_.has_value() && !input->second->events().empty()) {
            captureStart_ = blockStart;
        }

        for (const auto& event : input->second->events()) {
            if (!captureStart_.has_value()) break;
            const auto absoluteEventTime = blockStart + event.time;
            if ((absoluteEventTime - *captureStart_) > captureSeconds_) {
                continue;
            }

            if (event.isNoteOn()) {
                activeNotes_[event.noteNumber()] = ActiveNote { event.velocityValue(), event.channel(), absoluteEventTime - *captureStart_ };
            } else if (event.isNoteOff()) {
                const auto it = activeNotes_.find(event.noteNumber());
                if (it == activeNotes_.end()) continue;
                spans_.push_back({
                    event.noteNumber(),
                    it->second.velocity,
                    it->second.channel,
                    it->second.start,
                    std::max(0.01, absoluteEventTime - *captureStart_ - it->second.start)
                });
                activeNotes_.erase(it);
            }
        }
    }

    void finalizeOpenSpans(double blockEnd)
    {
        if (!captureStart_.has_value()) return;
        if ((blockEnd - *captureStart_) < captureSeconds_) return;
        for (const auto& [note, active] : activeNotes_) {
            spans_.push_back({
                note,
                active.velocity,
                active.channel,
                active.start,
                std::max(0.05, captureSeconds_ - active.start)
            });
        }
        activeNotes_.clear();
    }

    [[nodiscard]] std::size_t chooseSliceIndex()
    {
        if (sliceCount_ <= 1) return 0;
        if (randomOrder_) {
            return static_cast<std::size_t>(nextBits() % static_cast<std::uint32_t>(sliceCount_));
        }
        const auto value = nextSliceIndex_;
        nextSliceIndex_ = (nextSliceIndex_ + 1) % static_cast<std::size_t>(sliceCount_);
        return value;
    }

    void scheduleSlice(std::size_t sliceIndex, double triggerTime, std::unordered_map<std::string, EventBuffer*>& outputs)
    {
        const auto baseSize = captureSeconds_ / static_cast<double>(std::max(1, sliceCount_));
        const auto startJitter = ((nextUnit() * 2.0) - 1.0) * startDriftSeconds_;
        const auto sizeJitter = ((nextUnit() * 2.0) - 1.0) * sizeDriftSeconds_;
        const auto sliceStart = std::clamp((static_cast<double>(sliceIndex) * baseSize) + startJitter, 0.0, std::max(0.0, captureSeconds_ - 0.01));
        const auto sliceSize = std::max(0.01, std::min(captureSeconds_ - sliceStart, baseSize + sizeJitter));
        const auto reversed = nextUnit() < reverseChance_;

        if (const auto index = outputs.find("index"); index != outputs.end()) {
            index->second->push(Event::makeValue(static_cast<double>(sliceIndex), triggerTime));
        }

        for (const auto& span : spans_) {
            const auto spanStart = span.start;
            const auto spanEnd = span.start + span.duration;
            const auto overlapStart = std::max(spanStart, sliceStart);
            const auto overlapEnd = std::min(spanEnd, sliceStart + sliceSize);
            if (overlapEnd <= overlapStart) {
                continue;
            }

            const auto localStart = overlapStart - sliceStart;
            const auto localDuration = overlapEnd - overlapStart;
            const auto emitStart = reversed ? std::max(0.0, sliceSize - (localStart + localDuration)) : localStart;

            queued_.push_back({ triggerTime + emitStart, Event::makeNoteOn(span.note, span.velocity, span.channel, triggerTime + emitStart) });
            queued_.push_back({ triggerTime + emitStart + localDuration, Event::makeNoteOff(span.note, 0, span.channel, triggerTime + emitStart + localDuration) });
        }
    }

    void emitQueued(double blockDuration, std::unordered_map<std::string, EventBuffer*>& outputs)
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) return;

        std::vector<ScheduledEvent> remaining;
        remaining.reserve(queued_.size());
        for (auto& scheduled : queued_) {
            scheduled.timeUntilEmit -= blockDuration;
            if (scheduled.timeUntilEmit <= 0.0) {
                scheduled.event.time = std::max(0.0, blockDuration + scheduled.timeUntilEmit);
                out->second->push(scheduled.event);
            } else {
                remaining.push_back(scheduled);
            }
        }
        queued_ = std::move(remaining);
    }

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double nextUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    double captureSeconds_ = 0.5;
    int sliceCount_ = 8;
    bool randomOrder_ = false;
    double startDriftSeconds_ = 0.0;
    double sizeDriftSeconds_ = 0.0;
    double reverseChance_ = 0.0;
    std::uint32_t seed_ = 0x9a17b1c3u;
    std::uint32_t state_ = seed_;
    double absoluteTime_ = 0.0;
    std::optional<double> captureStart_;
    std::unordered_map<int, ActiveNote> activeNotes_;
    std::vector<NoteSpan> spans_;
    std::vector<ScheduledEvent> queued_;
    std::size_t nextSliceIndex_ = 0;
};

class PoolNode final : public RuntimeNode {
public:
    explicit PoolNode(const Module& module)
    {
        if (const auto capture = findPropertyValues(module, "capture"); !capture.empty()) {
            captureSeconds_ = parseTimeToken(capture.front(), 120.0);
        }
        if (const auto slices = findPropertyValue(module, "slices")) {
            sliceCount_ = std::max(1, std::stoi(*slices));
        }
        if (const auto steps = findPropertyValues(module, "steps"); !steps.empty()) {
            for (const auto& value : steps) {
                stepSequence_.push_back(std::max(0, std::stoi(value)));
            }
        }
        for (const auto& property : module.properties) {
            if (property.key == "drift" && property.values.size() >= 2) {
                if (property.values[0] == "start") {
                    startDriftSeconds_ = std::max(0.0, parseTimeToken(property.values[1], 120.0));
                } else if (property.values[0] == "size") {
                    sizeDriftSeconds_ = std::max(0.0, parseTimeToken(property.values[1], 120.0));
                }
            }
        }
        if (const auto reverse = findPropertyValue(module, "reverse")) {
            reverse_ = (*reverse == "on");
        }
        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }
    }

    void reset(double) override
    {
        absoluteTime_ = 0.0;
        captureStart_.reset();
        activeNotes_.clear();
        spans_.clear();
        queued_.clear();
        state_ = seed_;
        stepIndex_ = 0;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        const auto blockStart = absoluteTime_;
        const auto blockEnd = blockStart + blockDuration;

        captureInput(inputs, blockStart);
        finalizeOpenSpans(blockEnd);

        if (const auto trigger = inputs.find("trigger"); trigger != inputs.end() && !spans_.empty()) {
            for (const auto& event : trigger->second->events()) {
                const auto sliceIndex = chooseStepSlice();
                scheduleSlice(sliceIndex, event.time, outputs);
            }
        }

        emitQueued(blockDuration, outputs);
        absoluteTime_ = blockEnd;
    }

private:
    struct NoteSpan {
        int note = 60;
        int velocity = 100;
        int channel = 1;
        double start = 0.0;
        double duration = 0.1;
    };

    struct ActiveNote {
        int velocity = 100;
        int channel = 1;
        double start = 0.0;
    };

    struct ScheduledEvent {
        double timeUntilEmit = 0.0;
        Event event;
    };

    void captureInput(const std::unordered_map<std::string, const EventBuffer*>& inputs, double blockStart)
    {
        const auto input = inputs.find("in");
        if (input == inputs.end()) return;

        if (!captureStart_.has_value() && !input->second->events().empty()) {
            captureStart_ = blockStart;
        }

        for (const auto& event : input->second->events()) {
            if (!captureStart_.has_value()) break;
            const auto absoluteEventTime = blockStart + event.time;
            if ((absoluteEventTime - *captureStart_) > captureSeconds_) {
                continue;
            }

            if (event.isNoteOn()) {
                activeNotes_[event.noteNumber()] = ActiveNote { event.velocityValue(), event.channel(), absoluteEventTime - *captureStart_ };
            } else if (event.isNoteOff()) {
                const auto it = activeNotes_.find(event.noteNumber());
                if (it == activeNotes_.end()) continue;
                spans_.push_back({
                    event.noteNumber(),
                    it->second.velocity,
                    it->second.channel,
                    it->second.start,
                    std::max(0.01, absoluteEventTime - *captureStart_ - it->second.start)
                });
                activeNotes_.erase(it);
            }
        }
    }

    void finalizeOpenSpans(double blockEnd)
    {
        if (!captureStart_.has_value()) return;
        if ((blockEnd - *captureStart_) < captureSeconds_) return;
        for (const auto& [note, active] : activeNotes_) {
            spans_.push_back({
                note,
                active.velocity,
                active.channel,
                active.start,
                std::max(0.05, captureSeconds_ - active.start)
            });
        }
        activeNotes_.clear();
    }

    [[nodiscard]] std::size_t chooseStepSlice()
    {
        if (stepSequence_.empty()) {
            return static_cast<std::size_t>(nextBits() % static_cast<std::uint32_t>(std::max(1, sliceCount_)));
        }
        const auto value = static_cast<std::size_t>(stepSequence_[stepIndex_ % stepSequence_.size()] % std::max(1, sliceCount_));
        ++stepIndex_;
        return value;
    }

    void scheduleSlice(std::size_t sliceIndex, double triggerTime, std::unordered_map<std::string, EventBuffer*>& outputs)
    {
        const auto baseSize = captureSeconds_ / static_cast<double>(std::max(1, sliceCount_));
        const auto startJitter = ((nextUnit() * 2.0) - 1.0) * startDriftSeconds_;
        const auto sizeJitter = ((nextUnit() * 2.0) - 1.0) * sizeDriftSeconds_;
        const auto sliceStart = std::clamp((static_cast<double>(sliceIndex) * baseSize) + startJitter, 0.0, std::max(0.0, captureSeconds_ - 0.01));
        const auto sliceSize = std::max(0.01, std::min(captureSeconds_ - sliceStart, baseSize + sizeJitter));

        if (const auto index = outputs.find("index"); index != outputs.end()) {
            index->second->push(Event::makeValue(static_cast<double>(sliceIndex), triggerTime));
        }

        for (const auto& span : spans_) {
            const auto spanStart = span.start;
            const auto spanEnd = span.start + span.duration;
            const auto overlapStart = std::max(spanStart, sliceStart);
            const auto overlapEnd = std::min(spanEnd, sliceStart + sliceSize);
            if (overlapEnd <= overlapStart) {
                continue;
            }

            const auto localStart = overlapStart - sliceStart;
            const auto localDuration = overlapEnd - overlapStart;
            const auto emitStart = reverse_ ? std::max(0.0, sliceSize - (localStart + localDuration)) : localStart;

            queued_.push_back({ triggerTime + emitStart, Event::makeNoteOn(span.note, span.velocity, span.channel, triggerTime + emitStart) });
            queued_.push_back({ triggerTime + emitStart + localDuration, Event::makeNoteOff(span.note, 0, span.channel, triggerTime + emitStart + localDuration) });
        }
    }

    void emitQueued(double blockDuration, std::unordered_map<std::string, EventBuffer*>& outputs)
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) return;

        std::vector<ScheduledEvent> remaining;
        remaining.reserve(queued_.size());
        for (auto& scheduled : queued_) {
            scheduled.timeUntilEmit -= blockDuration;
            if (scheduled.timeUntilEmit <= 0.0) {
                scheduled.event.time = std::max(0.0, blockDuration + scheduled.timeUntilEmit);
                out->second->push(scheduled.event);
            } else {
                remaining.push_back(scheduled);
            }
        }
        queued_ = std::move(remaining);
    }

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double nextUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    double captureSeconds_ = 0.5;
    int sliceCount_ = 8;
    std::vector<int> stepSequence_;
    double startDriftSeconds_ = 0.0;
    double sizeDriftSeconds_ = 0.0;
    bool reverse_ = false;
    std::uint32_t seed_ = 0x13f0c5ab;
    std::uint32_t state_ = seed_;
    double absoluteTime_ = 0.0;
    std::optional<double> captureStart_;
    std::unordered_map<int, ActiveNote> activeNotes_;
    std::vector<NoteSpan> spans_;
    std::vector<ScheduledEvent> queued_;
    std::size_t stepIndex_ = 0;
};

class GrooveNode final : public RuntimeNode {
public:
    explicit GrooveNode(const Module& module)
    {
        offsetPattern_ = parseOffsets(findPropertyValues(module, "offsets"));
        if (offsetPattern_.empty()) {
            offsetPattern_ = { -0.004, 0.002, -0.001, 0.003 };
        }
        if (const auto chance = findPropertyValue(module, "chance")) {
            chance_ = parseChance(*chance);
        }
        if (const auto seed = findPropertyValue(module, "seed")) {
            seed_ = static_cast<std::uint32_t>(std::stoul(*seed));
        }
    }

    void reset(double) override
    {
        queued_.clear();
        stepIndex_ = 0;
        state_ = seed_;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto input = inputs.find("in");
        const auto out = outputs.find("out");
        if (input == inputs.end() || out == outputs.end()) return;

        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        for (const auto& event : input->second->events()) {
            auto offset = offsetPattern_[stepIndex_ % offsetPattern_.size()];
            if (nextUnit() > chance_) {
                offset = 0.0;
            }
            ++stepIndex_;

            auto shifted = event;
            shifted.time = std::max(0.0, event.time + offset);
            queued_.push_back({ shifted.time, shifted });
        }

        emitQueued(blockDuration, *out->second);
    }

private:
    struct QueuedEvent {
        double timeUntilEmit = 0.0;
        Event event;
    };

    static std::vector<double> parseOffsets(const std::vector<std::string>& values)
    {
        std::vector<double> result;
        result.reserve(values.size());
        for (const auto& value : values) {
            result.push_back(parseTimeToken(value, 120.0));
        }
        return result;
    }

    static double parseChance(std::string value)
    {
        if (!value.empty() && value.back() == '%') {
            value.pop_back();
            return std::clamp(std::stod(value) / 100.0, 0.0, 1.0);
        }
        return std::clamp(std::stod(value), 0.0, 1.0);
    }

    void emitQueued(double blockDuration, EventBuffer& output)
    {
        std::vector<QueuedEvent> remaining;
        remaining.reserve(queued_.size());
        for (auto& scheduled : queued_) {
            scheduled.timeUntilEmit -= blockDuration;
            if (scheduled.timeUntilEmit <= 0.0) {
                scheduled.event.time = std::max(0.0, blockDuration + scheduled.timeUntilEmit);
                output.push(scheduled.event);
            } else {
                remaining.push_back(scheduled);
            }
        }
        queued_ = std::move(remaining);
    }

    [[nodiscard]] std::uint32_t nextBits()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    [[nodiscard]] double nextUnit()
    {
        return static_cast<double>(nextBits() & 0x00ffffffu) / static_cast<double>(0x01000000u);
    }

    std::vector<double> offsetPattern_;
    double chance_ = 1.0;
    std::vector<QueuedEvent> queued_;
    std::size_t stepIndex_ = 0;
    std::uint32_t seed_ = 0x4f1a2b3cu;
    std::uint32_t state_ = seed_;
};

class RetrigNode final : public RuntimeNode {
public:
    explicit RetrigNode(const Module& module)
    {
        if (const auto count = findPropertyValue(module, "count")) {
            count_ = std::max(1, std::stoi(*count));
        }
        if (const auto spacing = findPropertyValues(module, "spacing"); spacing.size() == 3 && spacing[1] == "->") {
            startSpacingSeconds_ = parseTimeToken(spacing[0], 120.0);
            endSpacingSeconds_ = parseTimeToken(spacing[2], 120.0);
        } else if (spacing.size() == 1) {
            startSpacingSeconds_ = endSpacingSeconds_ = parseTimeToken(spacing[0], 120.0);
        }
        if (const auto velocity = findPropertyValues(module, "velocity"); velocity.size() == 3 && velocity[1] == "->") {
            startVelocity_ = std::clamp(std::stoi(velocity[0]), 1, 127);
            endVelocity_ = std::clamp(std::stoi(velocity[2]), 1, 127);
        }
        if (const auto shape = findPropertyValue(module, "shape")) {
            shape_ = *shape;
        }
    }

    void reset(double) override
    {
        queued_.clear();
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto input = inputs.find("in");
        const auto out = outputs.find("out");
        if (input == inputs.end() || out == outputs.end()) return;

        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        for (const auto& event : input->second->events()) {
            if (event.isNoteOn()) {
                scheduleRetrig(event);
            } else if (!event.isNoteOff()) {
                queued_.push_back({ event.time, event });
            }
        }

        emitQueued(blockDuration, *out->second);
    }

private:
    struct ScheduledEvent {
        double timeUntilEmit = 0.0;
        Event event;
    };

    [[nodiscard]] double shapeMix(double t) const
    {
        t = clamp01(t);
        if (shape_ == "accelerate") return t * t;
        if (shape_ == "decelerate") return std::sqrt(t);
        return t;
    }

    void scheduleRetrig(const Event& trigger)
    {
        double accumulated = trigger.time;
        for (int i = 0; i < count_; ++i) {
            const auto mix = shapeMix((count_ <= 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(count_ - 1));
            const auto spacing = startSpacingSeconds_ + ((endSpacingSeconds_ - startSpacingSeconds_) * mix);
            const auto velocity = static_cast<int>(std::lround(startVelocity_ + ((endVelocity_ - startVelocity_) * mix)));
            const auto noteLength = std::max(0.008, std::min(0.12, std::max(spacing * 0.7, 0.012)));

            queued_.push_back({ accumulated, Event::makeNoteOn(trigger.noteNumber(), velocity, trigger.channel(), accumulated) });
            queued_.push_back({ accumulated + noteLength, Event::makeNoteOff(trigger.noteNumber(), 0, trigger.channel(), accumulated + noteLength) });
            accumulated += spacing;
        }
    }

    void emitQueued(double blockDuration, EventBuffer& output)
    {
        std::vector<ScheduledEvent> remaining;
        remaining.reserve(queued_.size());
        for (auto& scheduled : queued_) {
            scheduled.timeUntilEmit -= blockDuration;
            if (scheduled.timeUntilEmit <= 0.0) {
                scheduled.event.time = std::max(0.0, blockDuration + scheduled.timeUntilEmit);
                output.push(scheduled.event);
            } else {
                remaining.push_back(scheduled);
            }
        }
        queued_ = std::move(remaining);
    }

    int count_ = 3;
    double startSpacingSeconds_ = 0.022;
    double endSpacingSeconds_ = 0.008;
    int startVelocity_ = 100;
    int endVelocity_ = 48;
    std::string shape_ = "accelerate";
    std::vector<ScheduledEvent> queued_;
};

class LengthNode final : public RuntimeNode {
public:
    explicit LengthNode(const Module& module)
    {
        if (const auto multiply = findPropertyValue(module, "multiply")) {
            multiply_ = std::clamp(std::stod(*multiply), 0.01, 8.0);
        }
        if (const auto quantize = findPropertyValue(module, "quantize")) {
            quantizeSeconds_ = std::max(0.0, parseTimeToken(*quantize, 120.0));
        }
        if (const auto clamp = findPropertyValues(module, "clamp"); !clamp.empty()) {
            if (clamp.size() == 1) {
                if (const auto dots = clamp[0].find(".."); dots != std::string::npos) {
                    minSeconds_ = std::max(0.001, parseTimeToken(clamp[0].substr(0, dots), 120.0));
                    maxSeconds_ = std::max(minSeconds_, parseTimeToken(clamp[0].substr(dots + 2), 120.0));
                }
            } else if (clamp.size() >= 2) {
                minSeconds_ = std::max(0.001, parseTimeToken(clamp[0], 120.0));
                maxSeconds_ = std::max(minSeconds_, parseTimeToken(clamp[1], 120.0));
            }
        }
    }

    void reset(double) override
    {
        absoluteTime_ = 0.0;
        active_.clear();
        queued_.clear();
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto input = inputs.find("in");
        const auto out = outputs.find("out");
        if (input == inputs.end() || out == outputs.end()) return;

        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        const auto blockStart = absoluteTime_;

        for (const auto& event : input->second->events()) {
            const auto absoluteEventTime = blockStart + event.time;
            if (event.isNoteOn()) {
                active_[keyFor(event.noteNumber(), event.channel())] = { absoluteEventTime, event.channel() };
                out->second->push(event);
            } else if (event.isNoteOff()) {
                const auto key = keyFor(event.noteNumber(), event.channel());
                const auto found = active_.find(key);
                if (found == active_.end()) {
                    out->second->push(event);
                    continue;
                }

                auto duration = std::max(0.005, absoluteEventTime - found->second.startTime);
                duration *= multiply_;
                if (quantizeSeconds_ > 0.0) {
                    duration = std::max(quantizeSeconds_, std::round(duration / quantizeSeconds_) * quantizeSeconds_);
                }
                duration = std::clamp(duration, minSeconds_, maxSeconds_);
                const auto targetTime = found->second.startTime + duration;
                queued_.push_back({ std::max(0.0, targetTime - blockStart), Event::makeNoteOff(event.noteNumber(), 0, event.channel(), std::max(0.0, targetTime - blockStart)) });
                active_.erase(found);
            } else {
                out->second->push(event);
            }
        }

        emitQueued(blockDuration, *out->second);
        absoluteTime_ = blockStart + blockDuration;
    }

private:
    struct ActiveNote {
        double startTime = 0.0;
        int channel = 1;
    };

    struct ScheduledEvent {
        double timeUntilEmit = 0.0;
        Event event;
    };

    static int keyFor(int note, int channel)
    {
        return (channel * 256) + note;
    }

    void emitQueued(double blockDuration, EventBuffer& output)
    {
        std::vector<ScheduledEvent> remaining;
        remaining.reserve(queued_.size());
        for (auto& scheduled : queued_) {
            scheduled.timeUntilEmit -= blockDuration;
            if (scheduled.timeUntilEmit <= 0.0) {
                scheduled.event.time = std::max(0.0, blockDuration + scheduled.timeUntilEmit);
                output.push(scheduled.event);
            } else {
                remaining.push_back(scheduled);
            }
        }
        queued_ = std::move(remaining);
    }

    double multiply_ = 0.5;
    double quantizeSeconds_ = 0.0;
    double minSeconds_ = 0.01;
    double maxSeconds_ = 0.12;
    double absoluteTime_ = 0.0;
    std::unordered_map<int, ActiveNote> active_;
    std::vector<ScheduledEvent> queued_;
};

class SplitNode final : public RuntimeNode {
public:
    explicit SplitNode(const Module& module)
    {
        for (const auto& property : module.properties) {
            if (property.values.empty()) continue;

            if (property.key == "low" && property.values.size() >= 2 && property.values[0] == "below") {
                lowUpperBound_ = parseNoteName(property.values[1]);
            }

            if (property.key == "mid" && !property.values.empty()) {
                const auto dots = property.values[0].find("..");
                if (dots != std::string::npos) {
                    midLowBound_ = parseNoteName(property.values[0].substr(0, dots));
                    midHighBound_ = parseNoteName(property.values[0].substr(dots + 2));
                }
            }

            if (property.key == "high" && property.values.size() >= 2 && property.values[0] == "above") {
                highLowerBound_ = parseNoteName(property.values[1]);
            }
        }
    }

    void reset(double) override {}

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto input = inputs.find("in");
        if (input == inputs.end()) return;

        for (const auto& event : input->second->events()) {
            if (!event.isNoteOn() && !event.isNoteOff()) {
                for (const auto* port : { "low", "mid", "high" }) {
                    if (const auto out = outputs.find(port); out != outputs.end()) {
                        out->second->push(event);
                    }
                }
                continue;
            }

            const auto note = event.noteNumber();
            const auto* portName = classify(note);
            if (const auto out = outputs.find(portName); out != outputs.end()) {
                out->second->push(event);
            }
        }
    }

private:
    [[nodiscard]] const char* classify(int note) const
    {
        if (note < lowUpperBound_) return "low";
        if (note >= midLowBound_ && note <= midHighBound_) return "mid";
        if (note > highLowerBound_) return "high";
        if (note < midLowBound_) return "low";
        return "high";
    }

    int lowUpperBound_ = parseNoteName("C3");
    int midLowBound_ = parseNoteName("C3");
    int midHighBound_ = parseNoteName("B4");
    int highLowerBound_ = parseNoteName("B4");
};

class DelayNode final : public RuntimeNode {
public:
    explicit DelayNode(const Module& module)
    {
        if (const auto time = findPropertyValue(module, "time")) {
            delaySeconds_ = parseTimeToken(*time, 120.0);
        }
    }

    void reset(double) override
    {
        queued_.clear();
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) return;

        const auto input = inputs.find("in");
        if (input != inputs.end()) {
            for (const auto& event : input->second->events()) {
                queued_.push_back({ delaySeconds_ + event.time, event });
            }
        }

        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        std::vector<QueuedEvent> remaining;
        remaining.reserve(queued_.size());

        for (auto& queued : queued_) {
            queued.timeUntilEmit -= blockDuration;
            if (queued.timeUntilEmit <= 0.0) {
                queued.event.time = std::max(0.0, blockDuration + queued.timeUntilEmit);
                out->second->push(queued.event);
            } else {
                remaining.push_back(queued);
            }
        }

        queued_ = std::move(remaining);
    }

private:
    struct QueuedEvent {
        double timeUntilEmit = 0.0;
        Event event;
    };

    double delaySeconds_ = 0.0;
    std::vector<QueuedEvent> queued_;
};

class LoopNode final : public RuntimeNode {
public:
    explicit LoopNode(const Module& module)
    {
        if (const auto capture = findPropertyValues(module, "capture"); !capture.empty()) {
            captureSeconds_ = parseTimeValues(capture);
        }

        if (const auto playback = findPropertyValue(module, "playback")) {
            playbackEnabled_ = (*playback == "on");
        }

        if (const auto overdub = findPropertyValue(module, "overdub")) {
            overdubEnabled_ = (*overdub == "on");
        }

        if (const auto quantize = findPropertyValues(module, "quantize"); quantize.size() >= 2 && quantize[0] == "to") {
            quantizeSeconds_ = parseTimeToken(quantize[1], 120.0);
        }
    }

    void reset(double) override
    {
        absoluteTime_ = 0.0;
        nextLoopTime_ = captureSeconds_;
        recorded_.clear();
        firstCaptureStart_.reset();
        lastLoopStart_ = 0.0;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) return;

        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        const auto blockStart = absoluteTime_;
        const auto blockEnd = absoluteTime_ + blockDuration;

        captureIncoming(inputs, blockStart);

        if (playbackEnabled_ && captureSeconds_ > 0.0 && !recorded_.empty()) {
            while (nextLoopTime_ <= blockEnd + 1.0e-9) {
                lastLoopStart_ = nextLoopTime_;
                nextLoopTime_ += captureSeconds_;
            }

            emitLoopWindow(blockStart, blockEnd, *out->second);
        }

        absoluteTime_ = blockEnd;
    }

private:
    struct RecordedEvent {
        double offset = 0.0;
        Event event;
    };

    static double parseTimeValues(const std::vector<std::string>& values)
    {
        if (values.size() == 1) {
            return parseTimeToken(values[0], 120.0);
        }

        if (values.size() == 2 && (values[1] == "bar" || values[1] == "bars")) {
            return std::stod(values[0]) * parseTimeToken("1/1", 120.0);
        }

        if (values.size() == 2 && (values[1] == "beat" || values[1] == "beats")) {
            return std::stod(values[0]) * parseTimeToken("1/4", 120.0);
        }

        return parseTimeToken(values[0], 120.0);
    }

    [[nodiscard]] double quantizeOffset(double offset) const
    {
        if (quantizeSeconds_ <= 0.0) {
            return offset;
        }
        return std::round(offset / quantizeSeconds_) * quantizeSeconds_;
    }

    void captureIncoming(const std::unordered_map<std::string, const EventBuffer*>& inputs, double blockStart)
    {
        const auto input = inputs.find("in");
        if (input == inputs.end()) return;

        if (!firstCaptureStart_.has_value()) {
            firstCaptureStart_ = blockStart;
        }

        for (const auto& event : input->second->events()) {
            const auto absoluteEventTime = blockStart + event.time;
            const auto offset = quantizeOffset(absoluteEventTime - *firstCaptureStart_);
            if (offset < 0.0 || offset >= captureSeconds_) {
                continue;
            }

            if (overdubEnabled_) {
                recorded_.erase(std::remove_if(recorded_.begin(), recorded_.end(), [&](const RecordedEvent& recorded) {
                    if (std::abs(recorded.offset - offset) > overlapWindow()) {
                        return false;
                    }

                    if (recorded.event.type != event.type) {
                        return false;
                    }

                    if (recorded.event.ints.size() < 2 || event.ints.size() < 2) {
                        return false;
                    }

                    return recorded.event.ints[1] == event.ints[1];
                }), recorded_.end());
            } else {
                const auto existing = std::find_if(recorded_.begin(), recorded_.end(), [&](const RecordedEvent& recorded) {
                    return std::abs(recorded.offset - offset) < overlapWindow()
                        && recorded.event.ints == event.ints
                        && recorded.event.type == event.type;
                });
                if (existing != recorded_.end()) {
                    continue;
                }
            }

            recorded_.push_back({ offset, event });
        }
    }

    void emitLoopWindow(double blockStart, double blockEnd, EventBuffer& output) const
    {
        const auto currentLoopStart = std::floor(blockStart / captureSeconds_) * captureSeconds_;
        const auto currentLoopEnd = currentLoopStart + captureSeconds_;

        auto emitFromBase = [&](double loopBase) {
            for (const auto& recorded : recorded_) {
                const auto absoluteEventTime = loopBase + recorded.offset;
                if (absoluteEventTime < blockStart || absoluteEventTime >= blockEnd) {
                    continue;
                }

                auto emitted = recorded.event;
                emitted.time = absoluteEventTime - blockStart;
                output.push(emitted);
            }
        };

        emitFromBase(currentLoopStart);
        if (blockEnd > currentLoopEnd) {
            emitFromBase(currentLoopEnd);
        }
    }

    double captureSeconds_ = 1.0;
    double quantizeSeconds_ = 0.0;
    double absoluteTime_ = 0.0;
    double nextLoopTime_ = 1.0;
    double lastLoopStart_ = 0.0;
    bool playbackEnabled_ = true;
    bool overdubEnabled_ = false;
    std::optional<double> firstCaptureStart_;
    std::vector<RecordedEvent> recorded_;

    [[nodiscard]] double overlapWindow() const
    {
        return quantizeSeconds_ > 0.0 ? std::max(1.0e-4, quantizeSeconds_ * 0.5) : 1.0e-4;
    }
};

class BounceNode final : public RuntimeNode {
public:
    explicit BounceNode(const Module& module)
    {
        if (const auto count = findPropertyValue(module, "count")) {
            count_ = std::max(1, std::stoi(*count));
        }

        if (const auto spacing = findPropertyValues(module, "spacing"); spacing.size() == 3 && spacing[1] == "->") {
            startSpacingSeconds_ = parseTimeToken(spacing[0], 120.0);
            endSpacingSeconds_ = parseTimeToken(spacing[2], 120.0);
        }

        if (const auto velocity = findPropertyValues(module, "velocity"); velocity.size() == 3 && velocity[1] == "->") {
            startVelocity_ = std::clamp(std::stoi(velocity[0]), 1, 127);
            endVelocity_ = std::clamp(std::stoi(velocity[2]), 1, 127);
        }
    }

    void reset(double) override
    {
        queued_.clear();
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto out = outputs.find("out");
        if (out == outputs.end()) return;

        const auto input = inputs.find("in");
        if (input != inputs.end()) {
            for (const auto& event : input->second->events()) {
                if (!event.isNoteOn()) {
                    continue;
                }
                scheduleBounce(event);
            }
        }

        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        std::vector<ScheduledEvent> remaining;
        remaining.reserve(queued_.size());

        for (auto& scheduled : queued_) {
            scheduled.timeUntilEmit -= blockDuration;
            if (scheduled.timeUntilEmit <= 0.0) {
                scheduled.event.time = std::max(0.0, blockDuration + scheduled.timeUntilEmit);
                out->second->push(scheduled.event);
            } else {
                remaining.push_back(scheduled);
            }
        }

        queued_ = std::move(remaining);
    }

private:
    struct ScheduledEvent {
        double timeUntilEmit = 0.0;
        Event event;
    };

    void scheduleBounce(const Event& trigger)
    {
        double accumulated = trigger.time;
        for (int i = 0; i < count_; ++i) {
            const double mix = (count_ <= 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(count_ - 1);
            const auto velocity = static_cast<int>(std::lround(startVelocity_ + ((endVelocity_ - startVelocity_) * mix)));
            const auto spacing = startSpacingSeconds_ + ((endSpacingSeconds_ - startSpacingSeconds_) * mix);
            const auto noteLength = std::max(0.01, spacing * 0.6);

            queued_.push_back({
                accumulated,
                Event::makeNoteOn(trigger.noteNumber(), velocity, trigger.channel(), accumulated)
            });

            queued_.push_back({
                accumulated + noteLength,
                Event::makeNoteOff(trigger.noteNumber(), 0, trigger.channel(), accumulated + noteLength)
            });

            accumulated += spacing;
        }
    }

    int count_ = 8;
    double startSpacingSeconds_ = 0.24;
    double endSpacingSeconds_ = 0.02;
    int startVelocity_ = 110;
    int endVelocity_ = 40;
    std::vector<ScheduledEvent> queued_;
};

class ArpNode final : public RuntimeNode {
public:
    explicit ArpNode(const Module& module)
    {
        if (const auto gate = findPropertyValue(module, "gate")) {
            auto text = *gate;
            if (!text.empty() && text.back() == '%') {
                text.pop_back();
            }
            gateFraction_ = std::clamp(std::stod(text) / 100.0, 0.05, 1.0);
        }
    }

    void reset(double) override
    {
        activeNotes_.clear();
        lastPlayedNote_.reset();
        step_ = 0;
    }

    void process(const ProcessContext& context,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        ingestMidi(inputs);

        const auto out = outputs.find("out");
        if (out == outputs.end()) return;

        const auto trigger = inputs.find("trigger");
        if (trigger == inputs.end()) return;
        if (activeNotes_.empty()) return;

        const auto blockDuration = static_cast<double>(context.blockSize) / context.sampleRate;
        const auto noteLength = blockDuration * gateFraction_;

        for (const auto& event : trigger->second->events()) {
            if (lastPlayedNote_.has_value()) {
                out->second->push(Event::makeNoteOff(*lastPlayedNote_, 0, 1, event.time));
            }

            const auto note = activeNotes_[step_ % activeNotes_.size()];
            out->second->push(Event::makeNoteOn(note, 100, 1, event.time));
            out->second->push(Event::makeNoteOff(note, 0, 1, std::min(blockDuration, event.time + noteLength)));
            lastPlayedNote_ = note;
            ++step_;
        }
    }

private:
    void ingestMidi(const std::unordered_map<std::string, const EventBuffer*>& inputs)
    {
        const auto input = inputs.find("in");
        if (input == inputs.end()) return;

        for (const auto& event : input->second->events()) {
            if (event.isNoteOn()) {
                const auto note = event.noteNumber();
                if (std::find(activeNotes_.begin(), activeNotes_.end(), note) == activeNotes_.end()) {
                    activeNotes_.push_back(note);
                    std::sort(activeNotes_.begin(), activeNotes_.end());
                }
            } else if (event.isNoteOff()) {
                const auto note = event.noteNumber();
                activeNotes_.erase(std::remove(activeNotes_.begin(), activeNotes_.end(), note), activeNotes_.end());
                if (lastPlayedNote_.has_value() && *lastPlayedNote_ == note) {
                    lastPlayedNote_.reset();
                }
                if (step_ >= activeNotes_.size() && !activeNotes_.empty()) {
                    step_ %= activeNotes_.size();
                }
            }
        }
    }

    double gateFraction_ = 0.8;
    std::vector<int> activeNotes_;
    std::optional<int> lastPlayedNote_;
    std::size_t step_ = 0;
};

class OutputMidiNode final : public RuntimeNode {
public:
    void reset(double) override {}

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>&) override
    {
        captured_.clear();
        if (const auto in = inputs.find("in"); in != inputs.end()) {
            captured_ = in->second->events();
        }
    }

    [[nodiscard]] const std::vector<Event>& captured() const
    {
        return captured_;
    }

private:
    std::vector<Event> captured_;
};

class PassThroughNode final : public RuntimeNode {
public:
    void reset(double) override {}

    void process(const ProcessContext&,
        const std::unordered_map<std::string, const EventBuffer*>& inputs,
        std::unordered_map<std::string, EventBuffer*>& outputs) override
    {
        const auto in = inputs.find("in");
        const auto out = outputs.find("out");
        if (in == inputs.end() || out == outputs.end()) {
            return;
        }
        out->second->append(*in->second);
    }
};

std::unique_ptr<RuntimeNode> makeRuntimeNode(const NodeInfo& info, const Module& module)
{
    if (info.family == ModuleFamily::input && info.kind == "midi") {
        return std::make_unique<InputMidiNode>();
    }

    if (info.family == ModuleFamily::analyze && info.kind == "motion") {
        return std::make_unique<MotionNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "clock") {
        return std::make_unique<ClockNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "phrase") {
        return std::make_unique<PhraseNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "progression") {
        return std::make_unique<ProgressionNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "section") {
        return std::make_unique<SectionNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "pattern") {
        return std::make_unique<PatternNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "growth") {
        return std::make_unique<GrowthNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "swarm") {
        return std::make_unique<SwarmNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "collapse") {
        return std::make_unique<CollapseNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "fibonacci") {
        return std::make_unique<FibonacciNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "random") {
        return std::make_unique<RandomNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "chance") {
        return std::make_unique<ChanceNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "table") {
        return std::make_unique<TableNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "markov") {
        return std::make_unique<MarkovNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "tree") {
        return std::make_unique<TreeNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "field") {
        return std::make_unique<FieldNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "formula") {
        return std::make_unique<FormulaNode>(module);
    }

    if (info.family == ModuleFamily::generate && info.kind == "moment") {
        return std::make_unique<MomentNode>(module);
    }

    if (info.family == ModuleFamily::shape && info.kind == "stages") {
        return std::make_unique<StagesNode>(module);
    }

    if (info.family == ModuleFamily::shape && info.kind == "modulator") {
        return std::make_unique<ModulatorNode>(module);
    }

    if (info.family == ModuleFamily::shape && info.kind == "lists") {
        return std::make_unique<ListsNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "quantize") {
        return std::make_unique<QuantizeNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "passthrough") {
        return std::make_unique<PassThroughNode>();
    }

    if (info.family == ModuleFamily::transform && info.kind == "sieve") {
        return std::make_unique<SieveNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "warp") {
        return std::make_unique<WarpNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "equation") {
        return std::make_unique<EquationNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "bits") {
        return std::make_unique<BitsNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "groove") {
        return std::make_unique<GrooveNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "retrig") {
        return std::make_unique<RetrigNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "length") {
        return std::make_unique<LengthNode>(module);
    }

    if (info.family == ModuleFamily::project && info.kind == "to_notes") {
        return std::make_unique<ToNotesNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "split") {
        return std::make_unique<SplitNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "delay") {
        return std::make_unique<DelayNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "bounce") {
        return std::make_unique<BounceNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "loop") {
        return std::make_unique<LoopNode>(module);
    }

    if (info.family == ModuleFamily::transform && info.kind == "arp") {
        return std::make_unique<ArpNode>(module);
    }

    if (info.family == ModuleFamily::output && info.kind == "midi") {
        return std::make_unique<OutputMidiNode>();
    }

    if (info.family == ModuleFamily::memory && info.kind == "smear") {
        return std::make_unique<SmearNode>(module);
    }

    if (info.family == ModuleFamily::memory && info.kind == "cutup") {
        return std::make_unique<CutupNode>(module);
    }

    if (info.family == ModuleFamily::memory && info.kind == "slicer") {
        return std::make_unique<SlicerNode>(module);
    }

    if (info.family == ModuleFamily::memory && info.kind == "pool") {
        return std::make_unique<PoolNode>(module);
    }

    return std::make_unique<StubRuntimeNode>(info, module);
}

void sortEventsByTime(std::vector<Event>& events)
{
    std::stable_sort(events.begin(), events.end(), [](const Event& left, const Event& right) {
        if (left.time != right.time) return left.time < right.time;
        if (left.isNoteOff() != right.isNoteOff()) return left.isNoteOff();
        return left.type < right.type;
    });
}

} // namespace

Event Event::makeTrigger(double time)
{
    return Event { SignalType::trigger, time, {}, {} };
}

Event Event::makeValue(double value, double time)
{
    return Event { SignalType::value, time, {}, { value } };
}

Event Event::makePitch(double value, double time)
{
    return Event { SignalType::pitch, time, {}, { value } };
}

Event Event::makeNoteOn(int note, int velocity, int channel, double time)
{
    return Event { SignalType::midi, time, { 0x90, clampMidiNote(note), std::clamp(velocity, 0, 127), std::clamp(channel, 1, 16) }, {} };
}

Event Event::makeNoteOff(int note, int velocity, int channel, double time)
{
    return Event { SignalType::midi, time, { 0x80, clampMidiNote(note), std::clamp(velocity, 0, 127), std::clamp(channel, 1, 16) }, {} };
}

bool Event::isNoteOn() const
{
    return type == SignalType::midi && ints.size() >= 3 && ints[0] == 0x90 && ints[2] > 0;
}

bool Event::isNoteOff() const
{
    return type == SignalType::midi && ints.size() >= 3 && (ints[0] == 0x80 || (ints[0] == 0x90 && ints[2] == 0));
}

int Event::noteNumber() const
{
    return ints.size() >= 2 ? ints[1] : 0;
}

int Event::velocityValue() const
{
    return ints.size() >= 3 ? ints[2] : 0;
}

int Event::channel() const
{
    return ints.size() >= 4 ? ints[3] : 1;
}

double Event::valueOr(double fallback) const
{
    return floats.empty() ? fallback : floats.front();
}

void RuntimeNode::setExternalEvents(const std::vector<Event>&)
{
}

std::optional<int> RuntimeNode::currentSectionIndex() const
{
    return std::nullopt;
}

std::optional<double> RuntimeNode::currentSectionPhase() const
{
    return std::nullopt;
}

std::optional<std::uint64_t> RuntimeNode::sectionAdvanceCount() const
{
    return std::nullopt;
}

std::optional<std::string> RuntimeNode::activeStateLabel() const
{
    return std::nullopt;
}

std::optional<ModulatorStateSnapshot> RuntimeNode::modulatorState() const
{
    return std::nullopt;
}

void EventBuffer::clear()
{
    events_.clear();
}

void EventBuffer::push(Event event)
{
    events_.push_back(std::move(event));
}

void EventBuffer::append(const EventBuffer& other)
{
    events_.insert(events_.end(), other.events_.begin(), other.events_.end());
}

void EventBuffer::append(const std::vector<Event>& events)
{
    events_.insert(events_.end(), events.begin(), events.end());
}

const std::vector<Event>& EventBuffer::events() const
{
    return events_;
}

StubRuntimeNode::StubRuntimeNode(NodeInfo info, Module module)
    : info_(std::move(info))
    , module_(std::move(module))
{
}

void StubRuntimeNode::reset(double sampleRate)
{
    sampleRate_ = sampleRate;
}

void StubRuntimeNode::process(const ProcessContext&,
    const std::unordered_map<std::string, const EventBuffer*>& inputs,
    std::unordered_map<std::string, EventBuffer*>& outputs)
{
    (void)module_;
    if (info_.outputs.empty()) return;

    const auto passthroughInput = inputs.find("in");
    if (passthroughInput == inputs.end()) return;

    const auto target = outputs.find(info_.outputs.front().name);
    if (target == outputs.end()) return;

    target->second->append(*passthroughInput->second);
}

const NodeInfo& StubRuntimeNode::info() const
{
    return info_;
}

RuntimeGraph::RuntimeGraph(CompiledPatch patch)
    : patch_(std::move(patch))
{
    nodes_.reserve(patch_.nodes.size());
    buffers_.resize(patch_.nodes.size());
    nodeModes_.assign(patch_.nodes.size(), NodeProcessingMode::normal);

    for (std::size_t i = 0; i < patch_.nodes.size(); ++i) {
        const auto& nodeInfo = patch_.nodes[i];
        const auto& module = patch_.source.modules[i];
        nodes_.push_back(makeRuntimeNode(nodeInfo, module));

        auto& ports = buffers_[i];
        for (const auto& input : nodeInfo.inputs) {
            ports.inputs.emplace(input.name, EventBuffer {});
        }
        for (const auto& output : nodeInfo.outputs) {
            ports.outputs.emplace(output.name, EventBuffer {});
        }
    }
}

void RuntimeGraph::reset(double sampleRate)
{
    for (auto& node : nodes_) {
        node->reset(sampleRate);
    }
}

void RuntimeGraph::process(const ProcessContext& context)
{
    renderedOutputs_.clear();

    for (auto& nodeBuffers : buffers_) {
        for (auto& [name, buffer] : nodeBuffers.inputs) {
            (void)name;
            buffer.clear();
        }
        for (auto& [name, buffer] : nodeBuffers.outputs) {
            (void)name;
            buffer.clear();
        }
    }

    for (const auto nodeIndex : patch_.executionOrder) {
        for (const auto& connection : patch_.connections) {
            if (connection.toNode != nodeIndex) continue;

            auto& inputBuffer = buffers_[connection.toNode].inputs.at(connection.toPort);
            const auto& outputBuffer = buffers_[connection.fromNode].outputs.at(connection.fromPort);
            inputBuffer.append(outputBuffer);
        }

        std::unordered_map<std::string, const EventBuffer*> inputViews;
        for (auto& [name, buffer] : buffers_[nodeIndex].inputs) {
            inputViews.emplace(name, &buffer);
        }

        std::unordered_map<std::string, EventBuffer*> outputViews;
        for (auto& [name, buffer] : buffers_[nodeIndex].outputs) {
            outputViews.emplace(name, &buffer);
        }

        const auto mode = nodeModes_[nodeIndex];
        if (mode == NodeProcessingMode::mute) {
            // Leave outputs empty.
        } else if (mode == NodeProcessingMode::bypass) {
            const auto input = inputViews.find("in");
            if (input != inputViews.end()) {
                if (const auto out = outputViews.find("out"); out != outputViews.end()) {
                    out->second->append(*input->second);
                } else if (outputViews.size() == 1) {
                    outputViews.begin()->second->append(*input->second);
                }
            }
        } else {
            nodes_[nodeIndex]->process(context, inputViews, outputViews);
        }

        if (patch_.nodes[nodeIndex].family == ModuleFamily::output) {
            const auto* buffer = inputBuffer(nodeIndex, "in");
            if (buffer != nullptr) {
                auto rendered = buffer->events();
                sortEventsByTime(rendered);
                renderedOutputs_[patch_.nodes[nodeIndex].name] = std::move(rendered);
            }
        }
    }
}

void RuntimeGraph::setInputEvents(const std::string& moduleName, const std::vector<Event>& events)
{
    const auto node = findNodeIndex(moduleName);
    if (!node.has_value()) return;
    nodes_[*node]->setExternalEvents(events);
}

void RuntimeGraph::setNodeMode(const std::string& moduleName, NodeProcessingMode mode)
{
    const auto node = findNodeIndex(moduleName);
    if (!node.has_value()) return;
    nodeModes_[*node] = mode;
}

NodeProcessingMode RuntimeGraph::nodeMode(const std::string& moduleName) const
{
    const auto node = findNodeIndex(moduleName);
    if (!node.has_value()) return NodeProcessingMode::normal;
    return nodeModes_[*node];
}

const CompiledPatch& RuntimeGraph::patch() const
{
    return patch_;
}

const EventBuffer* RuntimeGraph::outputBuffer(std::size_t nodeIndex, const std::string& port) const
{
    if (nodeIndex >= buffers_.size()) return nullptr;
    const auto& outputs = buffers_[nodeIndex].outputs;
    const auto it = outputs.find(port);
    return it == outputs.end() ? nullptr : &it->second;
}

const EventBuffer* RuntimeGraph::inputBuffer(std::size_t nodeIndex, const std::string& port) const
{
    if (nodeIndex >= buffers_.size()) return nullptr;
    const auto& inputs = buffers_[nodeIndex].inputs;
    const auto it = inputs.find(port);
    return it == inputs.end() ? nullptr : &it->second;
}

std::optional<std::size_t> RuntimeGraph::findNodeIndex(const std::string& moduleName) const
{
    for (std::size_t i = 0; i < patch_.nodes.size(); ++i) {
        if (patch_.nodes[i].name == moduleName) return i;
    }
    return std::nullopt;
}

std::vector<Event> RuntimeGraph::outputEvents(const std::string& moduleName) const
{
    const auto it = renderedOutputs_.find(moduleName);
    return it == renderedOutputs_.end() ? std::vector<Event> {} : it->second;
}

std::optional<int> RuntimeGraph::currentSectionIndex(const std::string& moduleName) const
{
    const auto node = findNodeIndex(moduleName);
    if (!node.has_value()) {
        return std::nullopt;
    }

    return nodes_[*node]->currentSectionIndex();
}

std::optional<double> RuntimeGraph::currentSectionPhase(const std::string& moduleName) const
{
    const auto node = findNodeIndex(moduleName);
    if (!node.has_value()) {
        return std::nullopt;
    }

    return nodes_[*node]->currentSectionPhase();
}

std::optional<std::uint64_t> RuntimeGraph::sectionAdvanceCount(const std::string& moduleName) const
{
    const auto node = findNodeIndex(moduleName);
    if (!node.has_value()) {
        return std::nullopt;
    }

    return nodes_[*node]->sectionAdvanceCount();
}

std::optional<std::string> RuntimeGraph::activeStateLabel(const std::string& moduleName) const
{
    const auto node = findNodeIndex(moduleName);
    if (!node.has_value()) {
        return std::nullopt;
    }

    return nodes_[*node]->activeStateLabel();
}

std::optional<ModulatorStateSnapshot> RuntimeGraph::modulatorState(const std::string& moduleName) const
{
    const auto node = findNodeIndex(moduleName);
    if (!node.has_value()) {
        return std::nullopt;
    }

    return nodes_[*node]->modulatorState();
}

} // namespace pulse
