#include "abmc.hpp"
#include "chain.hpp"
#include "expr.hpp"
#include "preprocessing.hpp"
#include "rulepreprocessing.hpp"
#include "smtfactory.hpp"
#include "loopacceleration.hpp"
#include "config.hpp"
#include "export.hpp"
#include "vector.hpp"
#include "z3_opt.hpp"
#include "z3.hpp"
#include "rule.hpp"
#include "dependencygraph.hpp"

using namespace Config::ABMC;

ABMC::ABMC(ITSProblem &its):
    its(its),
    trace_var(NumVar::next()),
    objective_var(NumVar::next()){
    if (max_smt || optimize) {
        solver = std::make_unique<Z3Opt<IntTheory, BoolTheory>>(smt::default_timeout);
        if (optimize) {
            solver->add_objective(objective_var);
        }
    } else {
        solver = std::make_unique<Z3<IntTheory, BoolTheory>>(smt::default_timeout);
    }
    vars.insert(trace_var);
    solver->enableModels();
}

bool ABMC::is_orig_clause(const TransIdx idx) const {
    return idx->getId() <= last_orig_clause;
}

template <class iterator>
bool starts_with_square(iterator &it, const unsigned size, const unsigned max_length) {
    auto start {it};
    auto end {it};
    for (auto length = 1u; length <= max_length; ++length) {
        ++end;
        if (std::equal(start, end, end)) {
            return true;
        }
    }
    return false;
}

bool ends_with_square(const std::vector<int> &w) {
    const auto it {w.rbegin()};
    return starts_with_square(it, w.size(), w.size() / 2);
}

bool has_square(const std::vector<int> &w, const unsigned max_length) {
    auto size = w.size();
    for (auto it = w.begin(); it != w.end(); ++it) {
        if (starts_with_square(it, size, max_length)) {
            return true;
        }
        --size;
    }
    return false;
}

bool ABMC::is_redundant(const std::vector<int> &ww) const {
    auto it {ww.begin()};
    const auto size {ww.size() / 2};
    for (auto i = 0u; i < size; ++i, ++it) {
        auto h {history.find(*it)};
        if (h != history.end()) {
            auto next {it};
            ++next;
            if (h->second.size() == size - 1 && std::equal(h->second.begin(), h->second.end(), next)) {
                return true;
            }
        }
    }
    return false;
}

std::optional<unsigned> ABMC::has_looping_suffix(unsigned start, std::vector<int> &lang) {
    const auto last {trace.back()};
    for (unsigned pos = start; pos > 0; --pos) {
        lang.push_back(get_language(pos));
        if (ends_with_square(lang)) {
            if (Config::Analysis::log) std::cout << "skipping square " << lang << std::endl;
            return {};
        } else {
            if (Config::Analysis::log) std::cout << lang << " does not have a square" << std::endl;
        }
        const auto &imp {trace[pos]};
        if (dependency_graph.hasEdge(last, imp)) {
            std::vector<int> ll{lang.begin(), lang.end()};
            ll.insert(ll.end(), lang.begin(), lang.end());
            if (has_square(ll, lang.size() / 2)) {
                if (Config::Analysis::log) std::cout << "skipping cyclic square " << lang << std::endl;
            } else if (is_redundant(ll)) {
                if (Config::Analysis::log) std::cout << "skipping redundant loop" << std::endl;
            } else {
                return pos;
            }
        }
    }
    return {};
}

int ABMC::get_language(unsigned i) {
    const auto imp {trace[i]};
    if (is_orig_clause(imp.first)) {
        const auto res {lang_map.emplace(imp, next)};
        if (res.second) {
            ++next;
        }
        return res.first->second;
    } else {
        return lang_map.at({imp.first, {}});
    }
}

