#pragma once

#include "math_types.hpp"
#include "types.hpp"
#include "utility.hpp"
#include "context.hpp"
#include "maybe.hpp"

#include <memory>
#include <utility>
#include <vector>
#include <array>
#include <functional>

#include <cstdint>
#include <cstddef>

//=====--------------------------------------------------------------------=====
//                            Forward Declarations
//=====--------------------------------------------------------------------=====
namespace boken {

namespace detail { struct tag_tile_flags; }

template <typename> class flag_set;
using tile_flags = flag_set<detail::tag_tile_flags>;

class string_buffer_base;
class item_pile;
class random_state;
struct tile_data;
struct tile_data_set;

enum class tile_type : uint16_t;
enum class tile_id : uint32_t;
enum class merge_item_result : uint32_t;

} // namespace boken
//=====--------------------------------------------------------------------=====

namespace boken {

struct tile_view {
    tile_id    const&       id;
    tile_type  const&       type;
    tile_flags const&       flags;
    region_id  const&       rid;
    tile_data  const* const data;
};

enum class placement_result : uint32_t {
    ok, failed_obstacle, failed_entity, failed_bounds, failed_bad_id
};

struct region_info {
    recti32 bounds;
    int32_t entity_count;
    int32_t item_count;
    int32_t tile_count;
    int32_t id;
};

//====---
// A generic level concept
//====---
class level {
public:
    virtual ~level();

    //===--------------------------------------------------------------------===
    //                               Queries
    //===--------------------------------------------------------------------===

    //! The width of the level in tiles.
    virtual sizei32x width() const noexcept = 0;

    //! The width of the level in tiles.
    virtual sizei32y height() const noexcept = 0;

    //! The bounds of the level in tiles.
    virtual recti32 bounds() const noexcept = 0;

    //! The identifier for the level.
    virtual size_t id() const noexcept = 0;

    //! Return a valid position if an entity with @p id exists on the level.
    virtual maybe<point2i32> find(entity_instance_id id) const noexcept = 0;

    //! Return a valid instance id if an entity is at the position given by @p p.
    virtual maybe<entity_instance_id> entity_at(point2i32 p) const noexcept = 0;

    //! Return a valid pointer if an item_pile exists at the position given by
    //! @p p. Otherwise, return a nullptr.
    virtual item_pile const* item_at(point2i32 p) const noexcept = 0;

    //! As entity_at, but return an array of instance ids.
    template <typename... Args>
    auto entities_at(point2i32 const p, Args const... points) const noexcept {
        constexpr size_t N = 1 + sizeof...(Args);

        std::array<point2i32, N> const pts {p, points...};
        std::array<maybe<entity_instance_id>, N> result;

        entities_at(pts.data()
                  , pts.data() + N
                  , result.data()
                  , result.data() + N);

        return result;
    }

    //! Return whether an entity can be placed at @p p. If not possible, return
    //! the reason for why placement is impossible.
    virtual placement_result can_place_entity_at(point2i32 p) const noexcept = 0;

    //! Return whether an item can be placed at @p p. If not possible, return
    //! the reason for why placement is impossible.
    virtual placement_result can_place_item_at(point2i32 p) const noexcept = 0;

    //! Return the number of regions in the level.
    virtual size_t region_count() const noexcept = 0;

    //! Return information about the region with index @p i.
    virtual region_info region(size_t i) const noexcept = 0;

    //! Return all information about the tile at the given position.
    virtual tile_view at(point2i32 p) const noexcept = 0;

    tile_view at(int x, int y) const noexcept {
        return at(point2i32 {x, y});
    }

    //! Returns the location of the stairs with index @p i.
    virtual point2i32 stair_up(int const i) const noexcept = 0;
    virtual point2i32 stair_down(int const i) const noexcept = 0;

    //! If @p f returns false, the entity is destroyed before control returns to
    //! the caller.
    virtual unique_entity with_entity_at(
        point2i32 p, std::function<bool (entity_instance_id)> const& f) = 0;

    virtual void for_each_pile(
        std::function<void (item_pile const&, point2i32)> const& f) const = 0;

    virtual void for_each_pile_while(
        std::function<bool (item_pile const&, point2i32)> const& f) const = 0;

    virtual void for_each_entity(
        std::function<void (entity_instance_id, point2i32)> const& f) const = 0;

    virtual void for_each_entity_while(
        std::function<bool (entity_instance_id, point2i32)> const& f) const = 0;

    //! The vector will have its contents cleared and will then be filled with a
    //! path from @p from to @p to.
    //! @note not thread safe
    virtual std::vector<point2i32> const& find_path(point2i32 from, point2i32 to) const = 0;

    virtual bool has_line_of_sight(point2i32 from, point2i32 to) const = 0;

    template <typename T>
    using const_range = std::pair<T const*, T const*>;

    template <typename T>
    using object_position = std::pair<point2i32, T>;

    using entity_position = object_position<entity_instance_id>;

    // O(n) where n is the total number of entities on the level
    virtual const_range<entity_position>
        entities_near(point2i32 p, int32_t distance) const = 0;

