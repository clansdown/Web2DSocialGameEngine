#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// A structured finding produced by a balance analyzer module.
struct balance_finding {
    enum class severity {
        info,
        warning,
        critical,
    };

    severity level = severity::info;
    std::string title;    // Short human/machine label for the finding.
    std::string detail;   // Longer human-readable explanation.
    double value = 0.0;   // Optional numeric value (e.g. an efficiency ratio).
    std::string ref;      // Optional reference id (building/unit/plant id).
};

std::string severity_name(balance_finding::severity s);

// A report collecting findings for one game module.
class balance_report {
public:
    void add(balance_finding finding);

    void add_info(const std::string& title, const std::string& detail,
                  const std::string& ref = "");
    void add_warning(const std::string& title, const std::string& detail,
                     const std::string& ref = "");
    void add_critical(const std::string& title, const std::string& detail,
                      const std::string& ref = "");

    const std::vector<balance_finding>& findings() const { return findings_; }
    bool empty() const { return findings_.empty(); }

    // Renders the report as a plain-text block with a section heading.
    std::string render_text(const std::string& heading) const;

    // Renders the report as a JSON array of findings.
    nlohmann::json render_json() const;

private:
    std::vector<balance_finding> findings_;
};
