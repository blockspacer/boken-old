#include "random.hpp"

#include <boost/predef/other/endian.h>

#if defined (BOOST_ENDIAN_LITTLE_BYTE_AVAILABLE)
#   define PCG_LITTLE_ENDIAN 1
#endif

#include <pcg_random.hpp>
#include <pcg_extras.hpp>

#include <boost/random/uniform_int_distribution.hpp>
#include <boost/random/uniform_smallint.hpp>
#include <boost/random/normal_distribution.hpp>

#include <random>

namespace boken {

class random_state_impl final : public random_state {
public:
    random_state_impl() = default;

    boost::random::uniform_smallint<int>         dist_coin    {0, 1};
    boost::random::uniform_int_distribution<int> dist_uniform {};
    boost::random::normal_distribution<>         dist_normal  {};

    pcg32 state {};
};


std::unique_ptr<random_state> make_random_state() {
    return std::make_unique<random_state_impl>();
}

bool random_coin_flip(random_state& rs) noexcept {
    auto& r = reinterpret_cast<random_state_impl&>(rs);
    return !!r.dist_coin(r.state);
}

int random_uniform_int(random_state& rs, int const lo, int const hi) noexcept {
    auto& r = reinterpret_cast<random_state_impl&>(rs);

    using param_t = decltype(r.dist_uniform)::param_type;

    r.dist_uniform.param(param_t {lo, hi});
    r.dist_uniform.reset();

    return r.dist_uniform(r.state);
}

double random_normal(random_state& rs, double const m, double const v) noexcept {
    auto& r = reinterpret_cast<random_state_impl&>(rs);

    using param_t = decltype(r.dist_normal)::param_type;

    r.dist_normal.param(param_t {m, v});
    r.dist_normal.reset();

    return r.dist_normal(r.state);
}

} //namespace boken