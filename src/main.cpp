// 2025, A. Martin

#define _USE_MATH_DEFINES
#include <cmath>
#include <filesystem>
#include <string_view>
#include <fstream>

#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <random>
#include <regex>
#include <limits>
#include <windows.h>
#include "json.hpp"
#include "gif_lib.h"

extern "C" const char *version = "0.4";
extern "C" const char *signature = "Dragon Signature";

using std::string;
using std::vector;
using std::filesystem::path;
using nlohmann::json, nlohmann::ordered_json;


// Random numbers
std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<int> dist(0, RAND_MAX);


// prototypes
struct AppContext;

class ScreenObject;
class LineObject;
class Signature;
class Image;
class AnimatedGif;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]);
SDL_AppResult SDL_AppIterate(void *appstate);
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event* event);
void SDL_AppQuit(void* appstate, SDL_AppResult result);
SDL_AppResult app_init_failed();

void update_screen_metrics(AppContext* app);
void update_layout_mode(AppContext* app);
void init_screen_objects(AppContext* app, json &objects);
void free_screen_objects(AppContext* app);
void draw_line_bresenham(int x1, int y1, int dx, int dy, int dash_len, int gap_len, int dash_offset, void *color, SDL_Surface* surface);
void draw(AppContext* app);
bool color_from_key(int key, COLORREF &color);
string int_to_hex_color(COLORREF color);
COLORREF get_color_value(const json& j, const string& key, COLORREF default_value);
COLORREF hex_color_to_int(const string& hex);
bool screen_objects_add_lines(AppContext *app);
bool screen_objects_add_text(float x, float y, const char* text, AppContext *app);
bool screen_objects_add_image(float x, float y, const char *full_path_name, AppContext *app);
void clipboard_insert(AppContext *app);
double round_to_precision(double value, int decimals);
void settings_write(AppContext* app);
bool settings_read_v0_2(AppContext* app, json &j, json &objects);
bool settings_read_v0_3(AppContext* app, json &j, json &objects);
bool settings_read(AppContext* app, json &objects);


#define UPDATE_VIEW_CHANGED 1
#define UPDATE_SETTINGS_CHANGED 2

#define BLENDED_ALPHA_FLOAT(img_alpha, glob_alpha) SDL_min(1.f, (float)img_alpha * 0.5f + (float)glob_alpha * 0.8f + 0.1f)
#define BLENDED_ALPHA_INT(img_alpha, glob_alpha) SDL_min(255, (int)(BLENDED_ALPHA_FLOAT(img_alpha, glob_alpha) * 255.f))


struct AppContext
{
    path base_path;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_AppResult app_quit = SDL_APP_CONTINUE;
    SDL_Cursor *handCursor = nullptr;
    Uint64 idle_ticks = 0;

    HWND hwnd = nullptr;
    SDL_Rect screen_rect_init = {-1, -1, -1, -1};  // To initialize `screen_rect` (editable in settimgs file)
    SDL_Rect screen_rect = {0, 0, 800, 600};  // To initialize `lines_area`
    SDL_Rect work_area = {0};  // Physical overlay window position
    int crop_bottom = 0;  // Crops work_area
    float center_x = 400, center_y = 300;

    bool hidden = false;
    float alpha = 0.55f;
    bool layout_mode = false;
    bool is_virgin = true;
    int idle_delay_ms = 600;
    bool needs_redraw = true;

    // Mouse capturing and dragging (screen objects)
    vector<ScreenObject*> screen_objects;
    ScreenObject *mouse_capture = nullptr;
    SDL_FPoint dragging_origin = {0}, dragging_offset = {0};

    // Text (initial) properties
    string text_file_name = "signature.txt";
    string text_content;
    string text_font_name = "Freeman-Regular.TTF";
    int text_font_size = 78;
    COLORREF text_font_color = RGB(112, 146, 190);
    float text_scale = 0.4f;
    float text_rotate = 0.f;
    float text_alpha = 1.f;

    // Logo (initial) properties
    string logo_file_name = "dragon.png";
    float logo_scale = 0.2f;
    float logo_alpha = 1.f;

    // Animated GIF
    bool have_animations = false;
};


class ScreenObject
{
public:
    SDL_FPoint pos = {0, 0};
    SDL_Rect extent = {0, 0, 0, 0};
    float scale = 1.f;
    float rotate = 0.f;
    float alpha = 1.f;
    bool deleted = false;

    ScreenObject(float x, float y)
    : pos(x, y)
    {};

    virtual ~ScreenObject() = default;
    [[nodiscard]] virtual const char* type_name() const = 0;
    [[nodiscard]] virtual json to_json() const = 0;
    [[nodiscard]] virtual bool valid() const = 0;
    [[nodiscard]] virtual bool hit_test(SDL_FPoint pt) const {return false;}
    virtual bool handle_event(const SDL_Event* event, int &needs_update, AppContext *app) = 0;
    virtual void draw(const SDL_FPoint &pt, float alpha, const SDL_Renderer* renderer) const = 0;

    [[nodiscard]]
    virtual bool hit_test_at_cursor() const
    {
        SDL_FPoint pt;

        SDL_GetGlobalMouseState(&pt.x, &pt.y);

        return hit_test(pt);
    }

    static void rotate_point(SDL_FPoint ct, SDL_FPoint* pt, double phi_deg)
    {
        double phi = phi_deg * M_PI / 180;
        double dx = pt->x - ct.x;
        double dy = pt->y - ct.y;
        double cphi = cos(phi);
        double sphi = sin(phi);

        pt->x = (float)((double)ct.x + dx * cphi - dy * sphi);
        pt->y = (float)((double)ct.y + dx * sphi + dy * cphi);
    }

    static void render_transformed_texture(
        SDL_Texture *texture,
        float x, float y, float w, float h,
        float scale,
        float rotate,
        bool flip_x, bool flip_y,
        float alpha,
        const SDL_Renderer *renderer)
    {
        if (!renderer) return;

        SDL_Vertex verts[4];
        SDL_FPoint texcoords[4] = {
            {0.f, 0.f}, {1.f, 0.f},
            {0.f, 1.f}, {1.f, 1.f}
        };

        // Apply flipping to texcoords
        if (flip_x) {
            float tmp = texcoords[0].x;
            texcoords[0].x = texcoords[1].x;
            texcoords[1].x = tmp;
            tmp = texcoords[2].x;
            texcoords[2].x = texcoords[3].x;
            texcoords[3].x = tmp;
        }
        if (flip_y) {
            float tmp = texcoords[0].y;
            texcoords[0].y = texcoords[2].y;
            texcoords[2].y = tmp;
            tmp = texcoords[1].y;
            texcoords[1].y = texcoords[3].y;
            texcoords[3].y = tmp;
        }

        // Center point
        float cx = x + w / 2.f;
        float cy = y + h / 2.f;

        // Apply scaling
        float hw = (w / 2.f) * scale;
        float hh = (h / 2.f) * scale;

        SDL_FPoint corners[4] = {
            {-hw, -hh}, {+hw, -hh},
            {-hw, +hh}, {+hw, +hh}
        };

        // Rotation
        float angle_rad = rotate * (float)M_PI / 180.f;
        float cos_a = cosf(angle_rad);
        float sin_a = sinf(angle_rad);

        for (int i = 0; i < 4; i++) {
            float rx = corners[i].x * cos_a - corners[i].y * sin_a;
            float ry = corners[i].x * sin_a + corners[i].y * cos_a;

            verts[i].position.x = cx + rx;
            verts[i].position.y = cy + ry;
            verts[i].color = {1.0, 1.0, 1.0, alpha};
            verts[i].tex_coord = texcoords[i];
        }

        int indices[6] = {0, 1, 2, 1, 3, 2};
        SDL_RenderGeometry(
                const_cast<SDL_Renderer*>(renderer),
                texture,
                verts,
                4,
                indices,
                6);
    }
};


class LineObject : public ScreenObject
{

public:
    int width;
    COLORREF color;
    bool dashed;
    int dashed_len;
    int dashed_gap;
    float line_angle;
    float line_spacing;
    SDL_Rect work_area;
    const Uint64 &idle_ticks;

    LineObject(
        const SDL_Rect &work_area,
        const Uint64 &idle_ticks,
        int width = 1,
        COLORREF color = 0,
        bool dashed = true,
        int dash_len = 10,
        int dash_gap = 10,
        float line_angle = 45.f,
        float line_spacing = 15.f)
        : ScreenObject(0, 0),
        width(width),
        color(color),
        dashed(dashed),
        dashed_len(dash_len),
        dashed_gap(dash_gap),
        line_angle(line_angle),
        line_spacing(line_spacing),
        work_area(work_area),
        idle_ticks(idle_ticks)
        {}

    [[nodiscard]]
    const char* type_name() const override { return "Lines"; }

    [[nodiscard]]
    json to_json() const override {
        return json{
            {"type", type_name()},
            {"alpha", 1.f},
            {"width", width},
            {"color", int_to_hex_color(color)},
            {"dashed", dashed},
            {"dashed_len", dashed_len},
            {"dashed_gap", dashed_gap},
            {"line_angle", round_to_precision(line_angle, 4)},
            {"line_spacing", round_to_precision(line_spacing, 1)}
        };
    }

    [[nodiscard]]
    bool valid() const override { return true; }

    [[nodiscard]]
    bool hit_test(SDL_FPoint pt) const override {
        // The line object is not selectable
        return false;
    }

    bool handle_event(const SDL_Event* event, int& needs_update, AppContext* app) override {
        if (!app->layout_mode) return false;

        if (event->type == SDL_EVENT_MOUSE_WHEEL)
        {
            if (SDL_GetModState() & SDL_KMOD_SHIFT)
            {
                // Adjust spacing
                float scale_factor = (event->wheel.y > 0) ? 1.1f : 0.9f;
                line_spacing *= scale_factor;
                line_spacing = SDL_max(2.f, line_spacing);
                line_spacing = SDL_min(50.f, line_spacing);
                needs_update = UPDATE_SETTINGS_CHANGED;
                app->is_virgin = false;
                return true;
            }
            else if (SDL_GetModState() & SDL_KMOD_CTRL)
            {
                // Adjust orientation
                float angle_delta = (event->wheel.y > 0) ? 5.f : -5.f; // degrees
                line_angle += angle_delta;
                needs_update = UPDATE_SETTINGS_CHANGED;
                app->is_virgin = false;
                return true;
            }
        }
        else if (event->type == SDL_EVENT_KEY_DOWN)
        {
            COLORREF c;
            if (color_from_key((int)event->key.key, c))
            {
                this->color = c;
                needs_update = UPDATE_SETTINGS_CHANGED;
                app->is_virgin = false;
                return true;
            }
            else if (event->key.key >= SDLK_0 && event->key.key <= SDLK_5)
            {
                this->width = (int)(event->key.key - SDLK_0);
                needs_update = UPDATE_SETTINGS_CHANGED;
                app->is_virgin = false;
                return true;
            }
            else if (event->key.key == SDLK_D)
            {
                this->dashed = !this->dashed;
                needs_update = UPDATE_SETTINGS_CHANGED;
                app->is_virgin = false;
                return true;
            }
        }
        return false;
    }