std::tuple<Rule, Subs, bool> ABMC::build_loop(const int backlink) {
    std::optional<Rule> loop;
    Subs var_renaming;
    bool nested {false};
    for (int i = trace.size() - 1; i >= backlink; --i) {
        const auto imp {trace[i]};
        nested |= is_orig_clause(imp.first);
        const auto rule {imp.first->withGuard(BExpression::buildAndFromLitPtrs(imp.second))};
        if (loop) {
            const auto [chained, sigma] {Chaining::chain(rule, *loop)};
            loop = chained;
            var_renaming = expr::compose(sigma, var_renaming);
        } else {
            loop = rule;
        }
        var_renaming = expr::compose(subs[i].project(rule.vars()), var_renaming);
    }
    auto vars {loop->vars()};
    expr::collectCoDomainVars(var_renaming, vars);
    const auto model {expr::compose(var_renaming, solver->model(vars).toSubs())};
    const auto imp {loop->getGuard()->implicant(model)};
    if (!imp) {
        throw std::logic_error("model, but no implicant");
    }
    const auto implicant {loop->withGuard(BExpression::buildAndFromLitPtrs(*imp))};
    if (Config::Analysis::log) {
        std::cout << "found loop of length " << (trace.size() - backlink) << ":" << std::endl;
        ITSExport::printRule(implicant, std::cout);
        std::cout << std::endl;
    }
    return {implicant, model, nested};
}

TransIdx ABMC::add_learned_clause(const Rule &accel, const unsigned backlink) {
    const auto idx {its.addLearnedRule(accel, trace.at(backlink).first, trace.back().first)};
    rule_map.emplace(idx->getId(), idx);
    return idx;
}

bool ABMC::handle_loop(int backlink, const std::vector<int> &lang) {
    auto [loop, sample_point, nested] {build_loop(backlink)};
    const auto it {cache.find(lang)};
    if (it != cache.end()) {
        for (const auto &[imp, t]: it->second) {
            if (imp->subs(sample_point)->isTriviallyTrue()) {
                if (Config::Analysis::log) std::cout << "cache hit" << std::endl;
                if (t) {
                    shortcut = t;
                    return true;
                } else {
                    return false;
                }
            }
        }
    }
    const auto simp {Preprocess::preprocessRule(loop)};
    if (nested && !simp->isDeterministic()) {
        if (Config::Analysis::log) std::cout << "not accelerating non-deterministic, nested loop" << std::endl;
        return false;
    }
    if (Config::Analysis::reachability() && simp->getUpdate() == expr::concat(simp->getUpdate(), simp->getUpdate())) {
        if (Config::Analysis::log) std::cout << "acceleration would yield equivalent rule" << std::endl;
    } else {
        if (Config::Analysis::log && simp) {
            std::cout << "simplified loop:" << std::endl;
            ITSExport::printRule(*simp, std::cout);
            std::cout << std::endl;
        }
        if (Config::Analysis::reachability() && simp->getUpdate().empty()) {
            if (Config::Analysis::log) std::cout << "trivial looping suffix" << std::endl;
        } else {
            AccelConfig config {.tryNonterm = Config::Analysis::tryNonterm()};
            const auto accel_res {LoopAcceleration::accelerate(*simp, sample_point, config)};
            if (accel_res.accel) {
                auto simplified = Preprocess::preprocessRule(accel_res.accel->rule);
                if (simplified->getUpdate() != simp->getUpdate() && simplified->isPoly()) {
                    const auto new_idx {add_learned_clause(*simplified, backlink)};
                    n_map.emplace(new_idx->getId(), *accel_res.n);
                    vars.insert(*accel_res.n);
                    post_vars.emplace(*accel_res.n, NumVar::next());
                    shortcut = new_idx;
                    history.emplace(next, lang);
                    lang_map.emplace(Implicant(new_idx, {}), next);
                    ++next;
                    auto &map {cache.emplace(lang, std::map<BoolExpr, TransIdx>()).first->second};
                    map.emplace(accel_res.accel->covered, new_idx);
                    ITSProof sub_proof, acceleration_proof;
                    acceleration_proof.storeSubProof(simp.getProof());
                    acceleration_proof.ruleTransformationProof(*simp, "Loop Acceleration", accel_res.accel->rule);
                    acceleration_proof.storeSubProof(accel_res.accel->proof);
                    sub_proof.concat(acceleration_proof);
                    sub_proof.concat(simplified.getProof());
                    proof.majorProofStep("Accelerate", sub_proof, its);
                    if (Config::Analysis::log) {
                        std::cout << "accelerated rule, idx " << new_idx->getId() << std::endl;
                        ITSExport::printRule(*simplified, std::cout);
                        std::cout << std::endl;
                    }
                    return true;
                }
            }
        }
    }
    auto &map {cache.emplace(lang, std::map<BoolExpr, TransIdx>()).first->second};
    map.emplace(BExpression::True, nullptr);
    return false;
}

