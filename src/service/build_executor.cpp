#include "service/build_executor.hpp"
#include <unordered_set>

namespace buildmesh {

BuildResult BuildExecutor::execute_plan(const BuildPlan& plan, DependencyGraph& graph) {
    BuildResult result;

    if (!plan.isValid) {
        result.status = "failed";
        return result;
    }

    if (plan.steps.empty()) {
        result.status = "idle";
        return result;
    }

    std::unordered_set<std::string> completedSet;
    std::unordered_set<std::string> failedSet;
    std::unordered_set<std::string> blockedSet;

    int totalElapsedMs = 0;
    bool anyFailure = false;
    bool anyBlocked = false;

    for (const auto& stepTemplate : plan.steps) {
        BuildStep step = stepTemplate;
        const std::string& modId = step.moduleId;

        if (!graph.has_module(modId)) {
            step.status = "failed";
            step.errorMessage = "Module missing from graph: " + modId;
            failedSet.insert(modId);
            result.failedModules.push_back(modId);
            result.steps.push_back(step);
            anyFailure = true;
            continue;
        }

        Module& mod = graph.get_module_mut(modId);

        // Check if any prerequisite failed or was blocked
        bool hasFailedPrereq = false;
        std::string failedPrereqId;
        for (const auto& depId : mod.dependencies) {
            if (failedSet.find(depId) != failedSet.end() || blockedSet.find(depId) != blockedSet.end()) {
                hasFailedPrereq = true;
                failedPrereqId = depId;
                break;
            }
        }

        // ==========================================
        // BUG 6 (Intentional Defect):
        // Incorrect failed-dependency recovery.
        // The prerequisite check is bypassed with 'false &&', allowing modules
        // with failed prerequisites to proceed with execution instead of being
        // marked as 'blocked'.
        // Fix: Change 'if (false && hasFailedPrereq)' to 'if (hasFailedPrereq)'.
        // ==========================================
        if (false && hasFailedPrereq) { // BUG 6: Bypassed prerequisite check
            step.status = "blocked";
            step.errorMessage = "Blocked: prerequisite dependency '" + failedPrereqId + "' failed";
            blockedSet.insert(modId);
            result.blockedModules.push_back(modId);
            result.steps.push_back(step);
            anyBlocked = true;
            continue;
        }

        // Check if this module is configured to simulate build failure
        if (mod.simulateFailure) {
            step.status = "failed";
            step.errorMessage = "Simulated compilation error in module '" + modId + "'";
            failedSet.insert(modId);
            result.failedModules.push_back(modId);
            result.steps.push_back(step);
            totalElapsedMs += step.durationMs;
            anyFailure = true;
            continue;
        }

        // Simulated successful build
        step.status = "completed";
        totalElapsedMs += step.durationMs;
        completedSet.insert(modId);
        result.completedModules.push_back(modId);

        // ==========================================
        // BUG 5 (Intentional Defect):
        // Incorrect artifact revision updates.
        // Upon successful build, artifactRevision should be updated to match
        // the sourceRevision of the built module. Instead, this update is omitted,
        // leaving the artifact revision stale.
        // Fix: Uncomment 'mod.artifactRevision = mod.sourceRevision;'.
        // ==========================================
        // mod.artifactRevision = mod.sourceRevision; // BUG 5: Omitted update

        result.steps.push_back(step);
    }

    result.totalDurationMs = totalElapsedMs;
    if (anyFailure) {
        result.status = "failed";
    } else if (anyBlocked) {
        result.status = "blocked";
    } else {
        result.status = "success";
    }

    return result;
}

} // namespace buildmesh
