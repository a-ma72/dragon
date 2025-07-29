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
#include <windows.h>
#include "json.hpp"
#include "gif_lib.h"

const char *version = "0.2";

#undef min
#undef max

using std::min;
using std::max;
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
void update_screen_metrics(AppContext* app);
void update_layout_mode(AppContext* app);
void init_screen_objects(AppContext* app, json &objects);
void free_screen_objects(AppContext* app);
void calc_coords([[maybe_unused]] AppContext *app, int i, POINT pt[2], int d);
void draw_dashed_line(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, int dashLength, int gapLength);
void draw_lines(AppContext *app);
void draw(AppContext* app);
bool color_from_key(int key, COLORREF &color);
string int_to_hex_color(COLORREF color);
COLORREF get_color_value(const json& j, const string& key, COLORREF default_value);
COLORREF hex_color_to_int(const string& hex);
void settings_write(AppContext* app);
bool settings_read(AppContext* app, json &objects);

#define UPDATE_VIEW_CHANGED 1
#define UPDATE_SETTINGS_CHANGED 2


struct AppContext {
    path base_path;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_AppResult app_quit = SDL_APP_CONTINUE;
    SDL_Cursor *handCursor = nullptr;

    HWND hwnd = nullptr;
    SDL_Rect screen_rect_init = {-1, -1, -1, -1};  // To initialize `screen_rect` (editable in settimgs file)
    SDL_Rect screen_rect = {0, 0, 800, 600};  // To initialize `lines_area`
    SDL_Rect work_area = {0};  // Physical overlay window position
    int crop_bottom = 48;  // Crops work_area
    float center_x = 500, center_y = 500;

    bool hidden = false;
    int alpha = 136;
    bool layout_mode = false;
    bool is_virgin = true;
    int idle_delay_ms = 600;
    int gif_delay_ms = 100;
    bool needs_redraw = true;

    // Mouse capturing and dragging (screen objects)
    vector<ScreenObject*> screen_objects;
    ScreenObject *mouse_capture = nullptr;
    SDL_FPoint dragging_origin = {0}, dragging_offset = {0};

    // Line properties
    int line_width = 1;
    int line_color = RGB(0, 0, 0);
    bool line_dashed = true;
    int line_dashed_len = 10;
    int line_dashed_gap = 10;

    // Text (initial) properties
    string text_file_name = "signature.txt";  // Not used
    string text_content = "Dragon Signature";
    string text_font_name = "Freeman-Regular.TTF";
    int text_font_size = 78;
    int text_font_color = RGB(112, 146, 190);
    float text_scale = 0.4f;
    float text_rotate = 0.0f;
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

    ScreenObject(float x, float y)
    : pos(x, y),
      extent(0, 0, 0, 0)
    {};

    virtual ~ScreenObject() = default;
    [[nodiscard]] virtual const char* type_name() const = 0;
    [[nodiscard]] virtual json to_json() const = 0;
    [[nodiscard]]
    virtual bool valid() const = 0;
    [[nodiscard]]
    virtual bool hit_test(SDL_FPoint pt) const {return false;}
    virtual bool handle_event(const SDL_Event* event, int &needs_update, AppContext *app) = 0;
    virtual void draw(const SDL_FPoint &pt, int alpha, SDL_Renderer *renderer) const = 0;

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
            {0.0f, 0.0f}, {1.0f, 0.0f},
            {0.0f, 1.0f}, {1.0f, 1.0f}
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
        float cx = x + w / 2.0f;
        float cy = y + h / 2.0f;

        // Apply scaling
        float hw = (w / 2.0f) * scale;
        float hh = (h / 2.0f) * scale;

        SDL_FPoint corners[4] = {
            {-hw, -hh}, {+hw, -hh},
            {-hw, +hh}, {+hw, +hh}
        };

        // Rotation
        float angle_rad = rotate * (float)M_PI / 180.0f;
        float cos_a = cosf(angle_rad);
        float sin_a = sinf(angle_rad);

