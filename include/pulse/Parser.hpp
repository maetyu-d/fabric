#pragma once

#include "pulse/Ast.hpp"
#include "pulse/Lexer.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pulse {

class Parser {
public:
    [[nodiscard]] std::optional<Patch> parse(const std::string& source, std::vector<Diagnostic>& diagnostics) const;

private:
    [[nodiscard]] static bool isModuleHeader(const LineToken& token);
    [[nodiscard]] static bool isConnection(const LineToken& token);
    [[nodiscard]] static Endpoint parseEndpoint(const std::string& text, const SourceLocation& location);
};

} // namespace pulse
