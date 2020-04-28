/*  This file is part of LoAT.
 *  Copyright (c) 2019 Florian Frohn
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses>.
 */

#include "../../accelerate/meter/farkas.hpp"
#include "constraintbuilder.hpp"
#include <algorithm>
#include "../../accelerate/meter/farkas.hpp"

namespace strengthening {

    ConstraintBuilder::ConstraintBuilder(
            const Templates &templates,
            const Rule &rule,
            const GuardContext &guardCtx,
            VariableManager &varMan
    ) : templates(templates),
        rule(rule),
        guardCtx(guardCtx),
        varMan(varMan) {}

    const BoolExpr ConstraintBuilder::buildSmtConstraints(const Templates &templates,
                                                          const Rule &rule,
                                                          const GuardContext &guardCtx,
                                                          VariableManager &varMan) {
        ConstraintBuilder builder(templates, rule, guardCtx, varMan);
        return builder.buildSmtConstraints();
    }

    const BoolExpr ConstraintBuilder::buildSmtConstraints() const {
        Guard monotonicityPremise;
        const RelSet &irrelevantConstraints = findIrrelevantConstraints();
        const BoolExpr reducedGuard = rule.getGuard()->removeRels(irrelevantConstraints).get();
        Implication imp = buildTemplatesInvariantImplication(reducedGuard);
        VarSet vars = rule.vars();
        BoolExpr res = FarkasLemma::apply(imp.premise, imp.conclusion, vars, templates.params(), varMan);
        RelSet conclusion;
        for (const Rel &rel: guardCtx.todo) {
            for (const Subs &up: rule.getUpdates()) {
                conclusion.insert(rel.subs(up));
            }
        }
        res = res & FarkasLemma::apply(imp.premise, conclusion, vars, templates.params(), varMan);
        res = res & constructInitiationConstraints(reducedGuard);
        return res;
    }

    const RelSet ConstraintBuilder::findIrrelevantConstraints() const {
        RelSet irrelevantConstraints;
        const VarSet &templateVars = templates.vars();
        for (const Rel &rel: guardCtx.guard->lits()) {
            bool irrelevant = true;
            for (const Var &var: rel.vars()) {
                if (templateVars.find(var) != templateVars.end()) {
                    irrelevant = false;
                    break;
                }
            }
            if (irrelevant) {
                irrelevantConstraints.insert(rel);
            }
        }
        return irrelevantConstraints;
    }

    const Implication ConstraintBuilder::buildTemplatesInvariantImplication(const BoolExpr &reducedGuard) const {
        Implication res;
        RelSet premise;
        RelSet conclusion;
        for (const Rel &invTemplate: templates) {
            Guard updatedTemplates;
            for (const Subs &up: rule.getUpdates()) {
                Rel updated = invTemplate;
                updated.applySubs(up);
                updatedTemplates.push_back(updated);
            }
            premise.insert(invTemplate);
            for (const Rel &rel: updatedTemplates) {
                conclusion.insert(rel);
            }
        }
        return {reducedGuard & buildAnd(premise), conclusion};
    }

    const BoolExpr ConstraintBuilder::constructInitiationConstraints(const BoolExpr &reducedGuard) const {
        std::vector<Rel> res;
        for (const Rel &e: templates) {
            res.push_back(e);
        }
        return reducedGuard & buildAnd(res);
    }

}