        for (int i = 0; i < 4; i++) {
            float rx = corners[i].x * cos_a - corners[i].y * sin_a;
            float ry = corners[i].x * sin_a + corners[i].y * cos_a;

            verts[i].position.x = cx + rx;
            verts[i].position.y = cy + ry;
            verts[i].color = {1.0, 1.0, 1.0, (float)alpha / 255.0f};
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
        init(
                j.value("text", "Example"),
                j["x"], j["y"],
                j.value("font_name", "Freeman-Regular.TTF"),
                j.value("font_size", 80.0f),
                get_color_value(j, "font_color", 0xffffff),
                font_path,
                j.value("scale", 1.0f),
                j.value("rotate", 0.0f),
                j.value("alpha", 255),
                renderer);
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

        pos.x = x;
        pos.y = y;
        scale = scale_by;
        rotate = rotate_by;
        text = signature;
        this->font_name = font_name;
        this->font_size = font_size;
        this->font_color = font_color;
        this->alpha = (float)alpha / 255.0f;

        for (;;)
        {
            auto font_fullpath = font_path / font_name;
            if (!renderer) break;

            font = TTF_OpenFont(font_fullpath.string().c_str(), font_size);
            if (!font) break;

            // render the font to a surface
            surface = TTF_RenderText_Blended(
                font,
                signature.c_str(), signature.length(),
                SDL_Color(
                    GetBValue(font_color),
                    GetGValue(font_color),
                    GetRValue(font_color)
                )
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
        return json( {
             {"x", pos.x},
             {"y", pos.y},
             {"text", text},
             {"scale", scale},
             {"rotate", rotate},
             {"alpha", (int)(alpha * 255.0f)},
             {"font_name", font_name},
             {"font_size", font_size},
             {"font_color", int_to_hex_color(font_color)},
             {"type", type_name()}
        } );
    }

    [[nodiscard]]
    bool valid() const override
    {
        return (bool) texture;
    }

    [[nodiscard]]
    bool hit_test(SDL_FPoint pt) const override
    {
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
        if (!app->layout_mode) return false;

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
                        alpha = max(0.0f, alpha - (5.0f / 255.0f));
                    }
                    else
                    {
                        alpha = min(1.0f, alpha + (5.0f / 255.0f));
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
                SDL_FPoint pt;

                SDL_GetGlobalMouseState(&pt.x, &pt.y);

                if (hit_test(pt))
                {
                    change_color((int) color, app->renderer);
                    needs_update = UPDATE_SETTINGS_CHANGED;
                    return true;
                }
            }
        }

        return false;
    }

    void draw(const SDL_FPoint &pt, int alpha, SDL_Renderer *renderer) const override
    {
        if (texture && renderer)
        {
            SDL_FRect rc = {
                pt.x - (float)extent.x * scale,
                pt.y - (float)extent.y * scale,
                (float)extent.w * scale,
                (float)extent.h * scale
            };
            SDL_SetTextureAlphaMod(texture, (int)((alpha * 0.8 + 50) * this->alpha));
            SDL_RenderTextureRotated(renderer, texture, nullptr, &rc, rotate, nullptr, SDL_FLIP_NONE);
        }
    }

    bool change_color(COLORREF color, SDL_Renderer *renderer)
    {
        if (!renderer || !surface) return false;

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
        init(
            j["x"], j["y"],
            j.value("image_name", ""),
            j["image_full_path"],
            j.value("scale", 1.0f),
            j.value("rotate", 0.0f),
            j.value("flip_horizontal", false),
            j.value("alpha", 255),
            renderer);
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
        this->alpha = (float)alpha / 255.0f;

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
        return json( {
             {"x", pos.x},
             {"y", pos.y},
             {"image_name", name},
             {"image_full_path", full_path},
             {"scale", scale},
             {"rotate", scale},
             {"flip_horizontal", flip_horizontal},
             {"alpha", (int)(alpha * 255.0f)},
             {"type", type_name()}
        });
    }

    [[nodiscard]]
    bool valid() const override
    {
        return (bool)texture;
    }

    [[nodiscard]]
    bool hit_test(SDL_FPoint pt) const override
    {
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
        if (!app->layout_mode) return false;

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
                    needs_update = UPDATE_SETTINGS_CHANGED;
                    return true;
                }
                else
                {
                    // Change text alpha
                    if (event->wheel.y < 0)
                    {
                        alpha = max(0.0f, alpha - (5.0f / 255.0f));
                    }
                    else
                    {
                        alpha = min(1.0f, alpha + (5.0f / 255.0f));
                    }
                    needs_update = UPDATE_SETTINGS_CHANGED;
                    return true;
                }
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
                SDL_FPoint pt;

