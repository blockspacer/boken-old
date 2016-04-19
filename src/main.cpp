#include "catch.hpp"        // for run_unit_tests
#include "allocator.hpp"
#include "command.hpp"
#include "data.hpp"
#include "entity.hpp"       // for entity
#include "entity_def.hpp"   // for entity_definition
#include "hash.hpp"         // for djb2_hash_32
#include "item.hpp"
#include "item_def.hpp"     // for item_definition
#include "level.hpp"        // for level, placement_result, make_level, etc
#include "math.hpp"         // for vec2i32, floor_as, point2f, basic_2_tuple, etc
#include "message_log.hpp"  // for message_log
#include "random.hpp"       // for random_state, make_random_state
#include "render.hpp"       // for game_renderer
#include "system.hpp"       // for system, kb_modifiers, mouse_event, etc
#include "text.hpp"         // for text_layout, text_renderer
#include "tile.hpp"         // for tile_map
#include "types.hpp"        // for value_cast, tag_size_type_x, etc
#include "utility.hpp"      // for BK_OFFSETOF
#include "world.hpp"        // for world, make_world
#include "inventory.hpp"
#include "scope_guard.hpp"
#include "item_properties.hpp"
#include "timer.hpp"
#include "item_list.hpp"

#include <algorithm>        // for move
#include <chrono>           // for microseconds, operator-, duration, etc
#include <functional>       // for function
#include <memory>           // for unique_ptr, allocator
#include <ratio>            // for ratio
#include <string>           // for string, to_string
#include <utility>          // for pair, make_pair
#include <vector>           // for vector

#include <cstdint>          // for uint16_t, uint32_t, uintptr_t
#include <cstdio>           // for printf
#include <cinttypes>

