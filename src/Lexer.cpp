#include "pulse/Lexer.hpp"

#include <cctype>
#include <sstream>

namespace pulse {

std::vector<LineToken> Lexer::lex(const std::string& source, std::vector<Diagnostic>& diagnostics) const
{
    std::vector<LineToken> lines;
    std::istringstream stream(source);
    std::string line;
    std::size_t lineNumber = 1;

    while (std::getline(stream, line)) {
        auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }

        auto parts = splitLine(line);
        if (!parts.empty()) {
            LineToken token;
            token.parts = std::move(parts);
            token.location = { lineNumber, 1 };
            token.raw = line;
            lines.push_back(std::move(token));
        }

        ++lineNumber;
    }

    if (lines.empty()) {
        diagnostics.push_back({
            Diagnostic::Severity::error,
            "Patch is empty.",
            { 1, 1 }
        });
    }

    return lines;
}

std::vector<std::string> Lexer::splitLine(const std::string& line)
{
    std::vector<std::string> parts;
    std::string current;
    bool inQuote = false;

    for (char ch : line) {
        if (ch == '"') {
            inQuote = !inQuote;
            current.push_back(ch);
            continue;
        }

        if (!inQuote && std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty()) {
        parts.push_back(current);
    }

    return parts;
}

} // namespace pulse
