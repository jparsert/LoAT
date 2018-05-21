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

#include "nl_accelerate.h"

//#include "preprocess.h"
#include "recurrence/recurrence.h" // for linear rules
#include "meter/metering.h"

#include "z3/z3toolbox.h"

#include "its/rule.h"
#include "its/export.h"

#include "debug.h"
#include "util/stats.h"
#include "util/timing.h"
#include "util/timeout.h"

#include <queue>


using namespace std;
using boost::optional;



AcceleratorNL::AcceleratorNL(ITSProblem &its, LocationIdx loc, set<TransIdx> &acceleratedRules)
        : its(its), targetLoc(loc), acceleratedRules(acceleratedRules) {
    // Use a fresh location as destination of accelerated rules.
    // This is needed, since we do not know what a nonlinear loop results in.
    sinkLocation = its.addLocation();
}



// #####################
// ##  Preprocessing  ##
// #####################




// #####################################
// ##  Acceleration, filling members  ##
// #####################################


bool AcceleratorNL::handleMeteringResult(TransIdx ruleIdx, const Rule &rule, MeteringFinder::Result res) {
    // TODO: Add proof output also for failures

    if (res.result == MeteringFinder::ConflictVar) {
        // ConflictVar is just Unsat with more information
        res.result = MeteringFinder::Unsat;
        rulesWithConflictingVariables.push_back({ruleIdx, res.conflictVar});
    }

    switch (res.result) {
        case MeteringFinder::Unsat:
            Stats::add(Stats::SelfloopNoRank);
            debugAccel("Farkas unsat for rule " << ruleIdx);

            // Maybe the loop is just too difficult for us, so we allow to skip it (in the end)
            failedRulesNeedingEmptyLoop.insert(ruleIdx);

            // Maybe we can only find a metering function if we nest this loop with an accelerated inner loop,
            // or if we try to strengthen the guard
            rulesWithUnsatMetering.insert(ruleIdx);

            // We cannot accelerate, so we just keep the unaccelerated rule
            keepRules.insert(ruleIdx);
            return false;

        case MeteringFinder::Nonlinear:
            Stats::add(Stats::SelfloopNoRank);
            debugAccel("Farkas nonlinear for rule " << ruleIdx);

            // Maybe the loop is just too difficult for us, so we allow to skip it (in the end)
            failedRulesNeedingEmptyLoop.insert(ruleIdx);

            // We cannot accelerate, so we just keep the unaccelerated rule
            keepRules.insert(ruleIdx);
            return false;

        case MeteringFinder::Unbounded:
            Stats::add(Stats::SelfloopInfinite);
            debugAccel("Farkas unbounded for rule " << ruleIdx);

            // In case we only got here in a second attempt (by some heuristic), clear some statistics
            failedRulesNeedingEmptyLoop.erase(ruleIdx);
            keepRules.erase(ruleIdx);

            // The rule is nonterminating. We can ignore the update, but the guard still has to be kept.
            {
                Rule newRule(rule.getLhsLoc(), rule.getGuard(), Expression::InfSymbol, sinkLocation, {});
                TransIdx t = its.addRule(std::move(newRule));
                acceleratedRules.insert(t);

                proofout << "Simple loop " << ruleIdx << " has unbounded runtime, ";
                proofout << "resulting in the new transition " << t << "." << endl;
            }
            return true;

        case MeteringFinder::Success:
            debugAccel("Farkas success, got " << res.metering << " for rule " << ruleIdx);
            {
                Rule newRule = rule;

                // The metering function might need additional guards
                if (res.integralConstraint) {
                    newRule.getGuardMut().push_back(res.integralConstraint.get());
                }

                if (newRule.isLinear()) {
                    // Use iterated cost/update computation as for linear rules
                    LinearRule linRule = newRule.toLinear();

                    // Compute iterated update and cost
                    if (!Recurrence::iterateRule(its, linRule, res.metering)) {
                        Stats::add(Stats::SelfloopNoUpdate);

                        // Maybe the loop is just too difficult for us, so we allow to skip it (in the end)
                        failedRulesNeedingEmptyLoop.insert(ruleIdx);

                        // We cannot accelerate, so we just keep the unaccelerated rule
                        keepRules.insert(ruleIdx);

                        // Note: We do not add this rule to outerNestingCandidates,
                        // since it will probably still fail after nesting.
                        return false;
                    }
                    newRule = linRule;

                } else {
                    // Note: Since the cost is exponential anyway, we do not compute the iterated cost
                    // (which would be more involved for nonlinear rules).
                    // Instead, we assume the cost of every step is at least 1 (we added cost >= 1 to the guard)
                    int degree = rule.rhsCount();
                    Expression newCost = GiNaC::pow(degree, res.metering); // d^b
                    newCost = (newCost - 1) / (degree - 1); // (d^b-1)/(d-1), the ceiling is not important (we do lower bounds)
                    newRule.getCostMut() = newCost;

                    // We don't know to what result the rule evaluates (multiple rhss, so no single result).
                    // So we have to clear the rhs (fresh sink location, update is irrelevant).
                    newRule = Rule(newRule.getLhs(), RuleRhs(sinkLocation, {}));
                }

                Stats::add(Stats::SelfloopRanked);
                TransIdx newIdx = its.addRule(std::move(newRule));
                acceleratedRules.insert(newIdx);

                // In case we only got here in a second attempt (by some heuristic), clear some statistics
                failedRulesNeedingEmptyLoop.erase(ruleIdx);
                keepRules.erase(ruleIdx);

                proofout << "Simple loop " << ruleIdx << " has the metering function ";
                proofout << res.metering << ", resulting in the new transition ";
                proofout << newIdx << "." << endl;
                return true;
            }
        default:;
    }
    unreachable();
}


