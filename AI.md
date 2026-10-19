# BuildMesh

## Description

BuildMesh is an incremental build dependency engine and build planning simulation written in C++17. The application allows developers to inspect a directed module dependency graph, simulate source code modifications, compute downstream invalidations, generate topologically ordered incremental build plans, and execute simulated compilation steps in memory. The backend is served via an embedded `cpp-httplib` HTTP server in `src/main.cpp` using `nlohmann/json` for serialization, binding to `0.0.0.0` on port 8080 (or `PORT`). Core business logic is partitioned into three services: `DependencyEngine` for graph validation, cycle detection, and reverse-dependency traversal; `BuildPlanner` for incremental rebuild-set selection and topological ordering; and `BuildExecutor` for simulated build execution, prerequisite verification, and artifact revision state management. A deterministic 10-module C++ project is preloaded in `include/model/build_models.hpp` with a vanilla HTML/CSS/JavaScript frontend served from `public/`.

## Repository Structure

```
.
├── CMakeLists.txt              # CMake 3.20+ build definitions (core_services, server, challenge_tests)
├── challenge.json              # Platform runtime configuration
├── start.sh                    # Startup and incremental hot-reload script
├── include/
│   ├── httplib.h               # Embedded single-header HTTP server
│   ├── json.hpp                # Single-header JSON parser
│   ├── model/
│   │   └── build_models.hpp    # Data models (Module, DependencyGraph, BuildPlan, BuildResult)
│   └── service/
│       ├── dependency_engine.hpp # Graph validation and dependent discovery interface
│       ├── build_planner.hpp     # Incremental plan generation interface
│       └── build_executor.hpp    # In-memory build execution interface
├── public/
│   ├── index.html              # Workbench web user interface
│   ├── css/
│   │   └── style.css           # Dark workbench stylesheet
│   └── js/
│       └── app.js              # REST client and SVG graph visualization
└── src/
    ├── main.cpp                # HTTP server routes and application coordinator
    └── service/
        ├── dependency_engine.cpp # Graph traversal and cycle detection implementation
        ├── build_planner.cpp     # Rebuild-set calculation and topological sort implementation
        └── build_executor.cpp    # Build execution and state update implementation
```

## Bugs and Bug Locations

1. Incomplete Downstream Dependent Traversal

- Bug location: `src/service/dependency_engine.cpp`, inside `DependencyEngine::get_affected_dependents()` (lines 42–56).
- How to observe it: Mark `core_types` as modified in the default graph and query its affected dependents.
- Failure: Only immediate direct dependents are included in the affected list; reachable transitive dependents such as `config`, `build_cli`, and `app` are omitted because newly discovered nodes are not enqueued with `q.push(dependent)`.
- Expected: All reachable downstream dependents across the full directed dependency graph must be included in the affected set.

2. Undetected Directed Cycles

- Bug location: `src/service/dependency_engine.cpp`, inside `DependencyEngine::dfs_cycle()` (line 81).
- How to observe it: Construct a directed cycle (e.g., `mod_a -> mod_b -> mod_c -> mod_a`) and validate the graph.
- Failure: The cycle is not detected because `state[nodeId]` is prematurely set to `VISITED (2)` before exploring outgoing dependencies instead of `VISITING (1)`, preventing recursive calls from recognizing back-edges.
- Expected: Any directed dependency cycle must be flagged, setting `hasCycle = true` and invalidating the graph.

3. Inverted Topological Build Order

- Bug location: `src/service/build_planner.cpp`, inside `BuildPlanner::compute_topological_order()` (line 99).
- How to observe it: Mark `core_types` as modified and inspect the step order of the generated build plan.
- Failure: The topological order calculated via Kahn's algorithm is reversed with `std::reverse()`, ordering dependents (e.g., `parser`, `config`) before their prerequisites (`core_types`, `string_utils`).
- Expected: Build steps must order every prerequisite module strictly before any module that depends on it.

4. Over-Invalidation of Clean Upstream Modules

- Bug location: `src/service/build_planner.cpp`, inside `BuildPlanner::determine_rebuild_set()` (lines 32–39).
- How to observe it: Mark `parser` as modified and generate an incremental build plan.
- Failure: The rebuild set includes clean upstream prerequisites (`string_utils`, `core_types`) that were never modified and have up-to-date cached artifacts, forcing unnecessary recompilations.
- Expected: The rebuild set must contain only the modified target and its downstream affected dependents, reusing valid cached artifacts.

5. Stale Artifact Revision After Successful Build

- Bug location: `src/service/build_executor.cpp`, inside `BuildExecutor::execute_plan()` (lines 90–99).
- How to observe it: Modify a module's source revision, generate a build plan, and execute the build.
- Failure: Successfully completed build steps omit the state update `mod.artifactRevision = mod.sourceRevision`, leaving the artifact revision behind the source revision and causing the module to remain flagged as out of date.
- Expected: A successfully built module must update its artifact revision to match its source revision.

6. Bypassed Blockage on Prerequisite Failure

- Bug location: `src/service/build_executor.cpp`, inside `BuildExecutor::execute_plan()` (lines 62–70).
- How to observe it: Configure a module (e.g., `parser`) to simulate build failure and execute a plan containing a downstream dependent (`config`).
- Failure: The prerequisite failure check is disabled with `if (false && hasFailedPrereq)`, allowing dependent modules to execute and report `completed` despite broken dependencies.
- Expected: Any module whose prerequisite dependencies failed or were blocked must be marked with status `blocked` and prevented from building.
