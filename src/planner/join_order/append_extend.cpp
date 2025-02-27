#include "planner/join_order/cost_model.h"
#include "planner/join_order_enumerator.h"
#include "planner/logical_plan/logical_operator/logical_extend.h"
#include "planner/logical_plan/logical_operator/logical_recursive_extend.h"
#include "planner/query_planner.h"

using namespace kuzu::common;

namespace kuzu {
namespace planner {

static bool extendHasAtMostOneNbrGuarantee(RelExpression& rel, NodeExpression& boundNode,
    ExtendDirection direction, const catalog::Catalog& catalog) {
    if (boundNode.isMultiLabeled()) {
        return false;
    }
    if (rel.isMultiLabeled()) {
        return false;
    }
    if (direction == ExtendDirection::BOTH) {
        return false;
    }
    auto relDirection = ExtendDirectionUtils::getRelDataDirection(direction);
    return catalog.getReadOnlyVersion()->isSingleMultiplicityInDirection(
        rel.getSingleTableID(), relDirection);
}

void JoinOrderEnumerator::appendNonRecursiveExtend(std::shared_ptr<NodeExpression> boundNode,
    std::shared_ptr<NodeExpression> nbrNode, std::shared_ptr<RelExpression> rel,
    ExtendDirection direction, const expression_vector& properties, LogicalPlan& plan) {
    auto hasAtMostOneNbr = extendHasAtMostOneNbrGuarantee(*rel, *boundNode, direction, catalog);
    auto extend = make_shared<LogicalExtend>(
        boundNode, nbrNode, rel, direction, properties, hasAtMostOneNbr, plan.getLastOperator());
    queryPlanner->appendFlattens(extend->getGroupsPosToFlatten(), plan);
    extend->setChild(0, plan.getLastOperator());
    extend->computeFactorizedSchema();
    // update cost
    plan.setCost(CostModel::computeExtendCost(plan));
    // update cardinality. Note that extend does not change cardinality.
    if (!hasAtMostOneNbr) {
        auto extensionRate = queryPlanner->cardinalityEstimator->getExtensionRate(*rel, *boundNode);
        auto group = extend->getSchema()->getGroup(nbrNode->getInternalIDProperty());
        group->setMultiplier(extensionRate);
    }
    plan.setLastOperator(std::move(extend));
}

void JoinOrderEnumerator::appendRecursiveExtend(std::shared_ptr<NodeExpression> boundNode,
    std::shared_ptr<NodeExpression> nbrNode, std::shared_ptr<RelExpression> rel,
    ExtendDirection direction, LogicalPlan& plan) {
    auto recursiveInfo = rel->getRecursiveInfo();
    queryPlanner->appendAccumulate(plan);
    std::vector<std::shared_ptr<LogicalOperator>> children;
    children.push_back(plan.getLastOperator());
    // Recursive node property scan plan
    auto recursiveNodePropertyScanPlan = std::make_unique<LogicalPlan>();
    createRecursiveNodePropertyScanPlan(recursiveInfo->node, *recursiveNodePropertyScanPlan);
    children.push_back(recursiveNodePropertyScanPlan->getLastOperator());
    // Recursive rel property scan plan
    auto recursiveRelPropertyScanPlan = std::make_unique<LogicalPlan>();
    createRecursiveRelPropertyScanPlan(
        recursiveInfo->node, nbrNode, recursiveInfo->rel, direction, *recursiveRelPropertyScanPlan);
    children.push_back(recursiveRelPropertyScanPlan->getLastOperator());
    // Recursive plan
    auto recursivePlan = std::make_unique<LogicalPlan>();
    createRecursivePlan(
        boundNode, recursiveInfo->node, recursiveInfo->rel, direction, *recursivePlan);
    auto extend = std::make_shared<LogicalRecursiveExtend>(boundNode, nbrNode, rel, direction,
        RecursiveJoinType::TRACK_PATH, std::move(children), recursivePlan->getLastOperator());
    queryPlanner->appendFlattens(extend->getGroupsPosToFlatten(), plan);
    extend->setChild(0, plan.getLastOperator());
    extend->computeFactorizedSchema();
    auto extensionRate = queryPlanner->cardinalityEstimator->getExtensionRate(*rel, *boundNode);
    plan.setCost(CostModel::computeRecursiveExtendCost(rel->getUpperBound(), extensionRate, plan));
    auto hasAtMostOneNbr = extendHasAtMostOneNbrGuarantee(*rel, *boundNode, direction, catalog);
    if (!hasAtMostOneNbr) {
        auto group = extend->getSchema()->getGroup(nbrNode->getInternalIDProperty());
        group->setMultiplier(extensionRate);
    }
    plan.setLastOperator(std::move(extend));
}

void JoinOrderEnumerator::createRecursivePlan(std::shared_ptr<NodeExpression> boundNode,
    std::shared_ptr<NodeExpression> recursiveNode, std::shared_ptr<RelExpression> recursiveRel,
    ExtendDirection direction, LogicalPlan& plan) {
    auto scanFrontier = std::make_shared<LogicalScanFrontier>(boundNode);
    scanFrontier->computeFactorizedSchema();
    plan.setLastOperator(std::move(scanFrontier));
    auto properties = expression_vector{recursiveRel->getInternalIDProperty()};
    appendNonRecursiveExtend(boundNode, recursiveNode, recursiveRel, direction, properties, plan);
}

void JoinOrderEnumerator::createRecursiveNodePropertyScanPlan(
    std::shared_ptr<NodeExpression> recursiveNode, LogicalPlan& plan) {
    appendScanNodeID(recursiveNode, plan);
    expression_vector properties;
    for (auto& property : recursiveNode->getPropertyExpressions()) {
        properties.push_back(property->copy());
    }
    queryPlanner->appendScanNodePropIfNecessary(properties, recursiveNode, plan);
}

void JoinOrderEnumerator::createRecursiveRelPropertyScanPlan(
    std::shared_ptr<NodeExpression> recursiveNode, std::shared_ptr<NodeExpression> nbrNode,
    std::shared_ptr<RelExpression> recursiveRel, ExtendDirection direction, LogicalPlan& plan) {
    appendScanNodeID(recursiveNode, plan);
    expression_vector properties;
    for (auto& property : recursiveRel->getPropertyExpressions()) {
        properties.push_back(property->copy());
    }
    appendNonRecursiveExtend(recursiveNode, nbrNode, recursiveRel, direction, properties, plan);
}

} // namespace planner
} // namespace kuzu
