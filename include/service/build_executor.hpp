#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include "model/build_models.hpp"

namespace buildmesh {

class BuildExecutor {
public:
    // Execute a build plan against the dependency graph, updating graph state on success
    BuildResult execute_plan(const BuildPlan& plan, DependencyGraph& graph);
};

} // namespace buildmesh
