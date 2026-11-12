#include "cfr.hpp"

#include <cmath>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdexcept>

namespace py = pybind11;
using namespace aof;

namespace {

py::dict metrics_dict(const Metrics& m) {
    py::dict result;
    result["sb_ev"] = m.sb_ev;
    result["bb_ev"] = m.bb_ev;
    result["sb_br_ev"] = m.sb_br_ev;
    result["bb_br_ev"] = m.bb_br_ev;
    result["sb_improvement"] = m.sb_gain;
    result["bb_improvement"] = m.bb_gain;
    result["nash_conv"] = m.nash_conv();
    result["exploitability"] = m.nash_conv() / 2;
    return result;
}

Strategy checked_strategy(const Range& sb, const Range& bb) {
    for (const auto* range : {&sb, &bb}) {
        for (const double p : *range) {
            if (!std::isfinite(p) || p < 0 || p > 1) {
                throw std::invalid_argument("Strategy probabilities must lie in [0, 1]");
            }
        }
    }
    return {sb, bb};
}

}  // namespace

PYBIND11_MODULE(_core, module) {
    module.def("hand_name", &hand_name);
    module.def("_regret_match", &regret_match);
    module.def("_terminal_payoff", &terminal_payoff);
    module.def("_canonical_key", [](const Deal& cards) {
        if (!legal_deal(cards)) throw std::invalid_argument("A deal requires four distinct cards");
        return canonical_key(cards);
    });
    py::class_<CFR>(module, "Engine")
        .def(py::init([](py::bytes bytes) { return CFR(std::string(bytes)); }))
        .def("train", &CFR::train, py::call_guard<py::gil_scoped_release>())
        .def_property_readonly("iterations", &CFR::iterations)
        .def("average", [](const CFR& cfr) {
            const auto strategy = cfr.average();
            return py::make_tuple(strategy.sb_allin, strategy.bb_call);
        })
        .def("best_response", [](const CFR& cfr) {
            const auto response = cfr.best_response();
            return py::make_tuple(response.sb_allin, response.bb_call);
        })
        .def("evaluate", [](const CFR& cfr) { return metrics_dict(cfr.evaluate()); })
        .def("history", [](const CFR& cfr) {
            py::list result;
            for (const auto& point : cfr.history()) {
                auto row = metrics_dict(point.metrics);
                row["iteration"] = point.iteration;
                result.append(row);
            }
            return result;
        })
        .def("_state", [](const CFR& cfr) {
            const auto& current = cfr.current();
            const auto values = cfr.game().action_values(current);
            return py::make_tuple(current.sb_allin, current.bb_call,
                                  cfr.sb_regrets(), cfr.bb_regrets(), values.sb, values.bb);
        })
        .def("_evaluate_profile", [](const CFR& cfr, const Range& sb, const Range& bb) {
            return metrics_dict(cfr.game().evaluate(checked_strategy(sb, bb)));
        })
        .def("_verify_concrete", [](const CFR& cfr, const Range& sb, const Range& bb) {
            return cfr.game().verify_concrete(checked_strategy(sb, bb));
        }, py::call_guard<py::gil_scoped_release>());
}
