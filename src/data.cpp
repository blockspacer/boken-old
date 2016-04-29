#include "data.hpp"
#include "entity_def.hpp"  // for entity_definition
#include "item_def.hpp"    // for item_definition
#include "tile.hpp"
#include "serialize.hpp"
#include "forward_declarations.hpp"
#include "algorithm.hpp"

#include "bkassert/assert.hpp"

#include <unordered_map>

namespace boken {

game_database::~game_database() = default;

class game_database_impl final : public game_database {
public:
    game_database_impl();

    item_definition const* find(item_id const id) const noexcept final override {
        return find_or_nullptr(item_defs_, id);
    }

    entity_definition const* find(entity_id const id) const noexcept final override {
        return find_or_nullptr(entity_defs_, id);
    }

    string_view find(item_property_id const id) const noexcept final override {
        auto const it = item_properties_.find(id);
        return it != end(item_properties_)
          ? string_view {it->second}
          : string_view {"{none such}"};
    }

    string_view find(entity_property_id const id) const noexcept final override {
        auto const it = entity_properties_.find(id);
        return it != end(entity_properties_)
          ? string_view {it->second}
          : string_view {"{none such}"};
    }

    tile_map const& get_tile_map(tile_map_type const type) const noexcept final override;
private:
    void load_entity_defs_();
    void load_item_defs_();

    std::unordered_map<entity_id, entity_definition, identity_hash> entity_defs_;
    std::unordered_map<item_id,   item_definition,   identity_hash> item_defs_;

    std::unordered_map<entity_property_id, std::string, identity_hash> entity_properties_;
    std::unordered_map<item_property_id,   std::string, identity_hash> item_properties_;

    tile_map tile_map_base_     {tile_map_type::base,   0, sizei32x {18}, sizei32y {18}, sizei32x {16}, sizei32y {16}};
    tile_map tile_map_entities_ {tile_map_type::entity, 1, sizei32x {18}, sizei32y {18}, sizei32x {26}, sizei32y {17}};
    tile_map tile_map_items_    {tile_map_type::item,   2, sizei32x {18}, sizei32y {18}, sizei32x {16}, sizei32y {16}};
};

std::unique_ptr<game_database> make_game_database() {
    return std::make_unique<game_database_impl>();
}

tile_map const&
game_database_impl::get_tile_map(tile_map_type const type) const noexcept {
    switch (type) {
    case tile_map_type::base:   break;
    case tile_map_type::entity: return tile_map_entities_;
    case tile_map_type::item:   return tile_map_items_;
    default:                    break;
    }

    return tile_map_base_;
}

void game_database_impl::load_entity_defs_() {
    load_entity_definitions(
        [&](entity_definition const& def) {
            auto const id = def.id;
            auto const tile_index = def.properties.value_or(djb2_hash_32c("tile_index"), 0);

            auto const result = entity_defs_.insert({id, std::move(def)});
            if (!result.second) {
                BK_ASSERT(false); //TODO collision
            }

            tile_map_entities_.add_mapping(id, tile_index);
        }
      , [&](string_view const string, uint32_t const hash, serialize_data_type const type, uint32_t const value) {
            auto const id = entity_property_id {hash};
            auto const it = entity_properties_.find(id);
            if (it == end(entity_properties_)) {
                entity_properties_.insert({id, string.to_string()});
            } else if (string != it->second) {
                BK_ASSERT(false); //TODO collision
            }

            return true;
        });
}

void game_database_impl::load_item_defs_() {
    load_item_definitions(
        [&](item_definition const& def) {
            auto const id = def.id;
            auto const tile_index = def.properties.value_or(djb2_hash_32c("tile_index"), 0);

            auto const result = item_defs_.insert({id, std::move(def)});
            if (!result.second) {
                BK_ASSERT(false); //TODO collision
            }

            tile_map_items_.add_mapping(id, tile_index);
        }
      , [&](string_view const string, uint32_t const hash, serialize_data_type const type, uint32_t const value) {
            auto const id = item_property_id {hash};
            auto const it = item_properties_.find(id);
            if (it == end(item_properties_)) {
                item_properties_.insert({id, string.to_string()});
            } else if (string != it->second) {
                BK_ASSERT(false); //TODO collision
            }

            return true;
        });
}

game_database_impl::game_database_impl() {
    load_entity_defs_();
    load_item_defs_();
}

item_definition const* find(game_database const& db, item_id const id) noexcept {
    return db.find(id);
}

entity_definition const* find(game_database const& db, entity_id const id) noexcept {
    return db.find(id);
}

} //namespace boken
