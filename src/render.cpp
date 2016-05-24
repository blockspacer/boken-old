#include "render.hpp"
#include "level.hpp"
#include "math.hpp"
#include "rect.hpp"
#include "message_log.hpp"
#include "system.hpp"
#include "text.hpp"
#include "tile.hpp"
#include "utility.hpp"
#include "inventory.hpp"
#include "scope_guard.hpp"

#include <bkassert/assert.hpp>

#include <vector>
#include <cstdint>

namespace boken {

void render_text(
    renderer2d& r
  , text_renderer& tr
  , text_layout const& text
  , vec2i32 const off
) noexcept {
    if (!text.is_visible()) {
        return;
    }

    text.update(tr);

    auto const& glyph_data = text.data();

    auto const p  = (text.extent() + off).top_left();
    auto const tx = value_cast_unsafe<float>(p.x);
    auto const ty = value_cast_unsafe<float>(p.y);

    using ptr_t = read_only_pointer_t;
    auto const params = renderer2d::tile_params_variable {
        3
      , static_cast<int32_t>(glyph_data.size())
      , ptr_t {glyph_data, BK_OFFSETOF(text_layout::data_t, position)}
      , ptr_t {glyph_data, BK_OFFSETOF(text_layout::data_t, texture)}
      , ptr_t {glyph_data, BK_OFFSETOF(text_layout::data_t, size)}
      , ptr_t {glyph_data, BK_OFFSETOF(text_layout::data_t, color)}
    };

    auto const trans = r.transform({1.0f, 1.0f, tx, ty});
    r.draw_tiles(params);
}

render_task::~render_task() = default;

tool_tip_renderer::~tool_tip_renderer() = default;

class tool_tip_renderer_impl final : public tool_tip_renderer {
public:
    tool_tip_renderer_impl(text_renderer& tr)
      : trender_ {tr}
    {
    }

    //---render_task interface
    void render(renderer2d& r) final override;

    //---tool_tip_renderer interface
    bool is_visible() const noexcept final override {
        return text_.is_visible();
    }

    bool visible(bool const state) noexcept final override {
        return text_.visible(state);
    }

    void set_text(std::string text) final override {
        text_.layout(trender_, std::move(text));
    }

    void set_position(point2i32 const p) noexcept final override {
        text_.move_to(value_cast(p.x), value_cast(p.y));
    }
private:
    text_renderer& trender_;
    text_layout    text_;
};

std::unique_ptr<tool_tip_renderer> make_tool_tip_renderer(text_renderer& tr) {
    return std::make_unique<tool_tip_renderer_impl>(tr);
}

void tool_tip_renderer_impl::render(renderer2d& r) {
    if (!is_visible()) {
        return;
    }

    auto const border_w = 2;
    auto const window_r = r.get_client_rect();
    auto const text_r   = text_.extent();
    auto const border_r = grow_rect(text_r, border_w);

    auto const dx = (border_r.x1 > window_r.x1)
      ? value_cast(window_r.x1 - border_r.x1)
      : 0;

    auto const dy = (border_r.y1 > window_r.y1)
      ? value_cast(window_r.y1 - border_r.y1)
      : 0;

    auto const v = vec2i32 {dx, dy};

    auto const trans = r.transform({1.0f, 1.0f, 0.0f, 0.0f});

    r.fill_rect(text_r + v, 0xDF666666u);
    r.draw_rect(border_r + v, border_w, 0xDF66DDDDu);

    render_text(r, trender_, text_, v);
}

game_renderer::~game_renderer() = default;

class game_renderer_impl final : public game_renderer {
    static auto position_to_pixel_(tile_map const& tmap) noexcept {
        auto const tw = value_cast(tmap.tile_width());
        auto const th = value_cast(tmap.tile_height());

        return [tw, th](auto const p) noexcept -> point2i16 {
            return {underlying_cast_unsafe<int16_t>(p.x * tw)
                  , underlying_cast_unsafe<int16_t>(p.y * th)};
        };
    }

