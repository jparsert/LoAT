#ifndef INFTYEXPRESSION_H
#define INFTYEXPRESSION_H

#include <set>

#include "expression.h"

/**
 * This enum represents a direction. POS stands for POS_INF or POS_CONS.
 */
enum Direction { POS_INF = 0, NEG_INF, POS_CONS, NEG_CONS, POS };
extern const int DirectionSize;
extern const char* DirectionNames[];

/**
 * This class extends an Expression by a direction.
 */
class InftyExpression : public Expression {
public:
    /**
     * Creates a new InftyExpression from the given direction.
     */
    InftyExpression(Direction dir);

    /**
     * Creates a new InftyExpression from the given ginac basic and direction.
     */
    InftyExpression(const GiNaC::basic &other, Direction dir);

    /**
     * Creates a new InftyExpression from the given ginac expression and direction.
     */
    InftyExpression(const GiNaC::ex &other, Direction dir);

    /**
     * Sets the direction of this InftyExpression.
     */
    void setDirection(Direction dir);

    /**
     * Returns the direction of this InftyExpression.
     */
    Direction getDirection() const;

    /**
     * Returns true if this InftyExpression is trivially unsatisfiable, e.g,
     * if this is a variable and the direction is POS_INF or NEG_INF.
     */
    bool isTriviallyUnsatisfiable() const;

private:
    Direction direction;

};

std::ostream& operator<<(std::ostream &os, const InftyExpression &ie);

typedef std::set<InftyExpression, GiNaC::ex_is_less> InftyExpressionSet;

#endif // INFTYEXPRESSION_H