    void draw(const SDL_FPoint& pt, float global_alpha, const SDL_Renderer* renderer) const override
    {
        if (this->width == 0) return;
        if (!valid() || !renderer) return;

        int wa_width = work_area.w;
        int wa_height = work_area.h;

        float angle_rad = line_angle * (float)M_PI / 180.f;
        float sa = sinf(angle_rad);
        float ca = cosf(angle_rad);

        // Line equation: -sa*x + ca*y = C
        float c00 = 0;
        float c10 = -sa * (float)wa_width;
        float c01 = ca * (float)wa_height;
        float c11 = -sa * (float)wa_width + ca * (float)wa_height;
        float c_min = (std::min)({c00, c10, c01, c11});
        float c_max = (std::max)({c00, c10, c01, c11});

        if (this->width > 1 && !this->dashed)
        {
            SDL_Color rgba = {
                GetRValue(color),
                GetGValue(color),
                GetBValue(color),
                SDL_min((Uint8)255, (Uint8)(global_alpha * 255.f))
            };
            SDL_FColor frgba = {
                rgba.r / 255.f,
                rgba.g / 255.f,
                rgba.b / 255.f,
                rgba.a / 255.f
            };

            for (float c = c_min; c < c_max; c += line_spacing)
            {
                // find intersections with screen boundaries
                vector<SDL_Point> intersections;

                if (sa != 0)
                {
                    float x = -c / sa;
                    if (x >= 0 && x <= (float)wa_width) intersections.push_back({(int)x, 0});
                }
                if (sa != 0)
                {
                    float x = ((float)wa_height * ca - c) / sa;
                    if (x >= 0 && x <= (float)wa_width) intersections.push_back({(int)x, wa_height});
                }
                if (ca != 0)
                {
                    float y = c / ca;
                    if (y >= 0 && y <= (float)wa_height) intersections.push_back({0, (int)y});
                }
                if (ca != 0)
                {
                    float y = (c + (float)wa_width * sa) / ca;
                    if (y >= 0 && y <= (float)wa_height) intersections.push_back({wa_width, (int)y});
                }

                if (intersections.size() >= 2)
                {
                    std::sort(
                            intersections.begin(),
                            intersections.end(),
                            [](const SDL_Point& a, const SDL_Point& b)
                            {
                                if (a.x != b.x) return a.x < b.x;
                                return a.y < b.y;
                            }
                    );
                    intersections.erase(
                            std::unique(
                                    intersections.begin(),
                                    intersections.end(),
                                    [](const SDL_Point& a, const SDL_Point& b)
                                    {
                                        return a.x == b.x && a.y == b.y;
                                    }
                            ),
                            intersections.end());

                    if (intersections.size() >= 2)
                    {
                        SDL_Point p1 = intersections[0];
                        SDL_Point p2 = intersections[1];

                        auto dx = (float)(p2.x - p1.x);
                        auto dy = (float)(p2.y - p1.y);
                        float len = sqrtf(dx*dx + dy*dy);
                        if (len == 0) continue;
                        float nx = -dy / len;
                        float ny = dx / len;
                        float w = (float)width / 2.f;

                        SDL_Vertex verts[4];
                        verts[0].position = { (float)p1.x + nx * w, (float)p1.y + ny * w };
                        verts[0].color = frgba;
                        verts[0].tex_coord = { 0, 0 };
                        verts[1].position = { (float)p1.x - nx * w, (float)p1.y - ny * w };
                        verts[1].color = frgba;
                        verts[1].tex_coord = { 0, 0 };
                        verts[2].position = { (float)p2.x + nx * w, (float)p2.y + ny * w };
                        verts[2].color = frgba;
                        verts[2].tex_coord = { 0, 0 };
                        verts[3].position = { (float)p2.x - nx * w, (float)p2.y - ny * w };
                        verts[3].color = frgba;
                        verts[3].tex_coord = { 0, 0 };

                        int indices[] = { 0, 1, 2, 1, 3, 2 };
                        SDL_RenderGeometry(
                            const_cast<SDL_Renderer *>(renderer),
                            nullptr,
                            verts,
                            4,
                            indices,
                            6);
                    }
                }
            }
        }
        else
        {
            int gap_len = this->dashed ? this->dashed_gap : 0;
            int dash_len = this->dashed_len;
            int quarter_dash_len = (dash_len + 2) / 4;
            Uint32 pixel;
            SDL_Surface *surface;
            SDL_Texture *texture;

            gen.seed((unsigned)idle_ticks);
            surface = SDL_CreateSurface(wa_width, wa_height, SDL_PIXELFORMAT_RGBA8888);
            SDL_ClearSurface(surface, 0, 0, 0, 0);

            pixel = SDL_MapSurfaceRGBA(
                        surface,
                        GetRValue(this->color),
                        GetGValue(this->color),
                        GetBValue(this->color),
                        SDL_min((Uint8)255, (Uint8)(global_alpha * 255.f)));

            SDL_LockSurface(surface);

            for (float c = c_min; c < c_max; c += line_spacing)
            {
                int dash_offset = this->dashed ? dist(gen) % (dash_len + gap_len) : 0;
                vector<SDL_Point> intersections;

                if (sa != 0)
                {
                    float x = -c / sa;
                    if (x >= 0 && x <= (float)wa_width) intersections.push_back({(int)x, 0});
                }
                if (sa != 0)
                {
                    float x = ((float)wa_height * ca - c) / sa;
                    if (x >= 0 && x <= (float)wa_width) intersections.push_back({(int)x, wa_height});
                }
                if (ca != 0)
                {
                    float y = c / ca;
                    if (y >= 0 && y <= (float)wa_height) intersections.push_back({0, (int)y});
                }
                if (ca != 0)
                {
                    float y = (c + (float)wa_width * sa) / ca;
                    if (y >= 0 && y <= (float)wa_height) intersections.push_back({wa_width, (int)y});
                }

                if (intersections.size() >= 2)
                {
                    std::sort(
                            intersections.begin(),
                            intersections.end(),
                            [](const SDL_Point& a, const SDL_Point& b)
                            {
                                if (a.x != b.x) return a.x < b.x;
                                return a.y < b.y;
                            }
                    );
                    intersections.erase(
                            std::unique(intersections.begin(),
                                        intersections.end(),
                                        [](const SDL_Point& a, const SDL_Point& b)
                                        {
                                            return a.x == b.x && a.y == b.y;
                                        }
                            ),
                            intersections.end());

                    if (intersections.size() >= 2)
                    {
                        SDL_Point p1 = intersections[0];
                        SDL_Point p2 = intersections[1];
                        int dx = p2.x - p1.x;
                        int dy = p2.y - p1.y;
                        int jitter = 0;

                        if (abs(dx) > abs(dy))
                        { // more horizontal
                            for (int d = -(this->width - 1) / 2; d <= this->width / 2; d++)
                            {
                                if (this->dashed)
                                {
                                    jitter = (dist(gen) % max(4, quarter_dash_len)) - quarter_dash_len / 2;
                                }
                                draw_line_bresenham(
                                    p1.x, p1.y + d,
                                    dx, dy,
                                    dash_len, gap_len, dash_offset + jitter,
                                    &pixel,
                                    surface);
                            }
                        }
                        else
                        { // more vertical
                            for (int d = -(this->width - 1) / 2; d <= this->width / 2; d++)
                            {
                                if (this->dashed)
                                {
                                    jitter = (dist(gen) % max(4, quarter_dash_len)) - quarter_dash_len / 2;
                                }
                                draw_line_bresenham(
                                    p1.x + d, p1.y,
                                    dx, dy,
                                    dash_len, gap_len,
                                    dash_offset + jitter,
                                    &pixel, surface);
                            }
                        }
                    }
                }
            }

            SDL_UnlockSurface(surface);

            texture = SDL_CreateTextureFromSurface(const_cast<SDL_Renderer *>(renderer), surface);
            SDL_ClearSurface(surface, 0, 0, 0, 0);
            SDL_DestroySurface(surface);

            if (texture)
            {
                SDL_FRect rect = {0, 0, (float)wa_width, (float)wa_height};
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                SDL_RenderTexture(const_cast<SDL_Renderer *>(renderer), texture, nullptr, &rect);
                SDL_DestroyTexture(texture);
            }
        }
    }
};


class Signature : public ScreenObject
{
public:
    string text;

    string font_name;
    float font_size;
    COLORREF font_color;
    const SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Surface *surface;

    Signature(
        const string &signature,
        float x,
        float y,
        const string &font_name,
        float font_size,
        COLORREF font_color,
        const path &font_path,
        float scale_by,
        float rotate_by,
        float alpha,
        const SDL_Renderer *renderer)
        : ScreenObject(x, y),
          texture(nullptr),
          surface(nullptr),
          renderer(renderer)
    {
        init(signature, x, y, font_name, font_size, font_color, font_path, scale_by, rotate_by, alpha);
    }

    Signature(json &j, path &font_path, const SDL_Renderer *renderer)
    : ScreenObject(-1, -1),
      texture(nullptr),
      surface(nullptr),
      renderer(renderer)
    {
        try
        {
            init(
                j.value("text", "Example"),
                j["x"], j["y"],
                j.value("font_name", "Freeman-Regular.TTF"),
                j.value("font_size", 80.f),
                get_color_value(j, "font_color", 0xffffff),
                font_path,
                j.value("scale", 1.f),
                j.value("rotate", 0.f),
                j.value("alpha", 1.f));
        }
        catch (const std::exception &e)
        {
            SDL_Log("Error creating signature: %s", e.what());
        }
    }

    [[nodiscard]]
    const char* type_name() const override {return "Signature";}

