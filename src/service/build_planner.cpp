#include "service/build_planner.hpp"
#include <queue>
#include <algorithm>

namespace buildmesh {

std::unordered_set<std::string>
BuildPlanner::determine_rebuild_set(const DependencyGraph& graph, const std::string& modifiedModuleId) const {
    std::unordered_set<std::string> rebuildSet;

    if (modifiedModuleId.empty() || !graph.has_module(modifiedModuleId)) {
        return rebuildSet;
    }

    // Always include the modified module itself
    rebuildSet.insert(modifiedModuleId);

    // Discover all downstream dependents
    std::vector<std::string> affectedList = dependencyEngine_.get_affected_dependents(graph, modifiedModuleId);
    for (const auto& id : affectedList) {
        rebuildSet.insert(id);
    }

    // ==========================================
    // BUG 4 (Intentional Defect):
    // Incorrect incremental invalidation.
    // In addition to affected downstream modules, this loop incorrectly pulls in
    // clean upstream prerequisites into the rebuild set, invalidating cached
    // artifacts that did not change (e.g., string_utils and core_types when parser is changed).
    // Fix: Remove this loop so only modified and downstream dependents are rebuilt.
    // ==========================================
    std::vector<std::string> candidates(rebuildSet.begin(), rebuildSet.end());
    for (const auto& modId : candidates) {
        if (graph.has_module(modId)) {
            for (const auto& depId : graph.get_module(modId).dependencies) {
                rebuildSet.insert(depId);
            }
        }
    }

    return rebuildSet;
}

std::vector<std::string>
BuildPlanner::compute_topological_order(const DependencyGraph& graph, const std::unordered_set<std::string>& rebuildSet) const {
    // Kahn's algorithm for topological sorting of rebuildSet
    // Dependencies must be built BEFORE dependents.
    // In-degree for module M = number of prerequisites of M that are ALSO in rebuildSet.
    std::unordered_map<std::string, int> inDegree;
    std::unordered_map<std::string, std::vector<std::string>> adj; // prereq -> dependents in rebuildSet

    for (const auto& modId : rebuildSet) {
        inDegree[modId] = 0;
    }

    for (const auto& modId : rebuildSet) {
        if (!graph.has_module(modId)) continue;
        const auto& mod = graph.get_module(modId);
        for (const auto& depId : mod.dependencies) {
            if (rebuildSet.find(depId) != rebuildSet.end()) {
                adj[depId].push_back(modId);
                inDegree[modId]++;
            }
        }
    }

    std::queue<std::string> q;
    // Push nodes with in-degree 0 (no unbuilt prerequisites)
    for (const auto& pair : inDegree) {
        if (pair.second == 0) {
            q.push(pair.first);
        }
    }

    std::vector<std::string> order;
    while (!q.empty()) {
        std::string curr = q.front();
        q.pop();
        order.push_back(curr);

        auto it = adj.find(curr);
        if (it != adj.end()) {
            for (const auto& dependent : it->second) {
                inDegree[dependent]--;
                if (inDegree[dependent] == 0) {
                    q.push(dependent);
                }
            }
        }
    }

    // ==========================================
    // BUG 3 (Intentional Defect):
    // Invalid dependency build order.
    // The calculated topological order is reversed, which places dependent modules
    // before their prerequisites (e.g. config before parser, and parser before core_types).
    // Fix: Remove the std::reverse call so prerequisites are built before dependents.
    // ==========================================
    std::reverse(order.begin(), order.end()); // BUG 3: Inverts dependency build order!

    return order;
}

BuildPlan BuildPlanner::generate_plan(const DependencyGraph& graph, const std::string& modifiedModuleId) const {
    BuildPlan plan;

    // Validate graph first
    auto validation = dependencyEngine_.validate_graph(graph);
    if (!validation.isValid) {
        plan.isValid = false;
        plan.warnings = validation.diagnostics;
        return plan;
    }

    if (modifiedModuleId.empty() || !graph.has_module(modifiedModuleId)) {
        if (!modifiedModuleId.empty()) {
            plan.warnings.push_back("Unknown module: " + modifiedModuleId);
        }
        return plan;
    }

    auto rebuildSet = determine_rebuild_set(graph, modifiedModuleId);
    auto orderedModuleIds = compute_topological_order(graph, rebuildSet);

    int totalDuration = 0;
    for (const auto& modId : orderedModuleIds) {
        plan.affectedModules.push_back(modId);
        BuildStep step;
        step.moduleId = modId;
        step.status = "pending";
        if (graph.has_module(modId)) {
            step.durationMs = graph.get_module(modId).durationMs;
        } else {
            step.durationMs = 100;
        }
        totalDuration += step.durationMs;
        plan.steps.push_back(step);
    }

    plan.totalEstimatedDurationMs = totalDuration;
    return plan;
}

} // namespace buildmesh