    virtual void for_each_entity_near_while(point2i32 p, int32_t distance
        , std::function<bool (entity_position)> const& f) const = 0;

    virtual void for_each_entity_near(point2i32 p, int32_t distance
        , std::function<void (entity_position)> const& f) const = 0;

    //===--------------------------------------------------------------------===
    //                          State Mutation
    //===--------------------------------------------------------------------===
    using transform_f = std::function<
        std::pair<entity_descriptor, point2i32> (entity_instance_id, point2i32)>;

    using transform_callback_f = std::function<
        void (entity_descriptor, placement_result, point2i32, point2i32)>;

    virtual void transform_entities(
        transform_f tranform, transform_callback_f callback) = 0;

    //!@{
    //! Add an object at the position given by @p p.
    //! @returns The instance id of the object added.
    //! @pre     The position given by @p p must be valid for the object.

    virtual item_instance_id   add_object_at(unique_item&&   i, point2i32 p) = 0;
    virtual entity_instance_id add_object_at(unique_entity&& e, point2i32 p) = 0;
    //!@}

    //! @returns an empty id if there is no entity on the level with the given
    //!          position or instance id.
    virtual unique_entity remove_entity_at(point2i32 p) noexcept = 0;
    virtual unique_entity remove_entity(entity_instance_id id) noexcept = 0;

    //! Attempt to place the item @p i at the location given by @p p. If not
    //! possible, randomly probe adjacent tiles no more than @p max_distance
    //! from @p p. If still not possible the item @p i will be left unmodified.
    //! Otherwise the level will take ownership of @p i.
    //! @returns The actual position that item was placed. Otherwise {0, 0} and
    //! the reason why placement failed.
    virtual std::pair<point2i32, placement_result>
        add_object_nearest_random(random_state& rng, unique_item&& i
                                , point2i32 p, int max_distance) = 0;

    virtual std::pair<point2i32, placement_result>
        add_object_nearest_random(random_state& rng, unique_entity&& e
                                , point2i32 p, int max_distance) = 0;

    virtual std::pair<point2i32, placement_result>
        find_valid_item_placement_neareast(random_state& rng, point2i32 p
                                         , int max_distance) const noexcept = 0;

    virtual std::pair<point2i32, placement_result>
        find_valid_entity_placement_neareast(random_state& rng, point2i32 p
                                           , int max_distance) const noexcept = 0;

    virtual placement_result
        move_by(item_instance_id id, vec2i32 v) noexcept = 0;

    virtual placement_result
        move_by(entity_instance_id id, vec2i32 v) noexcept = 0;

    virtual const_sub_region_range<tile_id>
        update_tile_at(random_state& rng, point2i32 p
                     , tile_data_set const& data) noexcept = 0;

    virtual std::pair<merge_item_result, int>
    move_items(
        point2i32 from
      , std::function<void (unique_item&&, int)> const& pred) = 0;

    virtual std::pair<merge_item_result, int>
    move_items(
        point2i32  from
      , int const* first
      , int const* last
      , std::function<void (unique_item&&, int)> const& pred) = 0;

    //===--------------------------------------------------------------------===
    //                         Block-based data access
    //===--------------------------------------------------------------------===
    virtual const_sub_region_range<tile_id>
        tile_ids(recti32 area) const noexcept = 0;

    virtual const_sub_region_range<region_id>
        region_ids(recti32 area) const noexcept = 0;

private:
    virtual void entities_at(
        point2i32 const* first, point2i32 const* last
      , maybe<entity_instance_id>* out_first, maybe<entity_instance_id>* out_last) const noexcept = 0;
};

std::unique_ptr<level>
make_level(random_state& rng, world& w, sizei32x width, sizei32y height
         , size_t id);

namespace detail {

bool impl_can_add_item(
    const_context           ctx
  , const_entity_descriptor subject
  , const_item_descriptor   itm
  , const_level_location    itm_dest
  , string_buffer_base&     result
) noexcept;

bool impl_can_remove_item(
    const_context           ctx
  , const_entity_descriptor subject
  , const_level_location    itm_source
  , const_item_descriptor   itm
  , string_buffer_base&     result
) noexcept;

} // namespace detail

inline bool can_add_item(
    const_context                      const ctx
  , subject_t<const_entity_descriptor> const subject
  , object_t<const_item_descriptor>    const itm
  , to_t<const_level_location>         const itm_dest
  , string_buffer_base&                      result
) noexcept {
    return detail::impl_can_add_item(ctx, subject, itm, itm_dest, result);
}

inline bool can_remove_item(
    const_context                      const ctx
  , subject_t<const_entity_descriptor> const subject
  , from_t<const_level_location>       const itm_source
  , object_t<const_item_descriptor>    const itm
  , string_buffer_base&                      result
) noexcept {
    return detail::impl_can_remove_item(ctx, subject, itm_source, itm, result);
}

void merge_into_pile(context ctx, unique_item itm_ptr, item_descriptor itm
    , level_location dst);

} //namespace boken
