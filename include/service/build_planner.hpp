#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include "model/build_models.hpp"
#include "service/dependency_engine.hpp"

namespace buildmesh {

class BuildPlanner {
public:
    explicit BuildPlanner(DependencyEngine engine = DependencyEngine{})
        : dependencyEngine_(std::move(engine)) {}

    // Determine the minimal set of modules requiring rebuilding
    std::unordered_set<std::string> determine_rebuild_set(const DependencyGraph& graph, const std::string& modifiedModuleId) const;

    // Generate an incremental build plan respecting dependencies
    BuildPlan generate_plan(const DependencyGraph& graph, const std::string& modifiedModuleId) const;

    const DependencyEngine& get_dependency_engine() const { return dependencyEngine_; }

private:
    DependencyEngine dependencyEngine_;

    // Compute topological order for modules in the rebuild set
    std::vector<std::string> compute_topological_order(const DependencyGraph& graph, const std::unordered_set<std::string>& rebuildSet) const;
};

} // namespace buildmesh
