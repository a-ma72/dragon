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

extern "C" const char *version = "0.3";
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
class Signature;
class Image;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]);
SDL_AppResult SDL_AppIterate(void *appstate);
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event* event);
void SDL_AppQuit(void* appstate, SDL_AppResult result);
SDL_AppResult app_init_failed();

void update_screen_metrics(AppContext* app);
void update_layout_mode(AppContext* app);
void init_screen_objects(AppContext* app, json &objects);
void free_screen_objects(AppContext* app);
void draw_line_bresenham(int x1, int y1, int dx, int dy, int dash_len, int gap_len, void *color, SDL_Surface* surface);
void draw_lines(AppContext *app);
void draw(AppContext* app);
bool color_from_key(int key, COLORREF &color);
string int_to_hex_color(COLORREF color);
COLORREF get_color_value(const json& j, const string& key, COLORREF default_value);
COLORREF hex_color_to_int(const string& hex);
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

#define BLENDED_ALPHA(img_alpha_1, glob_alpha_255) SDL_min(255, (int)((((float)img_alpha_1 * 0.5) + (float)glob_alpha_255 / 255.f * 0.8 + 0.1) * 255.f))


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
    int alpha = 136;
    bool layout_mode = false;
    bool is_virgin = true;
    int idle_delay_ms = 600;
    bool needs_redraw = true;

    // Mouse capturing and dragging (screen objects)
    vector<ScreenObject*> screen_objects;
    ScreenObject *mouse_capture = nullptr;
    SDL_FPoint dragging_origin = {0}, dragging_offset = {0};

    // Line properties
    int line_width = 1;
    COLORREF line_color = RGB(0, 0, 0);
    bool line_dashed = true;
    int line_dashed_len = 10;
    int line_dashed_gap = 10;
    int line_slope_dx = 10;
    int line_slope_dy = 10;

    // Text (initial) properties
    string text_file_name = "signature.txt";
    string text_content = "";
    string text_font_name = "Freeman-Regular.TTF";
    int text_font_size = 78;
    COLORREF text_font_color = RGB(112, 146, 190);
    float text_scale = 0.4f;
    float text_rotate = 0.f;
    int text_alpha = 255;

    // Logo (initial) properties
    string logo_file_name = "dragon.png";
    float logo_scale = 0.2f;
    int logo_alpha = 255;

    // Animated GIF
    bool have_animations = false;
};


class ScreenObject
{
public:
    SDL_FPoint pos;
    SDL_Rect extent;
    float scale;
    float rotate;
    float alpha;
    bool deleted;

    ScreenObject(float x, float y)
    : pos(x, y),
      extent(0, 0, 0, 0),
      deleted(false)
    {};

    virtual ~ScreenObject() = default;
    [[nodiscard]] virtual const char* type_name() const = 0;
    [[nodiscard]] virtual json to_json() const = 0;
    [[nodiscard]] virtual bool valid() const = 0;
    [[nodiscard]] virtual bool hit_test(SDL_FPoint pt) const {return false;}
    virtual bool handle_event(const SDL_Event* event, int &needs_update, AppContext *app) = 0;
    virtual void draw(const SDL_FPoint &pt, int alpha, SDL_Renderer *renderer) const = 0;

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
        int alpha,
        SDL_Renderer *renderer)
    {
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
            verts[i].color = {1.0, 1.0, 1.0, (float)alpha / 255.f};
            verts[i].tex_coord = texcoords[i];
        }

        int indices[6] = {0, 1, 2, 1, 3, 2};
        SDL_RenderGeometry(renderer, texture, verts, 4, indices, 6);
    }
};


class Signature : public ScreenObject
{
public:
    string text;

