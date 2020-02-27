/*  This file is part of LoAT.
 *  Copyright (c) 2015-2016 Matthias Naaf, RWTH Aachen University, Germany
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses>.
 */

#include "accelerator.hpp"

#include "../analysis/preprocess.hpp"
#include "recurrence/recurrence.hpp"
#include "meter/metering.hpp"
#include "../smt/smt.hpp"

#include "../its/rule.hpp"
#include "../its/export.hpp"

#include "../analysis/chain.hpp"
#include "../analysis/prune.hpp"

#include "../util/timeout.hpp"
#include "forward.hpp"
#include "backward.hpp"

#include <queue>
#include "../asymptotic/asymptoticbound.hpp"
#include "../strengthening/strengthener.hpp"
#include <stdexcept>
#include "../nonterm/nonterm.hpp"


using namespace std;
namespace Forward = ForwardAcceleration;
using Backward = BackwardAcceleration;


Accelerator::Accelerator(ITSProblem &its, LocationIdx loc, std::set<TransIdx> &resultingRules)
    : its(its), targetLoc(loc), resultingRules(resultingRules)
{
    // We need a sink location for INF rules and nonlinear rules
    // To avoid too many parallel rules (which would then be pruned), we use a new one for each run
    sinkLoc = its.addLocation();
}


TransIdx Accelerator::addResultingRule(Rule rule) {
    TransIdx idx = its.addRule(rule);
    resultingRules.insert(idx);
    return idx;
}


// #####################
// ##  Preprocessing  ##
// #####################

bool Accelerator::simplifySimpleLoops() {
    bool res = false;
    vector<TransIdx> loops = its.getSimpleLoopsAt(targetLoc);

    // Simplify all all simple loops.
    // This is especially useful to eliminate temporary variables before metering.
    if (Config::Accel::SimplifyRulesBefore) {
        for (TransIdx loop : loops) {
            res |= Preprocess::simplifyRule(its, its.getRuleMut(loop));
            if (Timeout::soft()) return res;
        }
    }
    if (res) {
        ProofOutput::Proof.minorProofStep("Simplified simple loops", its);
    }

    // Remove duplicate rules (does not happen frequently, but the syntactical check should be cheap anyway)
    if (Pruning::removeDuplicateRules(its, loops)) {
        res = true;
        ProofOutput::Proof.minorProofStep("Removed duplicate rules", its);
    }

    return res;
}


// ########################
// ##  Nesting of Loops  ##
// ########################

void Accelerator::nestRules(const NestingCandidate &fst, const NestingCandidate &snd) {
    // Avoid nesting a loop with its original transition or itself
    if (fst.oldRule == snd.oldRule) {
        return;
    }

    const LinearRule first = its.getLinearRule(fst.newRule);
    const LinearRule second = its.getLinearRule(snd.newRule);

    if (first.getCost().isNontermSymbol() || second.getCost().isNontermSymbol()) {
        return;
    }

    auto optNested = Chaining::chainRules(its, first, second);
    if (optNested) {
        LinearRule nestedRule = optNested.get();

        // Simplify the rule again (chaining can introduce many useless constraints)
        Preprocess::simplifyRule(its, nestedRule);

        // Note that we do not try all heuristics or backward accel to keep nesting efficient
        const Backward::AccelerationResult &accel = Backward::accelerate(its, nestedRule, sinkLoc);
        bool success = false;
        Complexity currentCpx = fst.cpx > snd.cpx ? fst.cpx : snd.cpx;
        ProofOutput proof;
        proof.chainingProof(first, second, nestedRule, its);
        proof.concat(accel.proof);
        for (const Rule &accelRule: accel.rules) {
            Complexity newCpx = AsymptoticBound::determineComplexityViaSMT(
                        its,
                        accelRule.getGuard(),
                        accelRule.getCost(),
                        false,
                        currentCpx).cpx;
            if (newCpx > currentCpx) {
                success = true;
                // Add the new rule
                addResultingRule(accelRule);
                // Try to combine chain and the accelerated loop
                auto chained = Chaining::chainRules(its, second, accelRule);
                if (chained) {
                    addResultingRule(chained.get());
                    proof.chainingProof(second, accelRule, chained.get(), its);
                }
            } else {
                proof.section("Heuristically decided not to add the following rule:");
                stringstream s;
                ITSExport::printRule(accelRule, its, s);
                proof.append(s);
            }
        }
        if (success) {
            ProofOutput::Proof.concat(proof);
        }
    }
}


