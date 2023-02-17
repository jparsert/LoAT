#include "reachability.hpp"
#include "chainstrategy.hpp"
#include "chain.hpp"
#include "preprocess.hpp"
#include "loopacceleration.hpp"
#include "result.hpp"
#include "yices.hpp"
#include "smt.hpp"
#include "export.hpp"
#include "expression.hpp"
#include "literal.hpp"
#include "vector.hpp"
#include "asymptoticbound.hpp"
#include "vareliminator.hpp"

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

std::optional<Dropped> LearningState::dropped() {
    return {};
}

std::optional<Failed> LearningState::failed() {
    return {};
}

std::optional<ProvedUnsat> LearningState::unsat() {
    return {};
}

Succeeded::Succeeded(const Result<std::vector<TransIdx>> &idx): idx(idx) {}

std::optional<Succeeded> Succeeded::succeeded() {
    return *this;
}

Result<std::vector<TransIdx>>& Succeeded::operator*() {
    return idx;
}

Result<std::vector<TransIdx>>* Succeeded::operator->() {
    return &idx;
}

std::optional<Covered> Covered::covered() {
    return *this;
}

Dropped::Dropped(const Result<std::vector<TransIdx>> &idx): idx(idx) {}

std::optional<Dropped> Dropped::dropped() {
    return *this;
}

Result<std::vector<TransIdx>>& Dropped::operator*() {
    return idx;
}

Result<std::vector<TransIdx>>* Dropped::operator->() {
    return &idx;
}

std::optional<Failed> Failed::failed() {
    return *this;
}

std::optional<ProvedUnsat> ProvedUnsat::unsat() {
    return *this;
}

ProvedUnsat::ProvedUnsat(const Proof &proof): proof(proof) {}

Proof& ProvedUnsat::operator*() {
    return proof;
}

Proof* ProvedUnsat::operator->() {
    return &proof;
}

PushPop::PushPop(LinearizingSolver<IntTheory, BoolTheory> &solver): solver(solver) {
    solver.push();
}

PushPop::~PushPop() {
    solver.pop();
}

Reachability::Reachability(ITSProblem &chcs): chcs(chcs), solver(chcs, smt_timeout), non_loops(chcs) {
    solver.enableModels();
}

Step::Step(const TransIdx transition, const BoolExpr &sat, const Subs &var_renaming, const Rule &resolvent):
    clause_idx(transition),
    implicant(sat),
    var_renaming(var_renaming),
    resolvent(resolvent) {}

std::ostream& operator<<(std::ostream &s, const std::vector<Step> &step) {
    for (auto it = step.begin(); it != step.end(); ++it) {
        if (it != step.begin()) {
            s << ", ";
        }
        s << it->clause_idx << "[" << it->implicant << "]";
    }
    return s;
}

NonLoops::NonLoops(const ITSProblem &chcs): chcs(chcs) {}

std::vector<long> NonLoops::build(const std::vector<Step> &trace, int backlink) {
   std::vector<long> sequence;
   for (int i = trace.size() - 1; i >= backlink; --i) {
       append(sequence, trace[i]);
   }
   return sequence;
}

void NonLoops::add(const std::vector<Step> &trace, int backlink) {
    non_loops.insert(build(trace, backlink));
}

bool NonLoops::contains(const std::vector<long> &sequence) {
    return non_loops.contains(sequence);
}

void NonLoops::append(std::vector<long> &sequence, const Step &step) {
    std::pair<TransIdx, BoolExpr> key;
    if (chcs.getRule(step.clause_idx).getGuard()->isConjunction()) {
        key = {step.clause_idx, BExpression::True};
    } else {
        key = {step.clause_idx, step.implicant};
    }
    const auto it = alphabet.find(key);
    if (it == alphabet.end()) {
        alphabet.emplace(key, next_char);
        sequence.push_back(next_char);
        ++next_char;
    } else {
        sequence.push_back(it->second);
    }
}