bool AcceleratorNL::accelerateAndStore(TransIdx ruleIdx, const Rule &rule, bool storeOnlySuccessful) {
    MeteringFinder::Result res = MeteringFinder::generate(its, rule);

    if (storeOnlySuccessful
        && res.result != MeteringFinder::Unbounded
        && res.result != MeteringFinder::Success) {
        return false;
    }

    return handleMeteringResult(ruleIdx, rule, res);
}



// #####################
// ## Main algorithm  ##
// #####################


void AcceleratorNL::run() {
    // Since we might add accelerated loops, we store the list of loops before acceleration
    const vector<TransIdx> loops = its.getSimpleLoopsAt(targetLoc);
    assert(!loops.empty());


    // Since we cannot compute iterated costs for nonlinear rules, we have to assume cost >= 1.
    // This has to hold in all loop iterations, so we add it to the guard before metering!
    bool addedCost = false;
    for (TransIdx idx : its.getSimpleLoopsAt(targetLoc)) {
        Rule &rule = its.getRuleMut(idx);
        if (!rule.isLinear()) {
            addedCost = true;
            rule.getGuardMut().push_back(rule.getCost() >= 1);
        }
    }

    if (addedCost) {
        proofout << "Added the constraint 'cost >= 1' to the guard of nonlinear rules." << endl;
    }


    // Proof output
    proofout << "Accelerating the following rules:" << endl;
    for (TransIdx loop : loops) {
        ITSExport::printLabeledRule(loop, its, proofout);
    }


    // TODO: Might restructure this code to apply all heuristics directly for each rule (so we do not need rulesWith*)
    // TODO: This might simplify the decision of what to do with the rule (nest it? add empty rule? keep it?)

    // TODO: Maybe: Add method handleWithResult() and pass the original result (before the heuristics), but only if heuristics fail
    // TODO: Or add a methods handleFailed(), handleSuccessful() etc.

    // Try to accelerate all loops
    for (TransIdx loop : loops) {
        // don't try to accelerate loops with INF cost
        if (its.getRule(loop).getCost().isInfty()) {
            debugAccel("Keeping unaccelerated rule with infty cost: " << loop);
            keepRules.insert(loop);
            continue;
        }

        accelerateAndStore(loop, its.getRule(loop));

        if (Timeout::soft()) {
            goto timeout;
        }
    }

#ifdef FARKAS_HEURISTIC_INSTANTIATE_FREEVARS
    // Instantiate temporary variables by their bounds (might help to find a metering function)
    for (TransIdx loop : rulesWithUnsatMetering) {
        Rule rule = its.getRule(loop);
        debugAccel("Trying temp var instantiation for rule: " << rule);

        if (MeteringFinder::instantiateTempVarsHeuristic(its, rule)) {
            if (accelerateAndStore(loop, rule, true)) {
                debugAccel("Temp var instantiation successful with modified rule: " << rule);
            }
        }

        if (Timeout::soft()) {
            goto timeout;
        }
    }
#endif

/*
#ifdef FARKAS_HEURISTIC_FOR_MINMAX
    // Min-Max heuristic (workaround for missing min/max(A,B) support)
    for (const ConflictVarsCandidate &can : rulesWithConflictingVariables) {
        VariableIdx A,B;
        tie(A,B) = can.conflictVars;
        LinearRule rule = its.getRule(can.oldRule);
        debugAccel("Trying MinMax heuristic with variables " << its.getVarName(A) << ", " << its.getVarName(B) << " for rule " << rule);

        // TODO: check satisfiability after adding constraints?

        // Add A > B to the guard, try to accelerate
        rule.getGuardMut().push_back(its.getGinacSymbol(A) > its.getGinacSymbol(B));
        if (accelerateAndStore(can.oldRule, rule, true)) {
            debugAccel("MinMax heuristic (A > B) successful with rule: " << rule);
        }

        // Add A < B to the guard, try to accelerate
        rule.getGuardMut().pop_back();
        rule.getGuardMut().push_back(its.getGinacSymbol(A) < its.getGinacSymbol(B));
        if (accelerateAndStore(can.oldRule, rule, true)) {
            debugAccel("MinMax heuristic (A < B) successful with rule: " << rule);
        }
    }
#endif
*/

//#ifdef FARKAS_TRY_ADDITIONAL_GUARD
#if 1
    // Guard strengthening heuristic (might help to find a metering function)
    for (TransIdx loop : rulesWithUnsatMetering) {
        Rule rule = its.getRule(loop);
        debugAccel("Trying guard strengthening for rule: " << rule);

        if (MeteringFinder::strengthenGuard(its, rule)) {
            if (accelerateAndStore(loop, rule, true)) {
                debugAccel("Guard strengthening successful with modified rule: " << rule);
            }
        }

        if (Timeout::soft()) {
            goto timeout;
        }
    }
#endif


    // in case of a timeout, we perform no further acceleration, but still delete the old rules
    timeout:;

    // Remove old rules
    proofout << "Removing the self-loops:";
    for (TransIdx loop : loops) {
        if (keepRules.count(loop) == 0) {
            proofout << " " << loop;
            its.removeRule(loop);
        }
    }
    proofout << "." << endl;

    // Add keepRules to acceleratedRules, since we want to apply chaining to these rules
    // (and then remove them, since we do not want to try to accelerate them again)
    for (TransIdx rule : keepRules) {
        acceleratedRules.insert(rule);
    }

    // Add a dummy rule to simulate the effect of not executing any loop
    // FIXME: This is no longer necessary, since we do not remove incoming rules after chaining with accelerated loops!
    // FIXME: So we always allow not executing any loop
/*    if (!failedRulesNeedingEmptyLoop.empty()) {
        TransIdx t = its.addRule(Rule::dummyRule(targetLoc, targetLoc));
        proofout << "Adding an empty self-loop: " << t << "." << endl;
    }
    */

    // FIXME: Remember to call this after accelerating loops!
    // removeDuplicateTransitions(getTransFromTo(node,node));
}



bool AcceleratorNL::accelerateSimpleLoops(ITSProblem &its, LocationIdx loc, set<TransIdx> &acceleratedRules) {
    if (its.getSimpleLoopsAt(loc).empty()) {
        return false;
    }

    proofout << endl;
    proofout.setLineStyle(ProofOutput::Headline);
    proofout << "Accelerating simple loops of location " << loc << "." << endl;
    proofout.increaseIndention();

    // Accelerate all loops (includes optimizations like nesting)
    AcceleratorNL accel(its, loc, acceleratedRules);
    accel.run();

    proofout.decreaseIndention();
    return true;
}
