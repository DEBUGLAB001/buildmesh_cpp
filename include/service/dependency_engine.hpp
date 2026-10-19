#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "model/build_models.hpp"

namespace buildmesh {

struct GraphValidationResult {
    bool isValid = true;
    bool hasCycle = false;
    std::string cycleDescription;
    std::vector<std::string> missingDependencies;
    std::vector<std::string> diagnostics;
};

inline void to_json(nlohmann::json& j, const GraphValidationResult& r) {
    j = nlohmann::json{
        {"isValid", r.isValid},
        {"hasCycle", r.hasCycle},
        {"cycleDescription", r.cycleDescription},
        {"missingDependencies", r.missingDependencies},
        {"diagnostics", r.diagnostics}
    };
}

class DependencyEngine {
public:
    // Build reverse adjacency list: maps dependency -> list of direct dependents
    std::unordered_map<std::string, std::vector<std::string>> build_reverse_dependency_map(const DependencyGraph& graph) const;

    // Detect if the dependency graph contains any directed cycles
    bool detect_cycle(const DependencyGraph& graph, std::string& cycleDescription) const;

    // Find all reachable downstream dependents of a modified module
    std::vector<std::string> get_affected_dependents(const DependencyGraph& graph, const std::string& modifiedModuleId) const;

    // Validate graph for missing references and cycles
    GraphValidationResult validate_graph(const DependencyGraph& graph) const;

private:
    bool dfs_cycle(const std::string& nodeId,
                   const DependencyGraph& graph,
                   std::unordered_map<std::string, int>& state,
                   std::vector<std::string>& path,
                   std::string& cycleDescription) const;
};

} // namespace buildmesh
