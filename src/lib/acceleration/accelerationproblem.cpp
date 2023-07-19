#include "accelerationproblem.hpp"
#include "recurrence.hpp"
#include "smtfactory.hpp"
#include "expr.hpp"
#include "boolexpr.hpp"
#include "relevantvariables.hpp"

AccelerationProblem::AccelerationProblem(
        const Rule &r,
        const std::optional<Recurrence::Result> &closed,
        const std::optional<Subs> &samplePoint,
        const AccelConfig &config):
    closed(closed),
    update(r.getUpdate()),
    guard(r.getGuard()->toG()),
    config(config),
    samplePoint(samplePoint) {
//    if (closed) {
//        update.get<IntTheory>() = closed->refined_equations;
//        proof.append(std::stringstream() << "refined update: " << update);
//    }
    for (const auto &l: guard->lits()) {
        todo.insert(l);
    }
    const auto subs {closed ? std::vector<Subs>{update, closed->closed_form} : std::vector<Subs>{update}};
    Logic logic {Smt<IntTheory, BoolTheory>::chooseLogic<LitSet, Subs>({todo}, subs)};
    this->solver = SmtFactory::modelBuildingSolver<IntTheory, BoolTheory>(logic);
    if (closed) {
        const auto bound {BExpression::buildTheoryLit(Rel(closed->n, Rel::geq, 1))};
        this->solver->add(bound);
        this->res.formula.push_back(bound);
    }
}

bool AccelerationProblem::trivial(const Lit &lit) {
    if (expr::subs(lit, update)->isTriviallyTrue()) {
        res.formula.push_back(BExpression::buildTheoryLit(lit));
        res.proof.newline();
        res.proof.append(std::stringstream() << lit << ": trivial");
        solver->add(BExpression::buildTheoryLit(lit));
        return true;
    }
    return false;
}

bool AccelerationProblem::unchanged(const Lit &lit) {
    if (BExpression::buildTheoryLit(lit) == expr::subs(lit, update)) {
        res.formula.push_back(BExpression::buildTheoryLit(lit));
        res.proof.newline();
        res.proof.append(std::stringstream() << lit << ": unchanged");
        solver->add(BExpression::buildTheoryLit(lit));
        return true;
    }
    return false;
}

