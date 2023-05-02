#include "reachability.hpp"
#include "chainstrategy.hpp"
#include "preprocess.hpp"
#include "loopacceleration.hpp"
#include "result.hpp"
#include "smtfactory.hpp"
#include "yices.hpp"
#include "smt.hpp"
#include "export.hpp"
#include "expression.hpp"
#include "literal.hpp"
#include "vector.hpp"
#include "asymptoticbound.hpp"
#include "vareliminator.hpp"
#include "substitution.hpp"
#include "chain.hpp"
#include "variable.hpp"

#include <numeric>
#include <random>

namespace reachability {

using ::operator<<;

bool Reachability::log = false;

LearningState::LearningState() {}

std::optional<Succeeded> LearningState::succeeded() {
    return {};
}

std::optional<Covered> LearningState::covered() {
    return {};
}

std::optional<Unroll> LearningState::unroll() {
    return {};
}

Succeeded::Succeeded(const Result<LearnedClauses> &learned): learned(learned) {}

std::optional<Succeeded> Succeeded::succeeded() {
    return *this;
}

const Result<LearnedClauses>& Succeeded::operator*() const {
    return learned;
}

const Result<LearnedClauses>* Succeeded::operator->() const {
    return &learned;
}

std::optional<Covered> Covered::covered() {
    return *this;
}

Unroll::Unroll() {}

Unroll::Unroll(unsigned max): max(max) {}

std::optional<unsigned> Unroll::get_max() {
    return max;
}

std::optional<Unroll> Unroll::unroll() {
    return *this;
}

PushPop::PushPop(LinearizingSolver<IntTheory, BoolTheory> &solver): solver(solver) {
    solver.push();
}

PushPop::~PushPop() {
    solver.pop();
}

Reachability::Reachability(ITSProblem &chcs): chcs(chcs), solver(smt_timeout) {
    solver.enableModels();
}

Step::Step(const TransIdx transition, const BoolExpr &sat, const Subs &var_renaming, const Subs &model, const Rule &resolvent):
    clause_idx(transition),
    implicant(sat),
    var_renaming(var_renaming),
    model(model),
    resolvent(resolvent) {}

std::ostream& operator<<(std::ostream &s, const Step &step) {
    return s << step.clause_idx << "[" << step.implicant << "]";
}

ResultViaSideEffects Reachability::remove_irrelevant_clauses(bool forward) {
    std::set<TransIdx> keep;
    std::stack<TransIdx> todo;
    for (const auto x: forward ? chcs.getInitialTransitions() : chcs.getSinkTransitions()) {
        todo.push(x);
    }
    do {
        const TransIdx current = todo.top();
        todo.pop();
        keep.insert(current);
        for (const auto p: forward ? chcs.getSuccessors(current) : chcs.getPredecessors(current)) {
            if (keep.find(p) == keep.end()) {
                todo.push(p);
            }
        }
    } while (!todo.empty());
    std::vector<LocationIdx> to_delete;
    for (const auto idx: chcs.getAllTransitions()) {
        if (keep.find(idx) == keep.end()) {
            to_delete.push_back(idx);
        }
    }
    std::set<TransIdx> deleted;
    for (const auto idx: to_delete) {
        chcs.removeRule(idx);
        deleted.insert(idx);
    }
    ResultViaSideEffects ret;
    if (!deleted.empty()) {
        ret.succeed();
        ret.deletionProof(deleted);
    }
    return ret;
}

ResultViaSideEffects Reachability::simplify() {
    ResultViaSideEffects ret;
    for (const TransIdx idx: chcs.getAllTransitions()) {
        const auto res = Preprocess::preprocessRule(chcs.getRule(idx));
        if (res) {
            ret.succeed();
            chcs.replaceRule(idx, *res);
            ret.concat(res.getProof());
        }
    }
    return ret;
}

ResultViaSideEffects Reachability::unroll() {
    ResultViaSideEffects ret;
    for (const TransIdx idx: chcs.getAllTransitions()) {
        const Rule &r = chcs.getRule(idx);
        if (chcs.isSimpleLoop(idx)) {
            const auto [res, period] = LoopAcceleration::chain(r);
            if (period > 1) {
                const auto simplified = Preprocess::simplifyRule(res);
                ret.succeed();
                ret.ruleTransformationProof(r, "Unrolling", res);
                if (simplified) {
                    ret.concat(simplified.getProof());
                }
                chcs.addRule(*simplified, idx, idx);
            }
        }
    }
    return ret;
}

ResultViaSideEffects Reachability::refine_dependency_graph() {
    ResultViaSideEffects res;
    const auto removed {chcs.refineDependencyGraph()};
    if (!removed.empty()) {
        res.succeed();
        res.dependencyGraphRefinementProof(removed);
    }
    return res;
}

std::optional<unsigned> Reachability::has_looping_suffix(int start) {
    if (trace.empty()) {
        return {};
    }
    const auto last_clause = trace.back().clause_idx;
    std::vector<long> sequence;
    for (int pos = start; pos >= 0; --pos) {
        const Step &step = trace[pos];
        if (chcs.areAdjacent(last_clause, step.clause_idx)) {
            auto upos = static_cast<unsigned>(pos);
            bool looping = upos < trace.size() - 1 || is_orig_clause(step.clause_idx);
            if (looping) {
                return upos;
            }
        }
    }
    return {};
}

Subs Reachability::handle_update(const TransIdx idx) {
    if (chcs.isSinkTransition(idx)) {
        // no need to compute a new variable renaming if we just applied a query
        return {};
    }
    const auto &r {chcs.getRule(idx)};
    const auto &last_var_renaming = trace.empty() ? Subs::Empty : trace.back().var_renaming;
    Subs new_var_renaming {last_var_renaming};
    const Subs up = r.getUpdate();
    for (const auto &x: prog_vars) {
        new_var_renaming.put(x, TheTheory::varToExpr(variable::next(x)));
    }
    for (const auto &var: r.vars()) {
        if (variable::isTempVar(var)) {
            new_var_renaming.put(var, TheTheory::varToExpr(variable::next(var)));
        }
    }
    for (const auto &x: prog_vars) {
        solver.add(literal::mkEq(new_var_renaming.get(x), expression::subs(up.get(x), last_var_renaming)));
    }
    return new_var_renaming;
}

void Reachability::block(const Step &step) {
    if (log) std::cout << "blocking " << step.clause_idx << ", " << step.implicant << std::endl;
    if (chcs.getRule(step.clause_idx).getGuard()->isConjunction()) {
        blocked_clauses.back()[step.clause_idx] = {};
    } else {
        auto block = blocked_clauses.back().find(step.clause_idx);
        if (block == blocked_clauses.back().end()) {
            blocked_clauses.back()[step.clause_idx] = {step.implicant};
        } else {
            block->second.insert(step.implicant);
        }
    }
}

void Reachability::pop() {
    blocked_clauses.pop_back();
    trace.pop_back();
    solver.pop();
}

void Reachability::backtrack() {
    if (log) std::cout << "backtracking" << std::endl;
    // copy the last step before popping it
    const Step step = trace.back();
    pop();
    block(step);
}

void Reachability::add_to_trace(const Step &step) {
    trace.emplace_back(step);
    blocked_clauses.emplace_back();
}

void Reachability::update_cpx() {
    if (!Config::Analysis::complexity()) {
        return;
    }
    const auto &resolvent = trace.back().resolvent;
    const auto &cost = chcs.getCost(resolvent);
    const auto max_cpx = toComplexity(cost);
    if (max_cpx <= cpx && !cost.hasVarWith([](const auto &x){return variable::isTempVar(x);})) {
        return;
    }
    for (const auto &tc: resolvent.getGuard()->transform<IntTheory>()->dnf()) {
        const auto res = AsymptoticBound::determineComplexity(tc, cost, false, cpx);
        if (res.cpx > cpx) {
            cpx = res.cpx;
            std::cout << cpx.toWstString() << std::endl;
            proof.result(cpx.toString());
        }
        if (res.cpx == max_cpx) {
            break;
        }
    }
}

Rule Reachability::compute_resolvent(const TransIdx idx, const BoolExpr &implicant) const {
    static Rule dummy(BExpression::True, Subs());
    if (!Config::Analysis::complexity()) {
        return dummy;
    }
    auto resolvent = chcs.getRule(idx).withGuard(implicant);
    if (!trace.empty()) {
        resolvent = Chaining::chain(trace.back().resolvent, resolvent);
    }
    return *Preprocess::simplifyRule(resolvent);
}

bool Reachability::store_step(const TransIdx idx, const Rule &resolvent) {
    solver.push();
    const auto r {trace.empty() ? resolvent : resolvent.subs(trace.back().var_renaming)};
    const auto implicant {r.getGuard()};
    solver.add(implicant);
    if (solver.check() == Sat) {
        auto vars = trace.empty() ? prog_vars : substitution::coDomainVars(trace.back().var_renaming.project(prog_vars));
        vars.insertAll(r.vars());
        const auto model {solver.model(vars).toSubs()};
        const auto new_var_renaming {handle_update(idx)};
        const Step step(idx, implicant, new_var_renaming, model, compute_resolvent(idx, implicant));
        add_to_trace(step);
        // block learned clauses after adding them to the trace
        if (is_learned_clause(idx)) {
            block(step);
        }
        return true;
    } else {
        solver.pop();
        return false;
    }
}

void Reachability::print_trace(std::ostream &s) {
    const auto model {solver.model().toSubs()};
    s << "(";
    bool first {true};
    for (const auto &x: prog_vars) {
        const auto &y {model.get(x)};
        if (TheTheory::varToExpr(x) == y) continue;
        if (first) {
            first = false;
        } else {
            s << ", ";
        }
        s << x << "=" << y;
    }
    s << ")" << std::endl;
    for (const auto &step: trace) {
        s << "-" << step.clause_idx << "-> " << "(";
        first = true;
        if (!chcs.isSinkTransition(step.clause_idx)) {
            for (const auto &x: prog_vars) {
                const auto y {expression::subs(step.var_renaming.get(x), model)};
                if (TheTheory::varToExpr(x) == y) continue;
                if (first) {
                    first = false;
                } else {
                    s << ", ";
                }
                s << x << "=" << y;
            }
        }
        s << " )" << std::endl;
    }
    s << std::endl;
}

void Reachability::print_state() {
    if (Proof::disabled()) {
        return;
    }
    Proof state_p;
    state_p.section("Trace");
    std::stringstream s;
    for (const auto &x: trace) {
        s << x << std::endl;
    }
    state_p.append(s);
    s.clear();
    state_p.section("Blocked");
    for (const auto &e: blocked_clauses) {
        s << "{";
        bool first_trans = true;
        for (const auto &[idx,blocked]: e) {
            if (first_trans) {
                first_trans = false;
            } else {
                s << ", ";
            }
            s << idx << "[" << BExpression::buildAnd(blocked) << "]";
        }
        s << "}" << std::endl;
    }
    state_p.append(s);
    proof.storeSubProof(state_p);
}

void Reachability::preprocess() {
    ResultViaSideEffects res;
    if (Config::Analysis::reachability()) {
        res = remove_irrelevant_clauses(true);
        if (res) {
            proof.majorProofStep("Removed Irrelevant Transitions (forward)", res.getProof(), chcs);
        }
        res = remove_irrelevant_clauses(false);
        if (res) {
            proof.majorProofStep("Removed Irrelevant Transitions (backward)", res.getProof(), chcs);
        }
    }
    res = Chaining::chainLinearPaths(chcs);
    if (res) {
        proof.majorProofStep("Chained Linear Paths", res.getProof(), chcs);
    }
    res = simplify();
    if (res) {
        proof.majorProofStep("Simplified Transitions", res.getProof(), chcs);
    }
    res = unroll();
    if (res) {
        proof.majorProofStep("Unrolled Loops", res.getProof(), chcs);
    }
    if (chcs.size() <= 1000) {
        res = refine_dependency_graph();
        if (res) {
            proof.majorProofStep("Refined Dependency Graph", res.getProof(), chcs);
        }
    }
    if (log) {
        std::cout << "Simplified ITS" << std::endl;
        ITSExport::printForProof(chcs, std::cout);
    }
}

void Reachability::init() {
    srand(42);
    for (const auto &x: chcs.getVars()) {
        if (!variable::isTempVar(x)) {
            prog_vars.insert(x);
        }
    }
    for (const TransIdx idx: chcs.getAllTransitions()) {
        last_orig_clause = std::max(last_orig_clause, idx);
    }
}

void Reachability::luby_next() {
    const auto [u,v] {luby};
    luby = (u & -u) == v ? std::pair<unsigned, unsigned>(u+1, 1) : std::pair<unsigned, unsigned>(u, 2 * v);
    solver.setSeed(rand());
    solver.resetSolver();
    luby_loop_count = 0;
}

void Reachability::unsat() {
    const auto res = Config::Analysis::reachability() ? "unsat" : "NO";
    std::cout << res << std::endl << std::endl;
    if (!log && Proof::disabled()) {
        return;
    }
    std::stringstream counterexample;
    print_trace(counterexample);
    if (log) {
        std::stringstream trace_stream;
        trace_stream << trace;
        std::cout << "final ITS:" << std::endl;
        ITSExport::printForProof(chcs, std::cout);
        std::cout << std::endl << "final trace:" << trace_stream.str() << std::endl << std::endl;
        std::cout << "counterexample: " << counterexample.str();
    }
    proof.headline("Refute");
    Proof subProof;
    subProof.section("Counterexample");
    subProof.append(counterexample);
    proof.storeSubProof(subProof);
    proof.result(res);
    proof.print();
}

std::optional<Rule> Reachability::resolve(const TransIdx idx) {
    const auto &var_renaming = trace.empty() ? Subs::Empty : trace.back().var_renaming;
    const auto clause = chcs.getRule(idx);
    const auto block = blocked_clauses.back().find(idx);
    if (block != blocked_clauses.back().end()) {
        // empty means all variants are blocked --> fail
        if (block->second.empty()) {
            return {};
        }
        // a non-conjunctive clause where some variants are blocked
        // --> make sure that we use a non-blocked variant, if any
        for (const auto &b: block->second) {
            solver.add(!b->subs(var_renaming));
        }
    }
    const auto vars {clause.getGuard()->vars()};
    const auto projected_var_renaming {var_renaming.project(vars)};
    const auto guard {clause.getGuard()->subs(projected_var_renaming)};
    solver.add(guard);
    if (solver.check() == Sat) {
        if (log) std::cout << "found model for " << idx << std::endl;
        const auto model {solver.model(guard->vars()).toSubs()};
        const auto implicant {clause.getGuard()->implicant(substitution::compose(projected_var_renaming, model))};
        if (implicant) {
            return {clause.withGuard(BExpression::buildAndFromLits(*implicant))};
        } else {
            throw std::logic_error("model, but no implicant");
        }
    } else {
        if (block == blocked_clauses.back().end()) {
            blocked_clauses.back()[idx] = {};
        } else {
            block->second.clear();
        }
        if (log) std::cout << "could not find a model for " << idx << std::endl;
        return {};
    }
}

Automaton Reachability::get_language(const Step &step) {
    if (is_orig_clause(step.clause_idx)) {
        return redundancy->get_singleton_language({step.clause_idx}, step.implicant->conjunctionToGuard());
    } else {
        return redundancy->get_language(step.clause_idx);
    }
}

Automaton Reachability::build_language(const int backlink) {
    auto lang = get_language(trace[backlink]);
    for (size_t i = backlink + 1; i < trace.size(); ++i) {
        redundancy->concat(lang, get_language(trace[i]));
    }
    return lang;
}

std::pair<Rule, Subs> Reachability::build_loop(const int backlink) {
    const auto &step {trace.back()};
    auto rule {chcs.getRule(step.clause_idx).withGuard(step.implicant)};
    auto loop {rule};
    auto model {step.model};
    for (int i = trace.size() - 2; i >= backlink; --i) {
        const auto &step {trace[i]};
        rule = chcs.getRule(step.clause_idx).withGuard(step.implicant);
        loop = Chaining::chain(rule, loop);
        model = substitution::compose(step.model, model);
    }
    if (backlink > 0) {
        model = substitution::compose(trace.at(backlink-1).var_renaming.project(prog_vars), model);
    }
    if (log) {
        std::cout << "found loop of length " << (trace.size() - backlink) << ":" << std::endl;
        ITSExport::printRule(loop, std::cout);
        std::cout << std::endl;
    }
    return {loop, model};
}

TransIdx Reachability::add_learned_clause(const Rule &accel, const unsigned backlink) {
    const auto fst = trace.at(backlink).clause_idx;
    const auto last = trace.back().clause_idx;
    const auto loop_idx = chcs.addLearnedRule(accel, fst, last);
    return loop_idx;
}

bool Reachability::is_learned_clause(const TransIdx idx) const {
    return idx > last_orig_clause;
}

bool Reachability::is_orig_clause(const TransIdx idx) const {
    return idx <= last_orig_clause;
}

Result<Rule> Reachability::instantiate(const NumVar &n, const Rule &rule) const {
    Result<Rule> res(rule);
    VarEliminator ve(rule.getGuard(), n, variable::isProgVar);
    if (ve.getRes().empty() || ve.getRes().size() > 1) {
        return res;
    }
    for (const auto &s : ve.getRes()) {
        if (s.get(n).isRationalConstant()) continue;
        if (res) {
            return Result<Rule>(rule);
        }
        res = rule.subs(Subs::build<IntTheory>(s));
        res.ruleTransformationProof(rule, "Instantiation", *res);
    }
    return res;
}

std::unique_ptr<LearningState> Reachability::learn_clause(const Rule &rule, const Subs &sample_point, const unsigned backlink) {
    Result<Rule> simp = Preprocess::simplifyRule(rule);
    if (Config::Analysis::reachability() && simp->getUpdate() == substitution::concat(simp->getUpdate(), simp->getUpdate())) {
        // The learned clause would be trivially redundant w.r.t. the looping suffix (but not necessarily w.r.t. a single clause).
        // Such clauses are pretty useless, so we do not store them.
        if (log) std::cout << "acceleration would yield equivalent rule" << std::endl;
        return std::make_unique<Unroll>(1);
    }
    if (log && simp) {
        std::cout << "simplified loop:" << std::endl;
        ITSExport::printRule(*simp, std::cout);
        std::cout << std::endl;
    }
    if (Config::Analysis::reachability()) {
        if (simp->getUpdate().empty()) {
            if (log) std::cout << "trivial looping suffix" << std::endl;
            return std::make_unique<Covered>();
        }
    }
    AccelConfig config {.allowDisjunctions = false, .tryNonterm = Config::Analysis::tryNonterm()};
    const auto accel_res {LoopAcceleration::accelerate(*simp, sample_point, config)};
    if (accel_res.status == acceleration::PseudoLoop) {
        return std::make_unique<Unroll>();
    }
    Result<LearnedClauses> res{{.res = {}, .prefix = accel_res.prefix, .period = accel_res.period}};
    const auto fst = trace.at(backlink).clause_idx;
    if (accel_res.nonterm) {
        res.succeed();
        const auto idx = chcs.addQuery(accel_res.nonterm->certificate, fst);
        res->res.emplace_back(idx, accel_res.nonterm->covered);
        ITSProof nonterm_proof;
        nonterm_proof.ruleTransformationProof(*simp, "Certificate of Non-Termination", chcs.getRule(idx));
        nonterm_proof.storeSubProof(accel_res.nonterm->proof);
        res.concat(nonterm_proof);
        if (log) {
            std::cout << "found certificate of non-termination:" << std::endl;
            std::cout << accel_res.nonterm->certificate << std::endl;
        }
    }
    if (accel_res.accel) {
        // acceleration succeeded, simplify the result
        auto simplified = Preprocess::simplifyRule(accel_res.accel->rule);
        if (simplified->getUpdate() != simp->getUpdate()) {
            // accelerated rule differs from the original one, update the result
            if (Config::Analysis::complexity()) {
                simplified.concat(instantiate(*accel_res.n, *simplified));
            }
            res.succeed();
            const auto loop_idx {add_learned_clause(*simplified, backlink)};
            res->res.emplace_back(loop_idx, accel_res.accel->covered);
            ITSProof acceleration_proof;
            acceleration_proof.ruleTransformationProof(*simp, "Loop Acceleration", accel_res.accel->rule);
            acceleration_proof.storeSubProof(accel_res.accel->proof);
            res.concat(acceleration_proof);
            res.concat(simplified.getProof());
            if (log) {
                std::cout << "accelerated rule:" << std::endl;
                ITSExport::printRule(*simplified, std::cout);
                std::cout << std::endl;
            }
        }
    }
    if (!res) {
        if (log) std::cout << "acceleration failed" << std::endl;
        return std::make_unique<Unroll>(1);
    }
    return std::make_unique<Succeeded>(res);
}

std::unique_ptr<LearningState> Reachability::handle_loop(const unsigned backlink) {
    const auto lang {build_language(backlink)};
    auto closure {lang};
    redundancy->transitive_closure(closure);
    if (redundancy->is_redundant(closure)) {
        if (log) std::cout << "loop already covered" << std::endl;
        return std::make_unique<Covered>();
    } else if (log) {
        std::cout << "learning clause for the following language:" << std::endl;
        std::cout << closure << std::endl;
    }
    const auto [loop, sample_point] {build_loop(backlink)};
    std::vector<unsigned> indices;
    for (unsigned i = backlink; i < trace.size(); ++i) {
        const auto &step {trace.at(i)};
        const auto c {*redundancy->get_singleton_language({step.clause_idx}, step.implicant->conjunctionToGuard()).get_alphabet().Begin()};
        indices.push_back(c);
    }
    const auto &old_redundance_condition {conditionally_redundant.emplace(indices, BExpression::False).first->second};
    if (old_redundance_condition->subs(sample_point)->isTriviallyTrue()) {
        if (log) std::cout << "loop partially covered" << std::endl;
        return std::make_unique<Covered>();
    }
    if (redundancy->is_accelerated(lang)) {
        if (log) std::cout << "loop must be unrolled" << std::endl;
        return std::make_unique<Unroll>();
    }
    auto &acceleration_condition {conditionally_accelerated.emplace(indices, BExpression::False).first->second};
    if (acceleration_condition->subs(sample_point)->isTriviallyTrue()) {
        if (log) std::cout << "loop must be unrolled (conditional)" << std::endl;
        return std::make_unique<Unroll>();
    }
    luby_loop_count++;
    auto state {learn_clause(loop, sample_point, backlink)};
    if (!state->succeeded()) {
        if (state->unroll()) {
            redundancy->mark_as_accelerated(lang);
            if (state->unroll()->get_max()) {
                const auto max {*state->unroll()->get_max()};
                auto redundant {lang};
                for (unsigned i = 0; i < max; ++i) {
                    redundancy->concat(redundant, lang);
                }
                redundancy->concat(redundant, closure);
                redundancy->mark_as_redundant(redundant);
            }
        } else {
            redundancy->mark_as_redundant(closure);
        }
        return state;
    }
    const auto accel_state {*state->succeeded()};
    const auto learned_clauses {**accel_state};
    std::vector<unsigned> new_indices;
    if (log) {
        std::cout << "prefix: " << learned_clauses.prefix << std::endl;
        std::cout << "period: " << learned_clauses.period << std::endl;
    }
    for (unsigned i = 0; i < learned_clauses.prefix + learned_clauses.period; ++i) {
        new_indices.insert(new_indices.end(), indices.begin(), indices.end());
    }
    auto &redundance_condition {conditionally_redundant.emplace(new_indices, BExpression::False).first->second};
    BoolExprSet new_redundance_condition {redundance_condition};
    BoolExprSet new_acceleration_condition {acceleration_condition};
    for (const auto &[idx, covered]: learned_clauses.res) {
        new_redundance_condition.insert(covered);
        new_acceleration_condition.insert(covered);
    }
    redundance_condition = BExpression::buildOr(new_redundance_condition);
    acceleration_condition = BExpression::buildOr(new_acceleration_condition);
    if (redundance_condition->isTriviallyTrue() || SmtFactory::check(!redundance_condition) == SmtResult::Unsat) {
        auto new_lang {closure};
        for (unsigned i = 1; i < learned_clauses.period; ++i) {
            redundancy->concat(closure, closure);
        }
        if (learned_clauses.prefix > 0) {
            auto prefix_lang {lang};
            for (unsigned i = 1; i < learned_clauses.prefix; ++i) {
                redundancy->concat(prefix_lang, prefix_lang);
            }
            redundancy->concat(prefix_lang, new_lang);
            new_lang = prefix_lang;
        }
        for (const auto &[idx, _]: learned_clauses.res) {
            if (!chcs.isSinkTransition(idx)) {
                redundancy->set_language(idx, new_lang);
            }
        }
        redundancy->mark_as_redundant(new_lang);
    } else {
        for (const auto &[idx, covered]: learned_clauses.res) {
            if (!chcs.isSinkTransition(idx)) {
                auto closure {redundancy->get_singleton_language(new_indices, covered->conjunctionToGuard())};
                redundancy->transitive_closure(closure);
                redundancy->mark_as_redundant(closure);
                redundancy->set_language(idx, closure);
            }
        }
    }
    if (acceleration_condition->isTriviallyTrue() || SmtFactory::check(!acceleration_condition) == SmtResult::Unsat) {
        redundancy->mark_as_accelerated(lang);
    }
    return state;
}

bool Reachability::try_to_finish() {
    solver.setTimeout(2000);
    std::set<TransIdx> clauses = trace.empty() ? chcs.getInitialTransitions() : chcs.getSuccessors(trace.back().clause_idx);
    for (const auto &q: clauses) {
        if (chcs.isSinkTransition(q)) {
            PushPop pp(solver);
            const auto implicant {resolve(q)};
            if (implicant) {
                // no need to compute the model and the variable renaming for the next step, as we are done
                add_to_trace(Step(q, implicant->getGuard(), Subs(), Subs(), compute_resolvent(q, implicant->getGuard())));
                proof.headline("Step with " + std::to_string(q));
                print_state();
                unsat();
                return true;
            }
        }
    }
    solver.setTimeout(smt_timeout);
    return false;
}

void Reachability::analyze() {
    static std::default_random_engine rnd {};
    proof.majorProofStep("Initial ITS", ITSProof(), chcs);
    if (log) {
        std::cout << "Initial ITS" << std::endl;
        ITSExport::printForProof(chcs, std::cout);
    }
    preprocess();
    init();
    if (try_to_finish()) {
        return;
    }
    blocked_clauses[0].clear();
    do {
        size_t next_restart = luby_unit * luby.second;
        if (log) std::cout << "trace: " << trace << std::endl;
        if (!trace.empty()) {
            for (auto backlink = has_looping_suffix(trace.size() - 1);
                 backlink && luby_loop_count < next_restart;
                 backlink = has_looping_suffix(*backlink - 1)) {
                Step step = trace[*backlink];
                bool simple_loop = *backlink == trace.size() - 1;
                auto state = handle_loop(*backlink);
                if (state->covered()) {
                    backtrack();
                    proof.headline("Covered");
                    print_state();
                } else if (state->succeeded()) {
                    if (simple_loop) {
                        block(step);
                    }
                    proof.majorProofStep("Accelerate", (*state->succeeded())->getProof(), chcs);
                    print_state();
                    update_cpx();
                    // try to apply a query before doing another step
                    if (try_to_finish()) {
                        return;
                    }
                }
            }
        }
        if (luby_loop_count == next_restart) {
            if (log) std::cout << "restarting after " << luby_loop_count << " loops" << std::endl;
            // restart
            while (!trace.empty()) {
                pop();
            }
            luby_next();
            proof.headline("Restart");
        }
        const auto try_set = trace.empty() ? chcs.getInitialTransitions() : chcs.getSuccessors(trace.back().clause_idx);
        std::vector<TransIdx> to_try(try_set.begin(), try_set.end());
        std::shuffle(to_try.begin(), to_try.end(), rnd);
        bool all_failed {true};
        for (const auto idx: to_try) {
            PushPop pp(solver);
            const auto implicant {resolve(idx)};
            if (implicant && store_step(idx, *implicant)) {
                proof.headline("Step with " + std::to_string(idx));
                print_state();
                update_cpx();
                all_failed = false;
                break;
            }
        }
        if (trace.empty()) {
            break;
        } else if (all_failed) {
            backtrack();
            proof.headline("Backtrack");
            print_state();
        } else if (try_to_finish()) { // check whether a query is applicable after every step and, importantly, before acceleration (which might approximate)
            return;
        }
    } while (true);
    proof.headline("Accept");
    if (Config::Analysis::complexity()) {
        proof.result(cpx.toString());
    } else {
        proof.result("unknown");
        std::cout << "unknown" << std::endl;
    }
    std::cout << std::endl;
    proof.print();
}

void Reachability::analyze(ITSProblem &its) {
    yices::init();
    Reachability(its).analyze();
    yices::exit();
}

}