    ~Signature() override
    {
        SDL_DestroyTexture(texture);
        texture = nullptr;
        SDL_DestroySurface(surface);
        surface = nullptr;
    }

protected:
    void init(
        const string &signature,
        float x,
        float y,
        const string &font_name,
        float font_size,
        COLORREF font_color,
        const path &font_path,
        float scale_by,
        float rotate_by,
        float alpha)
    {
        TTF_Font* font = nullptr;
        const SDL_PixelFormatDetails *format_details;

        // TTF_RenderText_Blended() creates an ARGB surface
        format_details = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888);

        pos.x = x;
        pos.y = y;
        scale = scale_by;
        rotate = rotate_by;
        text = signature;
        this->font_name = font_name;
        this->font_size = font_size;
        this->font_color = font_color;
        this->alpha = alpha;

        for (;;)
        {
            auto font_fullpath = font_path / font_name;
            if (!renderer) break;

            font = TTF_OpenFont(font_fullpath.string().c_str(), font_size);
            if (!font) break;

            // render the font to a surface
            // ()
            surface = TTF_RenderText_Blended(
                font,
                signature.c_str(), signature.length(),
                SDL_Color(
                        GetBValue(font_color),
                        GetGValue(font_color),
                        GetRValue(font_color),
                        (int)(alpha * 255.f))
            );
            if (!surface) break;

            // make a texture from the surface
            texture = SDL_CreateTextureFromSurface(const_cast<SDL_Renderer *>(renderer), surface);
            if (!texture) break;

            // get the on-screen dimensions of the text. this is necessary for rendering it
            auto props = SDL_GetTextureProperties(texture);
            if (!props) break;
            extent = {
                .w = (int)SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_WIDTH_NUMBER, 0),
                .h = (int)SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_HEIGHT_NUMBER, 0)
            };
            extent.x = extent.w / 2;
            extent.y = extent.h / 2;
            break;
        }

        // we no longer need the font or the surface, so we can destroy those now.
        TTF_CloseFont(font);
    }

public:
    [[nodiscard]]
    json to_json() const override
    {
        if (!valid()) return json::object();

        return json( {
             {"x", (int)pos.x},
             {"y", (int)pos.y},
             {"text", text},
             {"scale", round_to_precision(scale, 4)},
             {"rotate", round_to_precision(rotate, 4)},
             {"alpha", round_to_precision(alpha, 2)},
             {"font_name", font_name},
             {"font_size", round_to_precision(font_size, 1)},
             {"font_color", int_to_hex_color(font_color)},
             {"type", type_name()}
        } );
    }

    [[nodiscard]]
    bool valid() const override
    {
        return (bool) texture && !deleted;
    }

    [[nodiscard]]
    bool hit_test(SDL_FPoint pt) const override
    {
        if (!valid()) return false;

        SDL_FRect rc = {
            pos.x - (float)extent.x * scale,
            pos.y - (float)extent.y * scale,
            (float)extent.w * scale,
            (float)extent.h * scale
        };
        if (rotate != 0.0)
        {
            rotate_point(pos, &pt, -rotate);
        }
        return SDL_PointInRectFloat(&pt, &rc);
    }

    bool handle_event(const SDL_Event* event, int &needs_update, AppContext *app) override
    {
        if (!app->layout_mode || !valid()) return false;

        if (event->type == SDL_EVENT_MOUSE_WHEEL)
        {
            SDL_FPoint pt(event->wheel.mouse_x, event->wheel.mouse_y);

            if (hit_test(pt))
            {
                if (SDL_GetModState() & SDL_KMOD_CTRL)
                {
                    // Rotate text (when ctrl is pressed)
                    SDL_FPoint ct(pt.x, pt.y);
                    double phi_delta = 5 * (event->wheel.y < 0 ? -1 : 1);
                    rotate_point(ct, &pos, phi_delta);
                    rotate += (float) phi_delta;
                }
                else if (SDL_GetModState() & SDL_KMOD_SHIFT)
                {
                    // Scale text
                    float dScale = powf(1.1f, event->wheel.y);
                    scale *= dScale;
                    pos.x += (float) ((pos.x - pt.x) * (dScale - 1.0));
                    pos.y += (float) ((pos.y - pt.y) * (dScale - 1.0));
                }
                else
                {
                    // Change text alpha
                    if (event->wheel.y < 0)
                    {
                        alpha = SDL_max(0.f, alpha - (5.f / 255.f));
                    }
                    else
                    {
                        alpha = SDL_min(1.f, alpha + (5.f / 255.f));
                    }
                    needs_update = UPDATE_SETTINGS_CHANGED;
                    return true;
                }
                needs_update = UPDATE_SETTINGS_CHANGED;
                return true;
            }
        }

        else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            SDL_FPoint pt(event->motion.x, event->motion.y);

            if (hit_test(pt))
            {
                // Start mouse capturing (dragging object)
                app->mouse_capture = this;
                app->dragging_origin = pt;
                app->dragging_offset = SDL_FPoint(
                    pt.x - pos.x,
                    pt.y - pos.y
                );
                return true;
            }
        }

        else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            if (app->mouse_capture == this)
            {
                // Stop mouse capturing (dragging object)
                pos = app->dragging_origin;
                pos.x -= app->dragging_offset.x;
                pos.y -= app->dragging_offset.y;
                app->mouse_capture = nullptr;
                app->dragging_offset = {0};
                needs_update = UPDATE_SETTINGS_CHANGED;
                return true;
            }
        }

        else if (event->type == SDL_EVENT_MOUSE_MOTION)
        {
            SDL_FPoint pt(event->motion.x, event->motion.y);

            if (app->mouse_capture == this)
            {
                app->dragging_origin = pt;
                needs_update = UPDATE_VIEW_CHANGED;
                return true;
            }
            else
            {
                // Show hand cursor when hovering over object
                if (!app->mouse_capture && hit_test(pt))
                {
                    SDL_SetCursor(app->handCursor);
                    return true;
                }
            }
        }

        else if (event->type == SDL_EVENT_KEY_DOWN)
        {
            COLORREF color;

            if (color_from_key((int) event->key.key, color))
            {
                if (hit_test_at_cursor())
                {
                    change_color((int) color, app->renderer);
                    needs_update = UPDATE_SETTINGS_CHANGED;
                    return true;
                }
            }
            else if (event->key.key == SDLK_DELETE)
            {
                if (hit_test_at_cursor())
                {
                    deleted = true;
                    needs_update = UPDATE_SETTINGS_CHANGED;
                    return true;
                }
            }
        }

        return false;
    }

    void draw(const SDL_FPoint &pt, float alpha, const SDL_Renderer *renderer) const override
    {
        if (valid() && renderer && renderer == this->renderer)
        {
            SDL_FRect rc = {
                pt.x - (float)extent.x * scale,
                pt.y - (float)extent.y * scale,
                (float)extent.w * scale,
                (float)extent.h * scale
            };
            SDL_SetTextureAlphaMod(texture, BLENDED_ALPHA_INT(this->alpha, alpha));
            SDL_RenderTextureRotated(
                    const_cast<SDL_Renderer*>(renderer),
                            texture,
                            nullptr,
                            &rc,
                            rotate,
                            nullptr,
                            SDL_FLIP_NONE);
        }
    }

    bool change_color(COLORREF color, const SDL_Renderer *renderer)
    {
        if (!valid() || !renderer || renderer != this->renderer || !surface) return false;

        auto pixels = (Uint32*)surface->pixels;
        auto format_details = SDL_GetPixelFormatDetails(surface->format);
        int totalPixels = surface->w * surface->h;

        for (int i = 0; i < totalPixels; ++i)
        {
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixels[i], format_details, nullptr, &r, &g, &b, &a);

            Uint32 px = SDL_MapRGBA(format_details, nullptr, GetRValue(color), GetGValue(color), GetBValue(color), a); // grayscale RGB from alpha
            pixels[i] = px;
        }

        SDL_Texture* new_texture = SDL_CreateTextureFromSurface(const_cast<SDL_Renderer*>(renderer), surface);

        if (!new_texture) return false;
        SDL_DestroyTexture(texture);
        texture = new_texture;
        font_color = color;

        return true;
    }

};


class Image : public ScreenObject
{
public:
    string name;
    string full_path;
    float scale;
    float rotate;
    float alpha;
    bool flip_horizontal;
    const SDL_Renderer *renderer;
    SDL_Surface *surface;
    SDL_Texture *texture;

protected:
    Image(float x, float y, const SDL_Renderer *renderer = nullptr)
    : ScreenObject(x, y),
    surface((SDL_Surface*)nullptr),
    texture((SDL_Texture*)nullptr),
    renderer(renderer)
    {}

public:
    Image(
        float x, float y,
        const string &name,
        const path &base_path,
        float scale_by,
        float rotate_by,
        bool flip_horizontal,
        float alpha,
        const SDL_Renderer *renderer)
        : ScreenObject(x, y),
          surface((SDL_Surface*)nullptr),
          texture((SDL_Texture*)nullptr),
          renderer(renderer)
    {
        full_path = (base_path / name).string();
        init(x, y, name, full_path, scale_by, rotate_by, flip_horizontal, alpha);
    }

    Image(json &j, const SDL_Renderer *renderer)
    : ScreenObject(-1, -1),
    surface((SDL_Surface*)nullptr),
    texture((SDL_Texture*)nullptr),
    renderer(renderer)
    {
        try
        {
            init(
                j["x"], j["y"],
                j.value("image_name", ""),
                j.value("image_full_path", ""),
                j.value("scale", 1.f),
                j.value("rotate", 0.f),
                j.value("flip_horizontal", false),
                j.value("alpha", 1.f));
        }
        catch (const std::exception &e)
        {
            SDL_Log("Error creating image: %s", e.what());
        }
    }

    [[nodiscard]]
    const char* type_name() const override {return "Image";}

    ~Image() override
    {
        SDL_DestroySurface(surface);
        surface = nullptr;
        SDL_DestroyTexture(texture);
        texture = nullptr;
    };

protected:
    void init(
            float x, float y,
            const string &name,
            const string &full_path,
            float scale_by,
            float rotate_by,
            bool flip_horizontal,
            float alpha)
    {
        this->name = name;
        this->full_path = full_path;
        pos.x = x;
        pos.y = y;
        scale = scale_by;
        rotate = rotate_by;
        this->flip_horizontal = flip_horizontal;
        this->alpha = alpha;

        if (!this->renderer) return;

        // load the Image
        surface = IMG_Load(full_path.c_str());

        if (!surface)
        {
            SDL_Log("Error loading \"%s\":\n   %s", name.c_str(), SDL_GetError());
        }
        else
        {
            // Convert the surface to RGBA8888 format
            SDL_Surface *rgba_surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA8888);
            SDL_DestroySurface(surface);
            surface = nullptr;
            if (rgba_surface)
            {
                surface = rgba_surface;
                if (SDL_GetSurfaceClipRect(rgba_surface, &extent))
                {
                    extent.x = extent.w / 2;
                    extent.y = extent.h / 2;
                    texture = SDL_CreateTextureFromSurface(const_cast<SDL_Renderer *>(renderer), rgba_surface);
                }
            }
        }