                SDL_GetGlobalMouseState(&pt.x, &pt.y);

                if (hit_test(pt))
                {
                    flip_horizontal = !flip_horizontal;
                    needs_update = UPDATE_SETTINGS_CHANGED;
                    return true;
                }
            }
        }

        return false;
    }

    void draw(const SDL_FPoint &pt, int alpha, SDL_Renderer *renderer) const override
    {
        float x = pt.x - (float)extent.x;
        float y = pt.y - (float)extent.y;
        float w = (float)extent.w;
        float h = (float)extent.h;

        render_transformed_texture(
                texture,
                x, y, w, h,
                scale, rotate,
                flip_horizontal, false,
                (int)((alpha * 0.8 + 50) * this->alpha),
                renderer);
    }

};


class AnimatedGif : public Image
{
    struct frame_info_t {
        int delay_ms;
        int transparent_color_index;
        int disposal_mode;
    };

public:
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
            SDL_Renderer *renderer)
    : Image(x, y),
      gif((GifFileType*)nullptr)
    {
        full_path = (base_path / name).string();
        init(x, y, name, full_path, scale_by, rotate_by, flip_horizontal, alpha, renderer);
    }

    AnimatedGif(json &j, SDL_Renderer *renderer)
    : Image(-1, -1),
      gif((GifFileType*)nullptr)
    {
        init(
                j["x"], j["y"],
                j.value("image_name", ""),
                j["image_full_path"],
                j.value("scale", 1.0f),
                j.value("rotate", 0.0f),
                j.value("flip_horizontal", false),
                j.value("alpha", 255),
                renderer);
    }

    [[nodiscard]]
    const char* type_name() const override {return "AnimatedGif";}

    ~AnimatedGif() override {
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
        this->alpha = (float)alpha / 255.0f;

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
                    .disposal_mode = DISPOSAL_UNSPECIFIED
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
        return json( {
             {"x", pos.x},
             {"y", pos.y},
             {"image_name", name},
             {"image_full_path", full_path},
             {"scale", scale},
             {"rotate", rotate},
             {"flip_horizontal", flip_horizontal},
             {"alpha", (int)(alpha * 255.0f)},
             {"type", type_name()}
        });
    }

    void draw(const SDL_FPoint &pt, int alpha, SDL_Renderer *renderer) const override
    {
        float x = pt.x - (float)extent.x;
        float y = pt.y - (float)extent.y;
        float w = (float)extent.w;
        float h = (float)extent.h;

        render_transformed_texture(
                texture,
                x, y, w, h,
                scale, rotate,
                flip_horizontal, false,
                (int)((alpha * 0.8 + 50) * this->alpha),
                renderer);
    }

    void render_frame(SDL_Renderer *renderer)
    {
        SavedImage *frame;
        int left, top, width, height;
        const ColorMapObject *color_map;
        const uint8_t *raster_bits;
        int bg_color;
        frame_info_t *frame_info = &this->frame_info[current_frame];
        int transparent_color;
        uint32_t *addr, pixel;

        if (!renderer || !surface || surface->format != SDL_PIXELFORMAT_RGBA8888 || !gif || !gif->SavedImages) return;

		bg_color = gif->SBackGroundColor;
		transparent_color = frame_info->transparent_color_index;

        frame = &gif->SavedImages[current_frame];
        raster_bits = frame->RasterBits;
        if (!raster_bits) return;
        color_map = frame->ImageDesc.ColorMap ? frame->ImageDesc.ColorMap : gif->SColorMap;
        if (!color_map) return;

        left = frame->ImageDesc.Left;
        top = frame->ImageDesc.Top;
        width = frame->ImageDesc.Width;
        height = frame->ImageDesc.Height;

        // Prepare canvas
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
                    SDL_ClearSurface(surface, color->Red / 255.0f, color->Green / 255.0f, color->Blue / 255.0f, 1);
                }
                break;
            }
            case DISPOSE_DO_NOT:
                break;
        }

        // gif->frame is 8-bit indexed, so we have to convert to RGBA8888
        SDL_LockSurface(surface);
        for (int i = 0; i < height; i++)
        {
            addr = (uint32_t*)((uint8_t*)surface->pixels + (i + top) * surface->pitch) + left;
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

        SDL_DestroyTexture(texture);
        texture = SDL_CreateTextureFromSurface(renderer, surface);

        recent_disposal = frame_info->disposal_mode;
    }

};