void Accelerator::performNesting(std::vector<NestingCandidate> origRules, std::vector<NestingCandidate> todo) {
    for (const NestingCandidate &in : origRules) {
        Rule r = its.getLinearRule(in.newRule);
        for (const NestingCandidate &out : origRules) {
            if (in.oldRule == out.oldRule) continue;
            nestRules(in, out);
            if (Timeout::soft()) return;
        }
    }
    for (const NestingCandidate &in : origRules) {
        Rule r = its.getLinearRule(in.newRule);
        for (const NestingCandidate &out : todo) {
            nestRules(in, out);
            nestRules(out, in);
            if (Timeout::soft()) return;
        }
    }
}


// ############################
// ## Removal (cleaning up)  ##
// ############################

void Accelerator::removeOldLoops(const vector<TransIdx> &loops) {
    // Remove all old loops, unless we have decided to keep them
    std::set<TransIdx> deleted;
    for (TransIdx loop : loops) {
        if (keepRules.count(loop) == 0) {
            deleted.insert(loop);
            its.removeRule(loop);
        }
    }
    ProofOutput::Proof.deletionProof(deleted);

    // In some cases, two loops can yield similar accelerated rules, so we prune duplicates
    // and have to remove rules that were removed from resultingRules.
    if (Pruning::removeDuplicateRules(its, resultingRules)) {
        std::set<TransIdx> deleted;
        auto it = resultingRules.begin();
        while (it != resultingRules.end()) {
            if (its.hasRule(*it)) {
                it++;
            } else {
                deleted.insert(*it);
                it = resultingRules.erase(it);
            }
        }
        ProofOutput::Proof.deletionProof(deleted);
    }
}

const option<LinearRule> Accelerator::chain(const LinearRule &rule) const {
    LinearRule res = rule;
    bool changed = false;
    for (const auto &p: rule.getUpdate()) {
        const ExprSymbol &var = its.getVarSymbol(p.first);
        const Expression &up = p.second.expand();
        const ExprSymbolSet &upVars = up.getVariables();
        if (upVars.find(var) != upVars.end()) {
            if (up.isPolynomial() && up.degree(var) == 1) {
                const Expression &coeff = up.coeff(var);
                if (coeff.isRationalConstant() && coeff < 0) {
                    res = Chaining::chainRules(its, res, res, false).get();
                    changed = true;
                    break;
                }
            }
        }
    }
    const LinearRule &orig = res;
    unsigned int last = numNotInUpdate(res.getUpdate());
    do {
        LinearRule chained = Chaining::chainRules(its, res, orig, false).get();
        unsigned int next = numNotInUpdate(chained.getUpdate());
        if (next != last) {
            last = next;
            res = chained;
            changed = true;
            continue;
        }
    } while (false);
    if (changed) return {res};
    else return {};
}

unsigned int Accelerator::numNotInUpdate(const UpdateMap &up) const {
    unsigned int res = 0;
    for (auto const &p: up) {
        const ExprSymbol &x = its.getVarSymbol(p.first);
        const ExprSymbolSet &vars = p.second.getVariables();
        if (!vars.empty() && vars.find(x) == vars.end()) {
            res++;
        }
    }
    return res;
}

// ###################
// ## Acceleration  ##
// ###################

