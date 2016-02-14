#include "system.hpp"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <stdexcept>
#include <memory>

namespace {

struct sdl_deleter_window {
    void operator()(SDL_Window* const p) noexcept {
        SDL_DestroyWindow(p);
    }
};

struct sdl_deleter_renderer {
    void operator()(SDL_Renderer* const p) noexcept {
        SDL_DestroyRenderer(p);
    }
};

struct sdl_deleter_surface {
    void operator()(SDL_Surface* const p) noexcept {
        SDL_FreeSurface(p);
    }
};

struct sdl_deleter_texture {
    void operator()(SDL_Texture* const p) noexcept {
        SDL_DestroyTexture(p);
    }
};

//! Exception type to wrap SDL error codes
struct sdl_error : std::runtime_error {
    using runtime_error::runtime_error;
};

//! RAII wrapper around the SDL systeem
class sdl {
public:
    static constexpr auto systems =
        SDL_INIT_EVENTS | SDL_INIT_VIDEO;

    sdl() {
        if (SDL_WasInit(0) & systems) {
            throw sdl_error {"SDL already initialized"};
        }

        if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO)) {
            throw sdl_error {SDL_GetError()};
        }
    }

    sdl(sdl&& other) noexcept
      : cleanup_ {other.cleanup_}
    {
        other.cleanup_ = false;
    }

    sdl& operator=(sdl&& rhs) noexcept {
        std::swap(cleanup_, rhs.cleanup_);
    }

    ~sdl() {
        if (cleanup_) {
            SDL_QuitSubSystem(systems);
            SDL_Quit();
        }
    }
private:
    bool cleanup_ {false};
};

//! RAII wrapper around SDL_Window
class sdl_window {
public:
    sdl_window()
      : handle_ {SDL_CreateWindow(
            "Boken"
           , SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED
           , 1024, 768, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE)}
    {
        if (!handle_) {
            throw sdl_error {SDL_GetError()};
        }
    }

    operator SDL_Window*() const noexcept {
        return handle_.get();
    }
private:
    std::unique_ptr<SDL_Window, sdl_deleter_window> handle_;
};

//! RAII wrapper around SDL_Renderer
class sdl_renderer {
public:
    explicit sdl_renderer(sdl_window const& win)
      : handle_ {SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED
          | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE)}
    {
        if (!handle_) {
            throw sdl_error {SDL_GetError()};
        }
    }

    operator SDL_Renderer*() const noexcept {
        return handle_.get();
    }
private:
    std::unique_ptr<SDL_Renderer, sdl_deleter_renderer> handle_;
};

//! RAII wrapper around SDL_Surface
class sdl_surface {
public:
    explicit sdl_surface(SDL_Surface* const ptr)
      : handle_ {ptr}
    {
        if (!handle_) {
            throw sdl_error {SDL_GetError()};
        }
    }

    operator SDL_Surface*() const noexcept {
        return handle_.get();
    }
private:
    std::unique_ptr<SDL_Surface, sdl_deleter_surface> handle_;
};

//! RAII wrapper around SDL_Texture
class sdl_texture {
public:
    explicit sdl_texture(SDL_Texture* const ptr)
      : handle_ {ptr}
    {
        if (!handle_) {
            throw sdl_error {SDL_GetError()};
        }
    }

    operator SDL_Texture*() const noexcept {
        return handle_.get();
    }
private:
    std::unique_ptr<SDL_Texture, sdl_deleter_texture> handle_;
};


} //namespace

namespace boken {

class sdl_system final : public system {
public:
    sdl_system()
      : renderer_ {window_}
      , tiles_ {SDL_CreateTextureFromSurface(
          renderer_, sdl_surface {SDL_LoadBMP("./data/tiles.bmp")})}
    {
        handler_quit_         = [](    ) noexcept { return true; };
        handler_key_          = [](auto, auto) noexcept {};
        handler_mouse_move_   = [](auto, auto) noexcept {};
        handler_mouse_button_ = [](auto, auto) noexcept {};
        handler_mouse_wheel_  = [](auto, auto, auto) noexcept {};
    }

    static kb_modifiers get_key_mods() noexcept {
        return kb_modifiers {static_cast<uint32_t>(SDL_GetModState())};
    }

    void set_draw_color(uint32_t const c) noexcept {
        SDL_SetTextureColorMod(
            tiles_
          , static_cast<uint8_t>((c >>  0) & 0xFFu)
          , static_cast<uint8_t>((c >>  8) & 0xFFu)
          , static_cast<uint8_t>((c >> 16) & 0xFFu));

        SDL_SetRenderDrawColor(
            renderer_
          , static_cast<uint8_t>((c >>  0) & 0xFFu)
          , static_cast<uint8_t>((c >>  8) & 0xFFu)
          , static_cast<uint8_t>((c >> 16) & 0xFFu)
          , static_cast<uint8_t>((c >> 24) & 0xFFu));
    }

    void handle_event_mouse_button(SDL_MouseButtonEvent const& e) {
        using type = mouse_event::button_change_t;
        auto& m = last_mouse_event_;

        std::fill(begin(m.button_change), end(m.button_change), type::none);

        if (e.button > 0 && e.button < mouse_event::button_count) {
            auto const b = e.button - 1;
            std::tie(m.button_change[b], m.button_state[b]) =
                (e.state == SDL_PRESSED) ? std::make_pair(type::went_down, true)
                                         : std::make_pair(type::went_up,   false);
        }

        last_mouse_event_.x  = e.x;
        last_mouse_event_.y  = e.y;
        last_mouse_event_.dx = 0;
        last_mouse_event_.dy = 0;

        handler_mouse_button_(m, get_key_mods());
    }

