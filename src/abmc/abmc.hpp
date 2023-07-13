#pragma once

#include <limits>

#include "itsproblem.hpp"
#include "smt.hpp"
#include "redundanceviaautomata.hpp"

class ABMC {

private:

    using Red = RedundanceViaAutomata;

    static const bool max_smt;
    static const bool optimize;

    ABMC(const ITSProblem &its);

    void analyze();

    ITSProblem its;
    std::unique_ptr<Smt<IntTheory, BoolTheory>> solver;
    bool approx {false};
    TransIdx last_orig_clause;
    std::vector<Subs> subs {Subs::Empty};
    std::vector<TransIdx> trace;
    VarSet vars;
    std::map<Var, Var> post_vars;
    NumVar trace_var;
    BoolExpr shortcut {BExpression::True};
    std::optional<NumVar> n;
    Expr objective {0};
    NumVar objective_var;
    int lookback {0};
    Red red;

    Automaton get_language(unsigned i);
    BoolExpr encode_transition(const TransIdx idx);
    bool is_orig_clause(const TransIdx idx) const;
    std::optional<unsigned> has_looping_suffix(unsigned start);
    TransIdx add_learned_clause(const Rule &accel, const unsigned backlink);
    std::tuple<Rule, Subs, Automaton> build_loop(const int backlink);
    bool handle_loop(int backlink);

public:

    static void analyze(const ITSProblem &its);

};