        if (!texture)
        {
            SDL_DestroySurface(surface);
            surface = nullptr;
        }
    }

public:
    [[nodiscard]]
    json to_json() const override
    {
        if (!valid()) return json::object();

        return json( {
             {"x", (int)pos.x},
             {"y", (int)pos.y},
             {"image_name", name},
             {"image_full_path", full_path},
             {"scale", round_to_precision(scale, 4)},
             {"rotate", round_to_precision(rotate, 4)},
             {"flip_horizontal", (bool)flip_horizontal},
             {"alpha", round_to_precision(alpha, 2)},
             {"type", type_name()}
        });
    }

    [[nodiscard]]
    bool valid() const override
    {
        return (bool)texture && !deleted;
    }

    [[nodiscard]]
    bool hit_test(SDL_FPoint pt) const override
    {
        if (!valid()) return false;

        SDL_FRect rc = {
            (float)pos.x - (float)extent.x * scale,
            (float)pos.y - (float)extent.y * scale,
            (float)extent.w * scale,
            (float)extent.h * scale
        };
        if (rotate != 0.0)
        {
            rotate_point(pos, &pt, -rotate);
        }
        if (SDL_PointInRectFloat(&pt, &rc))
        {
            Uint8 alpha;
            int xoff = (int)((pt.x - rc.x) / scale);
            int yoff = (int)((pt.y - rc.y) / scale);
            if (flip_horizontal)
            {
                xoff = extent.w - xoff - 1;
            }
            if (SDL_ReadSurfacePixel(surface, xoff, yoff, nullptr, nullptr, nullptr, &alpha))
            {
                return alpha > 50;
            }
        }

        return false;
    }

    bool handle_event(const SDL_Event* event, int &needs_update, AppContext *app) override
    {
        if (!app->layout_mode || !valid()) return false;

        if (event->type == SDL_EVENT_MOUSE_WHEEL)
        {
            SDL_FPoint pt(event->wheel.mouse_x, event->wheel.mouse_y);

            if (hit_test(pt))
            {
                if (SDL_GetModState() & SDL_KMOD_CTRL)
                {
                    // Rotate image (when ctrl is pressed)
                    SDL_FPoint ct(pt.x, pt.y);
                    double phi_delta = 5 * (event->wheel.y < 0 ? -1 : 1);
                    rotate_point(ct, &pos, phi_delta);
                    rotate += (float) phi_delta;
                }
                else if (SDL_GetModState() & SDL_KMOD_SHIFT)
                {
                    // Scale image
                    float dScale = powf(1.1f, event->wheel.y);
                    scale *= dScale;
                    pos.x += (float) ((pos.x - pt.x) * (dScale - 1.0));
                    pos.y += (float) ((pos.y - pt.y) * (dScale - 1.0));
                }
                else
                {
                    // Change text alpha
                    if (event->wheel.y < 0)
                    {
                        alpha = SDL_max(0.f, alpha - (5.f / 255.f));
                    }
                    else
                    {
                        alpha = SDL_min(1.f, alpha + (5.f / 255.f));
                    }
                }
                needs_update = UPDATE_SETTINGS_CHANGED;
                return true;
            }
        }

        else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            SDL_FPoint pt(event->motion.x, event->motion.y);

            if (hit_test(pt))
            {
                app->mouse_capture = this;
                app->dragging_origin = pt;
                app->dragging_offset = SDL_FPoint(
                    pt.x - pos.x,
                    pt.y - pos.y
                );
                return true;
            }
        }

        else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            if (app->mouse_capture == this)
            {
                pos = app->dragging_origin;
                pos.x -= app->dragging_offset.x;
                pos.y -= app->dragging_offset.y;
                app->mouse_capture = nullptr;
                app->dragging_offset = {0};
                needs_update = UPDATE_SETTINGS_CHANGED;
                return true;
            }
        }

        else if (event->type == SDL_EVENT_MOUSE_MOTION)
        {
            SDL_FPoint pt(event->motion.x, event->motion.y);

            if (app->mouse_capture == this)
            {
                app->dragging_origin = pt;
                needs_update = UPDATE_VIEW_CHANGED;
                return true;
            }
            else
            {
                if (!app->mouse_capture && hit_test(pt))
                {
                    SDL_SetCursor(app->handCursor);
                    return true;
                }
            }
        }

        else if (event->type == SDL_EVENT_KEY_DOWN)
        {
            if (event->key.key == SDLK_F)
            {
                if (hit_test_at_cursor())
                {
                    flip_horizontal = !flip_horizontal;
                    needs_update = UPDATE_SETTINGS_CHANGED;
                    return true;
                }
            }
            else if (event->key.key == SDLK_DELETE)
            {
                if (hit_test_at_cursor())
                {
                    deleted = true;
                    needs_update = UPDATE_SETTINGS_CHANGED;
                    return true;
                }
            }
        }

        return false;
    }

    void draw(const SDL_FPoint &pt, float alpha, const SDL_Renderer *renderer) const override
    {
        if (!valid() || !renderer || renderer != this->renderer) return;

        float x = pt.x - (float)extent.x;
        float y = pt.y - (float)extent.y;
        float w = (float)extent.w;
        float h = (float)extent.h;

        render_transformed_texture(
                texture,
                x, y, w, h,
                scale, rotate,
                flip_horizontal, false,
                BLENDED_ALPHA_FLOAT(this->alpha, alpha),
                renderer);
    }

};


class AnimatedGif : public Image
{
    struct frame_info_t {
        int delay_ms;
        int transparent_color_index;
        int disposal_mode;
        bool texture_outdated;
        SDL_Texture *texture;
    };

public:
    bool cache_frames;
    int frame_count;
    int current_frame;
    int recent_disposal;
    Uint64 latest_ticks;
    SDL_Rect previous_frame_rect;
    vector<frame_info_t> frame_info;
    GifFileType *gif;

    AnimatedGif(
        float x, float y,
        const string &name,
        const path &base_path,
        float scale_by,
        float rotate_by,
        bool flip_horizontal,
        float alpha,
        bool cache_frames,
        const SDL_Renderer *renderer)
    : Image(x, y, renderer),
      gif((GifFileType*)nullptr)
    {
        full_path = (base_path / name).string();
        init(x, y, name, full_path, scale_by, rotate_by, flip_horizontal, alpha, cache_frames);
    }

    AnimatedGif(json &j, const SDL_Renderer *renderer)
    : Image(-1, -1, renderer),
      gif((GifFileType*)nullptr)
    {
        try
        {
            init(
                j["x"], j["y"],
                j.value("image_name", ""),
                j.value("image_full_path", ""),
                j.value("scale", 1.f),
                j.value("rotate", 0.f),
                j.value("flip_horizontal", false),
                j.value("alpha", 1.f),
                j.value("cache_frames", true));
        }
        catch (const std::exception &e)
        {
            SDL_Log("Error creating image: %s", e.what());
        }
    }

    [[nodiscard]]
    const char* type_name() const override {return "AnimatedGif";}

    ~AnimatedGif() override
    {
        invalidate(true);
        SDL_DestroyTexture(texture);
        SDL_DestroySurface(surface);
        if (gif) DGifCloseFile(gif, nullptr);
    }

protected:
    void init(
        float x, float y,
        const string &name,
        const string &full_path,
        float scale_by,
        float rotate_by,
        bool flip_horizontal,
        float alpha,
        bool cache_frames)
    {
        this->name = name;
        this->full_path = full_path;
        pos.x = x;
        pos.y = y;
        scale = scale_by;
        rotate = rotate_by;
        this->flip_horizontal = flip_horizontal;
        latest_ticks = SDL_GetTicks() + dist(gen) % 500;
        frame_count = 0;
        current_frame = 0;
        recent_disposal = DISPOSAL_UNSPECIFIED;
        this->alpha = alpha;
        this->cache_frames = cache_frames;
        this->previous_frame_rect = {0, 0, 0, 0};

        if (!renderer) return;

        gif = DGifOpenFileName(full_path.c_str(), nullptr);
        if (!gif)
        {
            SDL_Log("Error loading \"%s\":\n   %s", name.c_str(), SDL_GetError());
        }
        else
        {
            GraphicsControlBlock gcb;

            DGifSlurp(gif);
            frame_count = gif->ImageCount;
            for (int i = 0; i < frame_count; i++)
            {
                frame_info_t info {
                    .delay_ms = 100,
                    .transparent_color_index = NO_TRANSPARENT_COLOR,
                    .disposal_mode = DISPOSAL_UNSPECIFIED,
                    .texture_outdated = true,
                    .texture = (SDL_Texture *)nullptr
                };

                SavedImage *frame = &gif->SavedImages[i];
                for (int j = 0; j < frame->ExtensionBlockCount; j++)
                {
                    ExtensionBlock *ext = &frame->ExtensionBlocks[j];
                    if (ext && ext->Function == GRAPHICS_EXT_FUNC_CODE)
                    {
                        if (GIF_OK == DGifExtensionToGCB(ext->ByteCount, ext->Bytes, &gcb))
                        {
                            info.delay_ms = gcb.DelayTime * 10;
                            info.transparent_color_index = gcb.TransparentColor;
                            info.disposal_mode = gcb.DisposalMode;
                            break;
                        }
                    }
                }

                frame_info.push_back(info);
            }

            surface = SDL_CreateSurface(gif->SWidth, gif->SHeight, SDL_PIXELFORMAT_RGBA8888);
            if (surface)
            {
                SDL_ClearSurface(surface, 0, 0, 0, 0);
            }
            render_frame(const_cast<SDL_Renderer*>(renderer));
            extent.w = gif->SWidth;
            extent.h = gif->SHeight;
            extent.x = extent.w / 2;
            extent.y = extent.h / 2;
        }
    }

public:
    [[nodiscard]]
    json to_json() const override
    {
        if (!valid()) return json::object();

        return json( {
             {"x", (int)pos.x},
             {"y", (int)pos.y},
             {"image_name", name},
             {"image_full_path", full_path},
             {"scale", round_to_precision(scale, 4)},
             {"rotate", round_to_precision(rotate, 4)},
             {"flip_horizontal", (bool)flip_horizontal},
             {"alpha", round_to_precision(alpha, 2)},
             {"cache_frames", (bool)cache_frames},
             {"type", type_name()}
        });
    }

