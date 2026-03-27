#include "pulse/Parser.hpp"

#include <algorithm>

namespace pulse {

namespace {

std::optional<std::pair<ModuleFamily, std::string>> inferredModule(std::string_view head, const std::vector<std::string>& parts)
{
    if (head == "midi" && parts.size() >= 3) {
        if (parts[1] == "in") return std::pair { ModuleFamily::input, std::string("midi") };
        if (parts[1] == "out") return std::pair { ModuleFamily::output, std::string("midi") };
    }

    if (head == "motion") return std::pair { ModuleFamily::analyze, std::string("motion") };

    if (head == "clock" || head == "pattern" || head == "fibonacci" || head == "random"
        || head == "phrase" || head == "progression" || head == "growth" || head == "swarm"
        || head == "collapse" || head == "section") {
        return std::pair { ModuleFamily::generate, std::string(head) };
    }

    if (head == "stages" || head == "lists" || head == "modulator") {
        return std::pair { ModuleFamily::shape, std::string(head) };
    }

    if (head == "quantize" || head == "split" || head == "delay" || head == "loop"
        || head == "bounce" || head == "arp" || head == "warp" || head == "filter"
        || head == "bits") {
        return std::pair { ModuleFamily::transform, std::string(head) };
    }

    if (head == "smear" || head == "cutup") {
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
            connection.from = parseEndpoint(canonicalToken.parts[1], token.location);
            connection.to = parseEndpoint(canonicalToken.parts[3], token.location);
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
    return token.parts.size() >= 2 && inferredModule(token.parts[0], token.parts).has_value();
}

bool Parser::isConnection(const LineToken& token)
{
    return !token.parts.empty() && token.parts[0] == "connect";
}

Endpoint Parser::parseEndpoint(const std::string& text, const SourceLocation& location)
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

} // namespace pulse
