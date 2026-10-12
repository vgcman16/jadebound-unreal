#pragma once

#include <string>

namespace Jadebound::Development
{
struct ResumeOption
{
    bool Valid = true;
    bool Present = false;
    std::string Token;
};

// One bounded login option. Empty, duplicate and case-variant duplicates must
// not turn an attempted resume into a new-character allocation.
inline ResumeOption ParseResumeOption(const std::string& options)
{
    ResumeOption result;
    if (options.size() > 4096 || options.find('\0') != std::string::npos)
    { result.Valid = false; return result; }
    std::size_t start = 0;
    while (start < options.size())
    {
        const auto end = options.find('?', start);
        const auto part = options.substr(start, end == std::string::npos ? end : end - start);
        const auto equals = part.find('=');
        auto key = part.substr(0, equals);
        for (auto& ch : key) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
        if (key == "jadeboundresume")
        {
            if (result.Present || equals == std::string::npos || equals + 1 == part.size())
            { result.Valid = false; return result; }
            result.Present = true;
            result.Token = part.substr(equals + 1);
            if (result.Token.size() != 32) { result.Valid = false; return result; }
            bool nonzero = false;
            for (const auto ch : result.Token)
            {
                if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
                { result.Valid = false; return result; }
                nonzero |= ch != '0';
            }
            if (!nonzero) { result.Valid = false; return result; }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}
}
