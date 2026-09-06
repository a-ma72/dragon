#include "dragon_util.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

static int g_failed = 0;

#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

static void test_hex_color_round_trip()
{
    uint8_t r = 0, g = 0, b = 0;

    EXPECT(parse_hex_color("#FF0000", r, g, b) && r == 255 && g == 0 && b == 0);
    EXPECT(format_hex_color(255, 0, 0) == "#FF0000");

    EXPECT(parse_hex_color("#00FF00", r, g, b) && r == 0 && g == 255 && b == 0);
    EXPECT(format_hex_color(0, 255, 0) == "#00FF00");

    EXPECT(parse_hex_color("#0000FF", r, g, b) && r == 0 && g == 0 && b == 255);
    EXPECT(format_hex_color(0, 0, 255) == "#0000FF");

    EXPECT(parse_hex_color("#7092BE", r, g, b) && r == 0x70 && g == 0x92 && b == 0xBE);
    EXPECT(format_hex_color(0x70, 0x92, 0xBE) == "#7092BE");

    EXPECT(!parse_hex_color("FF0000", r, g, b));
    EXPECT(!parse_hex_color("#FFF", r, g, b));
    EXPECT(!parse_hex_color("#GG0000", r, g, b));

    uint32_t color = 0;
    EXPECT(hex_to_colorref("#FF0000", color));
    EXPECT((color & 0xFFu) == 255);
    EXPECT(((color >> 8) & 0xFFu) == 0);
    EXPECT(((color >> 16) & 0xFFu) == 0);
    EXPECT(colorref_to_hex(color) == "#FF0000");

    EXPECT(hex_to_colorref("#0000FF", color));
    EXPECT((color & 0xFFu) == 0);
    EXPECT(((color >> 16) & 0xFFu) == 255);
    EXPECT(colorref_to_hex(color) == "#0000FF");

    EXPECT(hex_to_colorref("#7092BE", color));
    EXPECT(colorref_to_hex(color) == "#7092BE");
    EXPECT(colorref_to_hex(pack_colorref(255, 0, 0)) == "#FF0000");
    EXPECT(!hex_to_colorref("#GG0000", color));
}

static void test_bresenham()
{
    std::vector<std::pair<int, int>> down;
    bresenham_visit(0, 0, 100, 100, [&](int x, int y) { down.emplace_back(x, y); });
    EXPECT(down.size() == 101);
    EXPECT(down.front() == std::make_pair(0, 0));
    EXPECT(down.back() == std::make_pair(100, 100));

    std::vector<std::pair<int, int>> up;
    bresenham_visit(0, 100, 100, -100, [&](int x, int y) { up.emplace_back(x, y); });
    EXPECT(up.size() == 101);
    EXPECT(up.front() == std::make_pair(0, 100));
    EXPECT(up.back() == std::make_pair(100, 0));

    bool y_changed = false;
    for (size_t i = 1; i < up.size(); i++)
    {
        if (up[i].second != up[0].second)
        {
            y_changed = true;
            break;
        }
    }
    EXPECT(y_changed);

    std::vector<std::pair<int, int>> dot;
    bresenham_visit(3, 4, 0, 0, [&](int x, int y) { dot.emplace_back(x, y); });
    EXPECT(dot.size() == 1);
    EXPECT(dot[0] == std::make_pair(3, 4));
}

static void test_gif_row_map()
{
    std::vector<int> sequential;
    gif_build_row_map(8, false, sequential);
    EXPECT(sequential.size() == 8);
    for (int i = 0; i < 8; i++)
    {
        EXPECT(sequential[i] == i);
    }

    std::vector<int> interlaced;
    gif_build_row_map(8, true, interlaced);
    EXPECT(interlaced.size() == 8);
    const int expected[8] = {0, 4, 2, 6, 1, 3, 5, 7};
    for (int i = 0; i < 8; i++)
    {
        EXPECT(interlaced[i] == expected[i]);
    }

    std::vector<int> empty;
    gif_build_row_map(0, true, empty);
    EXPECT(empty.empty());

    std::vector<int> odd;
    gif_build_row_map(5, true, odd);
    EXPECT(odd.size() == 5);
    const int expected_odd[5] = {0, 4, 2, 1, 3};
    for (int i = 0; i < 5; i++)
    {
        EXPECT(odd[i] == expected_odd[i]);
    }

    std::vector<int> negative;
    gif_build_row_map(-3, true, negative);
    EXPECT(negative.empty());
}

static void test_gif_frame_delay()
{
    EXPECT(gif_frame_delay_ms(0) == 100);
    EXPECT(gif_frame_delay_ms(1) == 100);
    EXPECT(gif_frame_delay_ms(2) == 20);
    EXPECT(gif_frame_delay_ms(10) == 100);
}

static void test_image_extensions()
{
    EXPECT(path_has_gif_extension("anim.GIF"));
    EXPECT(path_has_image_extension("logo.jpeg"));
    EXPECT(path_has_image_extension("logo.WEBP"));
    EXPECT(path_has_image_extension("scan.tif"));
    EXPECT(path_has_image_extension("scan.tiff"));
    EXPECT(!path_has_image_extension("notes.txt"));
    EXPECT(!path_has_gif_extension("logo.png"));

    EXPECT(path_filename_utf8("C:\\Users\\x\\logo.png") == "logo.png");
    EXPECT(path_parent_utf8("C:\\Users\\x\\logo.png") == "C:\\Users\\x");
    EXPECT(path_join_utf8("C:\\Users\\x", "logo.png") == "C:\\Users\\x\\logo.png");
    EXPECT(path_filename_utf8("logo.png") == "logo.png");
    EXPECT(path_parent_utf8("logo.png").empty());
    EXPECT(path_join_utf8("C:\\Users\\x\\", "logo.png") == "C:\\Users\\x\\logo.png");
    EXPECT(path_filename_utf8("C:/Users/x/logo.png") == "logo.png");

    EXPECT(path_is_absolute_utf8("C:\\Users\\x\\logo.png"));
    EXPECT(path_is_absolute_utf8("\\\\server\\share\\logo.png"));
    EXPECT(path_is_absolute_utf8("/tmp/logo.png"));
    EXPECT(!path_is_absolute_utf8("logo.png"));
    EXPECT(!path_is_absolute_utf8("images/logo.png"));
    EXPECT(path_resolve_utf8("C:\\app", "logo.png") == "C:\\app\\logo.png");
    EXPECT(path_resolve_utf8("C:\\app", "D:\\pic.png") == "D:\\pic.png");
}

static void test_settings_version_dispatch()
{
    EXPECT(settings_is_v0_4_or_0_5("0.5"));
    EXPECT(settings_is_v0_4_or_0_5("0.4"));
    EXPECT(!settings_is_v0_4_or_0_5("0.3"));

    EXPECT(settings_is_v0_3("0.3"));
    EXPECT(!settings_is_v0_3("0.5"));

    EXPECT(settings_is_v0_2("0.2", false));
    EXPECT(settings_is_v0_2("", true));
    EXPECT(!settings_is_v0_2("0.6", false));
    EXPECT(!settings_is_v0_2("0.6", true));
    EXPECT(!settings_is_v0_2("", false));
}

int main()
{
    test_hex_color_round_trip();
    test_bresenham();
    test_gif_row_map();
    test_gif_frame_delay();
    test_image_extensions();
    test_settings_version_dispatch();

    if (g_failed)
    {
        std::fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }

    std::printf("All tests passed\n");
    return 0;
}
