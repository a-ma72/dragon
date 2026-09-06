#pragma once

#include <climits>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

bool parse_hex_color(std::string_view hex, uint8_t &r, uint8_t &g, uint8_t &b);
std::string format_hex_color(uint8_t r, uint8_t g, uint8_t b);

// Windows COLORREF layout: 0x00BBGGRR
uint32_t pack_colorref(uint8_t r, uint8_t g, uint8_t b);
void unpack_colorref(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b);
bool hex_to_colorref(std::string_view hex, uint32_t &color);
std::string colorref_to_hex(uint32_t color);

void gif_build_row_map(int height, bool interlace, std::vector<int> &dest_rows);

bool path_has_gif_extension(std::string_view path);
bool path_has_image_extension(std::string_view path);
std::string path_filename_utf8(std::string_view path);
std::string path_parent_utf8(std::string_view path);
std::string path_join_utf8(std::string_view dir, std::string_view name);
bool path_is_absolute_utf8(std::string_view path);
std::string path_resolve_utf8(std::string_view base, std::string_view path);

int gif_frame_delay_ms(int delay_cs);

bool settings_is_v0_2(std::string_view version, bool has_legacy_shape);
bool settings_is_v0_3(std::string_view version);
bool settings_is_v0_4_or_0_5(std::string_view version);

template <typename Fn>
void bresenham_visit(int x1, int y1, int dx, int dy, Fn &&fn)
{
    const int sx = (dx >= 0) ? 1 : -1;
    const int sy = (dy >= 0) ? 1 : -1;
    dx = dx >= 0 ? dx : (dx == INT_MIN ? INT_MAX : -dx);
    dy = dy >= 0 ? dy : (dy == INT_MIN ? INT_MAX : -dy);
    int err = dx - dy;
    int n = (dx > dy ? dx : dy) + 1;

    while (n--)
    {
        fn(x1, y1);
        const int e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y1 += sy;
        }
    }
}
