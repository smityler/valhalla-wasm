#include <emscripten/bind.h>
#include <valhalla/tyr/actor.h>
#include <valhalla/midgard/logging.h>
#include <valhalla/exceptions.h>
#include <iostream>
#include <string>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <sstream>
#include <cstdio>

using namespace emscripten;

// Escape a message for a JSON string, so a quote or newline in it cannot break the reply.
static std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

class ValhallaRouter {
private:
    std::shared_ptr<valhalla::tyr::actor_t> actor;

public:
    ValhallaRouter(const std::string& config_json) {
        try {
            // Parse the JSON config string to bootstrap the engine
            boost::property_tree::ptree pt;
            std::stringstream ss(config_json);
            boost::property_tree::read_json(ss, pt);
            
            actor = std::make_shared<valhalla::tyr::actor_t>(pt, /*auto_cleanup=*/true);
            valhalla::midgard::logging::Configure({{"type", "std_out"}, {"color", "true"}});
            std::cout << "[Valhalla WASM] Engine initialized successfully!" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[Valhalla WASM] Failed to initialize engine: " << e.what() << std::endl;
        }
    }

    // Expose the route method to Javascript
    std::string route(const std::string& request_json) {
        if (!actor) return "{\"error\":\"Engine not initialized\"}";
        try {
            return actor->route(request_json);
        } catch (const valhalla::valhalla_exception_t& e) {
            // Same shape as Valhalla's HTTP service: the numeric code survives, so a caller can
            // tell "no road near this point" (171) from any other failure.
            return std::string("{\"error_code\":") + std::to_string(e.code) +
                   ",\"error\":\"" + json_escape(e.message) +
                   "\",\"status_code\":" + std::to_string(e.http_code) +
                   ",\"status\":\"" + json_escape(e.http_message) + "\"}";
        } catch (const std::exception& e) {
            return std::string("{\"error\":\"") + json_escape(e.what()) + "\"}";
        }
    }
    
    // Other endpoints can be exposed similarly (e.g. locate, isochrone)
};

// Bind the C++ class to Javascript using Embind
EMSCRIPTEN_BINDINGS(valhalla_module) {
    class_<ValhallaRouter>("ValhallaRouter")
        .constructor<const std::string&>()
        .function("route", &ValhallaRouter::route);
}
