#include "entity.hpp"
#include "math.hpp"

namespace boken {

entity_instance_id get_instance(entity const& e) noexcept {
    return e.instance();
}

entity_id get_id(entity const& e) noexcept {
    return e.definition();
}

//=====--------------------------------------------------------------------=====
//                                  entity
//=====--------------------------------------------------------------------=====
int merge_item_piles(item_pile& from, entity& to, item_merge_f const& f) {
    return 0; // TODO
}

bool entity::is_alive() const noexcept {
    return cur_health_ > 0;
}

bool entity::modify_health(int16_t const delta) noexcept {
    constexpr int32_t lo = std::numeric_limits<int16_t>::min();
    constexpr int32_t hi = std::numeric_limits<int16_t>::max();

    auto const sum = int32_t {delta} + int32_t {cur_health_};

    cur_health_ = clamp_as<int16_t>(sum, lo, hi);

    return is_alive();
}

bool entity::can_add_item(item const& itm) {
    return true;
}

void entity::add_item(unique_item itm) {
    items_.add_item(std::move(itm));
}

} //namespace boken