    [[nodiscard]]
    bool valid() const override
    {
        return (bool)surface && !deleted;
    }

    bool handle_event(const SDL_Event* event, int &needs_update, AppContext *app) override
    {
        bool result = valid() && Image::handle_event(event, needs_update, app);

        if (needs_update == UPDATE_SETTINGS_CHANGED)
        {
            invalidate();
        }

        return result;
    }

    void draw(const SDL_FPoint &pt, float alpha, const SDL_Renderer *renderer) const override
    {
        // If the GIF is not valid or the renderer is not available, do nothing.
        if (!valid()) return;

        // Calculate the position and dimensions of the GIF on the screen.
        float x = pt.x - (float)extent.x;
        float y = pt.y - (float)extent.y;
        float w = (float)extent.w;
        float h = (float)extent.h;

        // Render the current frame of the GIF with the specified transformations.
        render_transformed_texture(
                texture,
                x, y, w, h,
                scale, rotate,
                flip_horizontal, false,
                BLENDED_ALPHA_FLOAT(this->alpha, alpha),
                renderer);
    }

    // The render_frame function is the core of the animated GIF rendering.
    // It is responsible for decoding the current frame, handling disposal methods,
    // and updating the texture that is displayed on the screen.
    void render_frame(const SDL_Renderer *renderer)
    {
        SavedImage *frame;
        int left, top, width, height;
        const ColorMapObject *color_map;
        const Uint8 *raster_bits;
        int bg_color;
        frame_info_t *frame_info = &this->frame_info[current_frame];
        int transparent_color;
        Uint32 *addr;

        // If the GIF is not valid or the renderer is not available, do nothing.
        if (!valid() || !renderer) return;

        // If the texture for the current frame is already cached, just use it.
        if (!frame_info->texture_outdated)
        {
            texture = frame_info->texture;
            recent_disposal = frame_info->disposal_mode;
            return;
        }

        // If the surface is not valid or the pixel format is not RGBA8888, do nothing.
        if (!surface || surface->format != SDL_PIXELFORMAT_RGBA8888) return;

        // Prepare canvas for the current frame
        if (!gif || !gif->SavedImages) return;
        frame = &gif->SavedImages[current_frame];
        bg_color = gif->SBackGroundColor;
        raster_bits = frame->RasterBits;
        if (!raster_bits) return;
        color_map = frame->ImageDesc.ColorMap ? frame->ImageDesc.ColorMap : gif->SColorMap;
        if (!color_map) return;

        // Handle the disposal method of the previous frame.
        switch (recent_disposal)
        {
            case DISPOSE_BACKGROUND:
            {
                // Clear the area of the previous frame to the background color.
                if (bg_color == NO_TRANSPARENT_COLOR || bg_color < color_map->ColorCount)
                {
                    SDL_FillSurfaceRect(surface, &previous_frame_rect, 0);
                }
                else
                {
                    GifColorType *color = &color_map->Colors[bg_color];
                    const SDL_PixelFormatDetails* format_details = SDL_GetPixelFormatDetails(surface->format);
                    Uint32 mapped_color = SDL_MapRGBA(
                            format_details,
                            nullptr,
                            color->Red,
                            color->Green,
                            color->Blue,
                            255);
                    SDL_FillSurfaceRect(surface, &previous_frame_rect, mapped_color);
                }
                break;
            }
            case DISPOSE_DO_NOT:
                // Do nothing, the previous frame is kept.
                break;
            case DISPOSAL_UNSPECIFIED:
            default: // DISPOSE_NONE
                // Do nothing, just overwrite the previous frame.
                break;
        }

        // Get the dimensions and position of the current frame.
        left = frame->ImageDesc.Left;
        top = frame->ImageDesc.Top;
        width = frame->ImageDesc.Width;
        height = frame->ImageDesc.Height;

        transparent_color = frame_info->transparent_color_index;

        // Pre-calculate the palette colors for the current frame to optimize the rendering loop.
        vector<Uint32> palette_colors(color_map->ColorCount);
        const SDL_PixelFormatDetails* format_details = SDL_GetPixelFormatDetails(surface->format);
        for (int i = 0; i < color_map->ColorCount; i++)
        {
            if (i == transparent_color) {
                // Set the transparent color to have an alpha of 0 (values of R, G, B don't matter, since alpha is zero).
                palette_colors[i] = 0;
            } else {
                // Set the other colors with an alpha of 255 (fully opaque).
                palette_colors[i] = SDL_MapRGBA(
                        format_details,
                        nullptr,
                        color_map->Colors[i].Red,
                        color_map->Colors[i].Green,
                        color_map->Colors[i].Blue,
                        255);
            }
        }

        // Lock the surface to directly access the pixels.
        SDL_LockSurface(surface);
        // Iterate over the pixels of the current frame and update the surface.
        for (int i = 0; i < height; i++)
        {
            addr = (Uint32*)(void*)((Uint8*)surface->pixels + (i + top) * surface->pitch) + left;
            for (int j = 0; j < width; j++)
            {
                int color_index = *raster_bits++;
                if (color_index < color_map->ColorCount)
                {
                    // Only draw the pixel if it is not transparent.
                    Uint32 color = palette_colors[color_index];
                    if (color) *addr = color;
                }

                addr++;
            }
        }
        SDL_UnlockSurface(surface);

        // Destroy the old texture and create a new one from the updated surface.
        if (frame_info->texture)
        {
            SDL_DestroyTexture(frame_info->texture);
            frame_info->texture = (SDL_Texture *)nullptr;
            frame_info->texture_outdated = true;
        }
        texture = SDL_CreateTextureFromSurface(const_cast<SDL_Renderer*>(renderer), surface);
        // If caching is enabled, store the new texture.
        if (cache_frames)
        {
            frame_info->texture = texture;
            frame_info->texture_outdated = false;
        }

        // Store the rectangle of the current frame for the next iteration.
        previous_frame_rect = {left, top, width, height};
        // Store the disposal method of the current frame for the next iteration.
        recent_disposal = frame_info->disposal_mode;
    }

    void invalidate(bool remove = false)
    {
        for (auto info : frame_info)
        {
            info.texture_outdated = true;
            if (remove)
            {
                SDL_DestroyTexture(info.texture);
                if (texture == info.texture)
                {
                    texture = (SDL_Texture *)nullptr;
                }
                info.texture = (SDL_Texture *)nullptr;
            }
        }
    }

};


SDL_AppResult app_init_failed()
{
    SDL_LogError(
        SDL_LOG_CATEGORY_CUSTOM,
        "Error %s",
        SDL_GetError()
    );
    return SDL_APP_FAILURE;
}


SDL_AppResult SDL_AppInit(
    void** appstate,
    [[maybe_unused]] int argc,
    [[maybe_unused]] char* argv[]
)
{
    json objects;
    auto *app = new AppContext;
    *appstate = app;

    // Get the base path
    app->base_path = SDL_GetBasePath();
    if (app->base_path.empty())
    {
        return app_init_failed();
    }

    // Init SDL
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        return app_init_failed();
    }

    // Init TTF
    if (!TTF_Init())
    {
        return app_init_failed();
    }

    // Create hand cursor
    app->handCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    if (!app->handCursor)
    {
        return app_init_failed();
    }

    // Read settings
    if (!settings_read(app, objects))
    {
        return app_init_failed();
    }

    // Update screen metrics
    update_screen_metrics(app);

    // Create a window
    app->window = SDL_CreateWindow(
        "dragon",
        app->work_area.w, app->work_area.h,
        SDL_WINDOW_ALWAYS_ON_TOP |
        SDL_WINDOW_OCCLUDED |
        SDL_WINDOW_TRANSPARENT |
        SDL_WINDOW_BORDERLESS |
        //SDL_WINDOW_FULLSCREEN |
        SDL_WINDOW_HIGH_PIXEL_DENSITY |
        SDL_WINDOW_HIDDEN |
        SDL_WINDOW_OPENGL |
        0
    );

    if (!app->window)
    {
        return app_init_failed();
    }
    SDL_SetWindowPosition(app->window, app->work_area.x, app->work_area.y);

    // Retrieve the HWND from the SDL window
    SDL_PropertiesID props = SDL_GetWindowProperties(app->window);
    if (!props)
    {
        return app_init_failed();
    }
    else
    {
        app->hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (!app->hwnd)
        {
            return app_init_failed();
        }
        else
        {
            update_layout_mode(app);
        }
    }
    SetLayeredWindowAttributes(app->hwnd, 0, 255, LWA_ALPHA);

    // Add a renderer
    app->renderer = SDL_CreateRenderer(app->window, nullptr);
    if (!app->renderer)
    {
        return app_init_failed();
    }

    // Create the line object
    screen_objects_add_lines(app);

    // Initialize screen objects
    init_screen_objects(app, objects);

    // If no screen objects defined in settings file, create two default objects
    if (app->screen_objects.size() <= 1)
    {
        float x_pos = (float) app->work_area.x + (float) (app->work_area.w * 5.0 / 6.0);
        float y_pos = (float) app->work_area.y + (float) (app->work_area.h * 1.0 / 5.0);

        // create image object
        auto image = new Image(
                x_pos, y_pos,
                app->logo_file_name,
                app->base_path,
                app->logo_scale,
                0.0,
                false,
                1.f,
                app->renderer);
        app->screen_objects.push_back(image);

        // create signature object
        y_pos += (float) ((float) image->extent.h * image->scale * 0.6);
        auto text = new Signature(
                app->text_content,
                x_pos, y_pos,
                app->text_font_name,
                (float) app->text_font_size,
                app->text_font_color,
                app->base_path,
                app->text_scale,
                app->text_rotate,
                1.f,
                app->renderer);
        app->screen_objects.push_back(text);

        app->is_virgin = false;

        if (!text->valid() || !image->valid())
        {
            return app_init_failed();
        }
    }

    // print some information about the window
    {
        int width, height, bbwidth, bbheight;
        SDL_GetWindowSize(app->window, &width, &height);
        SDL_GetWindowSizeInPixels(app->window, &bbwidth, &bbheight);
        SDL_Log("Window size: %ix%i", width, height);
        SDL_Log("Backbuffer size: %ix%i", bbwidth, bbheight);
        if (width != bbwidth)
        {
            SDL_Log("This is a highdpi environment.");
        }
    }

    SDL_Log("Application started successfully!");

    // Prepare background
    SDL_SetRenderVSync(app->renderer, SDL_RENDERER_VSYNC_ADAPTIVE);   // enable vsync
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255); // Set black background
    SDL_RenderClear(app->renderer);
    SDL_RenderPresent(app->renderer); // Present first frame

    // draw
    draw(app);
    SDL_ShowWindow(app->window);

    return SDL_APP_CONTINUE;
}


