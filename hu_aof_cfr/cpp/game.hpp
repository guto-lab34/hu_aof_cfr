#pragma once

#include "equity_table.hpp"

#include <string>

namespace aof {

constexpr int classes = 169;
constexpr double stack = 8.0;
constexpr double small_blind = 0.5;
constexpr double big_blind = 1.0;
using Range = std::array<double, classes>;
using Actions = std::array<double, 2>;  // Fold, All-in/Call.
using ActionValues = std::array<Actions, classes>;

struct Strategy {
    Range sb_allin{};
    Range bb_call{};
};

struct Values {
    ActionValues sb{};
    ActionValues bb{};
    double bb_walk_ev = 0;
};

struct Metrics {
    double sb_ev = 0;
    double bb_ev = 0;
    double sb_br_ev = 0;
    double bb_br_ev = 0;
    double sb_gain = 0;
    double bb_gain = 0;
    double nash_conv() const { return sb_gain + bb_gain; }
};

std::string hand_name(int index);
double mix(Actions values, double active_probability);
Actions terminal_payoff(unsigned outcome);  // SB fold, BB fold, SB wins, tie, BB wins.

class Game {
public:
    explicit Game(std::string_view table_bytes);
    Values action_values(const Strategy& strategy) const;
    Metrics evaluate(const Strategy& strategy) const;
    double verify_concrete(const Strategy& strategy) const;

private:
    EquityTable table_;
    // Entry (i,j) is indexed by SB class i and BB class j.
    std::vector<double> chance_;
    std::vector<double> weighted_showdown_;
};

}  // namespace aof