    void handle_event_mouse_move(SDL_MouseMotionEvent const& e) {
        auto& m = last_mouse_event_;

        std::fill(begin(m.button_change), end(m.button_change)
          , mouse_event::button_change_t::none);

        m.x  = e.x;
        m.y  = e.y;
        m.dx = e.xrel;
        m.dy = e.yrel;

        handler_mouse_move_(m, get_key_mods());
    }
public:
    //
    // overridden functions
    //

    void on_request_quit(on_request_quit_handler handler) final override {
        handler_quit_ = std::move(handler);
    }

    void on_key(on_key_handler handler) final override {
        handler_key_ = std::move(handler);
    }

    void on_mouse_move(on_mouse_move_handler handler) final override {
        handler_mouse_move_ = std::move(handler);
    }

    void on_mouse_button(on_mouse_button_handler handler) final override {
        handler_mouse_button_ = std::move(handler);
    }

    void on_mouse_wheel(on_mouse_wheel_handler handler) final override {
        handler_mouse_wheel_ = std::move(handler);
    }

    bool is_running() final override {
        return running_;
    }

    int do_events() final override {
        int count = 0;

        for (SDL_Event event; SDL_PollEvent(&event); ++count) {
            switch (event.type) {
            case SDL_QUIT:
                running_ = !handler_quit_();
                break;
            case SDL_KEYDOWN :
            case SDL_KEYUP :
                handler_key_(kb_event {
                    event.key.timestamp
                  , static_cast<uint32_t>(event.key.keysym.scancode)
                  , static_cast<uint32_t>(event.key.keysym.sym)
                  , event.key.keysym.mod
                  , !!event.key.repeat
                  , event.key.state == SDL_PRESSED
                }, kb_modifiers {event.key.keysym.mod});
                break;
            case SDL_MOUSEMOTION :
                handle_event_mouse_move(event.motion);
                break;
            case SDL_MOUSEBUTTONDOWN :
            case SDL_MOUSEBUTTONUP :
                handle_event_mouse_button(event.button);
                break;
            case SDL_MOUSEWHEEL :
                handler_mouse_wheel_(event.wheel.y, event.wheel.x, get_key_mods());
                break;
            }
        }

        return count;
    }

    void render_clear() final override {
        SDL_SetRenderDrawColor(renderer_, 127, 127, 0, 255);
        SDL_RenderClear(renderer_);
    }

    void render_present() final override {
        SDL_RenderPresent(renderer_);
    }

    void render_set_data(
        render_data         const type
      , read_only_pointer_t const data
    ) noexcept final override {
        switch (type) {
        case render_data::position : position_data_ = data; break;
        case render_data::texture  : texture_data_  = data; break;
        case render_data::color    : color_data_    = data; break;
        }
    }

    void render_set_tile_size(int const w, int const h) noexcept final override {
        tile_w_ = w;
        tile_h_ = h;
    }

    void render_set_transform(float const sx, float const sy, float const tx, float const ty) noexcept final override {
        sx_ = sx;
        sy_ = sy;
        tx_ = tx;
        ty_ = ty;

        SDL_RenderSetScale(renderer_, sx_, sy_);
    }

    void render_data_n(int const n) noexcept final override {
        auto pd = position_data_;
        auto td = texture_data_;
        auto cd = color_data_;

        SDL_Rect src {0, 0, tile_w_, tile_h_};
        SDL_Rect dst {0, 0, static_cast<int>(std::ceil(tile_w_))
                          , static_cast<int>(std::ceil(tile_h_))};

        uint32_t last_color = 0;
        set_draw_color(last_color);

        auto const tx = static_cast<int>(std::ceil(tx_ / sx_));
        auto const ty = static_cast<int>(std::ceil(ty_ / sy_));

        for (int i = 0; i < n; ++i, ++pd, ++td, ++cd) {
            std::tie(src.x, src.y) = td.value<std::pair<uint16_t, uint16_t>>();
            std::tie(dst.x, dst.y) = pd.value<std::pair<uint16_t, uint16_t>>();

            dst.x = dst.x + tx;
            dst.y = dst.y + ty;

            auto const color = cd.value<uint32_t>();
            if (color != last_color) {
                set_draw_color(last_color = color);
            }

            SDL_RenderCopy(renderer_, tiles_, &src, &dst);
        }
    }
private:
    on_request_quit_handler handler_quit_;
    on_key_handler          handler_key_;
    on_mouse_move_handler   handler_mouse_move_;
    on_mouse_button_handler handler_mouse_button_;
    on_mouse_wheel_handler  handler_mouse_wheel_;

    mouse_event last_mouse_event_ {};

    bool running_ = true;

    sdl          sdl_;
    sdl_window   window_;
    sdl_renderer renderer_;
    sdl_texture  tiles_;

    read_only_pointer_t position_data_;
    read_only_pointer_t texture_data_;
    read_only_pointer_t color_data_;

    int tile_w_ {};
    int tile_h_ {};

    float sx_ = 1.0f;
    float sy_ = 1.0f;
    float tx_ = 0.0f;
    float ty_ = 0.0f;
};

std::unique_ptr<system> make_system() {
    return std::make_unique<sdl_system>();
}

} //namespace boken