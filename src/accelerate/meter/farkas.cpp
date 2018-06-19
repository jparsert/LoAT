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

#include "farkas.h"

#include "debug.h"
#include "expr/relation.h"
#include "expr/ginactoz3.h"
#include "z3/z3toolbox.h"

using namespace std;


z3::expr FarkasLemma::apply(
        const vector<Expression> &constraints,
        const vector<ExprSymbol> &vars,
        const vector<z3::expr> &coeffs,
        z3::expr c0,
        int delta,
        Z3Context &context)
{
    assert(vars.size() == coeffs.size());

#ifdef DEBUG_FARKAS
    debugFarkas("Applying Farkas Lemma to: (A*x <= b) implies (c*x + " << c0 << " <= " << delta << ")");
    dumpList("constraints A*x", constraints);
    dumpList("variables x    ", vars);
    dumpList("coefficients c ", coeffs);
#endif

    // List of expressions whose conjunction will be the result
    vector<z3::expr> res;

    // Create lambda variables, add the constraint "lambda >= 0"
    vector<z3::expr> lambda;
    for (const Expression &ex : constraints) {
        assert(Relation::isLinearInequality(ex));
        assert(Relation::isLessOrEqual(ex));

        z3::expr var = context.addFreshVariable("l", Z3Context::Real);
        lambda.push_back(var);
        res.push_back(var >= 0);
    }

    // Create mapping from every variable to its coefficient
    ExprSymbolMap<z3::expr> varToCoeff;
    for (int i=0; i < vars.size(); ++i) {
        varToCoeff.emplace(vars[i], coeffs[i]);
    }

    // Search for additional variables that are not contained in vars, but appear in constraints.
    // This is neccessary, since these variables appear in the A*x part and thus also have to appear in the c*x part.
    // The coefficients of additional variables are simply set to 0 (so they won't occur in the metering function).
    ExprSymbolSet constraintSymbols;
    for (const Expression &ex : constraints) {
        ex.collectVariables(constraintSymbols);
    }
    for (const ExprSymbol &sym : constraintSymbols) {
        if (varToCoeff.find(sym) == varToCoeff.end()) {
            debugFarkas("FARKAS NOTE: Adding additional variable with 0 coefficient: " << sym);
            varToCoeff.emplace(sym, context.real_val(0));
        }
    }

    // Build the constraints "lambda^T * A = c^T"
    for (auto varIt : varToCoeff) {
        z3::expr lambdaA = context.int_val(0);
        for (int j=0; j < constraints.size(); ++j) {
            Expression a = constraints[j].lhs().expand().coeff(varIt.first);
            z3::expr add = lambda[j] * GinacToZ3::convert(a, context);
            lambdaA = (j==0) ? add : lambdaA+add; // avoid superflous +0
        }
        res.push_back(lambdaA == varIt.second);
    }

    // Build the constraints "lambda^T * b + c0 <= delta"
    z3::expr sum = c0;
    for (int i=0; i < constraints.size(); ++i) {
        sum = sum + lambda[i] * GinacToZ3::convert(constraints[i].rhs(), context);
    }
    res.push_back(sum <= delta);

#ifdef DEBUG_FARKAS
    debugFarkas("Resulting z3 expressions:");
    for (z3::expr x : res) debugFarkas(" - " << x);
#endif

    return Z3Toolbox::concat(context, res, Z3Toolbox::ConcatAnd);
}
