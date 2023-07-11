#pragma once

#include "proof.hpp"
#include "rule.hpp"

#include <optional>

namespace acceleration {

enum Status {
    NotSat, Nondet, PseudoLoop, Disjunctive, AccelerationFailed, ClosedFormFailed, Success
};

std::ostream& operator<<(std::ostream &s, const Status x);

struct Accel {
    Rule rule;
    Proof proof;
};

struct Nonterm {
    BoolExpr certificate {BExpression::False};
    Proof proof;
};

struct Result {
    Status status;
    std::optional<Accel> accel;
    std::optional<Nonterm> nonterm;
    unsigned prefix = 0;
    unsigned period = 1;
    std::optional<NumVar> n;

};

}
