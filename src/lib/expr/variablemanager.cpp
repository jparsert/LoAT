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

#include "variablemanager.hpp"
#include "variable.hpp"

using namespace std;

bool VariableManager::isTempVar(const Var &var) const {
    return temporaryVariables.find(variable::getName(var)) != temporaryVariables.end();
}

void VariableManager::toLower(string &str) const {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
}

string VariableManager::getFreshName(string basename) {
    toLower(basename);
    auto &count = basenameCount.emplace(basename, 0).first->second;
    std::string res = count == 0 ? basename : basename + to_string(count);
    while (used.find(res) != used.end()) {
        ++count;
        res = basename + to_string(count);
    }
    count++;
    used.insert(res);
    return res;
}

Expr::Type VariableManager::getType(const Var &x) const {
    if (untrackedVariables.find(x) != untrackedVariables.end()) {
        return untrackedVariables.at(x);
    } else {
        return std::visit(Overload{
                              [](const NumVar &x){
                                  return Expr::Int;
                              },
                              [](const BoolVar &x){
                                  return Expr::Bool;
                              }
                          }, x);
    }
}