    template <typename Data, typename Type>
    void update_data_(
        Data&                       data
      , update_t<Type> const* const first
      , update_t<Type> const* const last
      , tile_map const&             tmap
    ) {
        auto const tranform_point = position_to_pixel_(tmap);

        auto const get_texture_coord = [&](update_t<Type> const& u) noexcept {
            return underlying_cast_unsafe<int16_t>(
                tmap.index_to_rect(id_to_index(tmap, u.id)).top_left());
        };

        auto const get_color = [&](update_t<Type> const&) noexcept {
            return 0xFF00FF00u;
        };

        std::for_each(first, last, [&](update_t<Type> const& update) {
            auto const p = tranform_point(update.prev_pos);

            auto const first_d = begin(data);
            auto const last_d  = end(data);

            auto const it = std::find_if(first_d, last_d
              , [p](data_t const& d) noexcept { return d.position == p; });


            // data to remove
            if (update.id == nullptr) {
                BK_ASSERT(it != last_d);
                data.erase(it);
                return;
            }

            auto const tex_coord = get_texture_coord(update);
            auto const color     = get_color(update);

            // new data
            if (it == last_d) {
                data.push_back({p, tex_coord, color});
                return;
            }

            // data to update
            it->position  = tranform_point(update.next_pos);
            it->tex_coord = tex_coord;
            it->color     = color;
        });
    }
public:
    game_renderer_impl(system& os, text_renderer& trender)
      : os_      {os}
      , trender_ {trender}
    {
    }

    bool debug_toggle_show_regions() noexcept final override {
        bool const result = debug_show_regions_;
        debug_show_regions_ = !debug_show_regions_;
        return result;
    }

    void set_level(level const& lvl) noexcept final override {
        entity_data.clear();
        item_data.clear();
        tile_data.clear();
        level_ = &lvl;
    }

    void set_tile_maps(std::initializer_list<std::pair<tile_map_type, tile_map const&>> tmaps) noexcept final override;

    void update_map_data() final override;
    void update_map_data(const_sub_region_range<tile_id> sub_region) final override;

    void set_tile_highlight(point2i32 const p) noexcept final override {
        tile_highlight_ = p;
    }

    void clear_tile_highlight() noexcept final override {
        tile_highlight_ = point2i32 {-1, -1};
    }

    void update_data(update_t<entity_id> const* first, update_t<entity_id> const* last) final override {
        update_data_(entity_data, first, last, *tile_map_entities_);
    }

    void update_data(update_t<item_id> const* first, update_t<item_id> const* last) final override {
        update_data_(item_data, first, last, *tile_map_items_);
    }

    void clear_data() final override {
        entity_data.clear();
        item_data.clear();
    }

    void set_message_window(message_log const* const window) noexcept final override {
        message_log_ = window;
    }

    void set_inventory_window(inventory_list const* const window) noexcept final override {
        inventory_list_ = window;
    }

    void set_inventory_window_focus(bool const focus) noexcept final override {
        inventory_list_focus_ = focus;
    }

    void render(duration_t delta, view const& v) const noexcept final override;

    void add_task_generic(uint32_t const id, std::unique_ptr<render_task> task, int const z) final override {
        BK_ASSERT(!!task);
        tasks_.push_back(std::move(task));
    }
private:
    uint32_t choose_tile_color_(tile_id tid, region_id rid) noexcept;

    void render_text_(text_layout const& text, vec2i32 off) const noexcept;
    void render_tool_tip_() const noexcept;
    void render_message_log_() const noexcept;
    void render_inventory_list_() const noexcept;

    system&        os_;
    text_renderer& trender_;

    level const* level_ {};

    struct data_t {
        point2i16 position;
        point2i16 tex_coord;
        uint32_t  color;
    };

    tile_map const* tile_map_base_ {};
    tile_map const* tile_map_entities_ {};
    tile_map const* tile_map_items_ {};

    std::vector<data_t> tile_data;
    std::vector<data_t> entity_data;
    std::vector<data_t> item_data;

    text_layout tool_tip_;
    message_log const* message_log_ {};

    inventory_list const* inventory_list_ {};

    point2i32 tile_highlight_ {-1, -1};

    bool inventory_list_focus_ {false};

    bool debug_show_regions_ = false;

    std::unique_ptr<renderer2d> r2d = make_renderer(os_);