bool AccelerationProblem::polynomial(const Lit &lit) {
    if (!closed || !std::holds_alternative<Rel>(lit)) {
        return false;
    }
    const auto &rel {std::get<Rel>(lit)};
    const auto nfold {rel.lhs().subs(closed->closed_form.get<IntTheory>()).expand()};
    if (!nfold.isPoly(closed->n)) {
        return false;
    }
    const auto &up {update.get<IntTheory>()};
    const ExprSubs but_last {closed->closed_form.get<IntTheory>().compose({{closed->n, Expr(closed->n) - 1}})};
    bool low_degree {false};
    RelSet guard;
    switch (nfold.degree(closed->n)) {
    case 0: return false;
    case 1: {
        const auto coeff {nfold.coeff(closed->n, 1)};
        if (coeff.isGround()) {
            if (coeff.toNum().is_positive()) {
                guard.insert(rel);
            } else {
                guard.insert(rel.subs(but_last));
            }
        } else {
            guard.insert(rel);
            guard.insert(rel.subs(but_last));
        }
        low_degree = true;
        break;
    }
    case 2: {
        const auto coeff {nfold.coeff(closed->n, 2)};
        if (coeff.isGround()) {
            if (coeff.toNum().is_negative()) {
                guard.insert(rel);
                guard.insert(rel.subs(but_last));
                low_degree = true;
            }
        }
        break;
    }
    default: {}
    }
    if (!low_degree && !samplePoint) {
        return false;
    } else if (!low_degree) {
        if (nfold.isGround() || !nfold.isPoly(closed->n)) {
            return false;
        }
        auto sample_point {samplePoint->get<IntTheory>()};
        std::vector<Expr> derivatives {rel.lhs()};
        std::vector<GiNaC::numeric> signs {rel.lhs().subs(sample_point).toNum()};
        Expr diff;
        do {
            const auto &last {derivatives.back()};
            diff = (last.subs(up) - last).expand();
            derivatives.push_back(diff);
            signs.push_back(diff.subs(sample_point).toNum());
        } while (!diff.isGround());
        for (unsigned i = 1; i < signs.size() - 1; ++i) {
            if (signs.at(i).is_zero()) {
                for (unsigned j = i + 1; j < signs.size(); ++j) {
                    if (!signs.at(j).is_zero()) {
                        signs[i] = signs[j];
                        break;
                    }
                }
            }
        }
        if (signs.at(1).is_positive()) {
            guard.insert(rel);
        } else {
            guard.insert(rel.subs(but_last));
        }
        for (unsigned i = 1; i < derivatives.size() - 1; ++i) {
            if (signs.at(i).is_positive()) {
                const auto r {Rel::buildGeq(derivatives.at(i), 0)};
                guard.insert(r);
            } else {
                const auto r {Rel::buildLeq(derivatives.at(i), 0)};
                guard.insert(r);
            }
            if (signs.at(i+1).is_positive()) {
                // the i-th derivative is monotonically increasing at the sampling point
                if (signs.at(i).is_positive()) {
                    guard.insert(Rel::buildGeq(derivatives.at(i), 0));
                } else {
                    guard.insert(Rel::buildLeq(derivatives.at(i).subs(but_last), 0));
                }
            } else {
                if (signs.at(i).is_positive()) {
                    guard.insert(Rel::buildGeq(derivatives.at(i).subs(but_last), 0));
                } else {
                    guard.insert(Rel::buildLeq(derivatives.at(i), 0));
                }
            }
        }
    }
    const auto g {BExpression::buildAndFromLits(guard)};
    res.formula.push_back(g);
    res.proof.newline();
    res.proof.append(std::stringstream() << lit << ": polynomial acceleration yields " << g);
    res.nonterm = false;
    solver->add(BExpression::buildTheoryLit(lit));
    return true;
}

bool AccelerationProblem::monotonicity(const Lit &lit) {
    if (!closed) {
        return false;
    }
    const auto updated {expr::subs(lit, update)};
    solver->push();
    solver->add(updated);
    solver->add(BExpression::buildTheoryLit(expr::negate(lit)));
    auto success {solver->check() == Unsat};
    solver->pop();
    if (success) {
        auto g {expr::subs(lit, closed->closed_form)->subs(Subs::build<IntTheory>(closed->n, Expr(closed->n)-1))};
        if (closed->prefix > 0) {
            g = g & lit;
        }
        res.formula.push_back(g);
        res.proof.newline();
        std::stringstream ss;
        ss << lit << ": montonic decrease yields " << g;
        res.proof.append(ss);
        res.nonterm = false;
        solver->add(BExpression::buildTheoryLit(lit));
    }
    return success;
}

bool AccelerationProblem::recurrence(const Lit &lit) {
    const auto updated {expr::subs(lit, update)};
    solver->push();
    solver->add(BExpression::buildTheoryLit(lit));
    solver->add(!updated);
    auto success {solver->check() == Unsat};
    solver->pop();
    if (success) {
        const auto g {BExpression::buildTheoryLit(lit)};
        res.formula.push_back(g);
        res.proof.newline();
        std::stringstream ss;
        ss << lit << ": montonic increase yields " << g;
        res.proof.append(ss);
        solver->add(BExpression::buildTheoryLit(lit));
    }
    return success;
}