const Forward::Result Accelerator::strengthenAndAccelerate(const LinearRule &rule) const {
    Forward::Result res;
    res.status = PartialSuccess;
    // chain rule if necessary
    const option<LinearRule> &optR = chain(rule);
    if (optR) {
        res.proof.ruleTransformationProof(rule, "unrolling", optR.get(), its);
    }
    LinearRule r = optR ? optR.get() : rule;
    bool sat = Smt::check(buildAnd(r.getGuard()), its) == Smt::Sat;
    // only proceed if the guard is sat
    if (sat) {
        // try acceleration
        bool nonterm = false;
        BackwardAcceleration::AccelerationResult accelRes = Backward::accelerate(its, r, sinkLoc);
        if (!accelRes.rules.empty()) {
            res.status = accelRes.status;
            res.proof.concat(accelRes.proof);
            for (const Rule &ar: accelRes.rules) {
                nonterm |= ar.getCost().isNontermSymbol();
                res.rules.emplace_back(ar);
            }
        }
        if (!nonterm) {
            option<std::pair<Rule, Status>> p = nonterm::NonTerm::universal(r, its, sinkLoc);
            if (p) {
                nonterm = true;
                const Rule &nontermRule = p.get().first;
                res.proof.ruleTransformationProof(r, "non-termination processor", nontermRule, its);
                res.rules.emplace_back(nontermRule);
            }
        }
        if (!nonterm) {
            vector<LinearRule> strengthened = strengthening::Strengthener::apply(r, its);
            if (!strengthened.empty()) {
                for (const LinearRule &sr: strengthened) {
                    bool sat = Smt::check(buildAnd(sr.getGuard()), its) == Smt::Sat;
                    // only proceed if the guard is sat
                    if (sat) {
                        if (nonterm::NonTerm::universal(sr, its, sinkLoc)) {
                            nonterm = true;
                            const Rule &nontermRule = LinearRule(sr.getLhsLoc(), sr.getGuard(), Expression::NontermSymbol, sinkLoc, {});
                            res.proof.ruleTransformationProof(r, "recurrent set", nontermRule, its);
                            res.rules.emplace_back(nontermRule);
                        }
                    }
                }
            }
        }
        if (!nonterm) {
            option<std::pair<Rule, Status>> p = nonterm::NonTerm::fixedPoint(r, its, sinkLoc);
            if (p) {
                const Rule &nontermRule = p.get().first;
                res.proof.ruleTransformationProof(r, "fixed-point processor", nontermRule, its);
                res.rules.emplace_back(nontermRule);
            }
        }
    }
    if (res.rules.empty()) {
        res.status = Failure;
    }
    return res;
}

Forward::Result Accelerator::tryAccelerate(const Rule &rule) const {
    // Forward acceleration
    if (!rule.isLinear()) {
        return Forward::accelerate(its, rule, sinkLoc);
    } else {
        return strengthenAndAccelerate(rule.toLinear());
    }

}


Forward::Result Accelerator::accelerateOrShorten(const Rule &rule) const {
    using namespace Forward;

    // Accelerate the original rule
    auto res = tryAccelerate(rule);

    // Stop if heuristic is not applicable or acceleration was already successful
    if (!Config::Accel::PartialDeletionHeuristic || rule.isLinear()) {
        return res;
    }
    if (res.status != Failure) {
        return res;
    }

    // Remember the original result (we return this in case shortening fails)
    auto originalRes = res;

    // Helper lambda that calls tryAccelerate and adds to the proof output.
    // Returns true if accelerating was successful.
    auto tryAccel = [&](const Rule &newRule) {
        res = tryAccelerate(newRule);
        if (res.status != Failure) {
            for (const Rule &r : res.rules) {
                res.proof.ruleTransformationProof(rule, "partial deletion", newRule, its);
                res.proof.ruleTransformationProof(newRule, "acceleration", r, its);
            }
            return true;
        }
        return false;
    };

    // If metering failed, we remove rhss to ease metering.
    // To keep the code efficient, we only try all pairs and each single rhs.
    // We start with pairs of rhss, since this can still yield exponential complexity.
    const vector<RuleRhs> &rhss = rule.getRhss();

    for (unsigned int i=0; i < rhss.size(); ++i) {
        for (unsigned int j=i+1; j < rhss.size(); ++j) {
            vector<RuleRhs> newRhss{ rhss[i], rhss[j] };
            Rule newRule(rule.getLhs(), newRhss);
            if (tryAccel(newRule)) {
                return res;
            }
        }
    }

    for (RuleRhs rhs : rhss) {
        if (tryAccel(Rule(rule.getLhs(), rhs))) {
            return res;
        }
    }

    return originalRes;
}


