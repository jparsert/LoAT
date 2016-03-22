#ifndef LIMITPROBLEM_H
#define LIMITPROBLEM_H

#include <ostream>
#include <set>
#include <sstream>

#include "guardtoolbox.h"
#include "inftyexpression.h"
#include "limitvector.h"

/**
 * This class represents a limit problem, i.e., a set of InftyExpressions.
 */
class LimitProblem {
public:
    /**
     * Creates a new, empty LimitProblem.
     */
    LimitProblem();

    /**
     * Creates the initial LimitProblem for the given guard and cost.
     * @param normalizedGuard the guard in normalized form, i.e., a list of relations
     *                        of the form t > 0
     * @param cost a term
     */
    LimitProblem(const GuardList &normalizedGuard, const Expression &cost);

    // copy constructor and assignment operator
    LimitProblem(const LimitProblem &other);
    LimitProblem& operator=(const LimitProblem &other);

    // move constructor and move assignment operator
    LimitProblem(LimitProblem &&other);
    LimitProblem& operator=(LimitProblem &&other);

    /**
     * Adds a new InftyExpression to this LimitProblem.
     * Might mark this LimitProblem as unsolvable.
     */
    void addExpression(const InftyExpression &ex);

    /**
     * Returns a const_iterator to the beginning of the underlying set.
     * The underlying set ignores the direction of a InftyExpression.
     */
    InftyExpressionSet::const_iterator cbegin() const;

    /**
     * Returns a const_iterator to the end of the underlying set.
     * The underlying set ignores the direction of a InftyExpression.
     */
    InftyExpressionSet::const_iterator cend() const;

    /**
     * Applies the given LimitVector to the InftyExpression specified by
     * the given const_iterator where the resulting expression are given by l and r.
     * transformation rule (A)
     * @param it must be a valid const_iterator of the underlying set
     * @param lv must be applicable to *it
     */
    void applyLimitVector(const InftyExpressionSet::const_iterator &it,
                          const Expression &l, const Expression &r,
                          const LimitVector &lv);

    /**
     * Removes an integer from this LimitProblem.
     * transformation rule (B)
     * @param it must be a valid const_iterator of the underlying set and
     *           must point to an integer whose sign matches its direction
     */
    void removeConstant(const InftyExpressionSet::const_iterator &it);

    /**
     * Applies the given substitution to this LimitProblem and stores the given number
     * as an identifier for the substitution.
     * transformation rule (C)
     * @param sub must be a valid substitution
     */
    void substitute(const GiNaC::exmap &sub, int substitutionIndex);

    /**
     * Discards all but the leading term of the given univariate polynomial.
     * transformation rule (D)
     * @param it must be a valid const_iterator of the underlying set and
     *           must point to a non-constant, univariate polynomial whose
     *           direction is POS, POS_INF, or NEG_INF
     */
    void trimPolynomial(const InftyExpressionSet::const_iterator &it);

    /**
     * Replaces a power by its exponent and base (minus one).
     * @param it must be a valid const_iterator of the underlying set and
     *           must point to a univariate addition that consists of exactly
     *           one power where the exponent is a non-constant polynomial
     *           and arbitrary many monoms. The direction of *it must be POS_INF
     *           or POS.
     */
    void reducePolynomialPower(const InftyExpressionSet::const_iterator &it);

    /**
     * "Unstacks" a power.
     * @param it must be a valid const_iterator of the underlying set and
     *           must point to an addition where a summand is a power that has
     *           at least two different variables or whose exponent is not a
     *           polynomial.
     */
    void reduceGeneralPower(const InftyExpressionSet::const_iterator &it);

    /**
     * Returns true iff this problem is marked as unsolvable.
     */
    bool isUnsolvable() const;

    /**
     * Marks this problem as unsolvable.
     */
    void setUnsolvable();

    /**
     * Returns true iff this problem is solved and not marked as unsolvable.
     */
    bool isSolved() const;

    /**
     * Returns a solution for this LimitProblem.
     * This LimitProblem must be solved and must not be marked as unsolvable.
     */
    GiNaC::exmap getSolution() const;

    /**
     * Returns the variable that is used in the solution returned by getSolution().
     */
    ExprSymbol getN() const;

    /**
     * Returns a reference to the vector storing the substitution identifiers.
     */
    const std::vector<int>& getSubstitutions() const;

    /**
     * Returns a const_iterator of the underlying set to the given InftyExpression.
     * Ignores the direction of the given InftyExpression.
     */
    InftyExpressionSet::const_iterator find(const InftyExpression &ex);

    /**
     * Returns this LimitProblem as a set of relational Expressions:
     * t (+), t (+!), t(+/+!) -> t > 0
     * t (-), t (-!) -> t < 0
     */
    std::vector<Expression> getQuery();

    /**
     * Returns true if the result of getQuery() is unsatisfiable according to z3.
     * Returns false if it is satisfiable or if satisfiability is unknown.
     */
    bool isUnsat();

    /**
     * Returns true iff the conditions for calling removeConstant(it) are met.
     */
    bool removeConstantIsApplicable(const InftyExpressionSet::const_iterator &it);


    /**
     * Returns true iff the conditions for calling trimPolynomial(it) are met.
     */
    bool trimPolynomialIsApplicable(const InftyExpressionSet::const_iterator &it);

    /**
     * Returns true iff the conditions for calling reducePolynomialPower(it) are met.
     */
    bool reducePolynomialPowerIsApplicable(const InftyExpressionSet::const_iterator &it);

    /**
     * Returns true iff the conditions for calling reduceGeneralPower(it) are met.
     */
    bool reduceGeneralPowerIsApplicable(const InftyExpressionSet::const_iterator &it);

public:
    InftyExpressionSet set;
    ExprSymbol variableN;
    std::vector<int> substitutions;
    //std::ostringstream log;
    bool unsolvable;
};

std::ostream& operator<<(std::ostream &os, const LimitProblem &lp);

#endif //LIMITPROBLEM_H