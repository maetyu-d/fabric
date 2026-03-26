#include "pulse/Parser.hpp"

#include <algorithm>

namespace pulse {

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

    const auto& first = tokens.front();
    if (first.parts.size() < 2 || first.parts[0] != "patch") {
        diagnostics.push_back({
            Diagnostic::Severity::error,
            "Patch must begin with `patch <name>`.",
            first.location
        });
        return std::nullopt;
    }

    Patch patch;
    patch.name = first.parts[1];
    patch.location = first.location;

    bool patchClosed = false;
    std::size_t index = 1;
    while (index < tokens.size()) {
        const auto& token = tokens[index];
        if (token.parts[0] == "end") {
            patchClosed = true;
            break;
        }

        if (isModuleHeader(token)) {
            Module module;
            module.family = parseModuleFamily(token.parts[0]);
            module.kind = token.parts[1];
            module.name = token.parts[2];
            module.location = token.location;
            ++index;

            bool closed = false;
            while (index < tokens.size()) {
                const auto& bodyToken = tokens[index];
                if (bodyToken.parts[0] == "end") {
                    closed = true;
                    ++index;
                    break;
                }

                Property property;
                property.key = bodyToken.parts[0];
                property.values.assign(bodyToken.parts.begin() + 1, bodyToken.parts.end());
                property.location = bodyToken.location;
                module.properties.push_back(std::move(property));
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

        if (isConnection(token)) {
            if (token.parts.size() != 4 || token.parts[2] != "->") {
                diagnostics.push_back({
                    Diagnostic::Severity::error,
                    "Connections must look like `connect a -> b`.",
                    token.location
                });
                return std::nullopt;
            }

            Connection connection;
            connection.from = parseEndpoint(token.parts[1], token.location);
            connection.to = parseEndpoint(token.parts[3], token.location);
            connection.location = token.location;
            patch.connections.push_back(std::move(connection));
            ++index;
            continue;
        }

        GlobalSetting global;
        global.key = token.parts[0];
        global.values.assign(token.parts.begin() + 1, token.parts.end());
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
    return token.parts.size() >= 3 && parseModuleFamily(token.parts[0]) != ModuleFamily::unknown;
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