void SDL_AppQuit(void* appstate, [[maybe_unused]] SDL_AppResult result)
{
    auto* app = (AppContext*)appstate;

    if (result == SDL_APP_SUCCESS)
    {
        settings_write(app);
    }

    if (app)
    {
        SDL_HideWindow(app->window);

        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        SDL_DestroyCursor(app->handCursor);
        free_screen_objects(app);

        delete app;
    }

    TTF_Quit();

    SDL_Log("Application quit successfully!");
    SDL_Quit();
}


SDL_AppResult SDL_AppIterate(void *appstate)
{
    auto* app = (AppContext*)appstate;
    int timeout = -1;
    Uint64 ticks = SDL_GetTicks();

    if (app->app_quit != SDL_APP_CONTINUE) return app->app_quit;

    if (app->needs_redraw)
    {
        draw(app);
    }

    auto *line_object = dynamic_cast<LineObject *>(app->screen_objects[0]);
    if (line_object && line_object->dashed && line_object->dashed_gap > 0 && line_object->width > 0 && !app->hidden)
    {
        Sint64 delay = (Sint64)(ticks - app->idle_ticks);

        if (delay >= app->idle_delay_ms)
        {
            timeout = 0;
            app->idle_ticks = ticks;
            app->needs_redraw = true;
        }
        else
        {
            timeout = SDL_max(0, app->idle_delay_ms - (int)delay);
        }
    }


    if (app->have_animations && !app->hidden)
    {
        for (auto &obj: app->screen_objects)
        {
            auto* gif = dynamic_cast<AnimatedGif *>(obj);

            if (gif && gif->valid())
            {
                Uint64 delay = (Sint64)(ticks - gif->latest_ticks);
                if (delay >= gif->frame_info[gif->current_frame].delay_ms)
                {
                    gif->current_frame = (gif->current_frame + 1) % gif->frame_count;
                    gif->render_frame(app->renderer);
                    gif->latest_ticks = ticks;
                    app->needs_redraw = true;
                }
                else
                {
                    timeout = SDL_min(
                                (timeout < 0) ? app->idle_delay_ms : timeout,
                                SDL_max(
                                        0, gif->frame_info[gif->current_frame].delay_ms - (int)delay));
                    // SDL_Log("next frame in %i ms", (int) (timeout));
                }
            }
        }
    }

    if (!app->needs_redraw)
    {
        // Wait for next event with timeout (timeout==-1 means "no timeout")
        SDL_WaitEventTimeout(nullptr, timeout);
        // SDL_Log("timeout: %i", timeout);
    }

    return app->app_quit;
}


SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event* event)
{
    auto* app = (AppContext*)appstate;

    // Check if the current event is a mouse motion event
    if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        SDL_Event e;

        // Drain the event queue of all pending mouse motion events,
        // but only keep the most recent one (the last in the queue)
        while (SDL_PeepEvents(
                &e,                  // Pointer to a temporary event structure
                1,                   // Number of events to process at a time
                SDL_GETEVENT,        // Action: get events from the queue
                SDL_EVENT_MOUSE_MOTION, // Minimum event type to retrieve
                SDL_EVENT_MOUSE_MOTION  // Maximum event type to retrieve (only motion)
        ))
        {
            // Overwrite the original event with the latest mouse motion event
            *event = e;
        }
    }

    ScreenObject* line_object = nullptr;
    bool event_handled = false;

    // Find the LineObject
    for (auto& obj : app->screen_objects)
    {
        if (strcmp(obj->type_name(), "Lines") == 0)
        {
            line_object = obj;
            break;
        }
    }

    // Handle events for all objects except LineObject
    for (auto it = app->screen_objects.begin(); it != app->screen_objects.end(); ++it)
    {
        if (*it == line_object) continue;

        int needs_update = 0;
        if ((*it)->handle_event(event, needs_update, app))
        
        {
            if (needs_update >= UPDATE_VIEW_CHANGED)
            {
                app->needs_redraw = true;
            }
            if (needs_update >= UPDATE_SETTINGS_CHANGED)
            {
                app->is_virgin = false;
            }
            event->type = SDL_EVENT_LAST;
            event_handled = true;
            break;
        }
    }

    // If no other object handled the event, handle it for the LineObject
    if (!event_handled && line_object)
    {
        int needs_update = 0;
        if (line_object->handle_event(event, needs_update, app))
        {
            if (needs_update >= UPDATE_VIEW_CHANGED)
            {
                app->needs_redraw = true;
            }
            if (needs_update >= UPDATE_SETTINGS_CHANGED)
            {
                app->is_virgin = false;
            }
            event->type = SDL_EVENT_LAST;
        }
    }

    if (event->type == SDL_EVENT_QUIT)
    {
        app->app_quit = SDL_APP_SUCCESS;
    }

    else if (event->type == SDL_EVENT_WINDOW_MINIMIZED)
    {
        SDL_RestoreWindow(app->window);
        app->needs_redraw = true;
    }

    else if (event->type == SDL_EVENT_WINDOW_RESTORED)
    {
        app->needs_redraw = true;
    }

    else if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED)
    {
        app->needs_redraw = true;
    }

    else if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        app->mouse_capture = nullptr;
    }

    else if (event->type == SDL_EVENT_MOUSE_WHEEL)
    {
        if (app->layout_mode)
        {
            if (event->wheel.y < 0)
            {
                app->alpha = SDL_max(0, app->alpha - 5.f / 255.f);
                app->is_virgin = false;
            }
            else
            {
                app->alpha = SDL_min(1.f, app->alpha + 5.f / 255.f);
                app->is_virgin = false;
            }
            app->needs_redraw = true;
        }
    }

    else if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        if (app->layout_mode)
        {
            SDL_SetCursor(SDL_GetDefaultCursor());
        }
    }

    else if (event->type == SDL_EVENT_KEY_DOWN)
    {
        app->needs_redraw = true;

        if (event->key.key == SDLK_LEFT)
        {
            app->alpha = SDL_max(0, app->alpha - 17.f / 255.f);
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_RIGHT)
        {
            app->alpha = SDL_min(1.f, app->alpha + 17.f / 255.f);
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_X)
        {
            app->app_quit = SDL_APP_SUCCESS;
        }
        else if ((event->key.key == SDLK_SPACE) or (event->key.key == SDLK_RETURN))
        {
            app->layout_mode = !app->layout_mode;
            update_layout_mode(app);
        }
        else if (event->key.key == SDLK_H)
        {
            app->hidden = !app->hidden;
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_V && SDL_GetModState() & SDL_KMOD_CTRL)
        {
            clipboard_insert(app);
        }
        else
        {
            app->needs_redraw = false;
        }
    }

    else if (event->type == SDL_EVENT_DROP_TEXT)
    {
        if (event->drop.data)
        {
            screen_objects_add_text(
                    event->drop.x,
                    event->drop.y,
                    event->drop.data,
                    app);
        }
    }

    else if (event->type == SDL_EVENT_DROP_FILE)
    {
        if (event->drop.data)
        {
            screen_objects_add_image(
                    event->drop.x,
                    event->drop.y,
                    event->drop.data,
                    app);
        }
    }

    return SDL_APP_CONTINUE;
}


void update_screen_metrics(AppContext* app)
{
    SDL_FPoint mouse;
    SDL_GetMouseState(&mouse.x, &mouse.y);
    SDL_Point pt((int)mouse.x, (int)mouse.y);
    int displayIndex = SDL_GetDisplayForPoint(&pt);

    SDL_GetDisplayUsableBounds(displayIndex, &app->screen_rect);

    if (app->screen_rect_init.x >= 0)
    {
        app->screen_rect.x = app->screen_rect_init.x;
    }
    if (app->screen_rect_init.y >= 0)
    {
        app->screen_rect.y = app->screen_rect_init.y;
    }
    if (app->screen_rect_init.w >= 0)
    {
        app->screen_rect.w = app->screen_rect_init.w;
    }
    if (app->screen_rect_init.h >= 0)
    {
        app->screen_rect.h = app->screen_rect_init.h;
    }
    app->work_area = app->screen_rect;

    // Not needed, since SDL_GetDisplayUsableBounds()
    if (app->crop_bottom < 0)
    {
        APPBARDATA abd = {0};
        abd.cbSize = sizeof(APPBARDATA);
        if (SHAppBarMessage(ABM_GETTASKBARPOS, &abd)) 
        {
            RECT rc = abd.rc;
            int height = 0;

            if (abd.uEdge == ABE_BOTTOM || abd.uEdge == ABE_TOP) 
            {
                height = rc.bottom - rc.top;
            }
            app->crop_bottom = height;
        }
    }
    app->work_area.h -= app->crop_bottom;

    app->center_x = (float) app->work_area.x + (float) app->work_area.w / 2.f;
    app->center_y = (float) app->work_area.y + (float) app->work_area.h / 2.f;
}


