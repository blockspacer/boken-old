#pragma once

#include "config.hpp"
#include "hash.hpp"
#include "math.hpp"
#include "types.hpp"

#include <bitset>
#include <type_traits>

#include <cstdint>

namespace boken {

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4307) // integral constant overflow
#endif

enum class tile_id : uint32_t {
    invalid = 0
  , empty = djb2_hash_32c("empty")
  , floor = djb2_hash_32c("floor")
  , wall_0000 = djb2_hash_32c("wall_0000")
  , wall_0001 = djb2_hash_32c("wall_0001")
  , wall_0010 = djb2_hash_32c("wall_0010")
  , wall_0011 = djb2_hash_32c("wall_0011")
  , wall_0100 = djb2_hash_32c("wall_0100")
  , wall_0101 = djb2_hash_32c("wall_0101")
  , wall_0110 = djb2_hash_32c("wall_0110")
  , wall_0111 = djb2_hash_32c("wall_0111")
  , wall_1000 = djb2_hash_32c("wall_1000")
  , wall_1001 = djb2_hash_32c("wall_1001")
  , wall_1010 = djb2_hash_32c("wall_1010")
  , wall_1011 = djb2_hash_32c("wall_1011")
  , wall_1100 = djb2_hash_32c("wall_1100")
  , wall_1101 = djb2_hash_32c("wall_1101")
  , wall_1110 = djb2_hash_32c("wall_1110")
  , wall_1111 = djb2_hash_32c("wall_1111")
};

template <typename Enum>
Enum string_to_enum(string_view str) noexcept;

string_view enum_to_string(tile_id id) noexcept;

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif

enum class tile_type : uint16_t {
    empty, wall, floor, door, stair
};

class tile_flags {
public:
    template <size_t Bit>
    using flag_t = std::integral_constant<size_t, Bit>;

    static constexpr flag_t<1> f_solid = flag_t<1> {};

    constexpr explicit tile_flags(uint32_t const n = 0) noexcept
      : bits_ {n}
    {
    }

    template <size_t Bit>
    constexpr explicit tile_flags(flag_t<Bit>) noexcept
      : tile_flags {Bit}
    {
    }

    bool none() const noexcept {
        return bits_.none();
    }

    template <size_t Bit>
    constexpr bool test(flag_t<Bit>) const noexcept {
        return bits_.test(Bit - 1);
    }
private:
    std::bitset<32> bits_;
};

struct tile_data {
    uint64_t data;
};

//should be sorted by alignment for packing
struct tile_data_set {
    tile_data  data;
    tile_flags flags;
    tile_id    id;
    tile_type  type;
    uint16_t   tile_index;
    uint16_t   region_id;
};

struct tile_map {
    recti index_to_rect(int32_t const i) const noexcept {
        return {
            offix {i % value_cast(tiles_x)}
          , offiy {i / value_cast(tiles_x)}
          , tile_w
          , tile_h
        };
    }

    uint16_t id_to_index(tile_id const id) const noexcept;

    sizeix tile_w  {18};
    sizeiy tile_h  {18};
    sizeix tiles_x {16};
    sizeiy tiles_y {16};
};

} //namespace boken
