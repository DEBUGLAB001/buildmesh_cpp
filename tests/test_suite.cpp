#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include "json.hpp"
#include "model/build_models.hpp"
#include "service/dependency_engine.hpp"
#include "service/build_planner.hpp"
#include "service/build_executor.hpp"

using namespace buildmesh;

struct TestCase {
    std::string name;
    bool passed = false;
    std::string error;
    long long durationMs = 0;
};

// ==========================================
// Seeded Bug Tests (Expected to FAIL in initial state)
// ==========================================

// Bug 1: Transitive dependent discovery
TestCase run_test_bug_1() {
    TestCase tc;
    tc.name = "Transitive dependent discovery";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        DependencyEngine engine;
        auto graph = DependencyGraph::create_default_graph();

        // Mark core_types as modified and request downstream dependents
        auto affected = engine.get_affected_dependents(graph, "core_types");
        std::unordered_set<std::string> affectedSet(affected.begin(), affected.end());

        // In the default graph:
        // core_types is depended on directly by math_utils, string_utils, parser, storage, build_graph
        // parser is depended on by config
        // config is depended on by build_cli
        // build_cli and reporter are depended on by app
        // Transitive dependents MUST include: config, build_cli, and app.
        if (affectedSet.find("config") == affectedSet.end() ||
            affectedSet.find("build_cli") == affectedSet.end() ||
            affectedSet.find("app") == affectedSet.end()) {
            tc.passed = false;
            tc.error = "Bug 1 detected: Transitive downstream dependents (config, build_cli, app) were omitted from the affected set.";
        } else {
            tc.passed = true;
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

// Bug 2: Directed cycle detection
TestCase run_test_bug_2() {
    TestCase tc;
    tc.name = "Directed cycle detection";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        DependencyEngine engine;
        DependencyGraph graph;
        // Construct directed cycle: A depends on B, B depends on C, C depends on A
        graph.add_or_update_module({"mod_a", "Module A", {"mod_b"}, 100, 1, 1, false});
        graph.add_or_update_module({"mod_b", "Module B", {"mod_c"}, 100, 1, 1, false});
        graph.add_or_update_module({"mod_c", "Module C", {"mod_a"}, 100, 1, 1, false});

        std::string cycleDesc;
        bool hasCycle = engine.detect_cycle(graph, cycleDesc);
        auto validation = engine.validate_graph(graph);

        if (!hasCycle || validation.isValid || !validation.hasCycle) {
            tc.passed = false;
            tc.error = "Bug 2 detected: Valid directed cycle (mod_a -> mod_b -> mod_c -> mod_a) was not detected.";
        } else {
            tc.passed = true;
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

// Bug 3: Valid dependency-respecting topological order
TestCase run_test_bug_3() {
    TestCase tc;
    tc.name = "Valid dependency-respecting topological order";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        DependencyEngine engine;
        BuildPlanner planner(engine);
        auto graph = DependencyGraph::create_default_graph();

        // Mark core_types as modified
        auto plan = planner.generate_plan(graph, "core_types");

        // Find positions of key modules in plan.steps
        int posCore = -1;
        int posParser = -1;
        int posConfig = -1;

        for (size_t i = 0; i < plan.steps.size(); ++i) {
            if (plan.steps[i].moduleId == "core_types") posCore = static_cast<int>(i);
            if (plan.steps[i].moduleId == "parser") posParser = static_cast<int>(i);
            if (plan.steps[i].moduleId == "config") posConfig = static_cast<int>(i);
        }

        // Prerequisites must precede dependents in build order:
        // core_types must appear BEFORE parser (if both in plan)
        // parser must appear BEFORE config (if both in plan)
        if (posCore != -1 && posParser != -1 && posCore > posParser) {
            tc.passed = false;
            tc.error = "Bug 3 detected: Prerequisite 'core_types' (step " + std::to_string(posCore) +
                       ") was placed after dependent 'parser' (step " + std::to_string(posParser) + ").";
        } else if (posParser != -1 && posConfig != -1 && posParser > posConfig) {
            tc.passed = false;
            tc.error = "Bug 3 detected: Prerequisite 'parser' (step " + std::to_string(posParser) +
                       ") was placed after dependent 'config' (step " + std::to_string(posConfig) + ").";
        } else {
            tc.passed = true;
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

// Bug 4: Correct incremental invalidation
TestCase run_test_bug_4() {
    TestCase tc;
    tc.name = "Correct incremental invalidation";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        DependencyEngine engine;
        BuildPlanner planner(engine);
        auto graph = DependencyGraph::create_default_graph();

        // When parser is modified, downstream dependents are config, build_cli, app.
        // Upstream clean dependencies (string_utils, core_types) and unrelated modules
        // (math_utils, storage) must NOT be included in the rebuild plan.
        auto plan = planner.generate_plan(graph, "parser");

        bool foundStringUtils = false;
        bool foundCoreTypes = false;

        for (const auto& step : plan.steps) {
            if (step.moduleId == "string_utils") foundStringUtils = true;
            if (step.moduleId == "core_types") foundCoreTypes = true;
        }

        if (foundStringUtils || foundCoreTypes) {
            tc.passed = false;
            tc.error = "Bug 4 detected: Clean cached upstream modules (string_utils, core_types) were unnecessarily invalidated when only 'parser' changed.";
        } else {
            tc.passed = true;
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

// Bug 5: Artifact revision consistency after execution
TestCase run_test_bug_5() {
    TestCase tc;
    tc.name = "Artifact revision consistency after execution";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        DependencyEngine engine;
        BuildPlanner planner(engine);
        BuildExecutor executor;
        auto graph = DependencyGraph::create_default_graph();

        // Increment source revision of string_utils to simulate editing code
        auto& mod = graph.get_module_mut("string_utils");
        mod.sourceRevision = 2; // artifactRevision remains 1

        BuildPlan plan;
        plan.isValid = true;
        plan.affectedModules = {"string_utils"};
        BuildStep step;
        step.moduleId = "string_utils";
        step.status = "pending";
        step.durationMs = mod.durationMs;
        plan.steps.push_back(step);

        auto result = executor.execute_plan(plan, graph);

        const auto& updatedMod = graph.get_module("string_utils");
        if (updatedMod.artifactRevision != updatedMod.sourceRevision) {
            tc.passed = false;
            tc.error = "Bug 5 detected: Successfully rebuilt module 'string_utils' artifact revision (" +
                       std::to_string(updatedMod.artifactRevision) + ") does not match source revision (" +
                       std::to_string(updatedMod.sourceRevision) + ").";
        } else {
            tc.passed = true;
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

// Bug 6: Blocking dependents after prerequisite failure
TestCase run_test_bug_6() {
    TestCase tc;
    tc.name = "Blocking dependents after prerequisite failure";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        DependencyEngine engine;
        BuildExecutor executor;
        auto graph = DependencyGraph::create_default_graph();

        // Set parser to simulate compilation failure
        graph.get_module_mut("parser").simulateFailure = true;

        // config depends on parser
        BuildPlan plan;
        plan.isValid = true;
        plan.affectedModules = {"parser", "config"};

        BuildStep step1;
        step1.moduleId = "parser";
        step1.status = "pending";
        step1.durationMs = 250;
        plan.steps.push_back(step1);

        BuildStep step2;
        step2.moduleId = "config";
        step2.status = "pending";
        step2.durationMs = 140;
        plan.steps.push_back(step2);

        auto result = executor.execute_plan(plan, graph);

        // parser must be failed
        // config must be blocked
        std::string configStatus;
        for (const auto& s : result.steps) {
            if (s.moduleId == "config") {
                configStatus = s.status;
            }
        }

        if (configStatus != "blocked") {
            tc.passed = false;
            tc.error = "Bug 6 detected: Dependent module 'config' was marked '" + configStatus +
                       "' instead of 'blocked' when prerequisite 'parser' failed.";
        } else {
            tc.passed = true;
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

// ==========================================
// Baseline Regression Tests (Expected to PASS)
// ==========================================

TestCase run_baseline_acyclic_graph() {
    TestCase tc;
    tc.name = "Valid acyclic graph handling";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        DependencyEngine engine;
        auto graph = DependencyGraph::create_default_graph();
        auto val = engine.validate_graph(graph);

        if (!val.isValid || val.hasCycle || !val.missingDependencies.empty()) {
            tc.passed = false;
            tc.error = "Default graph failed baseline validation.";
        } else {
            tc.passed = true;
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

TestCase run_baseline_disconnected_components() {
    TestCase tc;
    tc.name = "Disconnected graph components";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        DependencyEngine engine;
        DependencyGraph graph;
        // Two disconnected trees
        graph.add_or_update_module({"c1_base", "Base 1", {}, 50, 1, 1, false});
        graph.add_or_update_module({"c1_leaf", "Leaf 1", {"c1_base"}, 50, 1, 1, false});

        graph.add_or_update_module({"c2_base", "Base 2", {}, 50, 1, 1, false});
        graph.add_or_update_module({"c2_leaf", "Leaf 2", {"c2_base"}, 50, 1, 1, false});

        std::string cycleDesc;
        bool hasCycle = engine.detect_cycle(graph, cycleDesc);
        auto val = engine.validate_graph(graph);

        if (hasCycle || !val.isValid) {
            tc.passed = false;
            tc.error = "Disconnected acyclic components were falsely rejected.";
        } else {
            tc.passed = true;
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

TestCase run_baseline_independent_execution() {
    TestCase tc;
    tc.name = "Independent module execution";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        DependencyGraph graph;
        graph.add_or_update_module({"m1", "Module 1", {}, 100, 1, 1, false});
        graph.add_or_update_module({"m2", "Module 2", {}, 150, 1, 1, false});

        BuildPlan plan;
        plan.isValid = true;
        plan.affectedModules = {"m1", "m2"};
        plan.steps.push_back({"m1", "pending", 100, ""});
        plan.steps.push_back({"m2", "pending", 150, ""});

        BuildExecutor executor;
        auto result = executor.execute_plan(plan, graph);

        if (result.status != "success" || result.completedModules.size() != 2) {
            tc.passed = false;
            tc.error = "Independent modules failed clean execution.";
        } else {
            tc.passed = true;
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

TestCase run_baseline_invalid_identifiers() {
    TestCase tc;
    tc.name = "Invalid module identifiers";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        DependencyEngine engine;
        BuildPlanner planner(engine);
        auto graph = DependencyGraph::create_default_graph();

        auto affected = engine.get_affected_dependents(graph, "non_existent_id");
        auto plan = planner.generate_plan(graph, "non_existent_id");

        if (!affected.empty()) {
            tc.passed = false;
            tc.error = "Expected empty affected set for non-existent module.";
        } else {
            tc.passed = true;
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

TestCase run_baseline_empty_or_invalid_requests() {
    TestCase tc;
    tc.name = "Empty or invalid build requests";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        BuildExecutor executor;
        auto graph = DependencyGraph::create_default_graph();

        BuildPlan emptyPlan;
        emptyPlan.isValid = true;
        auto res = executor.execute_plan(emptyPlan, graph);

        if (res.status != "idle") {
            tc.passed = false;
            tc.error = "Empty plan execution should produce 'idle' status.";
        } else {
            tc.passed = true;
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

TestCase run_baseline_reset_behavior() {
    TestCase tc;
    tc.name = "Reset and subsequent build behavior";
    auto start = std::chrono::high_resolution_clock::now();

    try {
        auto graph = DependencyGraph::create_default_graph();
        auto allMods = graph.get_all_modules();

        if (allMods.size() != 10) {
            tc.passed = false;
            tc.error = "Expected 10 default modules in demonstration project.";
        } else {
            bool allCached = true;
            for (const auto& m : allMods) {
                if (!m.is_cached()) allCached = false;
            }
            if (!allCached) {
                tc.passed = false;
                tc.error = "All modules in default project should start in cached state.";
            } else {
                tc.passed = true;
            }
        }
    } catch (const std::exception& e) {
        tc.passed = false;
        tc.error = std::string("Exception: ") + e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    tc.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return tc;
}

int main() {
    auto totalStart = std::chrono::high_resolution_clock::now();

    std::vector<TestCase> results;

    // Run seeded bug tests
    results.push_back(run_test_bug_1());
    results.push_back(run_test_bug_2());
    results.push_back(run_test_bug_3());
    results.push_back(run_test_bug_4());
    results.push_back(run_test_bug_5());
    results.push_back(run_test_bug_6());

    // Run baseline regression tests
    results.push_back(run_baseline_acyclic_graph());
    results.push_back(run_baseline_disconnected_components());
    results.push_back(run_baseline_independent_execution());
    results.push_back(run_baseline_invalid_identifiers());
    results.push_back(run_baseline_empty_or_invalid_requests());
    results.push_back(run_baseline_reset_behavior());

    auto totalEnd = std::chrono::high_resolution_clock::now();
    long long totalDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - totalStart).count();

    int passedCount = 0;
    int failedCount = 0;

    nlohmann::json testList = nlohmann::json::array();
    for (const auto& tc : results) {
        if (tc.passed) {
            passedCount++;
        } else {
            failedCount++;
        }

        nlohmann::json item;
        item["name"] = tc.name;
        item["status"] = tc.passed ? "PASSED" : "FAILED";
        item["duration_ms"] = tc.durationMs;
        if (!tc.passed) {
            item["error"] = tc.error;
        }
        testList.push_back(item);
    }

    nlohmann::json report;
    report["total"] = results.size();
    report["passed"] = passedCount;
    report["failed"] = failedCount;
    report["execution_time_ms"] = totalDurationMs;
    report["tests"] = testList;

    // Output ONLY valid JSON on stdout
    std::cout << report.dump(2) << std::endl;

    // Return 0 if all passed, 1 if any failed
    return (failedCount == 0) ? 0 : 1;
}
