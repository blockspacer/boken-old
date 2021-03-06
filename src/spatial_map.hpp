#pragma once

#include "math.hpp"
#include "functional.hpp"

#include <vector>
#include <type_traits>
#include <algorithm>

namespace boken {

//! @returns the offset from the start to the first item in the container
//! matching the predicate. Otherwise returns -1.
template <typename Container, typename Predicate>
ptrdiff_t find_offset_to(Container&& c, Predicate pred) noexcept {
    auto const first = std::begin(c);
    auto const last  = std::end(c);
    auto const it    = std::find_if(first, last, pred);

    return (it == last)
        ? ptrdiff_t {-1}
        : std::distance(first, it);
}

//! @returns a begin / end iterator pair for the container (vector).
template <typename Container>
auto vector_to_range(Container&& c) noexcept {
    using iterator_cat = typename decltype(c.begin())::iterator_category;
    static_assert(std::is_same<std::random_access_iterator_tag, iterator_cat>::value, "");
    return std::make_pair(c.data(), c.data() + c.size());
}

struct identity {
    template <typename T>
    constexpr auto&& operator()(T&& v) const noexcept {
        return std::forward<T>(v);
    }
};

template <typename Value             //!< The value type stored
        , typename GetKey = identity //!< GetKey(value) -> key for value
        , typename Scalar = int32_t  //!< The scalar type for positions
>
class spatial_map {
public:
    using value_type  = Value;
    using key_type    = std::decay_t<std::result_of_t<GetKey (Value)>>;
    using scalar_type = Scalar;
    using point_type  = point2<Scalar>;

    static_assert(!std::is_void<key_type>::value, "");

    spatial_map(
        scalar_type const width
      , scalar_type const height
      , GetKey            get_key = GetKey {}
    )
      : get_key_ {std::move(get_key)}
      , width_   {width}
      , height_  {height}
    {
    }

    size_t size() const noexcept {
        return values_.size();
    }

    //! add the value at the point p if a value isn't already present for the
    //! the point given by p.
    std::pair<value_type*, bool> insert(point_type const p, value_type&& value) {
        auto const offset = find_offset_to_(p);
        if (offset < 0) {
            return insert_(p, std::move(value));
        }

        return {values_.data() + offset, false};
    }

    //! add the value at the point p overwriting any existing value.
    std::pair<value_type*, bool> insert_or_replace(point_type const p, value_type&& value) {
        auto const offset = find_offset_to_(p);
        if (offset < 0) {
            return insert_(p, std::move(value));
        }

        *(positions_.begin() + offset) = p;
        *(values_.begin()    + offset) = std::move(value);

        return {values_.data() + offset, false};
    }

    template <typename BinaryF>
    bool move_to_if(key_type const k, BinaryF f) noexcept {
        return move_to_if_(k, f);
    }

    bool move_to(key_type const k, point_type const p) noexcept {
        return move_to_(k, p);
    }

    template <typename BinaryF>
    bool move_to_if(point_type const p, BinaryF f) noexcept {
        return move_to_if_(p, f);
    }

    bool move_to(point_type const p, point_type const p0) noexcept {
        return move_to_(p, p0);
    }

    std::pair<key_type, bool> erase(point_type const p) {
        return erase_(p);
    }

    std::pair<key_type, bool> erase(key_type const k) {
        return erase_(k);
    }

    value_type* find(point_type const p) noexcept {
        auto const offset = find_offset_to_(p);
        return (offset < 0)
          ? nullptr
          : values_.data() + offset;
    }

    value_type const* find(point_type const p) const noexcept {
        return const_cast<spatial_map*>(this)->find(p);
    }

    std::pair<value_type*, point_type> find(key_type const k) noexcept {
        using pair_t = std::pair<value_type*, point_type>;
        auto const offset = find_offset_to_(k);
        return offset < 0
          ? pair_t {nullptr, {}}
          : pair_t {values_.data() + offset, *(positions_.data() + offset)};
    }

    std::pair<value_type const*, point_type>
    find(key_type const k) const noexcept {
        return const_cast<spatial_map*>(this)->find(k);
    }

    auto positions_range() const noexcept {
        return vector_to_range(positions_);
    }

    auto values_range() const noexcept {
        return vector_to_range(values_);
    }

    auto values_range() noexcept {
        return vector_to_range(values_);
    }

    template <typename F>
    void for_each(F f) const {
        auto const g = void_as_bool<true>(f);
        auto const n = values_.size();
        for (size_t i = 0; i < n; ++i) {
            if (!g(values_[i], positions_[i])) {
                break;
            }
        }
    }
private:
    template <typename Key, typename BinaryF>
    bool move_to_if_(Key const k, BinaryF f) noexcept {
        auto const offset = find_offset_to_(k);
        if (offset < 0) {
            return false;
        }

        auto const result = f(*(values_.begin()    + offset)
                            , *(positions_.begin() + offset));

        if (!result.second) {
            return false;
        }

        *(positions_.begin() + offset) = result.first;
        return true;
    }

    template <typename Key>
    bool move_to_(Key const k, point_type const p) noexcept {
        return move_to_if_(k, [p](auto&&) noexcept {
              return std::make_pair(true, p); });
    }

    std::pair<value_type*, bool> insert_(point_type const p, value_type&& value) {
        positions_.push_back(p);
        values_.push_back(std::move(value));
        return {std::addressof(values_.back()), true};
    }

    template <typename Key>
    std::pair<key_type, bool> erase_(Key const k) noexcept {
        auto const offset = find_offset_to_(k);
        if (offset < 0) {
            return {key_type {}, false};
        }

        auto const result_key = get_key_(*(values_.begin() + offset));

        positions_.erase(positions_.begin() + offset);
        values_.erase(values_.begin() + offset);

        return {result_key, true};
    }

    ptrdiff_t find_offset_to_(point_type const p) const noexcept {
        return find_offset_to(positions_
          , [p](point_type const p0) noexcept { return p == p0; });
    }

    ptrdiff_t find_offset_to_(key_type const k) const noexcept {
        return find_offset_to(values_
          , [&](value_type const& v) noexcept { return k == get_key_(v); });
    }
private:
    GetKey get_key_;

    std::vector<point_type> positions_;
    std::vector<value_type> values_;

    scalar_type width_;
    scalar_type height_;
};

} //namespace boken