bool AccelerationProblem::eventualWeakDecrease(const Lit &lit) {
    if (!closed || !std::holds_alternative<Rel>(lit)) {
        return false;
    }
    const Rel &rel {std::get<Rel>(lit)};
    const auto updated {rel.lhs().subs(update.get<IntTheory>())};
    const auto dec {Rel::buildGeq(rel.lhs(), updated)};
    const auto inc {Rel::buildLt(updated, updated.subs(update.get<IntTheory>()))};
    solver->push();
    solver->add(BExpression::buildTheoryLit(dec));
    solver->add(BExpression::buildTheoryLit(inc));
    const auto success {solver->check() == Unsat};
    solver->pop();
    if (success) {
        const auto g {BExpression::buildTheoryLit(rel) & rel.subs(closed->closed_form.get<IntTheory>()).subs({{closed->n, Expr(closed->n)-1}})};
        res.formula.push_back(g);
        res.proof.newline();
        std::stringstream ss;
        ss << rel << ": eventual decrease yields " << g;
        res.proof.append(ss);
        res.nonterm = false;
        solver->add(BExpression::buildTheoryLit(lit));
    }
    return success;
}

bool AccelerationProblem::eventualIncrease(const Lit &lit, const bool strict) {
    if (!std::holds_alternative<Rel>(lit)) {
        return false;
    }
    const auto &rel {std::get<Rel>(lit)};
    const auto updated {rel.lhs().subs(update.get<IntTheory>())};
    const auto inc = strict ? Rel::buildLt(rel.lhs(), updated) : Rel::buildLeq(rel.lhs(), updated);
    if (samplePoint && inc.subs(samplePoint->get<IntTheory>()).isTriviallyFalse()) {
        return false;
    }
    const auto dec = strict ? Rel::buildGeq(updated, updated.subs(update.get<IntTheory>())) : Rel::buildGt(updated, updated.subs(update.get<IntTheory>()));
    solver->push();
    solver->add(BExpression::buildTheoryLit(inc));
    if (solver->check() != Sat) {
        solver->pop();
        return false;
    }
    solver->add(BExpression::buildTheoryLit(dec));
    const auto success {solver->check()};
    solver->pop();
    if (success) {
        const auto g {BExpression::buildTheoryLit(rel) & inc};
        res.formula.push_back(g);
        res.proof.newline();
        std::stringstream ss;
        ss << rel << ": eventual increase yields " << g;
        res.proof.append(ss);
        solver->add(BExpression::buildTheoryLit(lit));
    }
    return success;
}

bool AccelerationProblem::fixpoint(const Lit &lit) {
    std::vector<BoolExpr> eqs;
    const auto vars {util::RelevantVariables::find(expr::variables(lit), update)};
    for (const auto& v: vars) {
        if (std::holds_alternative<BoolVar>(v)) {
            // encoding equality for booleans introduces a disjunction
            return false;
        }
        eqs.push_back(expr::mkEq(TheTheory::varToExpr(v), update.get(v)));
    }
    const auto allEq {BExpression::buildAnd(eqs)};
    if (samplePoint && !allEq->subs(*samplePoint)->isTriviallyTrue()) {
        return false;
    }
    BoolExpr g {allEq & lit};
    res.formula.push_back(g);
    res.proof.newline();
    std::stringstream ss;
    ss << lit << ": fixpoint yields " << g;
    res.proof.append(ss);
    return true;
}

std::optional<AccelerationProblem::Accelerator> AccelerationProblem::computeRes() {
    auto progress {true};
    while (!todo.empty() && progress) {
        progress = false;
        for (auto it = todo.begin(); it != todo.end();) {
            if (trivial(*it) ||
                unchanged(*it) ||
                recurrence(*it) ||
                monotonicity(*it) ||
                eventualWeakDecrease(*it) ||
                (config.approx == UnderApprox && (eventualIncrease(*it, false) || eventualIncrease(*it, true) || fixpoint(*it)))) {
                it = todo.erase(it);
                progress = true;
            } else {
                ++it;
            }
        }
    }
    if (progress) {
        return res;
    } else {
        return {};
    }
}
