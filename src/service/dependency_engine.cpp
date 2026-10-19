#include "service/dependency_engine.hpp"
#include <queue>
#include <sstream>

namespace buildmesh {

std::unordered_map<std::string, std::vector<std::string>>
DependencyEngine::build_reverse_dependency_map(const DependencyGraph& graph) const {
    // Reverse adjacency map: dependency -> list of dependents (modules that depend on it)
    std::unordered_map<std::string, std::vector<std::string>> reverseMap;
    for (const auto& pair : graph.modules) {
        const auto& modId = pair.first;
        const auto& mod = pair.second;
        for (const auto& depId : mod.dependencies) {
            reverseMap[depId].push_back(modId);
        }
    }
    return reverseMap;
}

std::vector<std::string>
DependencyEngine::get_affected_dependents(const DependencyGraph& graph, const std::string& modifiedModuleId) const {
    std::vector<std::string> affectedList;
    if (modifiedModuleId.empty() || !graph.has_module(modifiedModuleId)) {
        return affectedList;
    }

    auto reverseMap = build_reverse_dependency_map(graph);
    std::unordered_set<std::string> visited;
    std::queue<std::string> q;

    visited.insert(modifiedModuleId);
    q.push(modifiedModuleId);

    while (!q.empty()) {
        std::string current = q.front();
        q.pop();

        auto it = reverseMap.find(current);
        if (it != reverseMap.end()) {
            for (const auto& dependent : it->second) {
                if (visited.find(dependent) == visited.end()) {
                    visited.insert(dependent);
                    affectedList.push_back(dependent);

                    // ==========================================
                    // BUG 1 (Intentional Defect):
                    // Incomplete dependent traversal.
                    // The dependent is added to affectedList, but is NOT pushed to
                    // the traversal queue. As a result, transitive dependents
                    // (such as config, build_cli, and app) are never discovered.
                    // Fix: q.push(dependent);
                    // ==========================================
                    // q.push(dependent);
                }
            }
        }
    }

    return affectedList;
}

bool DependencyEngine::dfs_cycle(const std::string& nodeId,
                                 const DependencyGraph& graph,
                                 std::unordered_map<std::string, int>& state,
                                 std::vector<std::string>& path,
                                 std::string& cycleDescription) const {
    // State values:
    // 0 = Unvisited
    // 1 = Visiting (currently in recursion call stack)
    // 2 = Visited (fully explored)

    // ==========================================
    // BUG 2 (Intentional Defect):
    // Incorrect cycle detection state management.
    // The node is prematurely marked as VISITED (2) before exploring dependencies,
    // instead of VISITING (1). Consequently, when a directed cycle revisits this node,
    // state[dep] is 2 instead of 1, failing to detect valid multi-node cycles.
    // Fix: Set state[nodeId] = 1 here, and state[nodeId] = 2 after the loop.
    // ==========================================
    state[nodeId] = 2; // BUG 2: Should be 1 (VISITING)
    path.push_back(nodeId);

    if (graph.has_module(nodeId)) {
        const auto& mod = graph.get_module(nodeId);
        for (const auto& depId : mod.dependencies) {
            if (state[depId] == 1) {
                // Cycle found
                std::ostringstream oss;
                oss << "Cycle detected: ";
                for (const auto& p : path) {
                    oss << p << " -> ";
                }
                oss << depId;
                cycleDescription = oss.str();
                return true;
            } else if (state[depId] == 0) {
                if (dfs_cycle(depId, graph, state, path, cycleDescription)) {
                    return true;
                }
            }
        }
    }

    path.pop_back();
    // state[nodeId] = 2; // Should be marked VISITED here after all dependencies are explored
    return false;
}

bool DependencyEngine::detect_cycle(const DependencyGraph& graph, std::string& cycleDescription) const {
    std::unordered_map<std::string, int> state; // 0=unvisited, 1=visiting, 2=visited
    for (const auto& pair : graph.modules) {
        state[pair.first] = 0;
    }

    std::vector<std::string> path;
    for (const auto& pair : graph.modules) {
        const auto& modId = pair.first;
        if (state[modId] == 0) {
            if (dfs_cycle(modId, graph, state, path, cycleDescription)) {
                return true;
            }
        }
    }
    return false;
}

GraphValidationResult DependencyEngine::validate_graph(const DependencyGraph& graph) const {
    GraphValidationResult result;

    // Check for missing dependency references
    for (const auto& pair : graph.modules) {
        const auto& mod = pair.second;
        for (const auto& depId : mod.dependencies) {
            if (!graph.has_module(depId)) {
                result.isValid = false;
                std::string diag = "Module '" + mod.id + "' references missing dependency '" + depId + "'";
                result.missingDependencies.push_back(depId);
                result.diagnostics.push_back(diag);
            }
        }
    }

    // Check for cycles
    std::string cycleDesc;
    if (detect_cycle(graph, cycleDesc)) {
        result.isValid = false;
        result.hasCycle = true;
        result.cycleDescription = cycleDesc;
        result.diagnostics.push_back(cycleDesc);
    }

    return result;
}

} // namespace buildmesh
