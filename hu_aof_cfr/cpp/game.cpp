#include "game.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace aof {
namespace {

int hand_class(int first, int second) {
    const int a = 12 - first / 4;
    const int b = 12 - second / 4;
    const auto [high, low] = std::minmax(a, b);
    return first % 4 == second % 4 ? 13 * high + low : 13 * low + high;
}

unsigned combos(int index) {
    const int row = index / 13;
    const int col = index % 13;
    return row == col ? 6 : (row < col ? 4 : 12);
}

struct Counts {
    std::uint32_t deals = 0;
    std::uint64_t wins = 0;
    std::uint64_t ties = 0;
};

double showdown_sum(std::uint64_t wins, std::uint64_t losses, double probability) {
    // The tie contributes zero net profit.
    return stack * (double(wins) - double(losses)) * probability / board_count;
}

}  // namespace

std::string hand_name(int index) {
    if (index < 0 || index >= classes) throw std::out_of_range("Invalid hand class");
    constexpr std::string_view ranks = "AKQJT98765432";
    const int row = index / 13;
    const int col = index % 13;
    std::string result{ranks[std::min(row, col)], ranks[std::max(row, col)]};
    if (row != col) result += row < col ? 's' : 'o';
    return result;
}

double mix(Actions values, double active_probability) {
    return (1 - active_probability) * values[0] + active_probability * values[1];
}

Actions terminal_payoff(unsigned outcome) {
    switch (outcome) {
    case 0: return {-small_blind, small_blind};
    case 1: return {big_blind, -big_blind};
    case 2: return {stack, -stack};
    case 3: return {0, 0};
    case 4: return {-stack, stack};
    default: throw std::out_of_range("Invalid terminal outcome");
    }
}

Game::Game(std::string_view table_bytes)
    : table_(table_bytes), chance_(classes * classes), weighted_showdown_(classes * classes) {
    std::vector<Counts> totals(chance_.size());
    for (const auto& record : table_.records()) {
        const auto [a, b, c, d] = decode(record.key);
        auto& cell = totals[hand_class(a, b) * classes + hand_class(c, d)];
        const auto weight = orbit_size({a, b, c, d});
        cell.deals += weight;
        cell.wins += std::uint64_t{weight} * record.wins;
        cell.ties += std::uint64_t{weight} * record.ties;
    }

    unsigned all_deals = 0;
    for (int sb = 0; sb < classes; ++sb) {
        unsigned row_deals = 0;
        for (int bb = 0; bb < classes; ++bb) {
            const int index = sb * classes + bb;
            const auto& cell = totals[index];
            const auto& reverse = totals[bb * classes + sb];
            const auto losses = std::uint64_t{cell.deals} * board_count - cell.wins - cell.ties;
            if (cell.deals != reverse.deals || losses != reverse.wins ||
                cell.ties != reverse.ties) {
                throw std::invalid_argument("Asymmetric equity aggregation");
            }
            chance_[index] = double(cell.deals) / deal_count;
            weighted_showdown_[index] = showdown_sum(cell.wins, losses, 1.0 / deal_count);
            row_deals += cell.deals;
        }
        if (row_deals != 1225 * combos(sb)) {
            throw std::invalid_argument("Invalid HU hand-class marginal");
        }
        all_deals += row_deals;
    }
    if (all_deals != deal_count) throw std::invalid_argument("Invalid total deal count");
}

Values Game::action_values(const Strategy& strategy) const {
    Values values;
    for (int sb = 0; sb < classes; ++sb) {
        const double shove = strategy.sb_allin[sb];
        for (int bb = 0; bb < classes; ++bb) {
            const int index = sb * classes + bb;
            const double p = chance_[index];
            const double showdown = weighted_showdown_[index];
            const double call = strategy.bb_call[bb];

            // Values include chance and opponent reach, prior to our action.
            values.sb[sb][0] -= small_blind * p;
            values.sb[sb][1] += (1 - call) * big_blind * p + call * showdown;
            values.bb[bb][0] -= shove * big_blind * p;
            values.bb[bb][1] -= shove * showdown;
            values.bb_walk_ev += (1 - shove) * small_blind * p;
        }
    }
    return values;
}

Metrics Game::evaluate(const Strategy& strategy) const {
    const Values values = action_values(strategy);
    Metrics result;
    result.bb_ev = result.bb_br_ev = values.bb_walk_ev;
    for (int i = 0; i < classes; ++i) {
        const auto& sb = values.sb[i];
        const auto& bb = values.bb[i];
        result.sb_ev += mix(sb, strategy.sb_allin[i]);
        result.bb_ev += mix(bb, strategy.bb_call[i]);
        result.sb_br_ev += std::max(sb[0], sb[1]);
        result.bb_br_ev += std::max(bb[0], bb[1]);
        // Opponent fixed: maximize each own information set after summing over hidden deals.
        const double sb_edge = sb[1] - sb[0];
        const double bb_edge = bb[1] - bb[0];
        result.sb_gain += sb_edge >= 0 ? (1 - strategy.sb_allin[i]) * sb_edge
                                       : -strategy.sb_allin[i] * sb_edge;
        result.bb_gain += bb_edge >= 0 ? (1 - strategy.bb_call[i]) * bb_edge
                                       : -strategy.bb_call[i] * bb_edge;
    }
    return result;
}

double Game::verify_concrete(const Strategy& strategy) const {
    const Values reduced = action_values(strategy);
    unsigned deals = 0;
    double max_error = std::abs(std::accumulate(chance_.begin(), chance_.end(), 0.0) - 1);
    for (int first = 0; first < 52; ++first) {
        for (int second = first + 1; second < 52; ++second) {
            const int own_class = hand_class(first, second);
            Actions sb{}, bb{};
            unsigned opponents = 0;
            for (int third = 0; third < 52; ++third) {
                for (int fourth = third + 1; fourth < 52; ++fourth) {
                    const Deal deal{first, second, third, fourth};
                    if (!legal_deal(deal)) continue;
                    ++opponents;
                    const int opponent = hand_class(third, fourth);
                    const auto& record = table_.lookup(deal);
                    const double equity = (record.wins + 0.5 * record.ties) / board_count;
                    const double net = (2 * stack) * equity - stack;
                    sb[0] += terminal_payoff(0)[0];
                    sb[1] += mix({terminal_payoff(1)[0], net}, strategy.bb_call[opponent]);
                    bb[0] += strategy.sb_allin[opponent] * terminal_payoff(1)[1];
                    bb[1] += strategy.sb_allin[opponent] * net;
                }
            }
            if (opponents != 1225) throw std::logic_error("Wrong number of legal opponent hands");
            deals += opponents;
            const double scale = double(combos(own_class)) / deal_count;
            for (int action = 0; action < 2; ++action) {
                max_error = std::max(max_error, std::abs(scale * sb[action] - reduced.sb[own_class][action]));
                max_error = std::max(max_error, std::abs(scale * bb[action] - reduced.bb[own_class][action]));
            }
        }
    }
    if (deals != deal_count) throw std::logic_error("Concrete deal count differs from table");
    return max_error;
}

}  // namespace aof
