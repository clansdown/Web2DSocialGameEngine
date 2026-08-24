#include "config_loader.hpp"
#include "report.hpp"
#include "manor/manor_analyzer.hpp"
#include "manor/manor_strategy.hpp"
#include "manor/network.hpp"
#include "manor/units.hpp"
#include "td/td_analyzer.hpp"
#include "rts/rts_analyzer.hpp"
#include "weeding/weeding_analyzer.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

void usage(const char* prog) {
    std::cout
        << "game_balance_analyzer - analyze Ravenest game config balance.\n\n"
        << "Usage:\n"
        << "  " << prog << " [options] <module>...\n\n"
        << "Modules:\n"
        << "  manor     Static manor economy analysis\n"
        << "  td        Tower defense static analysis\n"
        << "  rts       RTS combat static analysis\n"
        << "  weeding   Weeding static analysis\n"
        << "  all       Run all static analyses\n\n"
        << "Options:\n"
        << "  --config-dir <path>  Config root (default: auto-resolved from CWD)\n"
        << "  --sim                Run the manor strategy simulation\n"
        << "  --sim-trace          With --sim, print day-by-day building & resource counts\n"
        << "                       for the highest-scoring run (alternating row backgrounds)\n"
        << "  --iterations N       Repeats per heuristic + Monte-Carlo samples (default 2000)\n"
        << "  --days N             Simulation horizon in days (default 30)\n"
        << "  --seed N             RNG seed for the simulation (default: random)\n"
        << "  --scales N           Production-network anchor scales (default 5)\n"
        << "  --build-slots N      Concurrent construction slots (default 3)\n"
        << "  --day-seconds N      Construction seconds per simulated day (default 60)\n"
        << "  --stages N           Production-network stages to analyze (default: all)\n"
        << "  --quiet              Suppress sim progress feedback on stderr\n"
        << "  --json               Emit JSON instead of text\n"
        << "  --out <file>         Write output to file\n"
        << "  -h, --help           Show this help\n\n"
        << "Value-taking flags accept either '--flag value' or '--flag=value'.\n"
        << "Run with no arguments or -h/--help to show this help.\n";
}

struct cli_options {
    std::string config_dir = "../game/config";
    bool config_dir_explicit = false;
    bool run_sim = false;
    bool sim_trace = false;
    int iterations = 2000;
    int days = 30;
    std::optional<std::uint64_t> seed;
    int scales = 5;
    int build_slots = 3;
    int day_seconds = 60;
    int max_stages = 0;
    bool quiet = false;
    bool json_output = false;
    std::string out_file;
    std::vector<std::string> modules;
};

// Parses a positive integer flag value from args[i+1]; on success stores it in
// `out`, advances i, and returns true. On malformed or non-positive input it
// prints an error and returns false.
bool parse_positive_int(const std::vector<std::string>& args, size_t& i,
                        const std::string& flag, int& out) {
    if (i + 1 >= args.size()) {
        std::cerr << "Missing value for " << flag << "\n";
        return false;
    }
    try {
        int v = std::stoi(args[i + 1]);
        if (v <= 0) {
            std::cerr << "Invalid value for " << flag << ": '" << args[i + 1] << "'\n";
            return false;
        }
        out = v;
        ++i;
        return true;
    } catch (const std::exception&) {
        std::cerr << "Invalid value for " << flag << ": '" << args[i + 1] << "'\n";
        return false;
    }
}

// When --config-dir is not given, resolve the config directory by walking up
// from the current working directory looking for a `<root>/game/config`
// directory. This lets the binary be invoked from anywhere inside (or above)
// the repository without needing a hard-coded relative path. Returns nullopt if
// no candidate is found, in which case the caller falls back to the legacy
// relative default (which yields the existing "config directory not found"
// error for genuinely misplaced runs).
std::optional<std::string> find_config_dir_from_cwd() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::current_path(ec);
    if (ec) {
        return std::nullopt;
    }
    while (true) {
        fs::path candidate = dir / "game" / "config";
        if (fs::is_directory(candidate, ec) && !ec) {
            return candidate.string();
        }
        ec.clear();
        fs::path parent = dir.parent_path();
        if (parent == dir) {
            break;  // reached the filesystem root
        }
        dir = parent;
    }
    return std::nullopt;
}