BoolExpr ABMC::encode_transition(const TransIdx idx) {
    const auto up {idx->getUpdate()};
    std::vector<BoolExpr> res {idx->getGuard()};
    res.emplace_back(BExpression::buildTheoryLit(Rel::buildEq(trace_var, idx->getId())));
    for (const auto &x: vars) {
        if (expr::isProgVar(x)) {
            res.push_back(expr::mkEq(expr::toExpr(post_vars.at(x)), up.get(x)));
        }
    }
    return BExpression::buildAnd(res);
}

void ABMC::unsat(const unsigned depth) {
    std::cout << "unsat" << std::endl;
    proof.append("reached error location at depth " + std::to_string(depth));
    proof.result("unsat");
    proof.print();
}

void ABMC::sat(const unsigned depth) {
    std::cout << "sat" << std::endl;
    proof.append(std::to_string(depth) + "-fold unrolling of the transition relation is unsatisfiable");
    proof.result("sat");
    proof.print();
}

void ABMC::build_trace() {
    trace.clear();
    const auto model {solver->model().toSubs()};
    if (optimize && Config::Analysis::log) {
        std::cout << "objective: " << objective.subs(model.get<IntTheory>()) << std::endl;
    }
    std::vector<Subs> run;
    std::optional<Implicant> prev;
    for (const auto &s: subs) {
        const auto rule {rule_map.at(s.get<IntTheory>(trace_var).subs(model.get<IntTheory>()).toNum().to_int())};
        const auto comp {expr::compose(s, model)};
        const auto imp {rule->getGuard()->implicant(comp)};
        run.push_back(comp.project(rule->getUpdate().domain()));
        if (!imp) {
            throw std::logic_error("model, but no implicant");
        }
        const Implicant i {rule, *imp};
        if (prev) {
            dependency_graph.addEdge(*prev, i);
        }
        prev = i;
        trace.emplace_back(rule, *imp);
    }
    if (Config::Analysis::log) {
        std::cout << "trace:" << std::endl << trace;
        std::cout << "run:" << std::endl;
        for (const auto &s: run) {
            std::cout << s << std::endl;
        }
    }
}

