#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace aof {

constexpr std::uint32_t board_count = 1'712'304;
constexpr std::uint32_t deal_count = 1'624'350;
constexpr std::uint32_t orbit_count = 93'769;
using Deal = std::array<int, 4>;  // SB's cards, then BB's cards.

struct EquityRecord {
    std::uint32_t key;
    std::uint32_t wins;
    std::uint32_t ties;
    std::uint32_t losses() const { return board_count - wins - ties; }
};

bool legal_deal(const Deal& deal);
Deal decode(std::uint32_t key);
std::uint32_t canonical_key(const Deal& deal);
unsigned orbit_size(const Deal& deal);

class EquityTable {
public:
    explicit EquityTable(std::string_view bytes);
    const std::vector<EquityRecord>& records() const { return records_; }
    const EquityRecord& lookup(const Deal& deal) const;

private:
    const EquityRecord& find(std::uint32_t key) const;
    std::vector<EquityRecord> records_;
};

}  // namespace aof