void update_layout_mode(AppContext* app)
{
    if (app->layout_mode)
    {
        SetWindowLong(
            app->hwnd,
            GWL_EXSTYLE,
            GetWindowLong(app->hwnd, GWL_EXSTYLE) & ~(WS_EX_LAYERED | WS_EX_TRANSPARENT)
        );
    }
    else
    {
        SetWindowLong(
            app->hwnd,
            GWL_EXSTYLE,
            GetWindowLong(app->hwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT
        );
    }
}


void init_screen_objects(AppContext* app, json &objects) {
    // Initialize objects
    for (auto & object : objects) {
        if (!object.contains("type"))
        {
            if (object.contains("image_full_path"))
            {
                if (string(object["image_full_path"]).ends_with(".gif"))
                {
                    object["type"] = "AnimatedGif";
                }
                else
                {
                    object["type"] = "Image";
                }
                path full_path = object["image_full_path"];
                object["image_name"] = full_path.filename().string();
                app->is_virgin = false;
            }
        }
        if (object.contains("type"))
        {
            ScreenObject* obj = nullptr;

            if (!object.contains("x") || object["x"] < 0)
            {
                object["x"] = app->center_x;
            }
            if (!object.contains("y") || object["y"] < 0)
            {
                object["y"] = app->center_y;
            }
            if (object["type"] == "Signature")
            {
                 obj = new Signature(
                         object,
                         app->base_path,
                         app->renderer);
            }
            else if (object["type"] == "Lines")
            {
                // Find the existing LineObject and update its properties
                for (auto& existing_obj : app->screen_objects) {
                    if (strcmp(existing_obj->type_name(), "Lines") == 0) {
                        auto lines = dynamic_cast<LineObject*>(existing_obj);
                        if (lines) {
                            lines->alpha = (float)object.value("alpha", 0.55f);
                            lines->width = object.value("width", 1);
                            lines->color = get_color_value(object, "color", 0x000000);
                            lines->dashed = object.value("dashed", true);
                            lines->dashed_len = object.value("dashed_len", 10);
                            lines->dashed_gap = object.value("dashed_gap", 10);
                            lines->line_angle = object.value("line_angle", 45.f);
                            lines->line_spacing = object.value("line_spacing", 15.f);
                        }
                        break;
                    }
                }
                obj = nullptr; // Don't add a new object
            }
            else if (object["type"] == "Image")
            {
                obj = new Image(
                        object,
                        app->renderer);
            }
            else if (object["type"] == "AnimatedGif")
            {
                obj = new AnimatedGif(
                        object,
                        app->renderer);
                app->have_animations = true;
            }
            // (Silently ignore unknown object types)

            if (obj) app->screen_objects.push_back(obj);
        }
    }
}


bool screen_objects_add_lines(AppContext *app)
{
    ScreenObject *obj = new LineObject(app->work_area, app->idle_ticks);

    app->screen_objects.push_back(obj);
    app->is_virgin = false;
    app->needs_redraw = true;

    return true;
}


bool screen_objects_add_text(float x, float y, const char* text, AppContext *app)
{
    ScreenObject *obj = new Signature(
        text,
        x,
        y,
        app->text_font_name,
        (float)app->text_font_size,
        app->text_font_color,
        app->base_path,
        1.f,
        0.f,
        1.f,
        app->renderer);

    app->screen_objects.push_back(obj);
    app->is_virgin = false;
    app->needs_redraw = true;

    return true;
}


bool screen_objects_add_image(float x, float y, const char *full_path_name, AppContext *app)
{
    ScreenObject *obj = nullptr;
    string buffer(full_path_name);
    path fullpath = full_path_name;

    std::transform(
            buffer.begin(),
            buffer.end(),
            buffer.begin(),
            [](unsigned char c){ return std::tolower(c); });

    if (buffer.ends_with(".jpg") ||
        buffer.ends_with(".gif") ||
        buffer.ends_with(".bmp") ||
        buffer.ends_with(".png") ||
        buffer.ends_with(".svg"))
    {
        SDL_PathInfo info;

        if (SDL_GetPathInfo(full_path_name, &info) && info.type == SDL_PATHTYPE_FILE)
        {
            if (buffer.ends_with(".gif"))
            {
                obj = new AnimatedGif(
                        x,
                        y,
                        fullpath.filename().string(),
                        fullpath.parent_path().string(),
                        1.f, 0.f, false,
                        1.f,
                        true,
                        app->renderer);

                if (obj && !obj->valid())
                {
                    delete obj;
                    obj = nullptr;
                }
                else
                {
                    app->have_animations = true;
                }
            }
            else
            {
                obj = new Image(
                        x,
                        y,
                        fullpath.filename().string(),
                        fullpath.parent_path().string(),
                        1.f, 0.f, false,
                        1.f,
                        app->renderer);

                if (obj && !obj->valid())
                {
                    delete obj;
                    obj = nullptr;
                }
            }
        }
    }

    if (obj)
    {
        app->screen_objects.push_back(obj);
        app->is_virgin = false;
        app->needs_redraw = true;
    }

    return (bool)obj;
}


void clipboard_insert(AppContext *app)
{
    if (SDL_HasClipboardText())
    {
        char *clp_text = SDL_GetClipboardText();

        screen_objects_add_text(app->center_x, app->center_y, clp_text, app);
        SDL_free(clp_text);
    }
    else if (OpenClipboard(nullptr))
    {
        if (IsClipboardFormatAvailable(CF_HDROP))
        {
            HANDLE h_drop = GetClipboardData(CF_HDROP);
            if (h_drop != nullptr)
            {
                UINT file_count = DragQueryFile((HDROP)h_drop, 0xFFFFFFFF, nullptr, 0);

                for (UINT i = 0; i < file_count; ++i)
                {
                    char file_path[MAX_PATH];
                    if (DragQueryFile((HDROP)h_drop, i, file_path, MAX_PATH))
                    {
                        if (!screen_objects_add_image(app->center_x, app->center_y, file_path, app))
                        {
                            screen_objects_add_text(app->center_x, app->center_y, file_path, app);
                        }
                    }
                }
            }
        }
        CloseClipboard();
    }
}


double round_to_precision(double value, int decimals)
{
    double factor = std::pow(10.0, decimals);

    return std::round(value * factor) / factor;
}


void free_screen_objects(AppContext* app)
{
    for (auto & screen_object : app->screen_objects)
    {
        delete screen_object;
    }
    app->screen_objects.clear();
}


// Implements a Bresenham-like line drawing algorithm with dashing capabilities.
// This function draws a line between two points (x1, y1) and (x1 + dx, y1 + dy)
// on a given SDL_Surface. It supports dashed lines with configurable dash and gap lengths.
void draw_line_bresenham(
        int x1, int y1, // Starting coordinates of the line segment
        int dx, int dy, // Differences in x and y coordinates (length of the segment)
        int dash_len,   // Length of a dash in pixels
        int gap_len,    // Length of a gap in pixels
        int dash_offset,// Starting offset for the dashing pattern
        void *color,    // Pointer to the color data to be used for drawing
        SDL_Surface* surface) // The target surface to draw on
{
    // Return immediately if the surface or color is invalid.
    if (!surface || !color) return;

    // Determine the direction of the line in x and y axes.
    int sx = (dx >= 0) ? 1 : -1; // x-step: 1 for right, -1 for left
    int sy = (dy >= 0) ? 1 : -1; // y-step: 1 for down, -1 for up

    // Initialize error term for Bresenham's algorithm.
    // This helps decide when to step in the y direction.
    int err = dx - dy;

    // Get bytes per pixel and pitch (row length in bytes) from the surface.
    int bpp = 4; // Assuming RGBA8888 format (4 bytes per pixel)
    int pitch = surface->pitch;

    // Pointer to the current pixel address on the surface.
    void *addr = nullptr;

    // Loop counter to prevent infinite loops for very long lines or zero-length segments.
    int n = 10000; // Max number of pixels to draw (safety limit)

    // Current position in the dash/gap pattern.
    int i = dash_offset;

    // Take absolute values of dx and dy for the algorithm.
    dx = SDL_abs(dx);
    dy = SDL_abs(dy);

    // Main loop for Bresenham's algorithm.
    while (n--)
    {
        int e2; // Error term multiplied by 2 for integer arithmetic

        // Increment dash/gap counter.
        i++;

        // Check if the current pixel is within the bounds of the surface.
        if (y1 >= 0 && y1 < surface->h && x1 >= 0 && x1 < surface->w)
        {
            // If the address has not been initialized yet, calculate it.
            if (!addr)
            {
                addr = (Uint8 *)surface->pixels + y1 * pitch + x1 * bpp;
            }
            // Check if the current segment should be a dash (not a gap).
            // If gap_len is 0, it's a solid line, so always draw.
            if (gap_len == 0 || i % (gap_len + dash_len) < dash_len)
            {
                // Copy the color data to the current pixel.
                memcpy(addr, color, bpp);
            }
        }
        else
        {
            // If we are outside the surface and have already drawn some pixels,
            // we can stop drawing. This prevents drawing lines far off-screen.
            if (addr) break;
        }

        // Calculate 2 * error for the next step.
        e2 = 2 * err;

        // Determine whether to step in x, y, or both.
        if (e2 > -dy) // If error is still positive, step in x direction.
        {
            err -= dy; // Update error term.
            x1 += sx;  // Move to the next pixel in x direction.
            if (addr) addr = (Uint8 *)addr + sx * bpp; // Update pixel address if applicable.
        }
        if (e2 < dx) // If error is still negative, step in y direction.
        {
            err += dx; // Update error term.
            y1 += sy;  // Move to the next pixel in y direction.
            if (addr) addr = (Uint8 *)addr + sy * pitch; // Update pixel address if applicable.
        }
    }
}


void draw(AppContext* app)
{
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        app->renderer,
        0, 0, 0,
        0
    );
    SDL_RenderClear(app->renderer);

    if (!app->hidden) {
        for (auto it = app->screen_objects.begin(); it != app->screen_objects.end(); ++it) {
            if (*it == app->mouse_capture) {
                SDL_FPoint pt;

                pt = app->dragging_origin;
                pt.x -= app->dragging_offset.x;
                pt.y -= app->dragging_offset.y;

                (*it)->draw(pt, app->alpha, app->renderer);
            } else {
                (*it)->draw((*it)->pos, app->alpha, app->renderer);
            }
        }
    }

    // Draw green frame, indicating layout mode
    if (app->layout_mode)
    {
        SDL_FRect rc = {
            .x = (float)0,
            .y = (float)0,
            .w = (float)app->work_area.w,
            .h = (float)app->work_area.h
        };
        for (int i = 0; i < 6; i++)
        {
            SDL_SetRenderDrawColor(app->renderer, 0, 200, 0, 50 + i*41);
            SDL_RenderRect(
                app->renderer,
                &rc
            );
            rc.x++;
            rc.y++;
            rc.w-=2;
            rc.h-=2;
        }
    }

    SDL_RenderPresent(app->renderer);
    app->needs_redraw = false;
}


// Convert key to color values
bool color_from_key(int key, COLORREF &color)
{
    const char* color_keys = "rgbksw";

    if (strchr(color_keys, key)) {
        COLORREF color_map[] = {
                RGB(255, 0, 0),
                RGB(0, 255, 0),
                RGB(0, 0, 255),
                RGB(0, 0, 0),
                RGB(0, 0, 0),
                RGB(255, 255, 255)
        };

        color = color_map[strchr(color_keys, key) - color_keys];
        color = RGB(GetRValue(color), GetGValue(color), GetBValue(color));  // (SDL sort order)
        return true;
    }
    return false;
}


