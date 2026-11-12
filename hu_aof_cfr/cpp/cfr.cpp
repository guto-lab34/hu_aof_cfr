#include "cfr.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace aof {
namespace {

void add_regrets(Actions& cumulative, Actions values, double probability) {
    const double expected = mix(values, probability);
    for (int action = 0; action < 2; ++action) {
        cumulative[action] += values[action] - expected;
    }
}

double best_action(Actions values) {
    if (values[1] > values[0]) return 1.0;
    if (values[1] < values[0]) return 0.0;
    return 0.5;
}

}  // namespace

double regret_match(Actions cumulative_regret) {
    const double fold = std::max(cumulative_regret[0], 0.0);
    const double active = std::max(cumulative_regret[1], 0.0);
    return fold + active > 0 ? active / (fold + active) : 0.5;
}

CFR::CFR(std::string_view table_bytes) : game_(table_bytes) {
    current_.sb_allin.fill(0.5);
    current_.bb_call.fill(0.5);
}

void CFR::train(std::uint64_t iterations, std::uint64_t evaluate_every) {
    if (iterations == 0 || evaluate_every == 0 ||
        iterations > std::numeric_limits<std::uint64_t>::max() - iterations_) {
        throw std::invalid_argument("Invalid iteration count or evaluation interval");
    }
    for (std::uint64_t round = 0; round < iterations; ++round) {
        // Freeze both seats' action values using the same pre-update profile.
        const Values values = game_.action_values(current_);
        for (int i = 0; i < classes; ++i) {
            strategy_sum_.sb_allin[i] += current_.sb_allin[i];
            strategy_sum_.bb_call[i] += current_.bb_call[i];
            add_regrets(sb_regrets_[i], values.sb[i], current_.sb_allin[i]);
            add_regrets(bb_regrets_[i], values.bb[i], current_.bb_call[i]);
        }
        for (int i = 0; i < classes; ++i) {
            current_.sb_allin[i] = regret_match(sb_regrets_[i]);
            current_.bb_call[i] = regret_match(bb_regrets_[i]);
        }
        ++iterations_;
        if (iterations_ % evaluate_every == 0) {
            history_.push_back({iterations_, evaluate()});
        }
    }
}

Strategy CFR::average() const {
    if (iterations_ == 0) return current_;
    Strategy result = strategy_sum_;
    for (int i = 0; i < classes; ++i) {
        result.sb_allin[i] /= double(iterations_);
        result.bb_call[i] /= double(iterations_);
    }
    return result;
}

Strategy CFR::best_response() const {
    const Values values = game_.action_values(average());
    Strategy result;
    for (int i = 0; i < classes; ++i) {
        result.sb_allin[i] = best_action(values.sb[i]);
        result.bb_call[i] = best_action(values.bb[i]);
    }
    return result;
}

}  // namespace aof