ResultViaSideEffects Reachability::remove_irrelevant_clauses() {
    std::set<LocationIdx> keep;
    std::stack<LocationIdx> todo;
    todo.push(chcs.getSink());
    do {
        const LocationIdx current = todo.top();
        todo.pop();
        keep.insert(current);
        for (const auto p: chcs.getPredecessorLocations(current)) {
            if (keep.find(p) == keep.end()) {
                todo.push(p);
            }
        }
    } while (!todo.empty());
    std::vector<LocationIdx> to_delete;
    for (const auto idx: chcs.getAllTransitions()) {
        const LocationIdx target = chcs.getRule(idx).getRhsLoc();
        if (keep.find(target) == keep.end()) {
            to_delete.push_back(target);
        }
    }
    std::set<TransIdx> deleted;
    for (const auto idx: to_delete) {
        const std::set<TransIdx> d = chcs.removeLocationAndRules(idx);
        deleted.insert(d.begin(), d.end());
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
        const auto res = Preprocess::preprocessRule(chcs, chcs.getRule(idx));
        if (res) {
            ret.succeed();
            chcs.replaceRules({idx}, {res.get()});
            ret.concat(res.getProof());
        }
    }
    return ret;
}

ResultViaSideEffects Reachability::unroll() {
    ResultViaSideEffects ret;
    for (const TransIdx idx: chcs.getAllTransitions()) {
        const Rule &r = chcs.getRule(idx);
        if (r.isSimpleLoop()) {
            const auto [res, period] = LoopAcceleration::chain(r, chcs);
            if (period > 1) {
                const auto simplified = Preprocess::simplifyRule(chcs, res);
                ret.succeed();
                ret.ruleTransformationProof(r, "Unrolling", res, chcs);
                if (simplified) {
                    chcs.addRule(*simplified);
                    ret.concat(simplified.getProof());
                } else {
                    chcs.addRule(res);
                }
            }
        }
    }
    return ret;
}

bool Reachability::leaves_scc(const TransIdx idx) const {
    const Rule &r = chcs.getRule(idx);
    return sccs.getSccIndex(r.getLhsLoc()) != sccs.getSccIndex(r.getRhsLoc());
}