// Safe conversion from "#RRGGBB" string to COLORREF
COLORREF hex_color_to_int(const string& hex)
{
    static const std::regex hexColorRegex("^#([0-9A-Fa-f]{6})$");

    std::smatch match;
    if (!std::regex_match(hex, match, hexColorRegex))
    {
        throw std::invalid_argument("Invalid hex color format: " + hex);
    }

    // Parse each channel
    Uint32 r = std::stoi(hex.substr(1, 2), nullptr, 16);
    Uint32 g = std::stoi(hex.substr(3, 2), nullptr, 16);
    Uint32 b = std::stoi(hex.substr(5, 2), nullptr, 16);

    return (r << 16) | (g << 8) | b;  // Pack RGB as COLORREF
}


// Reads color from JSON (int or "#RRGGBB")
COLORREF get_color_value(const json& j, const string& key, COLORREF default_value)
{
    if (!j.contains(key)) return default_value;

    const auto& value = j.at(key);

    if (value.is_number_integer())
    {
        return value.get<COLORREF>();
    }
    else if (value.is_string())
    {
        return hex_color_to_int(value.get<std::string>());
    }
    else
    {
        throw std::runtime_error("Unsupported type for key: " + key);
    }
}


// Converts COLORREF to "#RRGGBB" string
string int_to_hex_color(COLORREF color)
{
    if (color < 0x000000 || color > 0xFFFFFF) {
        throw std::out_of_range("Color integer out of RGB bounds (0x000000 to 0xFFFFFF)");
    }

    std::ostringstream oss;
    oss << '#' << std::uppercase << std::setfill('0') << std::setw(6) << std::hex << (color & 0xFFFFFF);
    return oss.str();
}


void settings_write(AppContext* app)
{
    char username[32];
    DWORD username_len = 32;
    string filename;

    if (app->is_virgin)
    {
        return;
    }

    ordered_json j = {
        {"info", {
                {"description", "Dragon setup file"},
                {"version", "0.4"},
                {"url", "https://github.com/a-ma72/dragon"},
                {"license", "BSD-2 clause"},
                {"comment_1", "This file contains the settings for the Dragon application."},
                {"comment_2", "It is automatically generated by the Dragon application."},
                {"comment_3", "You may edit this file manually."}
            }
        },
        {"screen_rect_init", {
            app->screen_rect_init.x, app->screen_rect_init.y,
            app->screen_rect_init.w, app->screen_rect_init.h
            }
        },
        {"crop_bottom", (int)app->crop_bottom},
        {"hidden", (bool)app->hidden},
        {"alpha", round_to_precision(app->alpha, 2)},
        {"idle_delay_ms", (int)app->idle_delay_ms},

        {"text_file_name", app->text_file_name},
        {"text_content", app->text_content},
        {"text_font_name", app->text_font_name},
        {"text_font_color", int_to_hex_color(app->text_font_color)},
        {"text_font_size", round_to_precision(app->text_font_size, 1)},
        {"text_scale", round_to_precision(app->text_scale, 4)},
        {"text_rotate", round_to_precision(app->text_rotate, 4)},
        {"text_alpha", round_to_precision(app->text_alpha, 2)},
        {"logo_file_name", app->logo_file_name},
        {"logo_scale", round_to_precision(app->logo_scale, 4)},
        {"logo_alpha", round_to_precision(app->logo_alpha, 2)},
    };

    json objects = json::array();
    for (auto & screen_object : app->screen_objects)
    {
        if (screen_object->valid())
        {
            objects.push_back(screen_object->to_json());
        }
    }
    j["objects"] = objects;

    if (GetUserNameA(username, &username_len))
    {
        filename = string(username) + "_";
    }
    filename += "dragon.settings";

    // Write JSON to a file
    std::ofstream file(app->base_path / filename);

    if (file.is_open())
    {
        file << j.dump(4);
        file.close();
        SDL_Log("Settings written.");
    }
}


bool settings_read_v0_2(AppContext* app, json &j, json &objects)
{
    app->crop_bottom = 0;  // j.value("task_bar_height", app->crop_bottom);
    app->alpha = j.value("alpha", app->alpha * 255.f) / 255.f;
    app->hidden = (bool)j.value("hidden", 0);

    app->logo_file_name = j.value("logo_filename", app->logo_file_name);
    app->logo_scale = j.value("logo_scale", app->logo_scale);
    app->text_content = j.value("text_content", app->text_content);
    app->text_file_name = j["text_file_name"];
    app->text_font_color = get_color_value(j, "text_font_color", app->text_font_color);
    app->text_font_name = j.value("text_font_name", app->text_font_name);
    app->text_font_size = j.value("text_font_size", app->text_font_size);
    app->text_rotate = j.value("text_rotate", app->text_rotate);
    app->text_scale = j.value("text_scale", app->text_scale);

    if (j.contains("textPos") || j.contains("logoPos"))
    {
        objects = json::array();

        if (j.contains("textPos"))
        {
            objects.push_back({
                {"x", j["textPos"][0]},
                {"y", j["textPos"][1]},
                {"text", app->text_content},
                {"font_name", "Freeman-Regular.ttf"},
                {"font_size", app->text_font_size},
                {"font_color", app->text_font_color},
                {"scale", app->text_scale},
                {"rotate", app->text_rotate},
                {"type", "Signature"},
            });
        }

        if (j.contains("logoPos"))
        {
            path full_path = path(app->logo_file_name);

            objects.push_back({
                {"x", j["logoPos"][0]},
                {"y", j["logoPos"][1]},
                {"image_name", full_path.filename().string()},
                {"image_full_path", full_path.string()},
                {"scale", app->logo_scale},
                {"rotate", 0},
                {"type", "Image"},
            });
        }
    }
    app->is_virgin = false;
    return true;
}


bool settings_read_v0_3(AppContext* app, json &j, json &objects)
{
	if (!j.contains("info") || !j["info"].contains("version") || j["info"]["version"] != "0.3")
	{
		return false;
	}

	app->screen_rect_init = SDL_Rect(
		j["screen_rect_init"][0],
		j["screen_rect_init"][1],
		j["screen_rect_init"][2],
		j["screen_rect_init"][3]
	);
	app->crop_bottom = j.value("crop_bottom", app->crop_bottom);
	app->alpha = j.value("alpha", app->alpha * 255.f) / 255.f;
	app->hidden = j.value("hidden", false);
	app->idle_delay_ms = j.value("idle_delay_ms", app->idle_delay_ms);

	app->logo_file_name = j.value("logo_file_name", app->logo_file_name);
	app->logo_scale = j.value("logo_scale", app->logo_scale);
	app->logo_alpha = j.value("logo_alpha", app->logo_alpha);
	app->text_content = j.value("text_content", app->text_content);
	app->text_file_name = j.value("text_file_name", app->text_file_name);
	app->text_font_color = get_color_value(j, "text_font_color", app->text_font_color);
	app->text_font_name = j.value("text_font_name", app->text_font_name);
	app->text_font_size = j.value("text_font_size", app->text_font_size);
	app->text_rotate = j.value("text_rotate", app->text_rotate);
	app->text_scale = j.value("text_scale", app->text_scale);
	app->text_alpha = j.value("text_alpha", app->text_alpha);

    if (j.contains("objects"))
    {
        for (auto & obj : j["objects"])
        {
            if (obj.contains("alpha") && obj["alpha"].is_number_integer())
            {
                obj["alpha"] = (float)obj["alpha"].get<int>() / 255.f;
            }
        }
        objects = j["objects"];
    }
	return true;
}


bool settings_read_v0_4(AppContext* app, json &j, json &objects)
{
    if (!j.contains("info") || !j["info"].contains("version") || j["info"]["version"] != "0.4")
    {
        return false;
    }

    app->screen_rect_init = SDL_Rect(
        j["screen_rect_init"][0],
        j["screen_rect_init"][1],
        j["screen_rect_init"][2],
        j["screen_rect_init"][3]
    );
    app->crop_bottom = j.value("crop_bottom", app->crop_bottom);
    app->alpha = j.value("alpha", app->alpha);
    app->hidden = j.value("hidden", false);
    app->idle_delay_ms = j.value("idle_delay_ms", app->idle_delay_ms);

    app->logo_file_name = j.value("logo_file_name", app->logo_file_name);
    app->logo_scale = j.value("logo_scale", app->logo_scale);
    app->logo_alpha = j.value("logo_alpha", app->logo_alpha);
    app->text_content = j.value("text_content", app->text_content);
    app->text_file_name = j["text_file_name"];
    app->text_font_color = get_color_value(j, "text_font_color", app->text_font_color);
    app->text_font_name = j.value("text_font_name", app->text_font_name);
    app->text_font_size = j.value("text_font_size", app->text_font_size);
    app->text_rotate = j.value("text_rotate", app->text_rotate);
    app->text_scale = j.value("text_scale", app->text_scale);
    app->text_alpha = j.value("text_alpha", app->text_alpha);

	if (j.contains("objects")) objects = j["objects"];

    return true;
}


bool settings_read(AppContext* app, json &objects)
{
    char username[32];
    DWORD username_len = 32;
    string filename;
    json j;

    if (GetUserNameA(username, &username_len))
    {
        filename = string(username) + "_";
    }
    filename += "dragon.settings";

    // Write JSON to a file
    std::ifstream file(app->base_path / filename);

    if (file.good())
    {
        try
        {
            file >> j;

            if (
                !settings_read_v0_4(app, j, objects) &&
                !settings_read_v0_3(app, j, objects) &&
                !settings_read_v0_2(app, j, objects)
            )
            {
                SDL_Log(
                    "Settings file version mismatch, should be \"%s\".\n",
                    version);
                SDL_Log("Using default settings.");
            }
        }
        catch (const std::exception& e)
        {
            file.close();
            SDL_Log("Error reading settings: %s", e.what());
            SDL_Log("Delete settings file and restart to reset settings.");
            SDL_SetError("Error reading settings: %s", e.what());
            return false;
        }
        file.close();
        SDL_Log("Settings read.");
    }

    if (app->text_content.empty())
    {
        app->text_content = signature;

        if (!app->text_file_name.empty())
        {
            auto signature_path = app->base_path / app->text_file_name;
            std::ifstream text_file(signature_path);
            std::string text_content;

            if (text_file.good() && std::getline(text_file, text_content))
            {
                app->text_content = text_content;
            }
        }
    }

    return true;
}
