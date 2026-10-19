#include <iostream>
#include <string>
#include <cstdlib>
#include <mutex>
#include <fstream>
#include <sstream>

#include "httplib.h"
#include "json.hpp"
#include "model/build_models.hpp"
#include "service/dependency_engine.hpp"
#include "service/build_planner.hpp"
#include "service/build_executor.hpp"

using namespace buildmesh;

class BuildMeshApp {
public:
    BuildMeshApp()
        : graph_(DependencyGraph::create_default_graph()),
          planner_(depEngine_),
          projectRevision_(1) {
        update_state();
    }

    nlohmann::json get_health() {
        return nlohmann::json{
            {"status", "ok"},
            {"service", "BuildMesh Incremental Dependency Engine"},
            {"version", "1.0.0"}
        };
    }

    nlohmann::json get_state() {
        std::lock_guard<std::mutex> lock(mutex_);
        update_state();
        nlohmann::json j;
        to_json(j, state_);
        return j;
    }

    nlohmann::json get_graph() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto val = depEngine_.validate_graph(graph_);
        auto revMap = depEngine_.build_reverse_dependency_map(graph_);

        nlohmann::json nodes = nlohmann::json::array();
        for (const auto& mod : graph_.get_all_modules()) {
            nlohmann::json nodeJson;
            to_json(nodeJson, mod);
            std::vector<std::string> dependents;
            auto it = revMap.find(mod.id);
            if (it != revMap.end()) {
                dependents = it->second;
            }
            nodeJson["dependents"] = dependents;
            nodes.push_back(nodeJson);
        }

        nlohmann::json valJson;
        to_json(valJson, val);

        return nlohmann::json{
            {"modules", nodes},
            {"validation", valJson}
        };
    }

    nlohmann::json mark_source_changed(const std::string& moduleId, bool toggleFailure = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!graph_.has_module(moduleId)) {
            throw std::runtime_error("Module not found: " + moduleId);
        }

        auto& mod = graph_.get_module_mut(moduleId);
        if (toggleFailure) {
            mod.simulateFailure = !mod.simulateFailure;
        } else {
            mod.sourceRevision++;
        }

        lastModifiedModule_ = moduleId;
        projectRevision_++;

        // Recalculate plan with real C++ service
        lastPlan_ = planner_.generate_plan(graph_, lastModifiedModule_);

        update_state();
        nlohmann::json j;
        to_json(j, state_);
        return j;
    }

    nlohmann::json calculate_plan(const std::string& moduleId) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string target = moduleId.empty() ? lastModifiedModule_ : moduleId;
        lastPlan_ = planner_.generate_plan(graph_, target);

        nlohmann::json j;
        to_json(j, lastPlan_);
        return j;
    }

    nlohmann::json execute_build() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lastPlan_.steps.empty() && !lastModifiedModule_.empty()) {
            lastPlan_ = planner_.generate_plan(graph_, lastModifiedModule_);
        }

        latestResult_ = executor_.execute_plan(lastPlan_, graph_);
        update_state();

        nlohmann::json j;
        to_json(j, latestResult_);
        return j;
    }

    nlohmann::json reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        graph_ = DependencyGraph::create_default_graph();
        lastModifiedModule_.clear();
        lastPlan_ = BuildPlan{};
        latestResult_ = BuildResult{};
        projectRevision_ = 1;
        update_state();

        nlohmann::json j;
        to_json(j, state_);
        return j;
    }

private:
    void update_state() {
        state_.modules = graph_.get_all_modules();
        state_.lastModifiedModule = lastModifiedModule_;
        state_.lastPlan = lastPlan_;
        state_.latestResult = latestResult_;
        state_.projectRevision = projectRevision_;
    }

    std::mutex mutex_;
    DependencyGraph graph_;
    DependencyEngine depEngine_;
    BuildPlanner planner_;
    BuildExecutor executor_;
    std::string lastModifiedModule_;
    BuildPlan lastPlan_;
    BuildResult latestResult_;
    int projectRevision_ = 1;
    ProjectState state_;
};

static void set_cors_headers(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

int main() {
    int port = 8080;
    const char* envPort = std::getenv("PORT");
    if (envPort) {
        try {
            port = std::stoi(envPort);
        } catch (...) {
            port = 8080;
        }
    }

    BuildMeshApp app;
    httplib::Server svr;

    // Static files mount
    svr.set_mount_point("/", "./public");

    // Options handler for CORS
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        res.status = 204;
    });

    // GET /api/health
    svr.Get("/api/health", [&app](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        res.set_content(app.get_health().dump(), "application/json");
    });

    // GET /api/state
    svr.Get("/api/state", [&app](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        try {
            res.set_content(app.get_state().dump(), "application/json");
        } catch (const std::exception& e) {
            nlohmann::json err{{"error", e.what()}};
            res.status = 500;
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/graph
    svr.Get("/api/graph", [&app](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        try {
            res.set_content(app.get_graph().dump(), "application/json");
        } catch (const std::exception& e) {
            nlohmann::json err{{"error", e.what()}};
            res.status = 500;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/source-change
    svr.Post("/api/source-change", [&app](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        try {
            auto body = nlohmann::json::parse(req.body);
            if (!body.contains("moduleId")) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Missing 'moduleId' parameter"}}.dump(), "application/json");
                return;
            }
            std::string modId = body["moduleId"].get<std::string>();
            bool toggleFailure = body.value("toggleFailure", false);
            auto state = app.mark_source_changed(modId, toggleFailure);
            res.set_content(state.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/plan
    svr.Post("/api/plan", [&app](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        try {
            std::string modId;
            if (!req.body.empty()) {
                auto body = nlohmann::json::parse(req.body);
                if (body.contains("moduleId")) {
                    modId = body["moduleId"].get<std::string>();
                }
            }
            auto plan = app.calculate_plan(modId);
            res.set_content(plan.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/build
    svr.Post("/api/build", [&app](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        try {
            auto result = app.execute_build();
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/reset
    svr.Post("/api/reset", [&app](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        try {
            auto state = app.reset();
            res.set_content(state.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    std::cout << "[BuildMesh] Server starting on 0.0.0.0:" << port << std::endl;
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "[BuildMesh] Failed to bind to 0.0.0.0:" << port << std::endl;
        return 1;
    }

    return 0;
}
