#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include "json.hpp"

namespace buildmesh {

struct Module {
    std::string id;
    std::string name;
    std::vector<std::string> dependencies; // dependent -> dependencies (modules this module depends on)
    int durationMs = 150;
    int sourceRevision = 1;
    int artifactRevision = 1;
    bool simulateFailure = false;

    bool is_cached() const {
        return sourceRevision == artifactRevision;
    }
};

inline void to_json(nlohmann::json& j, const Module& m) {
    j = nlohmann::json{
        {"id", m.id},
        {"name", m.name},
        {"dependencies", m.dependencies},
        {"durationMs", m.durationMs},
        {"sourceRevision", m.sourceRevision},
        {"artifactRevision", m.artifactRevision},
        {"simulateFailure", m.simulateFailure},
        {"isCached", m.is_cached()}
    };
}

inline void from_json(const nlohmann::json& j, Module& m) {
    if (j.contains("id")) m.id = j.at("id").get<std::string>();
    if (j.contains("name")) m.name = j.at("name").get<std::string>();
    if (j.contains("dependencies")) m.dependencies = j.at("dependencies").get<std::vector<std::string>>();
    if (j.contains("durationMs")) m.durationMs = j.at("durationMs").get<int>();
    if (j.contains("sourceRevision")) m.sourceRevision = j.at("sourceRevision").get<int>();
    if (j.contains("artifactRevision")) m.artifactRevision = j.at("artifactRevision").get<int>();
    if (j.contains("simulateFailure")) m.simulateFailure = j.at("simulateFailure").get<bool>();
}

class DependencyGraph {
public:
    std::unordered_map<std::string, Module> modules;

    bool has_module(const std::string& id) const {
        return modules.find(id) != modules.end();
    }

    const Module& get_module(const std::string& id) const {
        auto it = modules.find(id);
        if (it == modules.end()) {
            throw std::runtime_error("Module not found: " + id);
        }
        return it->second;
    }

    Module& get_module_mut(const std::string& id) {
        auto it = modules.find(id);
        if (it == modules.end()) {
            throw std::runtime_error("Module not found: " + id);
        }
        return it->second;
    }

    void add_or_update_module(const Module& mod) {
        modules[mod.id] = mod;
    }

    std::vector<Module> get_all_modules() const {
        std::vector<Module> list;
        list.reserve(modules.size());
        for (const auto& pair : modules) {
            list.push_back(pair.second);
        }
        std::sort(list.begin(), list.end(), [](const Module& a, const Module& b) {
            return a.id < b.id;
        });
        return list;
    }

