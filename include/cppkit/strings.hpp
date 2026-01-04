#pragma once

#include <string>
#include <vector>
#include <algorithm>

namespace cppkit
{
    // Trims whitespace from both ends of the string
    std::string trim(std::string_view s);

    // Joins a vector of strings into a single string with the given separator
    std::string join(const std::vector<std::string>& list, std::string_view sep);

    // Checks if the string starts with the given prefix
    bool startsWith(std::string_view s, std::string_view prefix);

    // Checks if the string ends with the given suffix
    bool endsWith(std::string_view s, std::string_view suffix);

    // Converts the string to lowercase
    std::string toLower(std::string s);

    // Converts the string to uppercase
    std::string toUpper(std::string s);

    // Splits the string by the given delimiter into a vector of strings
    std::vector<std::string> split(std::string_view s, char delimiter);

    // Replaces all occurrences of 'from' with 'to' in the string
    std::string replaceAll(std::string s, std::string_view from, std::string_view to);

    // Replaces up to maxReplaces occurrences of 'from' with 'to' in the string
    std::string replace(std::string s, std::string_view from, std::string_view to, size_t maxReplaces);

    // Escapes HTML special characters in the string
    std::string escapeHtml(std::string_view s);

    // Unescapes HTML special characters in the string
    std::string unescapeHtml(std::string_view s);

    // URL encodes the string
    std::string urlEncode(std::string_view s, bool spaceAsPlus = true);

    // URL decodes the string
    std::string urlDecode(std::string_view s, bool spaceAsPlus = true);

    // 寻找路径分隔符并返回文件名或相对路径
    consteval const char* shortFilename(const char* path)
    {
        const char* file = path;
        while (*path)
        {
            if (*path == '/' || *path == '\\')
            {
                file = path + 1;
            }
            path++;
        }
        return file;
    }
} // namespace cppkit