namespace boken {

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
int get_entity_loot(entity& e, random_state& rng, std::function<void(unique_item&&)> const& f) {
    int result = 0;

    auto& items = e.items();
    while (!items.empty()) {
        auto itm = items.remove_item(0);
        f(std::move(itm));
        ++result;
    }

    return result;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

bool can_add_item(game_database const& db, entity const& dest, item const& itm) {
    return true;
}

bool can_add_item(game_database const& db, item const& dest, item const& itm) {
    return false;
}

bool can_add_item(game_database const& db, entity const& dest, item_definition const& def) {
    return true;
}

bool can_add_item(game_database const& db, item const& dest, item_definition const& def) {
    auto const dest_capacity =
        get_property_value_or(db, dest, property(item_property::capacity), 0);

    // the destination is not a container
    if (dest_capacity <= 0) {
        return false;
    }

    // the destination is full
    if (dest.items().size() + 1 > dest_capacity) {
        return false;
    }

    auto const itm_capacity =
        get_property_value_or(def, property(item_property::capacity), 0);

    // the item to add is itself a container
    if (itm_capacity > 0) {
        return false;
    }

    return true;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// name of
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
string_view name_of(game_database const& db, item_id const id) noexcept {
    auto const def_ptr = db.find(id);
    return def_ptr
      ? string_view {def_ptr->name}
      : string_view {"{invalid idef}"};
}

string_view name_of(game_database const& db, item const& i) noexcept {
    return name_of(db, i.definition());
}

string_view name_of(world const& w, game_database const& db, item_instance_id const id) noexcept {
    return name_of(db, w.find(id));
}

string_view name_of(game_database const& db, entity_id const id) noexcept {
    auto const def_ptr = db.find(id);
    return def_ptr
      ? string_view {def_ptr->name}
      : string_view {"{invalid edef}"};
}

string_view name_of(game_database const& db, entity const& e) noexcept {
    return name_of(db, e.definition());
}

string_view name_of(world const& w, game_database const& db, entity_instance_id const id) noexcept {
    return name_of(db, w.find(id));
}

template <typename T>
inline T make_id(string_view const s) noexcept {
    return T {djb2_hash_32(s.begin(), s.end())};
}

template <typename T, size_t N>
inline constexpr T make_id(char const (&s)[N]) noexcept {
    return T {djb2_hash_32c(s)};
}

//! Game input sink
class input_context {
public:
    event_result on_key(kb_event const event, kb_modifiers const kmods) {
        return on_key_handler
          ? on_key_handler(event, kmods)
          : event_result::pass_through;
    }

    event_result on_text_input(text_input_event const event) {
        return on_text_input_handler
          ? on_text_input_handler(event)
          : event_result::pass_through;
    }

    event_result on_mouse_button(mouse_event const event, kb_modifiers const kmods) {
        return on_mouse_button_handler
          ? on_mouse_button_handler(event, kmods)
          : event_result::pass_through;
    }

    event_result on_mouse_move(mouse_event const event, kb_modifiers const kmods) {
        return on_mouse_move_handler
          ? on_mouse_move_handler(event, kmods)
          : event_result::pass_through;
    }

    event_result on_mouse_wheel(int const wy, int const wx, kb_modifiers const kmods) {
        return on_mouse_wheel_handler
          ? on_mouse_wheel_handler(wy, wx, kmods)
          : event_result::pass_through;
    }

    event_result on_command(command_type const type, uintptr_t const data) {
        return on_command_handler
          ? on_command_handler(type, data)
          : event_result::pass_through;
    }

    std::function<event_result (kb_event, kb_modifiers)>    on_key_handler;
    std::function<event_result (text_input_event)>          on_text_input_handler;
    std::function<event_result (mouse_event, kb_modifiers)> on_mouse_button_handler;
    std::function<event_result (mouse_event, kb_modifiers)> on_mouse_move_handler;
    std::function<event_result (int, int, kb_modifiers)>    on_mouse_wheel_handler;
    std::function<event_result (command_type, uintptr_t)>   on_command_handler;
};

struct game_state {
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Types
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    using clock_t     = std::chrono::high_resolution_clock;
    using timepoint_t = clock_t::time_point;

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Special member functions
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    std::unique_ptr<inventory_list> make_item_list_() {
        return make_inventory_list(trender
            , [&](item_instance_id const id) noexcept -> item const& {
                return the_world.find(id);
            });
    }

    void set_item_list_columns() {
        item_list.add_column("", [&](item const& itm) {
            auto const& tmap = database.get_tile_map(tile_map_type::item);
            auto const index = tmap.find(itm.definition());

            BK_ASSERT(index < 0x7Fu); //TODO

            std::array<char, 7> as_string {};
            as_string[0] =
                static_cast<char>(static_cast<uint8_t>(index & 0x7Fu));

            return std::string {as_string.data()};
        });

        item_list.add_column("Name"
            , [&](item const& itm) { return name_of(itm).to_string(); });

        item_list.add_column("Weight", [&](item const& itm) {
            auto const weight = get_property_value_or(database, itm
                , property(item_property::weight), 0);

            auto const stack = get_property_value_or(database, itm
                , property(item_property::current_stack_size), 1);

            return std::to_string(weight * stack);
        });

        item_list.add_column("Count", [&](item const& itm) {
            auto const stack = get_property_value_or(database, itm
                , property(item_property::current_stack_size), 1);

            return std::to_string(stack);
        });

        item_list.layout();
    }

    game_state()
      : item_list {make_item_list_()}
    {
        bind_event_handlers_();

        renderer.set_message_window(&message_window);

        renderer.set_tile_maps({
            {tile_map_type::base,   database.get_tile_map(tile_map_type::base)}
          , {tile_map_type::entity, database.get_tile_map(tile_map_type::entity)}
          , {tile_map_type::item,   database.get_tile_map(tile_map_type::item)}
        });

        renderer.set_inventory_window(&item_list.get());

        generate();

        reset_view_to_player();

        set_item_list_columns();

        item_list.hide();

        item_list.set_on_focus_change([&](bool const state) {
            renderer.set_inventory_window_focus(state);
            if (state) {
                renderer.update_tool_tip_visible(false);
            }
        });
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Utility / Helpers
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    point2i32 window_to_world(point2i32 const p) const noexcept {
        auto const& tile_map = database.get_tile_map(tile_map_type::base);
        auto const tw = value_cast_unsafe<float>(tile_map.tile_width());
        auto const th = value_cast_unsafe<float>(tile_map.tile_height());

        auto const q  = current_view.window_to_world(p);
        auto const tx = floor_as<int32_t>(value_cast(q.x) / tw);
        auto const ty = floor_as<int32_t>(value_cast(q.y) / th);

        return {tx, ty};
    }

    // @param p Position in world coordinates
    void update_tile_at(point2i32 const p) {
        auto& lvl = the_world.current_level();

        if (!intersects(lvl.bounds(), p)) {
            return;
        }

        if (lvl.at(p).type == tile_type::tunnel) {
            return;
        }

        tile_data_set const data {
            tile_data {}
          , tile_flags {0}
          , tile_id::tunnel
          , tile_type::tunnel
          , region_id {}
        };

        renderer.update_map_data(lvl.update_tile_at(rng_superficial, p, data));
    }

    //! @param p Position in window coordinates
    void show_tool_tip(point2i32 const p) {
        auto const p0 = window_to_world(p);
        auto const q  = window_to_world({last_mouse_x, last_mouse_y});

        auto const was_visible = renderer.update_tool_tip_visible(true);
        renderer.update_tool_tip_position(p);

        if (was_visible && p0 == q) {
            return; // the tile the mouse points to is unchanged from last time
        }

        auto const& lvl  = the_world.current_level();
        auto const& tile = lvl.at(p0);

        static_string_buffer<256> buffer;

        auto const print_entity = [&]() noexcept {
            auto* const entity = lvl.entity_at(p0);
            if (!entity) {
                return true;
            }

            auto* const def = database.find(entity->definition());

            return buffer.append(
                "Entities:\n"
                " Instance  : %0#10x\n"
                " Definition: %0#10x (%s)\n"
                " Name      : %s\n"
              , value_cast(entity->instance())
              , value_cast(entity->definition()), (def ? def->id_string.c_str() : "{empty}")
              , (def ? def->name.c_str() : "{empty}"));
        };

        auto const print_items = [&]() noexcept {
            auto* const pile = lvl.item_at(p0);
            if (!pile) {
                return true;
            }

            buffer.append("Items:\n");

            int i = 0;
            for (auto const& id : *pile) {
                if (!buffer.append(" Item: %d\n", i++)) {
                    break;
                }

                auto const& itm = the_world.find(id);
                auto* const def = database.find(itm.definition());

                buffer.append(
                    " Instance  : %0#10x\n"
                    " Definition: %0#10x (%s)\n"
                    " Name      : %s\n"
                  , value_cast(itm.instance())
                  , value_cast(itm.definition()), (def ? def->id_string.c_str() : "{empty}")
                  , (def ? def->name.c_str() : "{empty}"));
            }

            return !!buffer;
        };

        auto const result =
            buffer.append(
                "Position: %d, %d\n"
                "Region  : %d\n"
                "Tile    : %s\n"
              , value_cast(p0.x), value_cast(p0.y)
              , value_cast<int>(tile.rid)
              , enum_to_string(lvl.at(p0).id).data())
         && print_entity()
         && print_items();

        renderer.update_tool_tip_text(buffer.to_string());
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Initialization / Generation
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    void generate_player() {
        auto const result = create_object_at(
            make_id<entity_id>("player")
          , the_world.current_level().stair_up(0)
          , rng_substantive);

        BK_ASSERT(result.second);
    }

    void generate_entities() {
        weight_list<int, item_id> const w {
            {6, item_id {}}
          , {3, make_id<item_id>("coin")}
          , {1, make_id<item_id>("potion_health_small")}
        };

        auto const w_max = w.max();

        auto& rng = rng_substantive;
        auto& lvl = the_world.current_level();

        auto const def_ptr = database.find(make_id<entity_id>("rat_small"));
        BK_ASSERT(!!def_ptr);
        auto const& def = *def_ptr;

        for (size_t i = 0; i < lvl.region_count(); ++i) {
            auto const& region = lvl.region(i);
            if (region.tile_count <= 0) {
                continue;
            }

            point2i32 const p {region.bounds.x0 + region.bounds.width()  / 2
                             , region.bounds.y0 + region.bounds.height() / 2};

            auto const result =
                lvl.find_valid_entity_placement_neareast(rng, p, 3);

            if (result.second != placement_result::ok) {
                continue;
            }

            auto const instance_id = create_object_at(def, result.first, rng);
            auto& e = boken::find(the_world, instance_id);

            auto const id = random_weighted(rng, w);
            if (id == item_id {}) {
                continue;
            }

            auto* const idef = database.find(id);
            if (!idef) {
                BK_ASSERT(false);
                continue; //TODO
            }

            if (!can_add_item(database, e, *idef)) {
                continue;
            }

            e.add_item(create_object(*idef, rng));
        }
    }

    void generate_items() {
        auto& lvl = the_world.current_level();
        auto& rng = rng_substantive;

        auto const def_ptr = database.find(make_id<item_id>("container_chest"));
        BK_ASSERT(!!def_ptr);
        auto const& def = *def_ptr;


        auto const dag_def_ptr = database.find(make_id<item_id>("weapon_dagger"));
        BK_ASSERT(!!dag_def_ptr);
        auto const& dag_def = *dag_def_ptr;

        for (size_t i = 0; i < lvl.region_count(); ++i) {
            auto const& region = lvl.region(i);
            if (region.tile_count <= 0) {
                continue;
            }

            point2i32 const p {region.bounds.x0 + region.bounds.width()  / 2
                             , region.bounds.y0 + region.bounds.height() / 2};

            auto const result =
                lvl.find_valid_item_placement_neareast(rng, p, 3);

            if (result.second != placement_result::ok) {
                continue;
            }

            auto const instance_id =
                create_object_at(def, result.first, rng);

            auto& itm = boken::find(the_world, instance_id);

            if (!can_add_item(database, itm, dag_def)) {
                continue;
            }

            create_item_in(itm, dag_def, rng);
        }
    }

    void generate_level(level* const parent, size_t const id) {
        auto const level_w = 50;
        auto const level_h = 40;

        the_world.add_new_level(parent
          , make_level(rng_substantive, the_world, sizei32x {level_w}, sizei32y {level_h}, id));

        the_world.change_level(id);
    }

    void generate(size_t const id = 0) {
        BK_ASSERT(!the_world.has_level(id));

        if (id == 0) {
            generate_level(nullptr, id);
            generate_player();
        } else {
            generate_level(&the_world.current_level(), id);
        }

        generate_entities();
        generate_items();

        set_current_level(id, true);
    }

    //! get the item id to use for item pile
    item_id get_pile_id() const {
        auto const pile_def = find(database, make_id<item_id>("pile"));
        return pile_def ? pile_def->id : item_id {};
    }

    //! get the item id to display an item pile
    item_id get_pile_id(item_pile const& pile, item_id const pile_id) const {
        BK_ASSERT(!pile.empty());
        return (pile.size() == 1u)
          ? find(the_world, *pile.begin()).definition()
          : pile_id;
    }

    void set_current_level(size_t const id, bool const is_new) {
        BK_ASSERT(the_world.has_level(id));
        renderer.set_level(the_world.change_level(id));

        renderer.update_map_data();

        if (is_new) {
            return;
        }

        item_updates_.clear();
        entity_updates_.clear();

        auto& lvl = the_world.current_level();

        lvl.for_each_entity([&](entity_instance_id const id, point2i32 const p) {
            renderer_add(find(the_world, id).definition(), p);
        });

        auto const pile_id = get_pile_id();

        lvl.for_each_pile([&](item_pile const& pile, point2i32 const p) {
            renderer_add(get_pile_id(pile, pile_id), p);
        });
    }

    void reset_view_to_player() {
        auto const& tile_map = database.get_tile_map(tile_map_type::base);
        auto const tw = value_cast(tile_map.tile_width());
        auto const th = value_cast(tile_map.tile_height());

        auto const win_r = os.render_get_client_rect();
        auto const win_w = value_cast(win_r.width());
        auto const win_h = value_cast(win_r.height());

        auto const p  = get_player().second;
        auto const px = value_cast(p.x);
        auto const py = value_cast(p.y);

        current_view.x_off = static_cast<float>((win_w * 0.5) - tw * (px + 0.5));
        current_view.y_off = static_cast<float>((win_h * 0.5) - th * (py + 0.5));
        current_view.scale_x = 1.0f;
        current_view.scale_y = 1.0f;
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Events
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    //! @returns true if the event has not been filtered, false otherwise.
    template <typename... Args0, typename... Args1>
    bool process_context_stack(
        event_result (input_context::* handler)(Args0...)
      , Args1&&... args
    ) {
        // as a stack: back to front
        for (auto i = context_stack.size(); i > 0; --i) {
            // size == 1 ~> index == 0
            auto const where = i - 1u;
            auto const r =
                (context_stack[where].*handler)(std::forward<Args1>(args)...);

            switch (r) {
            case event_result::filter_detach :
                context_stack.erase(
                    begin(context_stack) + static_cast<ptrdiff_t>(where));
                BK_ATTRIBUTE_FALLTHROUGH;
            case event_result::filter :
                return false;
            case event_result::pass_through_detach :
                context_stack.erase(
                    begin(context_stack) + static_cast<ptrdiff_t>(where));
                BK_ATTRIBUTE_FALLTHROUGH;
            case event_result::pass_through :
                break;
            default:
                BK_ASSERT(false);
                break;
            }
        }

        return true;
    }

    //! @returns true if the event has not been filtered, false otherwise.
    template <typename... Args0, typename... Args1>
    void process_event(
        bool (game_state::* ui_handler)(Args0...)
      , event_result (input_context::* ctx_handler)(Args0...)
      , void (game_state::* base_handler)(Args0...)
      , Args1&&... args
    ) {
        // first allow the ui a chance to process the event
        if (!(this->*ui_handler)(std::forward<Args1>(args)...)) {
            return; // ui filtered the event
        }

        // then allow the input contexts a chance to process the event
        if (!process_context_stack(ctx_handler, std::forward<Args1>(args)...)) {
            return; // an input context filtered the event
        }

        // lastly, allow the default handler to process the event
        (this->*base_handler)(std::forward<Args1>(args)...);
    }

    void bind_event_handlers_() {
        os.on_key([&](kb_event const event, kb_modifiers const kmods) {
            process_event(&game_state::ui_on_key
                        , &input_context::on_key
                        , &game_state::on_key
                        , event, kmods);
            cmd_translator.translate(event, kmods);
        });

        os.on_text_input([&](text_input_event const event) {
            process_event(&game_state::ui_on_text_input
                        , &input_context::on_text_input
                        , &game_state::on_text_input
                        , event);
            cmd_translator.translate(event);
        });

        os.on_mouse_move([&](mouse_event const event, kb_modifiers const kmods) {
            process_event(&game_state::ui_on_mouse_move
                        , &input_context::on_mouse_move
                        , &game_state::on_mouse_move
                        , event, kmods);

            last_mouse_x = event.x;
            last_mouse_y = event.y;
        });

        os.on_mouse_button([&](mouse_event const event, kb_modifiers const kmods) {
            process_event(&game_state::ui_on_mouse_button
                        , &input_context::on_mouse_button
                        , &game_state::on_mouse_button
                        , event, kmods);
        });

        os.on_mouse_wheel([&](int const wx, int const wy, kb_modifiers const kmods) {
            process_event(&game_state::ui_on_mouse_wheel
                        , &input_context::on_mouse_wheel
                        , &game_state::on_mouse_wheel
                        , wx, wy, kmods);
        });

        cmd_translator.on_command([&](command_type const type, uint64_t const data) {
            process_event(&game_state::ui_on_command
                        , &input_context::on_command
                        , &game_state::on_command
                        , type, data);
        });
    }

    bool ui_on_key(kb_event const event, kb_modifiers const kmods) {
        return item_list.on_key(event, kmods);
    }

    bool ui_on_text_input(text_input_event const event) {
        return item_list.on_text_input(event);
    }

    bool ui_on_mouse_button(mouse_event const event, kb_modifiers const kmods) {
        return item_list.on_mouse_button(event, kmods);
    }

    bool ui_on_mouse_move(mouse_event const event, kb_modifiers const kmods) {
        return item_list.on_mouse_move(event, kmods);
    }

    bool ui_on_mouse_wheel(int const wy, int const wx, kb_modifiers const kmods) {
        return item_list.on_mouse_wheel(wy, wx, kmods);
    }

    bool ui_on_command(command_type const type, uintptr_t const data) {
        return item_list.on_command(type, data);
    }

    void on_key(kb_event const event, kb_modifiers const kmods) {
        if (event.went_down) {
            if (!kmods.test(kb_modifiers::m_shift)
               && (event.scancode == kb_scancode::k_lshift
                || event.scancode == kb_scancode::k_rshift)
            ) {
                show_tool_tip({last_mouse_x, last_mouse_y});
            }
        } else {
            if (!kmods.test(kb_modifiers::m_shift)) {
                renderer.update_tool_tip_visible(false);
            }
        }
    }

    void on_text_input(text_input_event const event) {
    }

    void on_mouse_button(mouse_event const event, kb_modifiers const kmods) {
        switch (event.button_state_bits()) {
        case 0b0000 :
            // no buttons down
            break;
        case 0b0001 :
            // left mouse button only
            if (event.button_change[0] == mouse_event::button_change_t::went_down) {
                update_tile_at(window_to_world({event.x, event.y}));
            }

            break;
        case 0b0010 :
            // middle mouse button only
            break;
        case 0b0100 :
            // right mouse button only
            break;
        case 0b1000 :
            // extra mouse button only
            break;
        default :
            break;
        }
    }

    void on_mouse_move(mouse_event const event, kb_modifiers const kmods) {
        switch (event.button_state_bits()) {
        case 0b0000 :
            // no buttons down
            if (kmods.test(kb_modifiers::m_shift)) {
                show_tool_tip({event.x, event.y});
            }
            break;
        case 0b0001 :
            // left mouse button only
            break;
        case 0b0010 :
            // middle mouse button only
            break;
        case 0b0100 :
            // right mouse button only
            if (kmods.none()) {
                current_view.x_off += static_cast<float>(event.dx);
                current_view.y_off += static_cast<float>(event.dy);
            }
            break;
        case 0b1000 :
            // extra mouse button only
            break;
        default :
            break;
        }
    }

    void on_mouse_wheel(int const wy, int const wx, kb_modifiers const kmods) {
        auto const p_window = point2i32 {last_mouse_x, last_mouse_y};
        auto const p_world  = current_view.window_to_world(p_window);

        current_view.scale_x *= (wy > 0 ? 1.1f : 0.9f);
        current_view.scale_y  = current_view.scale_x;

        auto const p_window_new = current_view.world_to_window(p_world);

        current_view.x_off += value_cast_unsafe<float>(p_window.x) - value_cast(p_window_new.x);
        current_view.y_off += value_cast_unsafe<float>(p_window.y) - value_cast(p_window_new.y);
    }

    void on_command(command_type const type, uint64_t const data) {
        using ct = command_type;
        switch (type) {
        case ct::none : break;

        case ct::move_here : advance(1); break;

        case ct::move_n  : do_player_move_by({ 0, -1}); break;
        case ct::move_ne : do_player_move_by({ 1, -1}); break;
        case ct::move_e  : do_player_move_by({ 1,  0}); break;
        case ct::move_se : do_player_move_by({ 1,  1}); break;
        case ct::move_s  : do_player_move_by({ 0,  1}); break;
        case ct::move_sw : do_player_move_by({-1,  1}); break;
        case ct::move_w  : do_player_move_by({-1,  0}); break;
        case ct::move_nw : do_player_move_by({-1, -1}); break;

        case ct::run_n  : do_player_run({ 0, -1}); break;
        case ct::run_ne : do_player_run({ 1, -1}); break;
        case ct::run_e  : do_player_run({ 1,  0}); break;
        case ct::run_se : do_player_run({ 1,  1}); break;
        case ct::run_s  : do_player_run({ 0,  1}); break;
        case ct::run_sw : do_player_run({-1,  1}); break;
        case ct::run_w  : do_player_run({-1,  0}); break;
        case ct::run_nw : do_player_run({-1, -1}); break;

        case ct::move_down : do_change_level(ct::move_down); break;
        case ct::move_up   : do_change_level(ct::move_up);   break;

        case ct::get_all_items : do_get_all_items(); break;
        case ct::get_items : do_get_items(); break;

        case ct::toggle_show_inventory : do_toggle_inventory(); break;

        case ct::reset_view : reset_view_to_player(); break;
        case ct::reset_zoom:
            current_view.scale_x = 1.0f;
            current_view.scale_y = 1.0f;
            break;
        case ct::debug_toggle_regions :
            renderer.debug_toggle_show_regions();
            renderer.update_map_data();
            break;
        case ct::debug_teleport_self : do_debug_teleport_self(); break;
        case ct::cancel : do_cancel(); break;
        case ct::confirm : break;
        case ct::toggle : break;
        case ct::drop_one : do_drop_one(); break;
        case ct::drop_some : do_drop_some(); break;
        case ct::open : do_open(); break;

        case ct::alt_get_items : break;
        case ct::alt_drop_some : break;

        default:
            BK_ASSERT(false);
            break;
        }
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Helpers
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    string_view name_of(item_id const id) noexcept {
        return boken::name_of(database, id);
    }

    string_view name_of(item const& i) noexcept {
        return boken::name_of(database, i.definition());
    }

    string_view name_of(unique_item const& i) noexcept {
        return name_of(i.get());
    }

    string_view name_of(item_instance_id const id) noexcept {
        return boken::name_of(the_world, database, id);
    }

    string_view name_of(entity_id const id) noexcept {
        return boken::name_of(database, id);
    }

    string_view name_of(entity const& e) noexcept {
        return boken::name_of(database, e.definition());
    }

    string_view name_of(unique_entity const& e) noexcept {
        return name_of(e.get());
    }

    string_view name_of(entity_instance_id const id) noexcept {
        return boken::name_of(the_world, database, id);
    }

    /////

    std::pair<entity&, point2i32> get_player() noexcept {
        constexpr auto player_id = entity_instance_id {1u};

        auto const result = the_world.current_level().find(player_id);
        BK_ASSERT(!!result.first);

        return {*result.first, result.second};
    }

    std::pair<entity const&, point2i32> get_player() const noexcept {
        return const_cast<game_state*>(this)->get_player();
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Commands
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    //! common implementation for choose_one_item and choose_n_items.
    template <typename Confirm, typename Cancel>
    void impl_choose_item_(
        std::string& title
      , Confirm      on_confirm
      , Cancel       on_cancel
    ) {
        auto& il = item_list;

        il.show();
        il.set_modal(true);
        il.set_title(std::move(title));

        using ct = command_type;
        il.set_on_command(
            [=](ct const type, int const* const first, int const* const last) {
                if (type == ct::confirm) {
                    if (!!first && !!last) {
                        on_confirm(first, last);
                        return event_result::filter_detach;
                    }
                } else if (type == ct::cancel) {
                    on_cancel();
                    return event_result::filter_detach;
                }

                return event_result::filter;
            });
    }

    template <typename Confirm, typename Cancel>
    void choose_one_item(std::string title, Confirm on_confirm, Cancel on_cancel) {
        item_list.set_multiselect(false);
        impl_choose_item_(title
          , [=](int const* const first, int const* const last) {
                BK_ASSERT(!!first && !!last && std::distance(first, last) == 1);
                on_confirm(*first);
            }
          , on_cancel);
    }

    template <typename Confirm, typename Cancel>
    void choose_n_items(std::string title, Confirm on_confirm, Cancel on_cancel) {
        item_list.set_multiselect(true);
        impl_choose_item_(title
          , [=](int const* const first, int const* const last) {
                BK_ASSERT(!!first && !!last && std::distance(first, last) >= 1);
                on_confirm(first, last);
            }
          , on_cancel);
    }

    template <typename Source, typename Dest>
    void view_item_pile(
        std::string title
      , Source& src
      , point2i32 const src_p
      , Dest& dst
    ) {
        enum class pile_type {
            inventory, container, floor, other_inventory, unknown
        };

        using src_t = std::decay_t<Source>;

        constexpr auto static_type =
            std::conditional_t<std::is_same<item, src_t>::value
              , std::integral_constant<pile_type, pile_type::container>
          , std::conditional_t<std::is_same<item_pile, src_t>::value
              , std::integral_constant<pile_type, pile_type::floor>
          , std::conditional_t<std::is_same<entity, src_t>::value
              , std::integral_constant<pile_type, pile_type::inventory>
              , std::integral_constant<pile_type, pile_type::unknown>>>>::value;

        static_assert(static_type != pile_type::unknown, "");

        auto const type = (static_type != pile_type::inventory)
          ? static_type
          : (src_p == get_player().second) ? pile_type::inventory
                                           : pile_type::other_inventory;

        auto const is_modal = (type != pile_type::inventory);

        auto& items = get_items(src);

        item_list.set_title(std::move(title));
        item_list.assign(items);
        item_list.set_modal(is_modal);
        item_list.set_multiselect(true);
        item_list.show();

        using ct = command_type;

        auto const update_list = [&](merge_item_result const r) {
            if (r != merge_item_result::ok_merged_all
             && r != merge_item_result::ok_merged_some
            ) {
                return;
            }

            item_list.assign(items);
        };

        item_list.set_on_command([=, &src, &items, &dst](ct const cmd, int const* const first, int const* const last) {
            if (cmd == ct::alt_drop_some) {
                update_list(move_items(
                    src, the_world.current_level(), src_p, first, last));

                return event_result::filter;
            } else if (cmd == ct::alt_get_items) {
                if (type == pile_type::container) {
                    update_list(move_items(items, dst, first, last));
                }

                return event_result::filter;
            } else if (cmd == ct::cancel) {
                return event_result::filter_detach;
            }

            return event_result::filter;
        });
    }

    void do_cancel() {
        if (item_list.is_visible()) {
            item_list.set_modal(false);
            item_list.hide();
        }
    }

    void do_toggle_inventory() {
        if (!item_list.toogle_visible()) {
            return;
        }

        do_view_inventory();
    }

    void do_view_inventory() {
        auto const p = get_player();
        view_item_pile("Inventory", p.first, p.second, p.first);
    }

    //! Common implementation for do_drop_one and do_drop_some
    //! @param first Iterator (pointer) to the first index of the item list to
    //!              remove from the player's inventory.
    //! @param last  Iterator (pointer) to the last (one past the end) index of
    //!              the item list to remove from the player's inventory.
    //! @pre @p first and @p last are non null and point to a contiguous range
    //!      of indicies.
    void impl_drop_selected_items_(item_pile& items, int const* const first, int const* const last) {
        BK_ASSERT(!!first && !!last);

        auto const p = get_player().second;

        static_string_buffer<128> buffer;

        for (auto it = first; it != last; ++it) {
            auto const  id   = item_list.get().row_data(*it);
            auto const& itm  = find(the_world, id);
            auto const  name = name_of(itm);

            buffer.clear();
            buffer.append("You drop the %s on the ground.", name.data());
            message_window.println(buffer.to_string());

            add_object_at(items.remove_item(id), p);
        }

        item_list.get().remove_rows(first, last);
        item_list.layout();
    }

    void impl_do_drop_n_(int const n) {
        BK_ASSERT(n > 0);

        auto const player_info  = get_player();
        auto const player_p     = player_info.second;
        auto&      player       = get_player().first;
        auto&      player_items = player.items();

        if (player_items.size() <= 0) {
            message_window.println("You have nothing to drop.");
            return;
        }

        auto const on_cancel = [&] {
            message_window.println("Nevermind.");
        };

        auto const on_confirm =
            [&, player_p](int const* const first, int const* const last) {
                move_items(player, the_world.current_level(), player_p
                         , first, last);
            };

        item_list.assign(player_items);

        if (n == 1) {
            choose_one_item("Drop which item?"
                          , [=](int const i) { on_confirm(&i, &i + 1); }
                          , on_cancel);
        } else {
            choose_n_items("Drop which item(s)?", on_confirm, on_cancel);
        }
    }

    //! Drop one item, or no items from the player's inventory at the player's
    //! current position.
    void do_drop_one() {
        impl_do_drop_n_(1);
    }

    //! Drop 0 or more items from the player's inventory at the player's
    //! current position.
    void do_drop_some() {
        impl_do_drop_n_(2);
    }

    void do_open() {
        auto const  player_info = get_player();
        auto&       player      = player_info.first;
        auto const  player_p    = player_info.second;
        auto&       lvl         = the_world.current_level();

        //
        // check for containers at the players position
        //

        // no items whatsoever here
        auto* const pile = lvl.item_at(player_p);
        if (!pile) {
            message_window.println("There is nothing here to open.");
            return;
        }

        // is the item given by id a container
        auto const is_container = [&](item_instance_id const id) noexcept {
            auto const& itm = find(the_world, id);
            auto const capacity = get_property_value_or(
                database, itm, property(item_property::capacity), 0);
            return capacity;
        };

        // find items which are containers
        auto const find_container = [&](auto const first, auto const last) {
            return std::find_if(first, last, is_container);
        };

        auto const last = pile->end();
        auto       it   = find_container(pile->begin(), last);

        // no containers here
        if (it == last) {
            message_window.println("There is nothing here to open.");
            return;
        }

        //
        // There is at least one container to consider here
        //

        // view the contents of the container give by id
        auto const show_container = [&, player_p](item_instance_id const id) {
            auto&      container = find(the_world, id);
            auto&      items     = container.items();
            auto const name      = name_of(container);

            static_string_buffer<128> buffer;
            buffer.append("You open the %s.", name.data());
            message_window.println(buffer.to_string());

            view_item_pile(name.to_string(), container, player_p, player);
        };

        auto const container0 = it;
        auto const container1 = find_container(++it, last);

        // there is only one container here; show its contents
        if (container1 == last) {
            show_container(*container0);
            return;
        }

        // there are at least two containers here; build a list and let the
        // player decide which to inspect
        auto& il = item_list;
        il.clear();
        il.append(*container0);

        it = container1;
        for ( ; it != last; it = find_container(++it, last)) {
            il.append(*it);
        }

        il.layout();

        choose_one_item("Open which container?"
          , [=](int const index) {
                auto const id = (*pile)[static_cast<size_t>(index)];
                show_container(id);
            }
          , [&] {
                message_window.println("Nevermind.");
            });
    }

    void do_debug_teleport_self() {
        message_window.println("Teleport where?");

        input_context c;

        c.on_mouse_button_handler =
            [&](mouse_event const event, kb_modifiers const kmods) {
                if (event.button_state_bits() != 1u) {
                    return event_result::filter;
                }

                auto const result =
                    do_player_move_to(window_to_world({event.x, event.y}));

                if (result != placement_result::ok) {
                    message_window.println("Invalid destination. Choose another.");
                    return event_result::filter;
                }

                message_window.println("Done.");
                return event_result::filter_detach;
            };

        c.on_command_handler =
            [&](command_type const type, uint64_t) {
                if (type == command_type::debug_teleport_self) {
                    message_window.println("Already teleporting.");
                    return event_result::filter;
                } else if (type == command_type::cancel) {
                    message_window.println("Canceled teleporting.");
                    return event_result::filter_detach;
                }

                return event_result::filter;
            };

        context_stack.push_back(std::move(c));
    }


    //! Common implementation for moving items from entity -> level and from
    //! item -> level.
    template <typename Object, typename Predicate, typename Success>
    merge_item_result impl_move_items_object_to_level_(
        Object&         src
      , level&          dest_lvl
      , point2i32 const dest_p
      , int const* const first, int const* const last
      , Predicate pred
      , Success on_success
    ) {
        auto& items = src.items();

        BK_ASSERT(dest_lvl.can_place_item_at(dest_p) == placement_result::ok);

        auto const sink = [&](unique_item&& itm) {
            auto const id = itm.get();
            dest_lvl.add_object_at(std::move(itm), dest_p);
            on_success(id);
        };

        items.remove_if(first, last, pred, sink);

        // didn't actually move anything
        auto* const pile = dest_lvl.item_at(dest_p);
        if (!pile) {
            return merge_item_result::ok_merged_none;
        }

        // make sure the resulting pile uses the correct icon
        renderer_add(get_pile_id(*pile, get_pile_id()), dest_p);

        return items.empty()
          ? merge_item_result::ok_merged_all
          : merge_item_result::ok_merged_some;
    }

    // move items from item (container) to level
    merge_item_result move_items(
        item&           src
      , level&          dest_lvl
      , point2i32 const dest_p
      , int const* const first, int const* const last
    ) {
        static_string_buffer<128> buffer;
        auto const src_name = name_of(src);

        return impl_move_items_object_to_level_(
            src, dest_lvl, dest_p, first, last
          , [](item_instance_id const id) noexcept { return true; }
          , [&](item_instance_id const id) {
                buffer.clear();
                buffer.append("You remove the %s from the %s and drop it on the ground."
                            , name_of(id).data(), src_name.data());
                message_window.println(buffer.to_string());
            }
        );
    }

    // move items from entity to level
    merge_item_result move_items(
        entity&         src
      , level&          dest_lvl
      , point2i32 const dest_p
      , int const* const first, int const* const last
    ) {
        static_string_buffer<128> buffer;

        return impl_move_items_object_to_level_(
            src, dest_lvl, dest_p, first, last
          , [](item_instance_id const id) noexcept { return true; }
          , [&](item_instance_id const id) {
                buffer.clear();
                buffer.append("You drop the %s on the ground."
                            , name_of(id).data());
                message_window.println(buffer.to_string());
            }
        );
    }

    //! move items from level to entity
    merge_item_result move_items(
        level&          src_lvl
      , point2i32 const src_p
      , entity&         dest
      , int const* const first, int const* const last
    ) {
        auto& lvl = the_world.current_level();
        BK_ASSERT(&lvl == &src_lvl);

        static_string_buffer<128> buffer;

        auto const pred = [&](item_instance_id const id) {
            return can_add_item(database, dest, find(the_world, id));
        };

        auto const sink = [&](unique_item&& itm, item_pile& pile) {
            auto const name = name_of(itm);

            merge_into_pile(the_world, database, std::move(itm), pile);

            buffer.clear();
            buffer.append("Picked up %s.", name.data());
            message_window.println(buffer.to_string());
        };

        return src_lvl.move_items(
            src_p, dest.items(), first, last, pred, sink);
    }

    //! move items from pile to entity
    merge_item_result move_items(
        item_pile& src
      , entity&    dest
      , int const* const first, int const* const last
    ) {
        auto& dest_pile = dest.items();
        BK_ASSERT(&src != &dest_pile);

        auto const pred = [&](item_instance_id const id) {
            return can_add_item(database, dest, find(the_world, id));
        };

        static_string_buffer<128> buffer;

        auto const sink = [&](unique_item&& itm) {
            auto const name = name_of(itm);

            merge_into_pile(the_world, database, std::move(itm), dest_pile);

            buffer.clear();
            buffer.append("Picked up %s.", name.data());
            message_window.println(buffer.to_string());
        };

        auto const size_before_src = src.size();
        auto const size_before_dst = dest_pile.size();

        if (!first && !last) {
            src.remove_if(pred, sink);
        } else {
            src.remove_if(first, last, pred, sink);
        }

        auto const size_after_src = src.size();
        auto const size_after_dst = dest_pile.size();

        BK_ASSERT(size_before_src >= size_after_src
               && size_before_dst <= size_after_dst);

        auto const n0 = size_before_src - size_after_src;
        auto const n1 = size_after_dst  - size_before_dst;

        BK_ASSERT(n0 == n1);

        if (n0 == 0) {
            return merge_item_result::ok_merged_none;
        } else if (n0 > 0 && !src.empty()) {
            return merge_item_result::ok_merged_some;
        }

        // TODO: this could be made smarter; requires some API reworking though
        //       to pass the index of the current item in consideration to either
        //       the predicate, sink, or both.
        item_list.clear();

        return merge_item_result::ok_merged_all;
    }

    //! Give to the player 0 to N items from the @p pile argument. The
    //! indicies of the items to get are given by the range [first, last).
    //!
    //! If both @p first and @p last are null, pickup all items. Otherwise,
    //! pick up only items at the indicies given by the range [first, last) from
    //! the item list.
    //!
    //! @param first An optional iterator (pointer) to the index of the first
    //!              item to get.
    //! @param last  An optional iterator (pointer) to the index of the last
    //!              (one past the end) item to get.
    //! @pre Either @p first and @p last are both null, or neither are null and
    //!      point to a contiguous range of indicies.
    //! @pre The @p pile must not be a floor pile. That it, it must be a pile
    //!     owned by another item, entity, etc.
    //! @note For piles on the ground use get_selected_items(int*, int*).
    merge_item_result get_selected_items(
        item_pile& pile
      , int const* const first = nullptr
      , int const* const last  = nullptr
    ) {
        BK_ASSERT(( !first &&  !last)
               || (!!first && !!last));

        auto const player_info = get_player();
        auto&      player      = player_info.first;

        auto const result = move_items(pile, player, first, last);

        switch (result) {
        case merge_item_result::ok_merged_none: break;
        case merge_item_result::ok_merged_all:  break;
        case merge_item_result::ok_merged_some: break;
        case merge_item_result::failed_bad_source:
            // should never happen as the pile is passed in as an argument
            BK_ATTRIBUTE_FALLTHROUGH;
        case merge_item_result::failed_bad_destination:
            // merging into an entity (the player) should never cause this
            BK_ATTRIBUTE_FALLTHROUGH;
        default:
            BK_ASSERT(false);
            break;
        }

        return result;
    }

    //! Give to the player 0 to N items from the ground beneath the player. The
    //! indicies of the items to get are given by the range [first, last).
    //!
    //! If both @p first and @p last are null, pickup all items. Otherwise,
    //! pick up only items at the indicies given by the range [first, last) from
    //! the item list.
    //!
    //! @param first An optional iterator (pointer) to the index of the first
    //!              item to get.
    //! @param last  An optional iterator (pointer) to the index of the last
    //!              (one past the end) item to get.
    //! @pre Either @p first and @p last are both null, or neither are null and
    //!      point to a contiguous range of indicies.
    merge_item_result get_selected_items(
        int const* const first = nullptr
      , int const* const last  = nullptr
    ) {
        BK_ASSERT(( !first &&  !last)
               || (!!first && !!last));

        auto&      lvl         = the_world.current_level();
        auto const player_info = get_player();
        auto&      player      = player_info.first;
        auto const player_p    = player_info.second;

        auto const result = move_items(lvl, player_p, player, first, last);

        switch (result) {
        case merge_item_result::ok_merged_none:
            break;
        case merge_item_result::ok_merged_all:
            renderer_remove_item(player_p);
            break;
        case merge_item_result::ok_merged_some:
            break;
        case merge_item_result::failed_bad_source:
            message_window.println("There is nothing here to pick up.");
            break;
        case merge_item_result::failed_bad_destination:
            // merging into an entity (the player) should never cause this
            BK_ATTRIBUTE_FALLTHROUGH;
        default:
            BK_ASSERT(false);
            break;
        }

        return result;
    }

    //! Pickup all items at the player's current position.
    void do_get_all_items() {
        auto const was_visible = item_list.is_visible();

        auto const result = get_selected_items();

        if (!was_visible) {
            return;
        }

        if (result != merge_item_result::ok_merged_all
         && result == merge_item_result::ok_merged_some
        ) {
            return;
        }

        item_list.assign(get_player().first.items());
        item_list.layout();
    }

    //! Pickup 0..N items from a list at the player's current position.
    void do_get_items() {
        auto* const pile = the_world.current_level().item_at(get_player().second);
        if (!pile) {
            message_window.println("There is nothing here to get.");
            return;
        }

        item_list.assign(*pile);
        choose_n_items("Pick up which item(s)?"
          , [&](int const* const first, int const* const last) {
                get_selected_items(first, last);
            }
          , [&] {
                message_window.println("Nevermind.");
            });
    }

    void do_kill(entity& e, point2i32 const p) {
        auto& lvl = the_world.current_level();

        BK_ASSERT(!e.is_alive()
               && lvl.is_entity_at(p));

        static_string_buffer<128> buffer;
        buffer.append("The %s dies.", name_of(e).data());
        message_window.println(buffer.to_string());

        get_entity_loot(e, rng_superficial, [&](unique_item&& itm) {
            add_object_at(std::move(itm), p);
        });

        lvl.remove_entity(e.instance());
        renderer_remove_entity(p);
    }

    void do_combat(point2i32 const att_pos, point2i32 const def_pos) {
        auto& lvl = the_world.current_level();

        auto* const att = lvl.entity_at(att_pos);
        auto* const def = lvl.entity_at(def_pos);

        BK_ASSERT(!!att && !!def);
        BK_ASSERT(att->is_alive() && def->is_alive());

        def->modify_health(-1);

        if (!def->is_alive()) {
            do_kill(*def, def_pos);
        }

        advance(1);
    }

    //! Attempt to change levels at the current player position.
    //! @param type Must be either command_type::move_down or
    //! command_type::move_up.
    void do_change_level(command_type const type) {
        BK_ASSERT(type == command_type::move_down
               || type == command_type::move_up);

        auto& cur_lvl = the_world.current_level();

        auto const player          = get_player();
        auto const player_p        = player.second;
        auto const player_instance = player.first.instance();
        auto const player_id       = player.first.definition();

        auto const delta = [&]() noexcept {
            auto const tile = cur_lvl.at(player_p);

            auto const tile_code = (tile.id == tile_id::stair_down)  ? 1
                                 : (tile.id == tile_id::stair_up)    ? 2
                                                                     : 0;
            auto const move_code = (type == command_type::move_down) ? 1
                                 : (type == command_type::move_up)   ? 2
                                                                     : 0;
            switch ((move_code << 2) | tile_code) {
            case 0b0100 : // move_down & other
            case 0b1000 : // move_up   & other
                message_window.println("There are no stairs here.");
                break;;
            case 0b0101 : // move_down & stair_down
                return 1;
            case 0b1010 : // move_up   & stair_up
                return -1;
            case 0b0110 : // move_down & stair_up
                message_window.println("You can't go down here.");
                break;
            case 0b1001 : // move_up   & stair_down
                message_window.println("You can't go up here.");
                break;
            default:
                BK_ASSERT(false); // some other command was given
                break;
            }

            return 0;
        }();

        if (delta == 0) {
            return;
        }

        auto const id = static_cast<ptrdiff_t>(cur_lvl.id());
        if (id + delta < 0) {
            message_window.println("You can't leave.");
            return;
        }

        auto const next_id = static_cast<size_t>(id + delta);

        auto player_ent = cur_lvl.remove_entity(player_instance);

        if (!the_world.has_level(next_id)) {
            generate(next_id);
        } else {
            set_current_level(next_id, false);
        }

        // the level has been changed at this point

        auto const p = (delta > 0)
          ? the_world.current_level().stair_up(0)
          : the_world.current_level().stair_down(0);

        add_object_near(std::move(player_ent), player_id, p, 5, rng_substantive);

        reset_view_to_player();
    }

    placement_result do_player_move_to(point2i32 const p) {
        auto&      lvl         = the_world.current_level();
        auto const player_info = get_player();
        auto&      player      = player_info.first;
        auto const p_cur       = player_info.second;
        auto const p_dst       = p;

        auto const result = lvl.move_by(player.instance(), p_dst - p_cur);

        switch (result) {
        case placement_result::ok:
            renderer_update(player, p_cur, p_dst);
            break;
        case placement_result::failed_entity:   BK_ATTRIBUTE_FALLTHROUGH;
        case placement_result::failed_obstacle: BK_ATTRIBUTE_FALLTHROUGH;
        case placement_result::failed_bounds:   BK_ATTRIBUTE_FALLTHROUGH;
        case placement_result::failed_bad_id:
            break;
        default:
            BK_ASSERT(false);
            break;
        }

        return result;
    }

    void do_player_run(vec2i32 const v) {
        BK_ASSERT(value_cast(abs(v.x)) <= 1
               && value_cast(abs(v.y)) <= 1
               && v != vec2i32 {});

        auto&      lvl         = the_world.current_level();
        auto const player_info = get_player();
        auto const player_id   = player_info.first.definition();
        auto const player_inst = player_info.first.instance();
        auto       player_p    = player_info.second;

        using namespace std::chrono;
        constexpr auto delay      = duration_cast<nanoseconds>(seconds {1}) / 100;
        constexpr auto timer_name = djb2_hash_32c("run timer");

        // TODO: this is a bit of a hack; pushing new contexts should return an
        //       identifier of for later use.
        auto const context_index = context_stack.size();

        auto const timer_id = timers.add(timer_name, timer::duration {0}
          , [=, &lvl](timer::duration, timer::timer_data) mutable -> timer::duration {
                auto const result = lvl.move_by(player_inst, v);
                if (result == placement_result::ok) {
                    auto const p_cur = player_p;
                    player_p = player_p + v;
                    renderer_update(player_id, p_cur, player_p);
                    advance(1);
                } else {
                    // TODO: see the above TODO
                    auto const where = context_stack.begin()
                        + static_cast<ptrdiff_t>(context_index);

                    context_stack.erase(where);

                    return timer::duration {};
                }

                return delay;
          });

        // add an input context that automatically terminates the run on
        // player input
        input_context c;

        c.on_mouse_button_handler = [=](auto, auto) {
            timers.remove(timer_id);
            return event_result::filter_detach;
        };

        c.on_command_handler = [=](auto, auto) {
            timers.remove(timer_id);
            return event_result::filter_detach;
        };

        context_stack.push_back(std::move(c));
    }

    placement_result do_player_move_by(vec2i32 const v) {
        BK_ASSERT(value_cast(abs(v.x)) <= 1
               && value_cast(abs(v.y)) <= 1
               && v != vec2i32 {});

        auto&      lvl         = the_world.current_level();
        auto const player_info = get_player();
        auto&      player      = player_info.first;
        auto const p_cur       = player_info.second;
        auto const p_dst       = p_cur + v;

        auto const result = lvl.move_by(player.instance(), v);

        switch (result) {
        case placement_result::ok:
            renderer_update(player, p_cur, p_dst);
            advance(1);
            break;
        case placement_result::failed_entity:
            do_combat(p_cur, p_dst);
            break;
        case placement_result::failed_obstacle:
            interact_obstacle(player, p_cur, p_dst);
            break;
        case placement_result::failed_bounds:
            break;
        case placement_result::failed_bad_id:
            // the player id should always be valid
            BK_ATTRIBUTE_FALLTHROUGH;
        default :
            BK_ASSERT(false);
            break;
        }

        return result;
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Object creation
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    unique_entity create_object(entity_definition const& def, random_state& rng) {
        return boken::create_object(the_world, def, rng);
    }

    unique_item create_object(item_definition const& def, random_state& rng) {
        return boken::create_object(the_world, def, rng);
    }

    template <typename Definition>
    auto impl_create_object_from_def_at_(Definition const& def, point2i32 const p, random_state& rng) {
        renderer_add(def.id, p);

        return the_world.current_level()
            .add_object_at(create_object(def, rng), p);
    }

    item_instance_id create_object_at(item_definition const& def, point2i32 const p, random_state& rng) {
        return impl_create_object_from_def_at_(def, p, rng);
    }

    entity_instance_id create_object_at(entity_definition const& def, point2i32 const p, random_state& rng) {
        return impl_create_object_from_def_at_(def, p, rng);
    }

    template <typename Definition>
    auto impl_create_object_from_id_at_(Definition const id, point2i32 const p, random_state& rng) {
        auto* const def = database.find(id);
        bool  const ok  = !!def;

        using instance_t = decltype(create_object_at(*def, p, rng));

        auto const instance_id = ok
          ? create_object_at(*def, p, rng)
          : instance_t {};

        return std::make_pair(instance_id, ok);
    }

    std::pair<item_instance_id, bool>
    create_object_at(item_id const id, point2i32 const p, random_state& rng) {
        return impl_create_object_from_id_at_(id, p, rng);
    }

    std::pair<entity_instance_id, bool>
    create_object_at(entity_id const id, point2i32 const p, random_state& rng) {
        return impl_create_object_from_id_at_(id, p, rng);
    }

    void create_item_in(item_instance_id const dest, item_definition const& def, random_state& rng) {
        auto itm = create_object(def, rng);
        boken::find(the_world, dest).add_item(std::move(itm));
    }

    void create_item_in(item& dest, item_definition const& def, random_state& rng) {
        create_item_in(dest.instance(), def, rng);
    }

    point2i32 add_object_near(
        unique_entity&& e
      , entity_id const id
      , point2i32 const p
      , int32_t   const distance
      , random_state&   rng
    ) {
        auto& lvl = the_world.current_level();

        auto const result =
            lvl.find_valid_entity_placement_neareast(rng, p, distance);

        BK_ASSERT(result.second == placement_result::ok);

        auto const q = result.first;

        lvl.add_object_at(std::move(e), q);
        renderer_add(id, q);

        return q;
    }

    item_instance_id add_object_at(unique_item&& i, point2i32 const p) {
        BK_ASSERT(!!i);
        auto const id = boken::find(the_world, i.get()).definition();
        renderer_add(id, p);
        return the_world.current_level().add_object_at(std::move(i), p);
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    void interact_obstacle(entity& e, point2i32 const cur_pos, point2i32 const obstacle_pos) {
        auto& lvl = the_world.current_level();

        auto const tile = lvl.at(obstacle_pos);
        if (tile.type == tile_type::door) {
            auto const id = (tile.id == tile_id::door_ns_closed)
                ? tile_id::door_ns_open
                : tile_id::door_ew_open;

            tile_data_set const data {
                tile_data {}
                , tile_flags {0}
                , id
                , tile.type
                , region_id {}
            };

            renderer.update_map_data(lvl.update_tile_at(rng_superficial, obstacle_pos, data));
        }
    }

    //! Advance the game time by @p steps
    void advance(int const steps) {
        auto& lvl = the_world.current_level();

        lvl.transform_entities(
            [&](entity& e, point2i32 const p) noexcept {
                // the player
                if (e.instance() == entity_instance_id {1u}) {
                    return p;
                }

                if (!random_chance_in_x(rng_superficial, 1, 10)) {
                    return p;
                }

                constexpr std::array<int, 4> dir_x {-1,  0, 0, 1};
                constexpr std::array<int, 4> dir_y { 0, -1, 1, 0};

                auto const dir = static_cast<size_t>(random_uniform_int(rng_superficial, 0, 3));
                auto const d   = vec2i32 {dir_x[dir], dir_y[dir]};

                return p + d;
            }
          , [&](entity& e, point2i32 const p, point2i32 const q) noexcept {
                entity_updates_.push_back({p, q, e.definition()});
            }
        );
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Rendering
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    void renderer_update(entity_id const id, point2i32 const p_old, point2i32 const p_new) {
        entity_updates_.push_back({p_old, p_new, id});
    }

    void renderer_update(item_id const id, point2i32 const p_old, point2i32 const p_new) {
        item_updates_.push_back({p_old, p_new, id});
    }

    void renderer_update(entity const& e, point2i32 const p_old, point2i32 const p_new) {
        renderer_update(e.definition(), p_old, p_new);
    }

    void renderer_update(item const& i, point2i32 const p_old, point2i32 const p_new) {
        renderer_update(i.definition(), p_old, p_new);
    }

    void renderer_add(entity_id const id, point2i32 const p) {
        renderer_update(id, p, p);
    }

    void renderer_add(item_id const id, point2i32 const p) {
        renderer_update(id, p, p);
    }

    void renderer_remove_item(point2i32 const p) {
        item_updates_.push_back({p, p, item_id {}});
    }

    void renderer_remove_entity(point2i32 const p) {
        entity_updates_.push_back({p, p, entity_id {}});
    }

    //! Render the game
    void render(timepoint_t const last_frame) {
        using namespace std::chrono;

        constexpr auto frame_time =
            duration_cast<nanoseconds>(seconds {1}) / 60;

        auto const now   = clock_t::now();
        auto const delta = now - last_frame;

        if (delta < frame_time) {
            return;
        }

        if (!entity_updates_.empty()) {
            auto const n = static_cast<ptrdiff_t>(entity_updates_.size());
            auto const p = entity_updates_.data();
            renderer.update_data(p, p + n);
            entity_updates_.clear();
        }

        if (!item_updates_.empty()) {
            auto const n = static_cast<ptrdiff_t>(item_updates_.size());
            auto const p = item_updates_.data();
            renderer.update_data(p, p + n);
            item_updates_.clear();
        }

        renderer.render(delta, current_view);

        last_frame_time = now;
    }

    //! The main game loop
    void run() {
        while (os.is_running()) {
            timers.update();
            os.do_events();
            render(last_frame_time);
        }
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Data
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    struct state_t {
        template <typename T>
        using up = std::unique_ptr<T>;

        up<system>             system_ptr          = make_system();
        up<random_state>       rng_substantive_ptr = make_random_state();
        up<random_state>       rng_superficial_ptr = make_random_state();
        up<game_database>      database_ptr        = make_game_database();
        up<world>              world_ptr           = make_world();
        up<text_renderer>      trender_ptr         = make_text_renderer();
        up<game_renderer>      renderer_ptr        = make_game_renderer(*system_ptr, *trender_ptr);
        up<command_translator> cmd_translator_ptr  = make_command_translator();
    } state {};

    system&             os              = *state.system_ptr;
    random_state&       rng_substantive = *state.rng_substantive_ptr;
    random_state&       rng_superficial = *state.rng_superficial_ptr;
    game_database&      database        = *state.database_ptr;
    world&              the_world       = *state.world_ptr;
    game_renderer&      renderer        = *state.renderer_ptr;
    text_renderer&      trender         = *state.trender_ptr;
    command_translator& cmd_translator  = *state.cmd_translator_ptr;

    timer timers;

    item_list_controller item_list;

    std::vector<input_context> context_stack;

    view current_view;

    int last_mouse_x = 0;
    int last_mouse_y = 0;

    std::vector<game_renderer::update_t<item_id>>   item_updates_;
    std::vector<game_renderer::update_t<entity_id>> entity_updates_;

    timepoint_t last_frame_time {};

    message_log message_window {trender};
};

} // namespace boken

namespace {
#if defined(BK_NO_TESTS)
void run_tests() {
}
#else
void run_tests() {
    using namespace std::chrono;

    auto const beg = high_resolution_clock::now();
    boken::run_unit_tests();
    auto const end = high_resolution_clock::now();

    std::printf("Tests took %" PRId64 " microseconds.\n",
        duration_cast<microseconds>(end - beg).count());
}
#endif // BK_NO_TESTS
} // namespace

int main(int const argc, char const* argv[]) try {
    run_tests();

    boken::game_state game;
    game.run();

    return 0;
} catch (...) {
    return 1;
}
