#include "equity_table.hpp"

#include <algorithm>
#include <stdexcept>

namespace aof {
namespace {

const auto& suit_permutations() {
    static const auto permutations = [] {
        std::array<std::array<int, 4>, 24> result{};
        std::array<int, 4> suits{0, 1, 2, 3};
        auto it = result.begin();
        do {
            *it++ = suits;
        } while (std::next_permutation(suits.begin(), suits.end()));
        return result;
    }();
    return permutations;
}

std::array<std::uint32_t, 24> orbit_keys(const Deal& deal) {
    std::array<std::uint32_t, 24> keys{};
    auto it = keys.begin();
    for (const auto& permutation : suit_permutations()) {
        Deal cards = deal;
        for (int& card : cards) {
            card = (card & ~3) + permutation[card % 4];
        }
        if (cards[0] > cards[1]) std::swap(cards[0], cards[1]);
        if (cards[2] > cards[3]) std::swap(cards[2], cards[3]);
        std::uint32_t key = 0;
        for (int card : cards) key = (key << 6) | static_cast<unsigned>(card);
        *it++ = key;
    }
    return keys;
}

std::uint32_t read_u32(std::string_view bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= std::uint32_t(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    }
    return value;
}

}  // namespace

bool legal_deal(const Deal& deal) {
    std::uint64_t used = 0;
    for (int card : deal) {
        if (card < 0 || card >= 52) return false;
        const std::uint64_t bit = std::uint64_t{1} << card;
        if (used & bit) return false;
        used |= bit;
    }
    return true;
}

Deal decode(std::uint32_t key) {
    return {int((key >> 18) & 63), int((key >> 12) & 63),
            int((key >> 6) & 63), int(key & 63)};
}

std::uint32_t canonical_key(const Deal& deal) {
    const auto keys = orbit_keys(deal);
    return *std::min_element(keys.begin(), keys.end());
}

unsigned orbit_size(const Deal& deal) {
    auto keys = orbit_keys(deal);
    std::sort(keys.begin(), keys.end());
    return static_cast<unsigned>(std::unique(keys.begin(), keys.end()) - keys.begin());
}

EquityTable::EquityTable(std::string_view bytes) {
    constexpr std::size_t header_size = 20;
    constexpr std::size_t record_size = 12;
    if (bytes.size() != header_size + orbit_count * record_size ||
        bytes.substr(0, 8) != "HUAOFEQ1" ||
        read_u32(bytes, 8) != orbit_count || read_u32(bytes, 12) != board_count ||
        read_u32(bytes, 16) != deal_count) {
        throw std::invalid_argument("Invalid HU equity table size or header");
    }

    records_.reserve(orbit_count);
    for (std::size_t offset = header_size; offset < bytes.size(); offset += record_size) {
        EquityRecord record{read_u32(bytes, offset), read_u32(bytes, offset + 4),
                            read_u32(bytes, offset + 8)};
        const Deal deal = decode(record.key);
        if (record.key >= (1u << 24) || !legal_deal(deal) ||
            record.key != canonical_key(deal) ||
            (!records_.empty() && record.key <= records_.back().key) ||
            record.wins > board_count || record.ties > board_count - record.wins) {
            throw std::invalid_argument("Invalid HU equity record");
        }
        records_.push_back(record);
    }

    for (const auto& record : records_) {
        const auto cards = decode(record.key);
        const auto& reversed = lookup({cards[2], cards[3], cards[0], cards[1]});
        if (record.wins != reversed.losses() || record.ties != reversed.ties) {
            throw std::invalid_argument("HU equity table fails seat reversal");
        }
    }
}

const EquityRecord& EquityTable::find(std::uint32_t key) const {
    const auto it = std::lower_bound(records_.begin(), records_.end(), key,
        [](const EquityRecord& record, std::uint32_t candidate) {
            return record.key < candidate;
        });
    if (it == records_.end() || it->key != key) {
        throw std::invalid_argument("Legal deal missing from HU equity table");
    }
    return *it;
}

const EquityRecord& EquityTable::lookup(const Deal& deal) const {
    if (!legal_deal(deal)) throw std::invalid_argument("A deal requires four distinct cards");
    return find(canonical_key(deal));
}

}  // namespace aof