std::optional<unsigned> Reachability::has_looping_suffix() {
    if (trace.empty()) {
        return {};
    }
    const auto last_clause = chcs.getRule(trace.back().clause_idx);
    const auto dst = last_clause.getRhsLoc();
    std::vector<long> sequence;
    for (int pos = trace.size() - 1; pos >= 0; --pos) {
        const Step &step = trace[pos];
        if (leaves_scc(step.clause_idx)) {
            return {};
        }
        non_loops.append(sequence, step);
        if (non_loops.contains(sequence)) {
            if (log) std::cout << sequence << " is a non-loop" << std::endl;
            continue;
        }
        if (chcs.getRule(step.clause_idx).getLhsLoc() == dst) {
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
    const Rule &r = chcs.getRule(idx);
    if (r.getRhsLoc() == chcs.getSink()) {
        // no need to compute a new variable renaming if we just applied a query
        return {};
    }
    const Subs last_var_renaming = trace.empty() ? Subs() : trace.back().var_renaming;
    Subs new_var_renaming;
    const Subs up = r.getUpdate();
    for (const auto &x: prog_vars) {
        new_var_renaming.put(x, TheTheory::varToExpr(chcs.addFreshTemporaryVariable(x)));
    }
    for (const auto &var: r.vars()) {
        if (chcs.isTempVar(var)) {
            new_var_renaming.put(var, TheTheory::varToExpr(chcs.addFreshTemporaryVariable(var)));
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
}

void Reachability::update_cpx() {
    if (!Config::Analysis::complexity()) {
        return;
    }
    const auto &resolvent = trace.back().resolvent;
    const auto &cost = resolvent.getCost();
    const auto max_cpx = toComplexity(cost);
    if (max_cpx <= cpx && !cost.hasVarWith([this](const auto &x){return chcs.isTempVar(x);})) {
        return;
    }
    for (const auto &tc: resolvent.getGuard()->transform<IntTheory>()->dnf()) {
        const auto res = AsymptoticBound::determineComplexity(chcs, tc, cost, false, cpx);
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
    static Rule dummy(0, BExpression::True, 0, 0, Subs());
    if (!Config::Analysis::complexity()) {
        return dummy;
    }
    auto resolvent = chcs.getRule(idx).withGuard(implicant);
    if (!trace.empty()) {
        resolvent = *Chaining::chainRules(chcs, trace.back().resolvent, resolvent, false);
    }
    return *Preprocess::simplifyRule(chcs, resolvent);
}

bool Reachability::store_step(const TransIdx idx, const BoolExpr &implicant) {
    solver.push();
    if (trace.empty()) {
        solver.add(implicant);
    } else {
        solver.add(implicant->subs(trace.back().var_renaming));
    }
    if (solver.check() == Sat) {
        const auto new_var_renaming = handle_update(idx);
        const Step step(idx, implicant, new_var_renaming, compute_resolvent(idx, implicant));
        add_to_trace(step);
        blocked_clauses.push_back({});
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
    const auto model = solver.model().toSubs();
    bool first = true;
    for (const auto &step: trace) {
        if (first) {
            first = false;
        } else {
            s << " ";
        }
        s << "[";
        for (const auto &x: prog_vars) {
            s << " " << x << "=" << expression::subs(step.var_renaming.get(x), model);
        }
        s << " ] " << step.clause_idx;
    }
    s << std::endl;
}

void Reachability::print_state() {
    if (Proof::disabled()) {
        return;
    }
    Proof state_p;
    std::stringstream s;
    state_p.section("Trace");
    s << trace;
    state_p.append(s);
    s.clear();
    state_p.section("Blocked");
    bool first_set = true;
    s << "[";
    for (const auto &e: blocked_clauses) {
        if (first_set) {
            first_set = false;
        } else {
            s << ", ";
        }
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
        s << "}";
    }
    s << "]";
    state_p.append(s);
    proof.storeSubProof(state_p);
}

void Reachability::preprocess() {
    ResultViaSideEffects res;
    if (Config::Analysis::reachability()) {
        res = remove_irrelevant_clauses();
        if (res) {
            proof.majorProofStep("Removed Irrelevant Transitions", chcs);
            proof.storeSubProof(res.getProof());
        }
    }
    res = Chaining::chainLinearPaths(chcs);
    if (res) {
        proof.majorProofStep("Chained Linear Paths", chcs);
        proof.storeSubProof(res.getProof());
    }
    res = simplify();
    if (res) {
        proof.majorProofStep("Simplified Transitions", chcs);
        proof.storeSubProof(res.getProof());
    }
    res = unroll();
    if (res) {
        proof.majorProofStep("Unrolled Loops", chcs);
        proof.storeSubProof(res.getProof());
    }
    if (log) {
        std::cout << "Simplified ITS" << std::endl;
        ITSExport::printForProof(chcs, std::cout);
    }
}

void Reachability::update_rules(const LocationIdx idx) {
    // the order of the clauses in this list determines the selection order for resolution
    std::list<LocationIdx> trans;
    if (!Config::Analysis::complexity()) {
        auto l_it = cross_scc.find(idx);
        // Try to get out of the current SCC as we want to reach 'bottom', which is its own SCC.
        if (l_it != cross_scc.end()) {
            trans.insert(trans.end(), l_it->second.begin(), l_it->second.end());
        }
    }
    // Then try original clauses that stay within the current SCC.
    // The motivation for trying them before learned clauses is that it increases the probability
    // that we visit different parts of the search space after restarts.
    auto l_it = in_scc.find(idx);
    if (l_it != in_scc.end()) {
        trans.insert(trans.end(), l_it->second.begin(), l_it->second.end());
    }
    // Try learned clauses last.
    const auto it = learned_clauses.find(idx);
    if (it != learned_clauses.end()) {
        trans.insert(trans.end(), it->second.begin(), it->second.end());
    }
    if (Config::Analysis::complexity()) {
        auto l_it = cross_scc.find(idx);
        if (l_it != cross_scc.end()) {
            trans.insert(trans.end(), l_it->second.begin(), l_it->second.end());
        }
    }
    rules[idx] = trans;
}

void Reachability::init() {
    srand(42);
    for (const auto &x: chcs.getVars()) {
        if (!chcs.isTempVar(x)) {
            prog_vars.insert(x);
        }
    }
    for (const TransIdx idx: chcs.getAllTransitions()) {
        last_orig_clause = std::max(last_orig_clause, idx);
        const auto rule = chcs.getRule(idx);
        const auto src = rule.getLhsLoc();
        const auto dst = rule.getRhsLoc();
        if (src == chcs.getInitialLocation() && dst == chcs.getSink()) {
            conditional_empty_clauses.push_back(idx);
        } else {
            std::map<LocationIdx, std::vector<TransIdx>> *to_insert;
            if (dst == chcs.getSink()) {
                to_insert = &queries;
            } else if (leaves_scc(idx)) {
                to_insert = &cross_scc;
            } else {
                to_insert = &in_scc;
            }
            const auto it = to_insert->find(src);
            if (it == to_insert->end()) {
                to_insert->emplace(src, std::vector<TransIdx>{idx});
            } else {
                it->second.push_back(idx);
            }
        }
    }
    for (const auto loc: chcs.getLocations()) {
        update_rules(loc);
    }
}

void Reachability::luby_next() {
    static std::default_random_engine rnd {};
    const auto [u,v] = luby;
    luby = (u & -u) == v ? std::pair<unsigned, unsigned>(u+1, 1) : std::pair<unsigned, unsigned>(u, 2 * v);
    const auto shuffle = [](auto &v) {
        for (auto &p: v) {
            std::shuffle(std::begin(p.second), std::end(p.second), rnd);
        }
    };
    shuffle(in_scc);
    shuffle(cross_scc);
    shuffle(learned_clauses);
    for (const auto idx: chcs.getLocations()) {
        update_rules(idx);
    }
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

std::optional<BoolExpr> Reachability::resolve(const TransIdx idx) {
    PushPop pp(solver);
    const auto var_renaming = trace.empty() ? Subs() : trace.back().var_renaming;
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
    const auto guard = clause.getGuard()->subs(var_renaming);
    solver.add(guard);
    if (solver.check() == Sat) {
        if (log) std::cout << "found model for " << idx << std::endl;
        const auto implicant = clause.getGuard()->implicant(substitution::compose(var_renaming, solver.model(guard->vars()).toSubs()));
        if (implicant) {
            return BExpression::buildAndFromLits(*implicant);
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

void Reachability::drop_until(const int new_size) {
    while (trace.size() > static_cast<unsigned>(new_size)) {
        pop();
    }
}

Reachability::Red::T Reachability::get_language(const Step &step) {
    if (is_orig_clause(step.clause_idx)) {
        return redundance->get_singleton_language(step.clause_idx, step.implicant->conjunctionToGuard());
    } else {
        return redundance->get_language(step.clause_idx);
    }
}

Reachability::Red::T Reachability::build_language(const int backlink) {
    auto lang = get_language(trace[backlink]);
    for (size_t i = backlink + 1; i < trace.size(); ++i) {
        redundance->concat(lang, get_language(trace[i]));
    }
    redundance->transitive_closure(lang);
    return lang;
}

Rule Reachability::build_loop(const int backlink) {
    Rule loop = chcs.getRule(trace.back().clause_idx).withGuard(trace.back().implicant);
    for (int i = trace.size() - 2; i >= backlink; --i) {
        const Step &step = trace[i];
        auto sigma = step.var_renaming;
        sigma.erase(prog_vars); // rename temporary variables before chaining
        loop = *Chaining::chainRules(chcs, chcs.getRule(step.clause_idx).withGuard(step.implicant).subs(sigma), loop, false);

    }
    assert(loop.getLhsLoc() == loop.getRhsLoc());
    if (log) {
        std::cout << "found loop of length " << (trace.size() - backlink) << ":" << std::endl;
        ITSExport::printRule(loop, chcs, std::cout);
        std::cout << std::endl;
    }
    return loop;
}

TransIdx Reachability::add_learned_clause(const Rule &accel, const Red::T &lang) {
    const auto loop_idx = chcs.addRule(accel);
    redundance->set_language(loop_idx, lang);
    const auto src = accel.getLhsLoc();
    auto acc_it = learned_clauses.find(src);
    if (acc_it == learned_clauses.end()) {
        learned_clauses.emplace(src, std::vector<TransIdx>{loop_idx});
    } else {
        acc_it->second.push_back(loop_idx);
    }
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
    VarEliminator ve(rule.getGuard(), n, chcs);
    if (ve.getRes().empty() || ve.getRes().size() > 1) {
        return res;
    }
    for (const auto &s : ve.getRes()) {
        if (s.get(n).isRationalConstant()) continue;
        if (res) {
            return Result<Rule>(rule);
        }
        res = rule.subs(Subs::build<IntTheory>(s));
        res.ruleTransformationProof(rule, "Instantiation", *res, chcs);
    }
    return res;
}

std::unique_ptr<LearningState> Reachability::learn_clause(const Rule &rule, const Red::T &lang) {
    Result<Rule> simp = Preprocess::simplifyRule(chcs, rule);
    if (Config::Analysis::reachability() && simp->getUpdate() == substitution::concat(simp->getUpdate(), simp->getUpdate())) {
        // The learned clause would be trivially redundant w.r.t. the looping suffix (but not necessarily w.r.t. a single clause).
        // Such clauses are pretty useless, so we do not store them. Return 'Failed', so that it becomes a non-loop.
        if (log) std::cout << "acceleration would yield equivalent rule -> dropping it" << std::endl;
        return std::make_unique<Failed>();
    }
    if (log && simp) {
        std::cout << "simplified loop:" << std::endl;
        ITSExport::printRule(*simp, chcs, std::cout);
        std::cout << std::endl;
    }
    AccelConfig config;
    config.allowDisjunctions = false;
    acceleration::Result accel_res = LoopAcceleration::accelerate(chcs, *simp, config);
    Result<std::vector<TransIdx>> res;
    const auto src = rule.getLhsLoc();
    if (accel_res.successful()) {
        res.concat(accel_res.preprocessingProof);
    }
    if (accel_res.nontermRule) {
        res.succeed();
        const auto idx = chcs.addRule(*accel_res.nontermRule);
        auto it = queries.find(src);
        if (it == queries.end()) {
            queries.emplace(src, std::vector<TransIdx>{idx});
        } else {
            it->second.push_back(idx);
        }
        res->push_back(idx);
        res.concat(accel_res.nontermProof);
        if (log) {
            std::cout << "accelerated non-terminating rule:" << std::endl;
            ITSExport::printRule(*accel_res.nontermRule, chcs, std::cout);
            std::cout << std::endl;
        }
    }
    if (accel_res.rule) {
        // acceleration succeeded, simplify the result
        auto simplified = Preprocess::simplifyRule(chcs, *accel_res.rule);
        if (simplified->getUpdate() != simp->getUpdate()) {
            // accelerated rule differs from the original one, update the result
            if (Config::Analysis::complexity()) {
                simplified.concat(instantiate(*accel_res.n, *simplified));
            }
            res.succeed();
            res->push_back(add_learned_clause(*simplified, lang));
            res.concat(accel_res.accelerationProof);
            res.concat(simplified.getProof());
            if (log) {
                std::cout << "accelerated rule:" << std::endl;
                ITSExport::printRule(*simplified, chcs, std::cout);
                std::cout << std::endl;
            }
        }
    }
    if (!res) {
        if (log) std::cout << "acceleration failed" << std::endl;
        return std::make_unique<Failed>();
    }
    update_rules(src);
    return std::make_unique<Succeeded>(res);
}

std::unique_ptr<LearningState> Reachability::handle_loop(const unsigned backlink) {
    const auto lang = build_language(backlink);
    if (redundance->is_redundant(lang)) {
        if (log) std::cout << "loop already covered" << std::endl;
        return std::make_unique<Covered>();
    } else if (log) {
        std::cout << "learning clause for the following language:" << std::endl;
        std::cout << lang << std::endl;
    }
    redundance->mark_as_redundant(lang);
    const auto loop = build_loop(backlink);
    if (Config::Analysis::reachability() && loop.getUpdate().empty()) {
        if (log) std::cout << "trivial looping suffix" << std::endl;
        return std::make_unique<Covered>();
    }
    luby_loop_count++;
    auto state = learn_clause(loop, lang);
    if (!state->succeeded()) {
        return state;
    }
    auto accel_state = *state->succeeded();
    const auto idxs = **accel_state;
    // drop the looping suffix
    drop_until(backlink);
    for (const auto idx: idxs) {
        const auto clause = chcs.getRule(idx);
        if (store_step(idx, clause.getGuard())) {
            if (clause.getRhsLoc() == chcs.getSink()) {
                return std::make_unique<ProvedUnsat>(accel_state->getProof());
            } else {
                return state;
            }
        }
    }
    if (log) std::cout << "applying accelerated rule failed" << std::endl;
    return std::make_unique<Dropped>(*accel_state);
}

LocationIdx Reachability::get_current_predicate() const {
    return trace.empty() ? chcs.getInitialLocation() : chcs.getRule(trace.back().clause_idx).getRhsLoc();
}

bool Reachability::try_to_finish(const std::vector<TransIdx> &clauses) {
    solver.setTimeout(2000);
    for (const auto &q: clauses) {
        const std::optional<BoolExpr> implicant = resolve(q);
        if (implicant) {
            // no need to compute the model and the variable renaming for the next step, as we are done
            add_to_trace(Step(q, *implicant, Subs(), compute_resolvent(q, *implicant)));
            proof.headline("Step with " + std::to_string(q));
            print_state();
            unsat();
            return true;
        }
    }
    solver.setTimeout(smt_timeout);
    return false;
}

bool Reachability::try_to_finish() {
    return try_to_finish(queries[get_current_predicate()]);
}

bool Reachability::try_conditional_empty_clauses() {
    return try_to_finish(conditional_empty_clauses);
}

void Reachability::analyze() {
    proof.majorProofStep("Initial ITS", chcs);
    if (log) {
        std::cout << "Initial ITS" << std::endl;
        ITSExport::printForProof(chcs, std::cout);
    }
    preprocess();
    init();
    if (try_conditional_empty_clauses()) {
        return;
    }
    blocked_clauses[0].clear();
    do {
        size_t next_restart = luby_unit * luby.second;
        if (log) std::cout << "trace: " << trace << std::endl;
        for (auto backlink = has_looping_suffix();
             backlink && luby_loop_count < next_restart;
             backlink = has_looping_suffix()) {
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
                proof.majorProofStep("Accelerate", chcs);
                proof.storeSubProof((*state->succeeded())->getProof());
                print_state();
                update_cpx();
                // try to apply a query before doing another step
                if (try_to_finish()) {
                    return;
                }
            } else if (state->dropped()) {
                if (simple_loop) {
                    block(step);
                }
                proof.majorProofStep("Accelerate and Drop", chcs);
                proof.storeSubProof((*state->dropped())->getProof());
                print_state();
            } else if (state->failed()) {
                // non-loop --> do not backtrack
                proof.headline("Acceleration Failed");
                proof.append("marked recursive suffix as redundant");
                non_loops.add(trace, *backlink);
            } else if (state->unsat()) {
                proof.majorProofStep("Nonterm", chcs);
                proof.storeSubProof(**state->unsat());
                proof.headline("Step with " + std::to_string(trace.back().clause_idx));
                print_state();
                unsat();
                return;
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
        const auto current = get_current_predicate();
        auto &to_try = rules[current];
        auto it = to_try.begin();
        std::vector<TransIdx> append;
        while (it != to_try.end()) {
            const std::optional<BoolExpr> implicant = resolve(*it);
            if (implicant && store_step(*it, *implicant)) {
                proof.headline("Step with " + std::to_string(*it));
                print_state();
                update_cpx();
                break;
            }
            append.push_back(*it);
            it = to_try.erase(it);
        }
        bool all_failed = it == to_try.end();
        to_try.insert(to_try.end(), append.begin(), append.end());
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
