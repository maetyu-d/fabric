#pragma once

#include "pulse/Types.hpp"

#include <string>
#include <vector>

namespace pulse {

struct LineToken {
    std::vector<std::string> parts;
    SourceLocation location {};
    std::string raw;
};

class Lexer {
public:
    [[nodiscard]] std::vector<LineToken> lex(const std::string& source, std::vector<Diagnostic>& diagnostics) const;

private:
    [[nodiscard]] static std::vector<std::string> splitLine(const std::string& line);
};

} // namespace pulse
