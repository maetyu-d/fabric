#include "pulse/Parser.hpp"

#include <algorithm>
#include <unordered_map>

namespace pulse {

namespace {

struct SubpatchExports {
    std::unordered_map<std::string, std::string> ports;
};

struct ReusableModuleDefinition {
    std::vector<std::string> parameters;
    std::vector<LineToken> body;
    SourceLocation location {};
};

struct ReusableProbabilityDefinition {
    std::vector<Property> properties;
    SourceLocation location {};
};

std::string flattenName(const std::string& prefix, const std::string& local)
{
    if (prefix.empty()) {
        return local;
    }
    return prefix + "__" + local;
}

SignalType parseSignalTypeName(std::string_view text)
{
    if (text == "midi") return SignalType::midi;
    if (text == "trigger") return SignalType::trigger;
    if (text == "gate") return SignalType::gate;
    if (text == "value") return SignalType::value;
    if (text == "pitch") return SignalType::pitch;
    return SignalType::unknown;
}

std::optional<std::pair<ModuleFamily, std::string>> inferredModule(std::string_view head, const std::vector<std::string>& parts)
{
    if (head == "midi" && parts.size() >= 3) {
        if (parts[1] == "in") return std::pair { ModuleFamily::input, std::string("midi") };
        if (parts[1] == "out") return std::pair { ModuleFamily::output, std::string("midi") };
    }

    if (head == "motion") return std::pair { ModuleFamily::analyze, std::string("motion") };

    if (head == "clock" || head == "pattern" || head == "fibonacci" || head == "random"
        || head == "chance" || head == "field" || head == "formula" || head == "moment"
        || head == "table" || head == "markov" || head == "tree"
        || head == "phrase" || head == "progression" || head == "growth" || head == "swarm"
        || head == "collapse" || head == "section") {
        return std::pair { ModuleFamily::generate, std::string(head) };
    }

    if (head == "stages" || head == "lists" || head == "modulator") {
        return std::pair { ModuleFamily::shape, std::string(head) };
    }

    if (head == "quantize" || head == "sieve" || head == "split" || head == "delay" || head == "loop"
        || head == "bounce" || head == "arp" || head == "warp" || head == "filter"
        || head == "groove" || head == "retrig" || head == "length"
        || head == "bits" || head == "equation") {
        return std::pair { ModuleFamily::transform, std::string(head) };
    }

    if (head == "smear" || head == "cutup" || head == "slicer" || head == "pool") {
        return std::pair { ModuleFamily::memory, std::string(head) };
    }

    if (head == "notes") {
        return std::pair { ModuleFamily::project, std::string("to_notes") };
    }

    return std::nullopt;
}

std::vector<std::string> canonicalizeTopLevelParts(const std::vector<std::string>& parts)
{
    if (parts.empty()) {
        return parts;
    }

    if (parts.size() == 3 && parts[1] == "->") {
        return { "connect", parts[0], "->", parts[2] };
    }

    if (parts[0] == "key" && parts.size() >= 2) {
        std::vector<std::string> canonical { "scale" };
        canonical.insert(canonical.end(), parts.begin() + 1, parts.end());
        return canonical;
    }

    return parts;
}

Property canonicalizeProperty(const LineToken& token)
{
    Property property;
    property.location = token.location;
    property.key = token.parts[0];
    property.values.assign(token.parts.begin() + 1, token.parts.end());

    if (property.key == "key") {
        property.key = "scale";
    }

    if (property.key == "play") {
        property.key = "playback";
    }

    if (property.key == "quantize") {
        property.key = "scale";
    }

    if (property.key == "held" && property.values.size() >= 2 && property.values[0] == "notes") {
        property.key = "from";
        property.values = { "held", "notes" };
    }

    if (property.key == "starts" || property.key == "begin") {
        property.key = "start";
    }

    if (property.key == "part" || property.key == "scene") {
        property.key = "section";
    }

    if ((property.key == "group" || property.key == "family") && !property.values.empty()) {
        property.key = "family";
    }

    if (property.key == "starts_on" || property.key == "start_on") {
        property.key = "root";
    }

    if (property.key == "leans" && property.values.size() >= 2 && property.values[0] == "to") {
        property.key = "target";
        property.values = { property.values[1] };
    }

    if (property.key == "keep" && property.values.size() >= 2 && property.values[1] == "notes") {
        property.key = "max";
        property.values = { "notes", property.values[0] };
    }

    if (property.key == "play" && property.values.size() >= 2 && property.values[0] == "as") {
        property.key = "chord";
        property.values = { property.values[1] };
    }

    if (property.key == "move" && !property.values.empty()) {
        property.key = "movement";
    }

    if (property.key == "arrive" && property.values.size() >= 2 && property.values[0] == "every") {
        property.values[0] = "on";
    }

    if (property.key == "follow" && property.values.size() >= 2 && property.values[0] == "the" && property.values[1] == "phrase") {
        property.values = { "phrase" };
    }

    if ((property.key == "come" || property.key == "return") && property.values.size() >= 2 && property.values[0] == "back" && property.values[1] == "to") {
        property.key = "recover";
        property.values.erase(property.values.begin());
    }

    if (property.key == "when" && !property.values.empty()) {
        if (property.values[0] == "broken") {
            property.key = "on";
            property.values[0] = "collapse";
        } else if (property.values[0] == "stable" || property.values[0] == "unstable") {
            const auto state = property.values[0];
            property.values.erase(property.values.begin());
            property.key = property.values.empty() ? state : property.values[0];
            if (!property.values.empty()) {
                property.values.erase(property.values.begin());
            }
            property.values.insert(property.values.begin(), "when");
            property.values.insert(property.values.begin() + 1, state);
        }
    }

    if (property.key == "prefer" && property.values.size() >= 3 && property.values[0] == "the" && property.values[1] == "center") {
        property.values.erase(property.values.begin());
    }

    if (property.key == "stage") {
        for (auto& value : property.values) {
            if (value == "ch") value = "channel";
            else if (value == "to") value = "level";
            else if (value == "for" || value == "in") value = "time";
        }
    }

    return property;
}

std::string rewriteEndpointText(const std::string& text,
    const std::unordered_map<std::string, std::string>& localRefs,
    const std::unordered_map<std::string, SubpatchExports>& nestedSubpatches)
{
    const auto dot = text.find('.');
    if (dot == std::string::npos) {
        if (const auto found = localRefs.find(text); found != localRefs.end()) {
            return found->second;
        }
        return text;
    }

    const auto head = text.substr(0, dot);
    const auto tail = text.substr(dot + 1);

    if (const auto subpatch = nestedSubpatches.find(head); subpatch != nestedSubpatches.end()) {
        if (const auto port = subpatch->second.ports.find(tail); port != subpatch->second.ports.end()) {
            return port->second;
        }
    }

    if (const auto found = localRefs.find(head); found != localRefs.end()) {
        return found->second + "." + tail;
    }

    return text;
}

bool parseSubpatchBlock(const std::vector<LineToken>& tokens,
    std::size_t& index,
    Patch& patch,
    std::vector<Diagnostic>& diagnostics,
    const std::string& flattenedPrefix,
    SubpatchExports& exports,
    const std::unordered_map<std::string, ReusableModuleDefinition>& moduleDefinitions,
    const std::unordered_map<std::string, ReusableProbabilityDefinition>& probabilityDefinitions);

bool collectBlockBody(const std::vector<LineToken>& tokens,
    std::size_t& index,
    std::vector<LineToken>& body,
    std::vector<Diagnostic>& diagnostics,
    const std::string& blockKind,
    const std::string& blockName)
{
    int depth = 1;
    while (index < tokens.size()) {
        const auto& token = tokens[index];
        const auto tokenParts = canonicalizeTopLevelParts(token.parts);
        if (!tokenParts.empty()) {
            if (tokenParts[0] == "end") {
                --depth;
                if (depth == 0) {
                    ++index;
                    return true;
                }
            } else if ((tokenParts.size() >= 2 && inferredModule(tokenParts[0], tokenParts).has_value())
                || tokenParts[0] == "subpatch"
                || tokenParts[0] == "module")
            {
                ++depth;
            }
        }

        body.push_back(token);
        ++index;
    }

    diagnostics.push_back({
        Diagnostic::Severity::error,
        blockKind + " `" + blockName + "` is missing a closing `end`.",
        {}
    });
    return false;
}

std::vector<LineToken> instantiateModuleBody(const ReusableModuleDefinition& definition,
    const std::unordered_map<std::string, std::string>& arguments)
{
    auto body = definition.body;
    for (auto& token : body) {
        for (auto& part : token.parts) {
            if (part.size() > 1 && part.front() == '$') {
                const auto name = part.substr(1);
                if (const auto found = arguments.find(name); found != arguments.end()) {
                    part = found->second;
                }
            }
        }
    }
    return body;
}

bool applyProbabilityDefinitions(Module& module,
    const std::unordered_map<std::string, ReusableProbabilityDefinition>& probabilityDefinitions,
    std::vector<Diagnostic>& diagnostics)
{
    std::vector<Property> explicitProperties;
    std::vector<Property> sharedProperties;

    for (const auto& property : module.properties) {
        if (property.key == "using" && !property.values.empty()) {
            for (const auto& name : property.values) {
                const auto found = probabilityDefinitions.find(name);
                if (found == probabilityDefinitions.end()) {
                    diagnostics.push_back({
                        Diagnostic::Severity::error,
                        "Unknown probability definition `" + name + "`.",
                        property.location
                    });
                    return false;
                }
                sharedProperties.insert(sharedProperties.end(), found->second.properties.begin(), found->second.properties.end());
            }
            continue;
        }

        explicitProperties.push_back(property);
    }

    explicitProperties.insert(explicitProperties.end(), sharedProperties.begin(), sharedProperties.end());
    module.properties = std::move(explicitProperties);
    return true;
}

bool isModuleHeaderImpl(const LineToken& token)
{
    return token.parts.size() >= 2 && inferredModule(token.parts[0], token.parts).has_value();
}

bool isConnectionImpl(const LineToken& token)
{
    return !token.parts.empty() && token.parts[0] == "connect";
}

Endpoint parseEndpointImpl(const std::string& text, const SourceLocation& location)
{
    Endpoint endpoint;
    endpoint.location = location;

    auto dot = text.find('.');
    if (dot == std::string::npos) {
        endpoint.module = text;
        return endpoint;
    }

    endpoint.module = text.substr(0, dot);
    endpoint.port = text.substr(dot + 1);
    return endpoint;
}

} // namespace

std::optional<Patch> Parser::parse(const std::string& source, std::vector<Diagnostic>& diagnostics) const
{
    Lexer lexer;
    auto tokens = lexer.lex(source, diagnostics);
    if (!diagnostics.empty() && tokens.empty()) {
        return std::nullopt;
    }

    if (tokens.empty()) {
        return std::nullopt;
    }

    const auto firstParts = canonicalizeTopLevelParts(tokens.front().parts);
    const auto& first = tokens.front();
    if (firstParts.size() < 2 || firstParts[0] != "patch") {
        diagnostics.push_back({
            Diagnostic::Severity::error,
            "Patch must begin with `patch <name>`.",
            first.location
        });
        return std::nullopt;
    }

    Patch patch;
    patch.name = firstParts[1];
    patch.location = first.location;
    std::unordered_map<std::string, SubpatchExports> topLevelSubpatches;
    std::unordered_map<std::string, ReusableModuleDefinition> moduleDefinitions;
    std::unordered_map<std::string, ReusableProbabilityDefinition> probabilityDefinitions;

    bool patchClosed = false;
    std::size_t index = 1;
    while (index < tokens.size()) {
        const auto& token = tokens[index];
        const auto tokenParts = canonicalizeTopLevelParts(token.parts);
        if (!tokenParts.empty() && tokenParts[0] == "end") {
            patchClosed = true;
            break;
        }

        LineToken canonicalToken = token;
        canonicalToken.parts = tokenParts;

        if (canonicalToken.parts.size() >= 2 && canonicalToken.parts[0] == "subpatch") {
            SubpatchExports exports;
            const auto subpatchName = canonicalToken.parts[1];
            const auto prefix = flattenName("", subpatchName);
            ++index;
            if (!parseSubpatchBlock(tokens, index, patch, diagnostics, prefix, exports, moduleDefinitions, probabilityDefinitions)) {
                return std::nullopt;
            }
            topLevelSubpatches[subpatchName] = std::move(exports);
            continue;
        }

        if (canonicalToken.parts.size() >= 2 && canonicalToken.parts[0] == "module") {
            ReusableModuleDefinition definition;
            definition.location = token.location;
            const auto moduleName = canonicalToken.parts[1];
            definition.parameters.assign(canonicalToken.parts.begin() + 2, canonicalToken.parts.end());
            ++index;
            if (!collectBlockBody(tokens, index, definition.body, diagnostics, "Module", moduleName)) {
                return std::nullopt;
            }
            moduleDefinitions[moduleName] = std::move(definition);
            continue;
        }

        if (canonicalToken.parts.size() >= 2 && canonicalToken.parts[0] == "probability") {
            ReusableProbabilityDefinition definition;
            definition.location = token.location;
            const auto probabilityName = canonicalToken.parts[1];
            ++index;

            bool closed = false;
            while (index < tokens.size()) {
                const auto& bodyToken = tokens[index];
                if (!bodyToken.parts.empty() && bodyToken.parts[0] == "end") {
                    closed = true;
                    ++index;
                    break;
                }

                definition.properties.push_back(canonicalizeProperty(bodyToken));
                ++index;
            }

            if (!closed) {
                diagnostics.push_back({
                    Diagnostic::Severity::error,
                    "Probability definition `" + probabilityName + "` is missing a closing `end`.",
                    definition.location
                });
                return std::nullopt;
            }

            probabilityDefinitions[probabilityName] = std::move(definition);
            continue;
        }

        if (canonicalToken.parts.size() >= 3 && canonicalToken.parts[0] == "use") {
            const auto found = moduleDefinitions.find(canonicalToken.parts[1]);
            if (found == moduleDefinitions.end()) {
                diagnostics.push_back({
                    Diagnostic::Severity::error,
                    "Unknown reusable module `" + canonicalToken.parts[1] + "`.",
                    token.location
                });
                return std::nullopt;
            }

            SubpatchExports exports;
            std::size_t bodyIndex = 0;
            std::unordered_map<std::string, std::string> arguments;
            for (std::size_t argIndex = 3; argIndex < canonicalToken.parts.size(); ++argIndex) {
                const auto& part = canonicalToken.parts[argIndex];
                const auto equals = part.find('=');
                if (equals == std::string::npos) {
                    diagnostics.push_back({
                        Diagnostic::Severity::error,
                        "Module arguments must look like `name=value`.",
                        token.location
                    });
                    return std::nullopt;
                }
                arguments[part.substr(0, equals)] = part.substr(equals + 1);
            }
            auto instancedBody = instantiateModuleBody(found->second, arguments);
            instancedBody.push_back(LineToken { { "end" }, token.location, "end" });
            if (!parseSubpatchBlock(instancedBody, bodyIndex, patch, diagnostics, canonicalToken.parts[2], exports, moduleDefinitions, probabilityDefinitions)) {
                return std::nullopt;
            }
            topLevelSubpatches[canonicalToken.parts[2]] = std::move(exports);
            ++index;
            continue;
        }

        if (isModuleHeader(canonicalToken)) {
            const auto inferred = inferredModule(canonicalToken.parts[0], canonicalToken.parts);
            if (!inferred.has_value()) {
                diagnostics.push_back({
                    Diagnostic::Severity::error,
                    "Unknown module header.",
                    token.location
                });
                return std::nullopt;
            }

            Module module;
            module.family = inferred->first;
            module.kind = inferred->second;
            module.name = (canonicalToken.parts[0] == "midi") ? canonicalToken.parts[2] : canonicalToken.parts[1];
            module.location = token.location;
            ++index;

            bool closed = false;
            while (index < tokens.size()) {
                const auto& bodyToken = tokens[index];
                if (!bodyToken.parts.empty() && bodyToken.parts[0] == "end") {
                    closed = true;
                    ++index;
                    break;
                }

                module.properties.push_back(canonicalizeProperty(bodyToken));
                ++index;
            }

            if (!closed) {
                diagnostics.push_back({
                    Diagnostic::Severity::error,
                    "Module `" + module.name + "` is missing a closing `end`.",
                    module.location
                });
                return std::nullopt;
            }

            if (!applyProbabilityDefinitions(module, probabilityDefinitions, diagnostics)) {
                return std::nullopt;
            }

            patch.modules.push_back(std::move(module));
            continue;
        }

        if (isConnection(canonicalToken)) {
            if (canonicalToken.parts.size() != 4 || canonicalToken.parts[2] != "->") {
                diagnostics.push_back({
                    Diagnostic::Severity::error,
                    "Connections must look like `a -> b`.",
                    token.location
                });
                return std::nullopt;
            }

            Connection connection;
            connection.from = parseEndpoint(rewriteEndpointText(canonicalToken.parts[1], {}, topLevelSubpatches), token.location);
            connection.to = parseEndpoint(rewriteEndpointText(canonicalToken.parts[3], {}, topLevelSubpatches), token.location);
            connection.location = token.location;
            patch.connections.push_back(std::move(connection));
            ++index;
            continue;
        }

        GlobalSetting global;
        global.key = tokenParts[0];
        global.values.assign(tokenParts.begin() + 1, tokenParts.end());
        global.location = token.location;
        patch.globals.push_back(std::move(global));
        ++index;
    }

    if (!patchClosed) {
        diagnostics.push_back({
            Diagnostic::Severity::error,
            "Patch `" + patch.name + "` is missing a closing `end`.",
            patch.location
        });
        return std::nullopt;
    }

    return patch;
}

bool Parser::isModuleHeader(const LineToken& token)
{
    return isModuleHeaderImpl(token);
}

bool Parser::isConnection(const LineToken& token)
{
    return isConnectionImpl(token);
}

Endpoint Parser::parseEndpoint(const std::string& text, const SourceLocation& location)
{
    return parseEndpointImpl(text, location);
}

namespace {

bool parseSubpatchBlock(const std::vector<LineToken>& tokens,
    std::size_t& index,
    Patch& patch,
    std::vector<Diagnostic>& diagnostics,
    const std::string& flattenedPrefix,
    SubpatchExports& exports,
    const std::unordered_map<std::string, ReusableModuleDefinition>& moduleDefinitions,
    const std::unordered_map<std::string, ReusableProbabilityDefinition>& probabilityDefinitions)
{
    std::unordered_map<std::string, std::string> localRefs;
    std::unordered_map<std::string, SubpatchExports> nestedSubpatches;

    while (index < tokens.size()) {
        const auto& token = tokens[index];
        const auto tokenParts = canonicalizeTopLevelParts(token.parts);
        if (!tokenParts.empty() && tokenParts[0] == "end") {
            ++index;
            return true;
        }

        LineToken canonicalToken = token;
        canonicalToken.parts = tokenParts;

        if (canonicalToken.parts.size() >= 2 && canonicalToken.parts[0] == "subpatch") {
            const auto nestedName = canonicalToken.parts[1];
            const auto nestedPrefix = flattenName(flattenedPrefix, nestedName);
            ++index;
            SubpatchExports nestedExports;
            if (!parseSubpatchBlock(tokens, index, patch, diagnostics, nestedPrefix, nestedExports, moduleDefinitions, probabilityDefinitions)) {
                return false;
            }
            nestedSubpatches[nestedName] = std::move(nestedExports);
            continue;
        }

        if (canonicalToken.parts.size() >= 3 && canonicalToken.parts[0] == "use") {
            const auto found = moduleDefinitions.find(canonicalToken.parts[1]);
            if (found == moduleDefinitions.end()) {
                diagnostics.push_back({
                    Diagnostic::Severity::error,
                    "Unknown reusable module `" + canonicalToken.parts[1] + "`.",
                    token.location
                });
                return false;
            }

            SubpatchExports nestedExports;
            std::size_t bodyIndex = 0;
            const auto nestedPrefix = flattenName(flattenedPrefix, canonicalToken.parts[2]);
            std::unordered_map<std::string, std::string> arguments;
            for (std::size_t argIndex = 3; argIndex < canonicalToken.parts.size(); ++argIndex) {
                const auto& part = canonicalToken.parts[argIndex];
                const auto equals = part.find('=');
                if (equals == std::string::npos) {
                    diagnostics.push_back({
                        Diagnostic::Severity::error,
                        "Module arguments must look like `name=value`.",
                        token.location
                    });
                    return false;
                }
                arguments[part.substr(0, equals)] = part.substr(equals + 1);
            }
            auto instancedBody = instantiateModuleBody(found->second, arguments);
            instancedBody.push_back(LineToken { { "end" }, token.location, "end" });
            if (!parseSubpatchBlock(instancedBody, bodyIndex, patch, diagnostics, nestedPrefix, nestedExports, moduleDefinitions, probabilityDefinitions)) {
                return false;
            }
            nestedSubpatches[canonicalToken.parts[2]] = std::move(nestedExports);
            ++index;
            continue;
        }

        if (canonicalToken.parts.size() >= 3 && (canonicalToken.parts[0] == "in" || canonicalToken.parts[0] == "out")) {
            const auto signal = parseSignalTypeName(canonicalToken.parts[1]);
            if (signal == SignalType::unknown) {
                diagnostics.push_back({
                    Diagnostic::Severity::error,
                    "Subpatch ports must declare a signal type like `midi`, `trigger`, `gate`, `value`, or `pitch`.",
                    token.location
                });
                return false;
            }

            Module portModule;
            portModule.family = ModuleFamily::transform;
            portModule.kind = "passthrough";
            portModule.name = flattenName(flattenedPrefix, canonicalToken.parts[2]);
            portModule.location = token.location;
            portModule.properties.push_back(Property { "signal", { toString(signal) }, token.location });
            patch.modules.push_back(std::move(portModule));

            exports.ports[canonicalToken.parts[2]] = flattenName(flattenedPrefix, canonicalToken.parts[2]);
            localRefs[canonicalToken.parts[2]] = flattenName(flattenedPrefix, canonicalToken.parts[2]);
            ++index;
            continue;
        }

        if (isModuleHeaderImpl(canonicalToken)) {
            const auto inferred = inferredModule(canonicalToken.parts[0], canonicalToken.parts);
            if (!inferred.has_value()) {
                diagnostics.push_back({
                    Diagnostic::Severity::error,
                    "Unknown module header inside subpatch.",
                    token.location
                });
                return false;
            }

            Module module;
            module.family = inferred->first;
            module.kind = inferred->second;
            const auto localName = (canonicalToken.parts[0] == "midi") ? canonicalToken.parts[2] : canonicalToken.parts[1];
            module.name = flattenName(flattenedPrefix, localName);
            module.location = token.location;
            localRefs[localName] = module.name;
            ++index;

            bool closed = false;
            while (index < tokens.size()) {
                const auto& bodyToken = tokens[index];
                if (!bodyToken.parts.empty() && bodyToken.parts[0] == "end") {
                    closed = true;
                    ++index;
                    break;
                }

                module.properties.push_back(canonicalizeProperty(bodyToken));
                ++index;
            }

            if (!closed) {
                diagnostics.push_back({
                    Diagnostic::Severity::error,
                    "Module `" + module.name + "` is missing a closing `end`.",
                    module.location
                });
                return false;
            }

            if (!applyProbabilityDefinitions(module, probabilityDefinitions, diagnostics)) {
                return false;
            }

            patch.modules.push_back(std::move(module));
            continue;
        }

        if (isConnectionImpl(canonicalToken)) {
            if (canonicalToken.parts.size() != 4 || canonicalToken.parts[2] != "->") {
                diagnostics.push_back({
                    Diagnostic::Severity::error,
                    "Connections must look like `a -> b`.",
                    token.location
                });
                return false;
            }

            Connection connection;
            connection.from = parseEndpointImpl(rewriteEndpointText(canonicalToken.parts[1], localRefs, nestedSubpatches), token.location);
            connection.to = parseEndpointImpl(rewriteEndpointText(canonicalToken.parts[3], localRefs, nestedSubpatches), token.location);
            connection.location = token.location;
            patch.connections.push_back(std::move(connection));
            ++index;
            continue;
        }

        diagnostics.push_back({
            Diagnostic::Severity::error,
            "Only modules, `in`/`out` ports, nested `subpatch` blocks, `use` instances, and connections are allowed inside a subpatch.",
            token.location
        });
        return false;
    }

    diagnostics.push_back({
        Diagnostic::Severity::error,
        "Subpatch `" + flattenedPrefix + "` is missing a closing `end`.",
        {}
    });
    return false;
}

} // namespace

} // namespace pulse
