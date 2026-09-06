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

uint32_t pack_colorref(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
}

void unpack_colorref(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b)
{
    r = (uint8_t)(color & 0xFFu);
    g = (uint8_t)((color >> 8) & 0xFFu);
    b = (uint8_t)((color >> 16) & 0xFFu);
}

bool hex_to_colorref(std::string_view hex, uint32_t &color)
{
    uint8_t r = 0, g = 0, b = 0;
    if (!parse_hex_color(hex, r, g, b))
    {
        return false;
    }
    color = pack_colorref(r, g, b);
    return true;
}

std::string colorref_to_hex(uint32_t color)
{
    uint8_t r = 0, g = 0, b = 0;
    unpack_colorref(color, r, g, b);
    return format_hex_color(r, g, b);
}

void gif_build_row_map(int height, bool interlace, std::vector<int> &dest_rows)
{
    if (height < 0) height = 0;
    dest_rows.resize((size_t)height);
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

int gif_frame_delay_ms(int delay_cs)
{
    const int ms = delay_cs * 10;
    return (ms < 20) ? 100 : ms;
}

bool path_has_gif_extension(std::string_view path)
{
    return lower_copy(path).ends_with(".gif");
}

static size_t path_last_separator(std::string_view path)
{
    const size_t slash = path.find_last_of('/');
    const size_t bslash = path.find_last_of('\\');
    if (slash == std::string_view::npos) return bslash;
    if (bslash == std::string_view::npos) return slash;
    return slash > bslash ? slash : bslash;
}

std::string path_filename_utf8(std::string_view path)
{
    const size_t sep = path_last_separator(path);
    if (sep == std::string_view::npos) return std::string(path);
    return std::string(path.substr(sep + 1));
}

std::string path_parent_utf8(std::string_view path)
{
    const size_t sep = path_last_separator(path);
    if (sep == std::string_view::npos) return {};
    if (sep == 0) return std::string(path.substr(0, 1));
    return std::string(path.substr(0, sep));
}

std::string path_join_utf8(std::string_view dir, std::string_view name)
{
    if (dir.empty()) return std::string(name);
    if (name.empty()) return std::string(dir);
    const char last = dir.back();
    if (last == '/' || last == '\\')
    {
        return std::string(dir) + std::string(name);
    }
    return std::string(dir) + '\\' + std::string(name);
}

bool path_is_absolute_utf8(std::string_view path)
{
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return true;
    if (path.size() >= 2 && path[1] == ':' &&
        ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')))
    {
        return true;
    }
    return false;
}

std::string path_resolve_utf8(std::string_view base, std::string_view path)
{
    if (path.empty()) return {};
    if (path_is_absolute_utf8(path)) return std::string(path);
    return path_join_utf8(base, path);
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

bool settings_is_v0_2(std::string_view version, bool has_legacy_shape)
{
    if (!version.empty())
    {
        return version == "0.2";
    }
    return has_legacy_shape;
}

bool settings_is_v0_3(std::string_view version)
{
    return version == "0.3";
}

bool settings_is_v0_4_or_0_5(std::string_view version)
{
    return version == "0.4" || version == "0.5";
}
