#include "dragon_util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>

bool parse_hex_color(std::string_view hex, uint8_t &r, uint8_t &g, uint8_t &b)
{
    static const std::regex hexColorRegex("^#([0-9A-Fa-f]{6})$");
    const std::string s(hex);

    if (!std::regex_match(s, hexColorRegex))
    {
        return false;
    }

    r = static_cast<uint8_t>(std::stoi(s.substr(1, 2), nullptr, 16));
    g = static_cast<uint8_t>(std::stoi(s.substr(3, 2), nullptr, 16));
    b = static_cast<uint8_t>(std::stoi(s.substr(5, 2), nullptr, 16));
    return true;
}

std::string format_hex_color(uint8_t r, uint8_t g, uint8_t b)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return buf;
}

void gif_build_row_map(int height, bool interlace, std::vector<int> &dest_rows)
{
    dest_rows.resize(height > 0 ? height : 0);
    for (int i = 0; i < height; i++)
    {
        dest_rows[i] = i;
    }
    if (!interlace || height <= 0)
    {
        return;
    }

    int n = 0;
    auto add_pass = [&](int start, int step)
    {
        for (int y = start; y < height && n < height; y += step)
        {
            dest_rows[n++] = y;
        }
    };
    add_pass(0, 8);
    add_pass(4, 8);
    add_pass(2, 4);
    add_pass(1, 2);
}

static std::string lower_copy(std::string_view path)
{
    std::string buffer(path);
    std::transform(buffer.begin(), buffer.end(), buffer.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return buffer;
}

bool path_has_gif_extension(std::string_view path)
{
    return lower_copy(path).ends_with(".gif");
}

bool path_has_image_extension(std::string_view path)
{
    const std::string buffer = lower_copy(path);
    return buffer.ends_with(".jpg") ||
           buffer.ends_with(".jpeg") ||
           buffer.ends_with(".gif") ||
           buffer.ends_with(".bmp") ||
           buffer.ends_with(".png") ||
           buffer.ends_with(".svg") ||
           buffer.ends_with(".webp") ||
           buffer.ends_with(".tif") ||
           buffer.ends_with(".tiff");
}

static std::string info_version(const nlohmann::json &j)
{
    if (!j.contains("info") || !j["info"].contains("version") || !j["info"]["version"].is_string())
    {
        return {};
    }
    return j["info"]["version"].get<std::string>();
}

bool settings_is_v0_2_document(const nlohmann::json &j)
{
    const std::string ver = info_version(j);
    if (!ver.empty())
    {
        return ver == "0.2";
    }
    return j.contains("textPos") || j.contains("logoPos") || j.contains("logo_filename");
}

bool settings_is_v0_3_document(const nlohmann::json &j)
{
    return info_version(j) == "0.3";
}

bool settings_is_v0_4_or_0_5_document(const nlohmann::json &j)
{
    const std::string ver = info_version(j);
    return ver == "0.4" || ver == "0.5";
}