SDL_AppResult SDL_Fail()
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
        return SDL_Fail();
    }

    // Read settings
    if (!settings_read(app, objects))
    {
        return SDL_APP_FAILURE;
    }

    // Update screen metrics
    update_screen_metrics(app);

    // Init SDL
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        return SDL_Fail();
    }

    // Init TTF
    if (!TTF_Init())
    {
        return SDL_Fail();
    }

    // Create hand cursor
    app->handCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    if (!app->handCursor)
    {
        return SDL_Fail();
    }

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
        return SDL_Fail();
    }
    SDL_SetWindowPosition(app->window, app->work_area.x, app->work_area.y);

    // Retrieve the HWND from the SDL window
    SDL_PropertiesID props = SDL_GetWindowProperties(app->window);
    if (!props)
    {
        return SDL_Fail();
    }
    else
    {
        app->hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (!app->hwnd)
        {
            return SDL_Fail();
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
        return SDL_Fail();
    }
    SDL_SetRenderVSync(app->renderer, SDL_RENDERER_VSYNC_ADAPTIVE);   // enable vsync

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
            return SDL_Fail();
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
    int timeout = app->idle_delay_ms;
    Uint64 ticks = SDL_GetTicks();

    if (app->app_quit != SDL_APP_CONTINUE) return app->app_quit;

    if (app->needs_redraw)
    {
        draw(app);
        app->needs_redraw = false;
    }

    if (app->have_animations)
    {
        for (auto &obj: app->screen_objects)
        {
            auto* gif = dynamic_cast<AnimatedGif *>(obj);

            if (gif)
            {
                Uint64 next_frame_ticks = gif->latest_ticks + gif->frame_info[gif->current_frame].delay_ms;
                if (next_frame_ticks < ticks)
                {
                    gif->current_frame = (gif->current_frame + 1) % gif->frame_count;
                    gif->render_frame(app->renderer);
                    gif->latest_ticks = ticks;
                    app->needs_redraw = true;
                }
                else
                {
                    timeout = min(timeout, (int) (next_frame_ticks - ticks));
                }
            }
        }

        if (!app->needs_redraw)
        {
            // Wait for next event with timeout
            SDL_WaitEventTimeout(nullptr, timeout);
        }
    }
    else
    {
        // Wait until next event occurs
        SDL_WaitEvent(nullptr);
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
                app->alpha = max(0, app->alpha - 5);
                app->is_virgin = false;
            }
            else
            {
                app->alpha = min(255, app->alpha + 5);
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
            app->line_color = (int) color;
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_LEFT)
        {
            app->alpha = max(0, app->alpha - 17);
            app->is_virgin = false;
        }
        else if (event->key.key == SDLK_RIGHT)
        {
            app->alpha = min(255, app->alpha + 17);
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
        else
        {
            app->needs_redraw = false;
        }
    }

    return SDL_APP_CONTINUE;
}


void update_screen_metrics(AppContext* app)
{
    HDC hdc = GetDC(nullptr);
    int physicalWidth  = GetDeviceCaps(hdc, DESKTOPHORZRES);
    int physicalHeight = GetDeviceCaps(hdc, DESKTOPVERTRES);

    if (app->screen_rect_init.x >= 0)
    {
        app->screen_rect.x = app->screen_rect_init.x;
    }
    else
    {
        app->screen_rect.x = 0;
    }
    if (app->screen_rect_init.y >= 0)
    {
        app->screen_rect.y = app->screen_rect_init.y;
    }
    else
    {
        app->screen_rect.y = 0;
    }
    if (app->screen_rect_init.w >= 0)
    {
        app->screen_rect.w = app->screen_rect_init.w;
    }
    else
    {
        app->screen_rect.w = physicalWidth;
    }
    if (app->screen_rect_init.h >= 0)
    {
        app->screen_rect.h = app->screen_rect_init.h;
    }
    else
    {
        app->screen_rect.h = physicalHeight;
    }
    app->work_area = app->screen_rect;

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

    app->center_x = (float) app->work_area.x + (float) app->work_area.w / 2.0f;
    app->center_y = (float) app->work_area.y + (float) app->work_area.h / 2.0f;
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
        if (object.contains("type")) {
            if (!object.contains("x") || object["x"] < 0)
            {
                object["x"] = app->center_x;
            }
            if (!object.contains("y") || object["y"] < 0)
            {
                object["y"] = app->center_y;
            }
            if (object["type"] == "Signature") {
                app->screen_objects.push_back(
                        new Signature(
                                object,
                                app->base_path,
                                app->renderer));
            } else if (object["type"] == "Image") {
                app->screen_objects.push_back(
                        new Image(
                                object,
                                app->renderer));
            } else if (object["type"] == "AnimatedGif") {
                app->screen_objects.push_back(
                        new AnimatedGif(
                                object,
                                app->renderer));
                app->have_animations = true;
            }
            // (Silently ignore unknown object types)
        }
    }
}