// #####################
// ## Main algorithm  ##
// #####################

void Accelerator::run() {
    // Simplifying rules might make it easier to find metering functions
    simplifySimpleLoops();

    // Since we might add accelerated loops, we store the list of loops before acceleration
    vector<TransIdx> loops = its.getSimpleLoopsAt(targetLoc);
    if (loops.empty()) {
        return; // may happen if rules get removed in simplifySimpleLoops
    }

    // While accelerating, collect rules that might be feasible for nesting
    // Inner candidates are accelerated rules, since they correspond to a loop within another loop.
    // Outer candidates are loops that cannot be accelerated on their own (because they are missing their inner loop)
    vector<NestingCandidate> origRules;
    vector<NestingCandidate> nestingCandidates;
    for (TransIdx loop : loops) {
        const Rule &r = its.getRule(loop);
        if (r.isLinear()) {
            Complexity cpx = AsymptoticBound::determineComplexityViaSMT(
                    its,
                    r.getGuard(),
                    r.getCost(),
                    false,
                    Complexity::Const).cpx;
            origRules.push_back({loop, loop, cpx});
        }
    }

    // Try to accelerate all loops
    for (TransIdx loop : loops) {
        if (Timeout::soft()) return;

        // Forward and backward accelerate (and partial deletion for nonlinear rules)
        const Rule &r = its.getRule(loop);
        Forward::Result res = accelerateOrShorten(r);

        if (res.status != Success) {
            keepRules.insert(loop);
        }

        // Interpret the results, add new rules
        if  (res.status != Failure) {
            // Add accelerated rules, also mark them as inner nesting candidates
            ProofOutput::Proof.concat(res.proof);
            for (const Rule &accel : res.rules) {
                TransIdx added = addResultingRule(accel);

                if (accel.isSimpleLoop()) {
                    // accel.rule is a simple loop iff the original was linear and not non-terminating.
                    Complexity cpx = AsymptoticBound::determineComplexityViaSMT(
                                its,
                                accel.getGuard(),
                                accel.getCost(),
                                false,
                                Complexity::Const).cpx;
                    nestingCandidates.push_back(NestingCandidate(loop, added, cpx));
                }
            }
        }
    }

    // Nesting
    if (Config::Accel::TryNesting) {
        performNesting(std::move(origRules), std::move(nestingCandidates));
        if (Timeout::soft()) return;
    }

    // Chaining accelerated rules amongst themselves would be another heuristic.
    // Although it helps in certain examples, it is currently not implemented
    // as it would most probably result in too many rules (and would thus be expensive).
    // It can easily be added in this place in the future, if desired.

    // Simplify the guards of accelerated rules.
    // Especially backward acceleration and nesting can introduce superfluous constraints.
    bool changed = false;
    for (TransIdx rule : resultingRules) {
        changed = Preprocess::simplifyGuard(its.getRuleMut(rule).getGuardMut()) || changed;
    }

    // Keep rules for which acceleration failed (maybe these rules are in fact not loops).
    // We add them to resultingRules so they are chained just like accelerated rules.
    for (TransIdx rule : keepRules) {
        resultingRules.insert(rule);
    }

    // Remove old rules
    removeOldLoops(loops);

    // Remove sink location if we did not need it
    if (!its.hasTransitionsTo(sinkLoc)) {
        its.removeOnlyLocation(sinkLoc);
    }

    if (changed) {
        ProofOutput::Proof.minorProofStep("Simplified guards", its);
    }
}


// #######################
// ## Public interface  ##
// #######################

bool Accelerator::accelerateSimpleLoops(ITSProblem &its, LocationIdx loc, std::set<TransIdx> &resultingRules) {
    if (its.getSimpleLoopsAt(loc).empty()) {
        return false;
    }

    // Accelerate all loops (includes optimizations like nesting)
    Accelerator accel(its, loc, resultingRules);
    accel.run();

    return true;
}