void ABMC::analyze() {
    if (Config::Analysis::log) {
        std::cout << "initial ITS" << std::endl;
        its.print(std::cout);
    }
    proof.majorProofStep("Initial ITS", ITSProof(), its);
    const auto res {Preprocess::preprocess(its)};
    if (res) {
        proof.concat(res.getProof());
        if (Config::Analysis::log) {
            std::cout << "Simplified ITS" << std::endl;
            ITSExport::printForProof(its, std::cout);
        }
    }
    vars.insertAll(its.getVars());
    for (const auto &var: vars) {
        post_vars.emplace(var, expr::next(var));
    }
    last_orig_clause = 0;
    for (const auto &r: its.getAllTransitions()) {
        rule_map.emplace(r.getId(), &r);
        last_orig_clause = std::max(last_orig_clause, r.getId());
    }
    std::vector<BoolExpr> inits;
    for (const auto &idx: its.getInitialTransitions()) {
        if (its.isSinkTransition(idx)) {
            switch (SmtFactory::check(idx->getGuard())) {
            case SmtResult::Sat:
                unsat(1);
                return;
            case SmtResult::Unknown:
                approx = true;
                break;
            case SmtResult::Unsat: {}
            }
        } else {
            inits.push_back(encode_transition(idx));
        }
    }
    solver->add(BExpression::buildOr(inits));

    std::vector<BoolExpr> steps;
    for (const auto &r: its.getAllTransitions()) {
        if (its.isInitialTransition(&r) || its.isSinkTransition(&r)) {
            continue;
        }
        steps.push_back(encode_transition(&r));
    }
    const auto step {BExpression::buildOr(steps)};

    std::vector<BoolExpr> queries;
    for (const auto &idx: its.getSinkTransitions()) {
        if (!its.isInitialTransition(idx)) {
            queries.push_back(idx->getGuard());
        }
    }
    const auto query {BExpression::buildOr(queries)};

    unsigned depth {0};
    VarSet trace_vars;
    trace_vars.insert(trace_var);
    trace_vars.insert(post_vars.at(trace_var));
    while (true) {
        if (Config::Analysis::log) {
            std::cout << "depth: " << depth << std::endl;
        }
        ++depth;
        Subs s;
        for (const auto &var: vars) {
            const auto &post_var {post_vars.at(var)};
            s.put(var, subs.back().get(post_var));
            const auto next {expr::next(post_var)};
            if (var == Var(trace_var)) {
                trace_vars.insert(next);
            }
            s.put(post_var, expr::toExpr(next));
        }
        solver->push();
        solver->add(query->subs(s));
        switch (solver->check()) {
        case SmtResult::Sat:
            build_trace();
            unsat(depth);
            return;
        case SmtResult::Unknown:
            if (Config::Analysis::log && !approx) {
                std::cout << "got unknown from SMT solver -- approximating" << std::endl;
            }
            approx = true;
            break;
        case SmtResult::Unsat: {}
        }
        solver->pop();
        if (!shortcut) {
            solver->add(step->subs(s));
        } else {
            const auto sc {encode_transition(*shortcut)->subs(s)};
            if (max_smt) {
                solver->add_soft(sc);
            }
            if (optimize) {
                const auto m {s.get<IntTheory>(n_map.at((*shortcut)->getId()))};
                objective = objective + m;
                solver->add(sc | (expr::mkEq(m, 0) & step->subs(s)));
            } else {
                solver->add(sc | step->subs(s));
            }
        }
        if (optimize) {
            solver->push();
            solver->add(expr::mkEq(objective_var, objective));
        }
        subs.push_back(s);
        switch (solver->check()) {
        case SmtResult::Unsat:
            if (!approx) {
                sat(depth);
            }
            return;
        case SmtResult::Sat: {
            shortcut.reset();
            build_trace();
            std::vector<int> lang;
            if (Config::Analysis::log) std::cout << "starting loop handling" << std::endl;
            for (auto backlink = has_looping_suffix(trace.size() - 1, lang);
                 backlink;
                 backlink = has_looping_suffix(*backlink - 1, lang)) {
                if (handle_loop(*backlink, lang)) {
                    break;
                }
            }
            if (Config::Analysis::log) std::cout << "done with loop handling" << std::endl;
            break;
        }
        case SmtResult::Unknown:
            shortcut.reset();
            trace.clear();
        }
        if (optimize) {
            solver->pop();
        }
    }
}

void ABMC::analyze(ITSProblem &its) {
    ABMC(its).analyze();
}

std::ostream& operator<<(std::ostream &s, const std::vector<Implicant> &trace) {
    for (const auto &imp: trace) {
        s << imp << std::endl;
    }
    return s;
}