void free_screen_objects(AppContext* app)
{
    for (auto & screen_object : app->screen_objects)
    {
        delete screen_object;
    }
    app->screen_objects.clear();
}


void calc_coords([[maybe_unused]] AppContext* app, int i, POINT pt[2], int d)
{
    // int width = app->workArea.w;
    // int height = app->workArea.h;
    int r = dist(gen) % 7;

    pt[0].x = 0 - r;
    pt[0].y = i + d + r;
    pt[1].x = i + d + r;
    pt[1].y = 0 - r;
}


// Draws a dashed (hatched) line from (x1, y1) to (x2, y2)
// dashLength: length of each drawn segment
// gapLength: length of the gap between segments
void draw_dashed_line(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, int dashLength, int gapLength)
{
    // Calculate differences and total distance
    auto dx = (float)(x2 - x1);
    auto dy = (float)(y2 - y1);
    float distance = std::sqrt(dx * dx + dy * dy);

    if (distance == 0)
    {
        return;
    }

    // Calculate the unit vector components along the line
    float ux = dx / distance;
    float uy = dy / distance;

    // Total length for each dash+gap cycle
    auto cycleLength = (float)(dashLength + gapLength);
    int cycles = static_cast<int>(distance / cycleLength);

    auto currentX = static_cast<float>(x1);
    auto currentY = static_cast<float>(y1);

    for (int i = 0; i < cycles; ++i)
    {
        // Calculate end position of the dash
        auto dashEndX = (float)currentX + ux * (float)dashLength;
        auto dashEndY = (float)(currentY) + uy * (float)dashLength;

        // Draw the dash segment
        SDL_RenderLine(
            renderer,
            currentX,
            currentY,
            dashEndX,
            dashEndY
        );

        // Move current position forward by one full cycle (dash + gap)
        currentX += ux * cycleLength;
        currentY += uy * cycleLength;
    }

    // Draw the final partial dash if any remains
    float remaining = distance - (float)cycles * cycleLength;
    if (remaining > 0)
    {
        float finalDash = (remaining >= (float)dashLength) ? (float)dashLength : remaining;
        float dashEndX = currentX + ux * finalDash;
        float dashEndY = currentY + uy * finalDash;
        SDL_RenderLine(
            renderer,
            currentX,
            currentY,
            dashEndX,
            dashEndY
        );
    }
}


void draw_lines(AppContext* app)
{
    int width = app->work_area.w;
    int height = app->work_area.h;
    int dashed_len = app->line_dashed_len;
    int dashed_gap = app->line_dashed_gap;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        app->renderer,
        GetBValue(app->line_color), GetGValue(app->line_color), GetRValue(app->line_color),
        app->alpha
    );

    for (int i = 0; i < width + height; i += 10)
    {
        POINT pt[2];
        for (int j = 0; j < app->line_width; j++)
        {
            calc_coords(app, i, pt, j);
            if (app->line_dashed)
            {
                draw_dashed_line(
                    app->renderer,
                    pt[0].x, pt[0].y,
                    pt[1].x, pt[1].y,
                    dashed_len, dashed_gap
                );
            }
            else
            {
                SDL_RenderLine(
                    app->renderer,
                    (float)pt[0].x, (float)pt[0].y,
                    (float)pt[1].x, (float)pt[1].y
                );
            }
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
        color = RGB(GetBValue(color), GetGValue(color), GetRValue(color));  // (SDL sort order)
        return true;
    }
    return false;
}


