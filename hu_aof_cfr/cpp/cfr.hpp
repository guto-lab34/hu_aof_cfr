#pragma once

#include "game.hpp"

namespace aof {

double regret_match(Actions cumulative_regret);

struct EvaluationPoint {
    std::uint64_t iteration;
    Metrics metrics;
};

class CFR {
public:
    explicit CFR(std::string_view table_bytes);
    void train(std::uint64_t iterations, std::uint64_t evaluate_every);
    Strategy average() const;
    Strategy best_response() const;
    Metrics evaluate() const { return game_.evaluate(average()); }
    std::uint64_t iterations() const { return iterations_; }
    const std::vector<EvaluationPoint>& history() const { return history_; }

    // Verification interface, used by the Python tests.
    const Game& game() const { return game_; }
    const Strategy& current() const { return current_; }
    const ActionValues& sb_regrets() const { return sb_regrets_; }
    const ActionValues& bb_regrets() const { return bb_regrets_; }

private:
    Game game_;
    Strategy current_;
    Strategy strategy_sum_;
    ActionValues sb_regrets_{};
    ActionValues bb_regrets_{};
    std::uint64_t iterations_ = 0;
    std::vector<EvaluationPoint> history_;
};

}  // namespace aof
