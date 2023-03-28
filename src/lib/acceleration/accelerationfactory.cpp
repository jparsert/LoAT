#include "accelerationfactory.hpp"
#include "accelerationviacalculus.hpp"

using AT = std::unique_ptr<AccelerationTechnique<IntTheory, BoolTheory>>;

AT AccelerationFactory::get(
        const Rule &rule,
        std::optional<Recurrence::Result> closed,
        VarMan &its,
        const AccelConfig &config) {
//    if (closed && rule.getGuard()->isPoly() && closed->update.isPoly()) {
//        return AT(new AccelerationViaQE(rule, *closed, its));
//    } else {
        return AT(new AccelerationViaCalculus(rule, closed, its, config));
//    }
}