#pragma once

#include <limits>

#include "itsproblem.hpp"
#include "smt.hpp"
#include "itsproof.hpp"

class ABMC {

private:

    ABMC(ITSProblem &its);

    void analyze();

    ITSProblem &its;
    std::unique_ptr<Smt<IntTheory, BoolTheory>> solver;
    bool approx {false};
    unsigned last_orig_clause;
    std::vector<Subs> subs {Subs::Empty};
    std::vector<Implicant> trace;
    VarSet vars;
    std::map<Var, Var> post_vars;
    std::map<Implicant, int> lang_map;
    std::map<std::vector<int>, std::map<BoolExpr, TransIdx>> cache;
    std::map<int, std::vector<int>> history;
    NumVar trace_var;
    std::optional<TransIdx> shortcut;
    std::map<unsigned, NumVar> n_map;
    Expr objective {0};
    NumVar objective_var;
    std::map<unsigned, TransIdx> rule_map;
    int next {0};
    ITSProof proof;
    DependencyGraph<Implicant> dependency_graph;

    int get_language(unsigned i);
    BoolExpr encode_transition(const TransIdx idx);
    bool is_orig_clause(const TransIdx idx) const;
    std::optional<unsigned> has_looping_suffix(unsigned start, std::vector<int> &lang);
    TransIdx add_learned_clause(const Rule &accel, const unsigned backlink);
    std::tuple<Rule, Subs, bool> build_loop(const int backlink);
    bool handle_loop(int backlink, const std::vector<int> &lang);
    void unsat(const unsigned depth);
    void sat(const unsigned depth);
    void build_trace();
    bool is_redundant(const std::vector<int> &w) const;

public:

    static void analyze(ITSProblem &its);

};

std::ostream& operator<<(std::ostream &s, const std::vector<Implicant> &trace);
