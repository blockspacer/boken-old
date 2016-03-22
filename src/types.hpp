#pragma once

#include "math_types.hpp"
#include <memory>
#include <cstdint>

namespace boken { class world; }

namespace boken {

//===------------------------------------------------------------------------===
//                                  Tags
//===------------------------------------------------------------------------===
struct tag_id_entity          {};
struct tag_id_instance_entity {};
struct tag_id_item            {};
struct tag_id_instance_item   {};

//===------------------------------------------------------------------------===
//                              Type aliases
//===------------------------------------------------------------------------===

using entity_id          = tagged_value<uint32_t, tag_id_entity>;
using entity_instance_id = tagged_value<uint32_t, tag_id_instance_entity>;
using item_id            = tagged_value<uint32_t, tag_id_item>;
using item_instance_id   = tagged_value<uint32_t, tag_id_instance_item>;

//===------------------------------------------------------------------------===
//                              Custom deleters
//===------------------------------------------------------------------------===
class item_deleter {
public:
    using pointer = item_instance_id;

    explicit item_deleter(world* const w) noexcept : world_ {w} { }
    void operator()(pointer const id) const noexcept;
    world const& source_world() const noexcept { return *world_; }
private:
    world* world_;
};

using unique_item = std::unique_ptr<item_instance_id, item_deleter const&>;

class entity_deleter {
public:
    using pointer = entity_instance_id;

    explicit entity_deleter(world* const w) noexcept : world_ {w} { }
    void operator()(pointer const id) const noexcept;
    world const& source_world() const noexcept { return *world_; }
private:
    world* world_;
};

using unique_entity = std::unique_ptr<entity_instance_id, entity_deleter const&>;

} //namespace boken