    std::vector<std::unique_ptr<render_task>> tasks_;
};

std::unique_ptr<game_renderer> make_game_renderer(system& os, text_renderer& trender) {
    return std::make_unique<game_renderer_impl>(os, trender);
}

void game_renderer_impl::set_tile_maps(
    std::initializer_list<std::pair<tile_map_type, tile_map const&>> tmaps
) noexcept {
    for (auto const& p : tmaps) {
        switch (p.first) {
        case tile_map_type::base   : this->tile_map_base_     = &p.second; break;
        case tile_map_type::entity : this->tile_map_entities_ = &p.second; break;
        case tile_map_type::item   : this->tile_map_items_    = &p.second; break;
        default :
            break;
        }
    }
}

uint32_t game_renderer_impl::choose_tile_color_(tile_id const tid, region_id const rid) noexcept {
    if (debug_show_regions_) {
        auto const n = value_cast(rid) + 1u;

        return (0xFFu     << 24)  //A
             | ((n * 11u) << 16)  //B
             | ((n * 23u) <<  8)  //G
             | ((n * 37u) <<  0); //R
    }

    if (tid == tile_id::empty) {
        return 0xFF222222u;
    }

    return 0xFFAAAAAAu;
}

void game_renderer_impl::update_map_data(const_sub_region_range<tile_id> const sub_region) {
    auto const& tmap = *tile_map_items_;

    auto dst_it = sub_region_iterator<data_t>(sub_region.first, tile_data.data());

    auto const region_range = level_->region_ids({
        point2i32 {
            static_cast<int32_t>(dst_it.off_x())
          , static_cast<int32_t>(dst_it.off_y())}
      , sizei32x {static_cast<int32_t>(dst_it.width())}
      , sizei32y {static_cast<int32_t>(dst_it.height())}});


    auto rgn_it = region_range.first;

    for (auto it = sub_region.first; it != sub_region.second; ++it, ++dst_it, ++rgn_it) {
        auto& dst = *dst_it;

        auto const tid = *it;
        auto const rid = *rgn_it;

        auto const tex_rect = tmap.index_to_rect(id_to_index(tmap, tid));
        dst.tex_coord = underlying_cast_unsafe<int16_t>(tex_rect.top_left());
        dst.color     = choose_tile_color_(tid, rid);
    }
}

void game_renderer_impl::update_map_data() {
    auto const& tmap = *tile_map_base_;
    auto const& lvl  = *level_;

    auto const bounds = lvl.bounds();
    auto const bounds_size = static_cast<size_t>(
        std::max(0, value_cast(bounds.area())));

    if (tile_data.size() < bounds_size) {
        tile_data.resize(bounds_size);
    }

    auto const transform_point = position_to_pixel_(tmap);

    auto const ids_range        = lvl.tile_ids(bounds);
    auto const region_ids_range = lvl.region_ids(bounds);

    auto dst = sub_region_iterator<data_t> {ids_range.first, tile_data.data()};
    auto it0 = ids_range.first;
    auto it1 = region_ids_range.first;

    for ( ; it0 != ids_range.second; ++it0, ++it1, ++dst) {
        auto const tid   = *it0;
        auto const rid   = *it1;
        auto const index = id_to_index(tmap, tid);

        auto const p = transform_point(point2<ptrdiff_t> {dst.x(), dst.y()});

        dst->position  = underlying_cast_unsafe<int16_t>(p);
        dst->tex_coord = underlying_cast_unsafe<int16_t>(tmap.index_to_rect(index).top_left());
        dst->color     = choose_tile_color_(tid, rid);
    }
}

namespace {

template <typename Data, typename T>
renderer2d::tile_params_uniform
make_uniform(tile_map const& tmap, T const& data) noexcept {
    using ptr_t = read_only_pointer_t;
    return {
        tmap.tile_width(), tmap.tile_height(), tmap.texture_id()
      , static_cast<int32_t>(data.size())
      , ptr_t {data, BK_OFFSETOF(Data, position)}
      , ptr_t {data, BK_OFFSETOF(Data, tex_coord)}
      , ptr_t {data, BK_OFFSETOF(Data, color)}
    };
}

} // namespace

void game_renderer_impl::render(duration_t const delta, view const& v) const noexcept {
    r2d->render_clear();
    r2d->transform();

    r2d->draw_background();

    auto const trans = r2d->transform({v.scale_x, v.scale_y, v.x_off, v.y_off});

    // Map tiles
    r2d->draw_tiles(make_uniform<data_t>(*tile_map_base_, tile_data));

    // Items
    r2d->draw_tiles(make_uniform<data_t>(*tile_map_items_, item_data));

    // Entities
    r2d->draw_tiles(make_uniform<data_t>(*tile_map_entities_, entity_data));


    // tile highlight
    if (value_cast(tile_highlight_.x) >= 0 && value_cast(tile_highlight_.y) >= 0) {
        auto const w = tile_map_base_->tile_width();
        auto const h = tile_map_base_->tile_height();

        auto const r = boken::grow_rect(recti32 {
            tile_highlight_.x * value_cast(w)
          , tile_highlight_.y * value_cast(h)
          , w
          , h}, 2);

        r2d->draw_rect(r, 2, 0xD000FFFFu);
    }

    // message log window
    render_message_log_();

    // inventory window
    render_inventory_list_();

    //
    for (auto const& task : tasks_) {
        task->render(*r2d);
    }

    r2d->render_present();
}

void game_renderer_impl::render_text_(text_layout const& text, vec2i32 const off) const noexcept {
    render_text(*r2d, trender_, text, off);
}

void game_renderer_impl::render_message_log_() const noexcept {
    if (!message_log_) {
        return;
    }

    auto const& log_window = *message_log_;

    auto const r        = log_window.bounds();
    auto const client_r = log_window.client_bounds();

    auto const v = [&]() noexcept {
        auto const ch = client_r.height();
        auto const rh = r.height();

        return (ch <= rh)
          ? vec2i32 {}
          : vec2i32 {0, rh - ch};
    }();

    auto const trans = r2d->transform({1.0f, 1.0f, 0.0f, 0.0f});
    r2d->fill_rect(r, 0xDF666666u);

    std::for_each(log_window.visible_begin(), log_window.visible_end()
      , [&](text_layout const& line) noexcept {
            if (line.extent().y1 + v.y <= r.y0) {
                return;
            }

            render_text_(line, v);
        });
}

void game_renderer_impl::render_inventory_list_() const noexcept {
    if (!inventory_list_ || !inventory_list_->is_visible()) {
        return;
    }

    auto const& inv_window = *inventory_list_;
    auto const  m = inv_window.metrics();

    uint32_t const color_border       = 0xEF555555u;
    uint32_t const color_border_focus = 0xEFEFEFEFu;
    uint32_t const color_title        = 0xEF886666u;
    uint32_t const color_header       = 0xDF66AA66u;
    uint32_t const color_row_even     = 0xDF666666u;
    uint32_t const color_row_odd      = 0xDF888888u;
    uint32_t const color_row_sel      = 0xDFBB2222u;
    uint32_t const color_row_ind      = 0xDF22BBBBu;

    auto const trans = r2d->transform({1.0f, 1.0f, 0.0f, 0.0f});

    // draw the frame
    {
        auto const frame_size = (m.frame.width() - m.client_frame.width()) / 2;
        auto const color = inventory_list_focus_
          ? color_border_focus
          : color_border;

        r2d->draw_rect(m.frame, value_cast(frame_size), color);
    }

    // draw the title
    {
        r2d->fill_rect(m.title, color_title);
        render_text_(inv_window.title(), m.title.top_left() - point2i32 {});
    }

    // draw the client area
    if (inv_window.cols() <= 0) {
        return; // TODO
    }

    // fill in any gap between the title and the client area
    auto const gap = m.client_frame.y0 - m.title.y1;
    if (gap > sizei32y {0}) {
        auto const r = recti32 {
            m.client_frame.x0
          , m.title.y1
          , m.client_frame.width()
          , gap
        };

        r2d->fill_rect(r, color_row_even);
    }

    auto const clip = r2d->clip_rect(m.client_frame);

    auto const v = (m.client_frame.top_left() - point2i32 {})
                 - inv_window.scroll_offset();

    for (size_t i = 0; i < inv_window.cols(); ++i) {
        auto const info = inv_window.col(static_cast<int>(i));
        auto const r = recti32 {
            info.text.position().x + info.width + v.x
          , m.client_frame.y0
          , sizei32x {2}
          , m.client_frame.height()
        };

        r2d->fill_rect(r, 0xEFFFFFFF);
    }

    // header background
    r2d->fill_rect({point2i32 {} + v, m.client_frame.width(), m.header_h}, color_header);

    int32_t last_y = value_cast(m.client_frame.y0);

    for (size_t i = 0; i < inv_window.cols(); ++i) {
        auto const info = inv_window.col(static_cast<int>(i));
        render_text_(info.text, v);
        last_y = std::max(last_y, value_cast(info.text.extent().y1 + v.y));
    }

    auto const indicated = static_cast<size_t>(inv_window.indicated());

    for (size_t i = 0; i < inv_window.rows(); ++i) {
        auto const range = inv_window.row(static_cast<int>(i));

        auto const p = range.first->position() + v;
        auto const w = m.client_frame.width();
        auto const h = range.first->extent().height();

        auto const color =
            (inv_window.is_selected(static_cast<int>(i))) ? color_row_sel
          : (i % 2u)                                      ? color_row_even
                                                          : color_row_odd;

        // row background
        auto const r = recti32 {p, w, h};
        r2d->fill_rect(r, color);

        if (i == indicated) {
            r2d->draw_rect(r, 2, color_row_ind);
        }

        std::for_each(range.first, range.second, [&](text_layout const& txt) noexcept {
            render_text_(txt, v);
        });

        last_y = std::max(last_y, value_cast(p.y + h));
        if (last_y >= value_cast(m.client_frame.y1)) {
            break;
        }
    }

    // fill unused background
    if (last_y < value_cast(m.client_frame.y1)) {
        auto const r = recti32 {
            m.client_frame.x0
          , offi32y {last_y}
          , m.client_frame.width()
          , m.client_frame.y1 - offi32y {last_y}
        };

        r2d->fill_rect(r, color_row_even);
    }
}

} //namespace boken