    static DependencyGraph create_default_graph() {
        DependencyGraph graph;
        // Preload default demonstration project
        // math_utils depends on core_types
        // string_utils depends on core_types
        // parser depends on string_utils and core_types
        // config depends on parser
        // storage depends on core_types
        // build_graph depends on storage and core_types
        // build_cli depends on config and build_graph
        // reporter depends on build_graph
        // app depends on build_cli and reporter

        graph.add_or_update_module({"core_types", "Core Types", {}, 100, 1, 1, false});
        graph.add_or_update_module({"math_utils", "Math Utilities", {"core_types"}, 120, 1, 1, false});
        graph.add_or_update_module({"string_utils", "String Utilities", {"core_types"}, 110, 1, 1, false});
        graph.add_or_update_module({"parser", "AST Parser", {"string_utils", "core_types"}, 250, 1, 1, false});
        graph.add_or_update_module({"config", "Config Loader", {"parser"}, 140, 1, 1, false});
        graph.add_or_update_module({"storage", "Storage Layer", {"core_types"}, 180, 1, 1, false});
        graph.add_or_update_module({"build_graph", "Graph Engine", {"storage", "core_types"}, 220, 1, 1, false});
        graph.add_or_update_module({"build_cli", "CLI Interface", {"config", "build_graph"}, 160, 1, 1, false});
        graph.add_or_update_module({"reporter", "Build Reporter", {"build_graph"}, 130, 1, 1, false});
        graph.add_or_update_module({"app", "Application Entry", {"build_cli", "reporter"}, 300, 1, 1, false});

        return graph;
    }
};

struct BuildStep {
    std::string moduleId;
    std::string status = "pending"; // pending, building, completed, failed, blocked
    int durationMs = 0;
    std::string errorMessage;
};

inline void to_json(nlohmann::json& j, const BuildStep& s) {
    j = nlohmann::json{
        {"moduleId", s.moduleId},
        {"status", s.status},
        {"durationMs", s.durationMs},
        {"errorMessage", s.errorMessage}
    };
}

inline void from_json(const nlohmann::json& j, BuildStep& s) {
    if (j.contains("moduleId")) s.moduleId = j.at("moduleId").get<std::string>();
    if (j.contains("status")) s.status = j.at("status").get<std::string>();
    if (j.contains("durationMs")) s.durationMs = j.at("durationMs").get<int>();
    if (j.contains("errorMessage")) s.errorMessage = j.at("errorMessage").get<std::string>();
}

struct BuildPlan {
    bool isValid = true;
    std::vector<std::string> affectedModules;
    std::vector<BuildStep> steps;
    int totalEstimatedDurationMs = 0;
    std::vector<std::string> warnings;
};

inline void to_json(nlohmann::json& j, const BuildPlan& p) {
    j = nlohmann::json{
        {"isValid", p.isValid},
        {"affectedModules", p.affectedModules},
        {"steps", p.steps},
        {"totalEstimatedDurationMs", p.totalEstimatedDurationMs},
        {"warnings", p.warnings}
    };
}

inline void from_json(const nlohmann::json& j, BuildPlan& p) {
    if (j.contains("isValid")) p.isValid = j.at("isValid").get<bool>();
    if (j.contains("affectedModules")) p.affectedModules = j.at("affectedModules").get<std::vector<std::string>>();
    if (j.contains("steps")) p.steps = j.at("steps").get<std::vector<BuildStep>>();
    if (j.contains("totalEstimatedDurationMs")) p.totalEstimatedDurationMs = j.at("totalEstimatedDurationMs").get<int>();
    if (j.contains("warnings")) p.warnings = j.at("warnings").get<std::vector<std::string>>();
}

struct BuildResult {
    std::string status = "idle"; // idle, success, failed, blocked
    std::vector<std::string> completedModules;
    std::vector<std::string> failedModules;
    std::vector<std::string> blockedModules;
    int totalDurationMs = 0;
    std::vector<BuildStep> steps;
};

inline void to_json(nlohmann::json& j, const BuildResult& r) {
    j = nlohmann::json{
        {"status", r.status},
        {"completedModules", r.completedModules},
        {"failedModules", r.failedModules},
        {"blockedModules", r.blockedModules},
        {"totalDurationMs", r.totalDurationMs},
        {"steps", r.steps}
    };
}

inline void from_json(const nlohmann::json& j, BuildResult& r) {
    if (j.contains("status")) r.status = j.at("status").get<std::string>();
    if (j.contains("completedModules")) r.completedModules = j.at("completedModules").get<std::vector<std::string>>();
    if (j.contains("failedModules")) r.failedModules = j.at("failedModules").get<std::vector<std::string>>();
    if (j.contains("blockedModules")) r.blockedModules = j.at("blockedModules").get<std::vector<std::string>>();
    if (j.contains("totalDurationMs")) r.totalDurationMs = j.at("totalDurationMs").get<int>();
    if (j.contains("steps")) r.steps = j.at("steps").get<std::vector<BuildStep>>();
}

struct ProjectState {
    std::vector<Module> modules;
    std::string lastModifiedModule;
    BuildPlan lastPlan;
    BuildResult latestResult;
    int projectRevision = 1;
};

inline void to_json(nlohmann::json& j, const ProjectState& s) {
    size_t modifiedCount = 0;
    size_t cachedCount = 0;
    for (const auto& m : s.modules) {
        if (!m.is_cached()) {
            modifiedCount++;
        } else {
            cachedCount++;
        }
    }

    j = nlohmann::json{
        {"modules", s.modules},
        {"lastModifiedModule", s.lastModifiedModule},
        {"lastPlan", s.lastPlan},
        {"latestResult", s.latestResult},
        {"projectRevision", s.projectRevision},
        {"metrics", {
            {"totalModules", s.modules.size()},
            {"modifiedModules", modifiedCount},
            {"affectedModules", s.lastPlan.affectedModules.size()},
            {"cachedArtifacts", cachedCount},
            {"lastBuildDurationMs", s.latestResult.totalDurationMs}
        }}
    };
}

} // namespace buildmesh