// Safe conversion from "#RRGGBB" string to integer RGB value
COLORREF hex_color_to_int(const string& hex)
{
    static const std::regex hexColorRegex("^#([0-9A-Fa-f]{6})$");

    std::smatch match;
    if (!std::regex_match(hex, match, hexColorRegex))
    {
        throw std::invalid_argument("Invalid hex color format: " + hex);
    }

    // Parse each channel
    uint32_t r = std::stoi(hex.substr(1, 2), nullptr, 16);
    uint32_t g = std::stoi(hex.substr(3, 2), nullptr, 16);
    uint32_t b = std::stoi(hex.substr(5, 2), nullptr, 16);

    return (r << 16) | (g << 8) | b;  // Pack RGB as int
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


// Converts RGB integer (e.g., 0xRRGGBB) to "#RRGGBB" string
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
        {"crop_bottom", app->crop_bottom},
        {"hidden", app->hidden},
        {"alpha", app->alpha},
        {"idle_delay_ms", app->idle_delay_ms},
        {"gif_delay_ms", app->gif_delay_ms},
        {"line_width", app->line_width},
        {"line_color", int_to_hex_color(app->line_color)},
        {"line_dashed", app->line_dashed},
        {"line_dashed_len", app->line_dashed_len},
        {"line_dashed_gap", app->line_dashed_gap},
        // {"text_file_name", app->text_file_name},
        {"text_content", app->text_content},
        {"text_font_name", app->text_font_name},
        {"text_font_color", int_to_hex_color(app->text_font_color)},
        {"text_font_size", app->text_font_size},
        {"text_scale", app->text_scale},
        {"text_rotate", app->text_rotate},
        {"text_alpha", app->text_alpha},
        {"logo_file_name", app->logo_file_name},
        {"logo_scale", app->logo_scale},
        {"logo_alpha", app->logo_alpha},
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
        }
        catch (const std::exception& e)
        {
            SDL_Log("Error reading settings: %s", e.what());
            SDL_Log("Delete settings file and restart to reset settings.");
            file.close();
            app->app_quit = SDL_APP_FAILURE;
            SDL_Quit();
            return false;
        }
        file.close();

        try
        {
            if (!j.contains("info") || !j["info"].contains("version") || j["info"]["version"] != version)
            {
                SDL_Log(
                        "Settings file version mismatch, should be \"%s\".\n"
                        "Delete settings file and restart to reset settings.",
                        version);
                return false;
            }
            else
            {
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
                app->gif_delay_ms = j.value("gif_delay_ms", app->gif_delay_ms);
                app->line_color = get_color_value(j, "line_color", app->line_color);
                app->line_dashed = j.value("line_dashed", app->line_dashed);
                app->line_dashed_gap = j.value("line_dashed_gap", app->line_dashed_gap);
                app->line_dashed_len = j.value("line_dashed_len", app->line_dashed_len);
                app->line_width = j.value("line_width", app->line_width);
                app->logo_file_name = j.value("logo_file_name", app->logo_file_name);
                app->logo_scale = j.value("logo_scale", app->logo_scale);
                app->logo_alpha = j.value("logo_alpha", app->logo_alpha);
                app->text_content = j.value("text_content", app->text_content);
                //app->text_file_name = j["text_file_name"];
                app->text_font_color = get_color_value(j, "text_font_color", app->text_font_color);
                app->text_font_name = j.value("text_font_name", app->text_font_name);
                app->text_font_size = j.value("text_font_size", app->text_font_size);
                app->text_rotate = j.value("text_rotate", app->text_rotate);
                app->text_scale = j.value("text_scale", app->text_scale);
                app->text_alpha = j.value("text_alpha", app->text_alpha);
                if (j.contains("objects")) objects = j["objects"];
            }
        }
        catch (const std::exception& e)
        {
            SDL_Log("Error reading settings: %s", e.what());
            SDL_Log("Delete settings file and restart to reset settings.");
            app->app_quit = SDL_APP_FAILURE;
            SDL_Quit();
            return false;
        }

        SDL_Log("Settings read.");
    }
    return true;
}