// Flags that take a value, supported in both "--flag value" and "--flag=value"
// forms. The equals form is normalized into two arguments before parsing.
const std::set<std::string> kValueFlags = {
    "--config-dir", "--iterations", "--days", "--seed",
    "--scales", "--build-slots", "--day-seconds", "--stages", "--out",
};

} // namespace

int main(int argc, char** argv) {
    cli_options opts;
    const std::vector<std::string> raw_args(argv + 1, argv + argc);

    // Normalize "--flag=value" into "--flag", "value" so every value-taking
    // branch reads the value from the next argument as today. Splitting only
    // applies to known value flags; boolean flags passed with '=' are left
    // intact and rejected by the unknown-option path below.
    std::vector<std::string> args;
    for (const auto& raw : raw_args) {
        if (raw.size() > 2 && raw[0] == '-' && raw[1] == '-') {
            size_t eq = raw.find('=');
            if (eq != std::string::npos) {
                std::string name = raw.substr(0, eq);
                if (kValueFlags.count(name)) {
                    args.push_back(name);
                    args.push_back(raw.substr(eq + 1));
                    continue;
                }
            }
        }
        args.push_back(raw);
    }

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else if (a == "--config-dir") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for --config-dir\n";
                return 2;
            }
            opts.config_dir = args[++i];
            opts.config_dir_explicit = true;
        } else if (a == "--sim") {
            opts.run_sim = true;
        } else if (a == "--sim-trace") {
            opts.sim_trace = true;
        } else if (a == "--iterations") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for --iterations\n";
                return 2;
            }
            opts.iterations = std::stoi(args[++i]);
        } else if (a == "--days") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for --days\n";
                return 2;
            }
            opts.days = std::stoi(args[++i]);
        } else if (a == "--seed") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for --seed\n";
                return 2;
            }
            const std::string& v = args[++i];
            try {
                size_t consumed = 0;
                opts.seed = std::stoull(v, &consumed, 10);
                if (consumed != v.size()) {
                    std::cerr << "Invalid value for --seed: '" << v << "'\n";
                    return 2;
                }
            } catch (const std::exception&) {
                std::cerr << "Invalid value for --seed: '" << v << "'\n";
                return 2;
            }
        } else if (a == "--scales") {
            if (!parse_positive_int(args, i, a, opts.scales)) {
                return 2;
            }
        } else if (a == "--build-slots") {
            if (!parse_positive_int(args, i, a, opts.build_slots)) {
                return 2;
            }
        } else if (a == "--day-seconds") {
            if (!parse_positive_int(args, i, a, opts.day_seconds)) {
                return 2;
            }
        } else if (a == "--stages") {
            if (!parse_positive_int(args, i, a, opts.max_stages)) {
                return 2;
            }
        } else if (a == "--quiet") {
            opts.quiet = true;
        } else if (a == "--json") {
            opts.json_output = true;
        } else if (a == "--out") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for --out\n";
                return 2;
            }
            opts.out_file = args[++i];
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n";
            usage(argv[0]);
            return 2;
        } else {
            opts.modules.push_back(a);
        }
    }

    // Running with no arguments shows the help text (same as -h/--help).
    if (args.empty()) {
        usage(argv[0]);
        return 0;
    }

    // --sim-trace only makes sense with --sim; warn and ignore otherwise.
    if (opts.sim_trace && !opts.run_sim) {
        std::cerr << "warning: --sim-trace requires --sim; ignoring\n";
        opts.sim_trace = false;
    }

    if (opts.modules.empty()) {
        opts.modules = {"all"};
    }

    // Resolve modules set.
    std::set<std::string> wanted;
    for (const auto& m : opts.modules) {
        if (m == "all") {
            wanted = {"manor", "td", "rts", "weeding"};
        } else if (m == "manor" || m == "td" || m == "rts" || m == "weeding") {
            wanted.insert(m);
        } else {
            std::cerr << "Unknown module: " << m << "\n";
            usage(argv[0]);
            return 2;
        }
    }

    config_loader loader;
    std::string err;
    if (!opts.config_dir_explicit) {
        // Smart resolution: walk up from the CWD looking for <root>/game/config.
        // Falls back to the legacy relative default if not found.
        if (auto found = find_config_dir_from_cwd()) {
            opts.config_dir = *found;
        }
    }
    if (!loader.set_config_dir(opts.config_dir, err)) {
        std::cerr << "Error: " << err << "\n";
        return 1;
    }

    std::ostringstream text;
    nlohmann::json all_json = nlohmann::json::object();

    if (wanted.count("manor")) {
        // Static manor analysis is skipped in --sim mode (which outputs only the
        // strategy simulation).
        if (!opts.run_sim) {
            manor_analyzer manor(loader);
            auto rep = manor.analyze();
            if (opts.json_output) {
                all_json["manor"] = rep.render_json();
            } else {
                text << rep.render_text("Manor balance analysis") << "\n";
            }
        }
        if (opts.run_sim) {
            manor_sim_config sc;
            sc.iterations = opts.iterations;
            sc.days = opts.days;
            sc.seed = opts.seed;
            sc.quiet = opts.quiet;
            sc.record_top_trace = opts.sim_trace;
            sc.color = ::isatty(STDOUT_FILENO) != 0;
            manor_strategy_sim sim(loader, sc);
            auto outcomes = sim.run_all();
            // Always print the effective seed so a run can be reproduced, even
            // in JSON mode.
            std::cerr << "simulation seed: " << sim.seed_used()
                      << "  (re-run with --seed " << sim.seed_used() << " to reproduce)\n";
            if (opts.json_output) {
                all_json["manor_simulation"] = sim.render_json(outcomes);
            } else {
                text << sim.render(outcomes) << "\n";
            }
        }

        // Production-network ratio analysis. Skipped in --sim mode (which
        // outputs only the strategy simulation).
        if (!opts.run_sim) {
            network_config nc;
            nc.scales = opts.scales;
            nc.build_slots = opts.build_slots;
            nc.day_seconds = static_cast<double>(opts.day_seconds);
            nc.max_stages = opts.max_stages;
            manor_network_analyzer network(loader, nc);
            auto stages = network.analyze();
            if (opts.json_output) {
                all_json["manor_network"] = network.render_json(stages);
            } else {
                text << network.render(stages) << "\n";
            }
        }

        // Units analysis: per-building, per-level net production assuming all
        // inputs and maintenance are imported. Skipped in --sim mode.
        if (!opts.run_sim) {
            manor_units_analyzer units(loader);
            auto unit_rows = units.analyze();
            if (opts.json_output) {
                all_json["manor_units"] = units.render_json(unit_rows);
            } else {
                text << units.render(unit_rows) << "\n";
            }
        }
    }
    if (wanted.count("td")) {
        td_analyzer td(loader);
        auto rep = td.analyze();
        if (opts.json_output) {
            all_json["td"] = rep.render_json();
        } else {
            text << rep.render_text("Tower Defense balance analysis") << "\n";
        }
    }
    if (wanted.count("rts")) {
        rts_analyzer rts(loader);
        auto rep = rts.analyze();
        if (opts.json_output) {
            all_json["rts"] = rep.render_json();
        } else {
            text << rep.render_text("RTS Combat balance analysis") << "\n";
        }
    }
    if (wanted.count("weeding")) {
        weeding_analyzer wd(loader);
        auto rep = wd.analyze();
        if (opts.json_output) {
            all_json["weeding"] = rep.render_json();
        } else {
            text << rep.render_text("Weeding balance analysis") << "\n";
        }
    }

    std::string output = opts.json_output ? all_json.dump(2) : text.str();
    if (!opts.out_file.empty()) {
        std::ofstream out(opts.out_file);
        if (!out) {
            std::cerr << "Error: cannot write to " << opts.out_file << "\n";
            return 1;
        }
        out << output;
    } else {
        std::cout << output;
    }

    return 0;
}
