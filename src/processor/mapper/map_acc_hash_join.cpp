#include "processor/mapper/plan_mapper.h"
#include "processor/operator/table_scan/factorized_table_scan.h"

using namespace kuzu::planner;

namespace kuzu {
namespace processor {

static FactorizedTableScan* getTableScanForAccHashJoin(PhysicalOperator* probe) {
    auto op = probe->getChild(0);
    while (op->getOperatorType() == PhysicalOperatorType::FLATTEN) {
        op = op->getChild(0);
    }
    assert(op->getOperatorType() == PhysicalOperatorType::FACTORIZED_TABLE_SCAN);
    return (FactorizedTableScan*)op;
}

void PlanMapper::mapAccHashJoin(kuzu::processor::PhysicalOperator* probe) {
    auto tableScan = getTableScanForAccHashJoin(probe);
    auto resultCollector = tableScan->moveUnaryChild();
    probe->addChild(std::move(resultCollector));
}

} // namespace processor
} // namespace kuzu