    string font_name;
    float font_size;
    COLORREF font_color;
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
            int alpha,
            SDL_Renderer *renderer)
            : ScreenObject(x, y),
              texture(nullptr),
              surface(nullptr)
    {
        init(signature, x, y, font_name, font_size, font_color, font_path, scale_by, rotate_by, alpha, renderer);
    }

    Signature(json &j, path &font_path, SDL_Renderer *renderer)
    : ScreenObject(-1, -1),
      texture(nullptr),
      surface(nullptr)
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
                j.value("alpha", 255),
                renderer);
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
            int alpha,
            SDL_Renderer *renderer)
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
        this->alpha = (float)alpha / 255.f;

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
                        alpha)
            );
            if (!surface) break;

            // make a texture from the surface
            texture = SDL_CreateTextureFromSurface(renderer, surface);
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
             {"alpha", (int)(alpha * 255.f)},
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

    void draw(const SDL_FPoint &pt, int alpha, SDL_Renderer *renderer) const override
    {
        if (valid() && renderer)
        {
            SDL_FRect rc = {
                pt.x - (float)extent.x * scale,
                pt.y - (float)extent.y * scale,
                (float)extent.w * scale,
                (float)extent.h * scale
            };
            SDL_SetTextureAlphaMod(texture, (int)BLENDED_ALPHA(this->alpha, alpha));
            SDL_RenderTextureRotated(renderer, texture, nullptr, &rc, rotate, nullptr, SDL_FLIP_NONE);
        }
    }

    bool change_color(COLORREF color, SDL_Renderer *renderer)
    {
        if (!valid() || !renderer || !surface) return false;

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

        SDL_Texture* new_texture = SDL_CreateTextureFromSurface(renderer, surface);

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
    SDL_Surface *surface;
    SDL_Texture *texture;

protected:
    Image(float x, float y) : ScreenObject(x, y), surface((SDL_Surface*)nullptr), texture((SDL_Texture*)nullptr) {}

public:
    Image(
            float x, float y,
            const string &name,
            const path &base_path,
            float scale_by,
            float rotate_by,
            bool flip_horizontal,
            int alpha,
            SDL_Renderer *renderer)
            : ScreenObject(x, y),
              surface((SDL_Surface*)nullptr),
              texture((SDL_Texture*)nullptr)
    {
        full_path = (base_path / name).string();
        init(x, y, name, full_path, scale_by, rotate_by, flip_horizontal, alpha, renderer);
    }

    Image(json &j, SDL_Renderer *renderer)
    : ScreenObject(-1, -1)
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
                j.value("alpha", 255),
                renderer);
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
            int alpha,
            SDL_Renderer *renderer)
    {
        this->name = name;
        this->full_path = full_path;
        pos.x = x;
        pos.y = y;
        scale = scale_by;
        rotate = rotate_by;
        this->flip_horizontal = flip_horizontal;
        this->alpha = (float)alpha / 255.f;

        if (!renderer) return;

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
                    texture = SDL_CreateTextureFromSurface(renderer, rgba_surface);
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
             {"alpha", (int)(alpha * 255.f)},
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

    void draw(const SDL_FPoint &pt, int alpha, SDL_Renderer *renderer) const override
    {
        if (!valid()) return;

        float x = pt.x - (float)extent.x;
        float y = pt.y - (float)extent.y;
        float w = (float)extent.w;
        float h = (float)extent.h;

        render_transformed_texture(
                texture,
                x, y, w, h,
                scale, rotate,
                flip_horizontal, false,
                (int)BLENDED_ALPHA(this->alpha, alpha),
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
    vector<frame_info_t> frame_info;
    GifFileType *gif;

    AnimatedGif(
            float x, float y,
            const string &name,
            const path &base_path,
            float scale_by,
            float rotate_by,
            bool flip_horizontal,
            int alpha,
            bool cache_frames,
            SDL_Renderer *renderer)
    : Image(x, y),
      gif((GifFileType*)nullptr)
    {
        full_path = (base_path / name).string();
        init(x, y, name, full_path, scale_by, rotate_by, flip_horizontal, alpha, cache_frames, renderer);
    }

    AnimatedGif(json &j, SDL_Renderer *renderer)
    : Image(-1, -1),
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
                j.value("alpha", 255),
                j.value("cache_frames", true),
                renderer);
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
            int alpha,
            bool cache_frames,
            SDL_Renderer *renderer)
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
        this->alpha = (float)alpha / 255.f;
        this->cache_frames = cache_frames;

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
            render_frame(renderer);
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
             {"alpha", (int)(alpha * 255.f)},
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

    void draw(const SDL_FPoint &pt, int alpha, SDL_Renderer *renderer) const override
    {
        if (!valid()) return;

        float x = pt.x - (float)extent.x;
        float y = pt.y - (float)extent.y;
        float w = (float)extent.w;
        float h = (float)extent.h;

        render_transformed_texture(
                texture,
                x, y, w, h,
                scale, rotate,
                flip_horizontal, false,
                (int)BLENDED_ALPHA(this->alpha, alpha),
                renderer);
    }

    void render_frame(SDL_Renderer *renderer)
    {
        SavedImage *frame;
        int left, top, width, height;
        const ColorMapObject *color_map;
        const Uint8 *raster_bits;
        int bg_color;
        frame_info_t *frame_info = &this->frame_info[current_frame];
        int transparent_color;
        Uint32 *addr, pixel;

        if (!valid() || !renderer) return;

        if (!frame_info->texture_outdated)
        {
            texture = frame_info->texture;
            recent_disposal = frame_info->disposal_mode;
            return;
        }

        if (!surface || surface->format != SDL_PIXELFORMAT_RGBA8888) return;

        // Prepare canvas
        if (!gif || !gif->SavedImages) return;
        frame = &gif->SavedImages[current_frame];
        bg_color = gif->SBackGroundColor;
        raster_bits = frame->RasterBits;
        if (!raster_bits) return;
        color_map = frame->ImageDesc.ColorMap ? frame->ImageDesc.ColorMap : gif->SColorMap;
        if (!color_map) return;

        switch (recent_disposal)
        {
            case DISPOSAL_UNSPECIFIED:
            case DISPOSE_BACKGROUND:
            {
                if (bg_color == NO_TRANSPARENT_COLOR || bg_color < color_map->ColorCount)
                {
                    SDL_ClearSurface(surface, 0, 0, 0, 0);
                }
                else
                {
                    GifColorType *color = &color_map->Colors[bg_color];
                    SDL_ClearSurface(
                            surface,
                            (float)color->Red / 255.f,
                            (float)color->Green / 255.f,
                            (float)color->Blue / 255.f,
                            1.f);
                }
                break;
            }
            case DISPOSE_DO_NOT:
                break;
        }

        // gif->frame is 8-bit indexed, so we have to convert to RGBA8888
        left = frame->ImageDesc.Left;
        top = frame->ImageDesc.Top;
        width = frame->ImageDesc.Width;
        height = frame->ImageDesc.Height;

        transparent_color = frame_info->transparent_color_index;

        SDL_LockSurface(surface);
        for (int i = 0; i < height; i++)
        {
            addr = (Uint32*)(void*)((Uint8*)surface->pixels + (i + top) * surface->pitch) + left;
            for (int j = 0; j < width; j++)
            {
                int color_index = *raster_bits++;

                if (transparent_color == NO_TRANSPARENT_COLOR || transparent_color != color_index)
                {
                    GifColorType *color;

                    if (color_index < color_map->ColorCount)
                    {
                        color = &color_map->Colors[color_index];
                        pixel = SDL_MapSurfaceRGB(
                                surface,
                                color->Red,
                                color->Green,
                                color->Blue);
                        *addr = pixel;
                    }
                }
                addr++;
            }
        }
        SDL_UnlockSurface(surface);

        if (frame_info->texture)
        {
            SDL_DestroyTexture(frame_info->texture);
            frame_info->texture = (SDL_Texture *)nullptr;
            frame_info->texture_outdated = true;
        }
        texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (cache_frames)
        {
            frame_info->texture = texture;
            frame_info->texture_outdated = false;
        }

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

    // Initialize screen objects
    init_screen_objects(app, objects);

    // If no screen objects defined in settings file, create two default objects
    if (app->screen_objects.empty())
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
                255,
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
                255,
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

    if (app->line_dashed && app->line_dashed_gap > 0 && app->line_width > 0 && !app->hidden)
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

    for (auto it=app->screen_objects.begin(); it!=app->screen_objects.end(); ++it)
    {
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
            break;
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
                app->alpha = SDL_max(0, app->alpha - 5);
                app->is_virgin = false;
            }
            else
            {
                app->alpha = SDL_min(255, app->alpha + 5);
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
        COLORREF color;

        app->needs_redraw = true;

        if (color_from_key((int)event->key.key, color))
        {
            app->line_color = (COLORREF) color;
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_LEFT)
        {
            app->alpha = SDL_max(0, app->alpha - 17);
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_RIGHT)
        {
            app->alpha = SDL_min(255, app->alpha + 17);
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_X)
        {
            app->app_quit = SDL_APP_SUCCESS;
        }
        else if (event->key.key == SDLK_0)
        {
            app->line_width = 0;
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_1)
        {
            app->line_width = 1;
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_2)
        {
            app->line_width = 2;
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_3)
        {
            app->line_width = 3;
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_4)
        {
            app->line_width = 4;
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_5)
        {
            app->line_width = 5;
            app->is_virgin = false;
        }
        else if ((event->key.key == SDLK_SPACE) or (event->key.key == SDLK_RETURN))
        {
            app->layout_mode = !app->layout_mode;
            update_layout_mode(app);
        }
        else if (event->key.key == SDLK_D)
        {
            app->line_dashed = !app->line_dashed;
            app->is_virgin = false;
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
        255,
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
                        255,
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
                        255,
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


void draw_line_bresenham(
        int x1, int y1,
        int dx, int dy,
        int dash_len, int gap_len,
        void *color,
        SDL_Surface* surface)
{
    if (!surface || !color) return;

    int sx = (dx >= 0) ? 1 : -1;
    int sy = (dy >= 0) ? 1 : -1;
    int err = dx - dy;
    int bpp = 4;
    int pitch = surface->pitch;
    void *addr = nullptr;
    int n = 10000;
    int i = dist(gen) % (dash_len + gap_len);

    dx = SDL_abs(dx);
    dy = SDL_abs(dy);

    while (n--)
    {
        int e2;

        i++;

        if (y1 >= 0 && y1 < surface->h && x1 >= 0 && x1 < surface->w)
        {
            if (!addr)
            {
                addr = (Uint8 *)surface->pixels + y1 * pitch + x1 * bpp;
            }
            if (gap_len == 0 || i % (gap_len + dash_len) < dash_len)
            {
                memcpy(addr, color, bpp);
            }
        }
        else
        {
            if (addr) break;
        }

        e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x1 += sx;
            if (addr) addr = (Uint8 *)addr + sx * bpp;
        }
        if (e2 < dx)
        {
            err += dx;
            y1 += sy;
            if (addr) addr = (Uint8 *)addr + sy * pitch;
        }
    }
}


void draw_lines(AppContext* app)
{
    int width = app->work_area.w;
    int height = app->work_area.h;
    int dash_len = app->line_dashed_len;
    int gap_len = app->line_dashed ? app->line_dashed_gap : 0;
    int slope_dx = app->line_slope_dx;
    int slope_dy = app->line_slope_dy;
    Uint32 pixel;
    SDL_FRect rect = {0, 0, (float)width, (float)height};
    SDL_Point extent;
    SDL_Surface *surface;
    SDL_Texture *texture;
    int negative_slope = false;
    int jitter = 0;

    gen.seed((unsigned)app->idle_ticks);

    surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);
    SDL_ClearSurface(surface, 0, 0, 0, 0);

    pixel = SDL_MapSurfaceRGBA(
                surface,
                GetRValue(app->line_color),
                GetGValue(app->line_color),
                GetBValue(app->line_color),
                app->alpha);

    if (slope_dy < 0)
    {
        slope_dy = -slope_dy;
        slope_dx = -slope_dx;
    }
    if (slope_dx < 0)
    {
        negative_slope = true;
        slope_dx = -slope_dx;
    }

    if (app->line_dashed)
    {
        jitter = SDL_min(
                    slope_dx ? slope_dx : slope_dy,
                    slope_dy ? slope_dy : slope_dx);
    }

    if (slope_dx > slope_dy)
    {
        extent = {(width + height), (int)((float)(width + height) * (float)slope_dy / (float)slope_dx)};
    }
    else
    {
        extent = {(int)((float)(width + height) * (float)slope_dx / (float)slope_dy), (width + height)};
    }

    SDL_LockSurface(surface);
    int j = jitter ? dist(gen) % jitter : 0;
    for (int d = j; d < j + app->line_width; d++)
    {
        if (slope_dx == 0)
        {
            for (int y = d; y < height; y += slope_dy)
            {
                draw_line_bresenham(0, y + d, 1, 0, dash_len, gap_len, &pixel, surface);
            }
        }
        else if (slope_dy == 0)
        {
            for (int x = 0; x < width; x += slope_dx)
            {
                draw_line_bresenham(x + d, 0, 0, 1, dash_len, gap_len, &pixel, surface);
            }
        }
        else
        {
            if (slope_dx <= slope_dy)
            {
                int y1 = height, dy = -slope_dy;
                if (negative_slope)
                {
                    y1 = 0;
                    dy = slope_dy;
                }
                for (int x = -extent.x; x < width; x += slope_dx)
                {
                    draw_line_bresenham(x + d, y1, slope_dx, dy, dash_len, gap_len, &pixel, surface);
                }
            }
            else
            {
                int x1 = width, dx = -slope_dx;
                if (negative_slope)
                {
                    x1 = 0;
                    dx = slope_dx;
                }
                for (int y = -extent.y; y < height; y += slope_dy)
                {
                    draw_line_bresenham(x1, y + d, dx, slope_dy, dash_len, gap_len, &pixel, surface);
                }
            }
        }
    }
    SDL_UnlockSurface(surface);

    texture = SDL_CreateTextureFromSurface(app->renderer, surface);
    SDL_RenderTexture(app->renderer, texture, nullptr, &rect);
    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
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
        if (app->line_width > 0 && !app->mouse_capture) {
            draw_lines(app);
        }

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
                {"version", version},
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
        {"alpha", (int)app->alpha},
        {"idle_delay_ms", (int)app->idle_delay_ms},
        {"line_width", (int)app->line_width},
        {"line_color", int_to_hex_color(app->line_color)},
        {"line_dashed", (bool)app->line_dashed},
        {"line_dashed_len", (int)app->line_dashed_len},
        {"line_dashed_gap", (int)app->line_dashed_gap},
        {"line_slope_dx", (int)app->line_slope_dx},
        {"line_slope_dy", (int)app->line_slope_dy},
        {"text_file_name", app->text_file_name},
        {"text_content", app->text_content},
        {"text_font_name", app->text_font_name},
        {"text_font_color", int_to_hex_color(app->text_font_color)},
        {"text_font_size", round_to_precision(app->text_font_size, 1)},
        {"text_scale", round_to_precision(app->text_scale, 4)},
        {"text_rotate", round_to_precision(app->text_rotate, 4)},
        {"text_alpha", (int)app->text_alpha},
        {"logo_file_name", app->logo_file_name},
        {"logo_scale", round_to_precision(app->logo_scale, 4)},
        {"logo_alpha", (int)app->logo_alpha},
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
    app->alpha = j.value("alpha", app->alpha);
    app->hidden = (bool)j.value("hidden", 0);
    app->line_width = j.value("line_width", app->line_width);
    app->line_color = get_color_value(j, "line_color", app->line_color);
    app->line_dashed = j.value("line_dashed", app->line_dashed);
    app->line_dashed_gap = j.value("line_dashed_gap", app->line_dashed_gap);
    app->line_dashed_len = j.value("line_dashed_len", app->line_dashed_len);
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
                {"text_font_name", "Freeman-Regular.ttf"},
                {"scale", app->text_scale},
                {"rotate", app->text_rotate},
                {"color", app->text_font_color},
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
    return true;
}


bool settings_read_v0_3(AppContext* app, json &j, json &objects)
{
    if (!j.contains("info") || !j["info"].contains("version") || j["info"]["version"] != version)
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
    app->line_width = j.value("line_width", app->line_width);
    app->line_color = get_color_value(j, "line_color", app->line_color);
    app->line_dashed = j.value("line_dashed", app->line_dashed);
    app->line_dashed_gap = j.value("line_dashed_gap", app->line_dashed_gap);
    app->line_dashed_len = j.value("line_dashed_len", app->line_dashed_len);
    app->line_slope_dx = j.value("line_slope_dx", app->line_slope_dx);
    app->line_slope_dy = j.value("line_slope_dy", app->line_slope_dy);
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
