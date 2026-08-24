#include "report.hpp"
#include "fmt.hpp"

#include <sstream>

using json = nlohmann::json;

std::string severity_name(balance_finding::severity s) {
    switch (s) {
        case balance_finding::severity::info: return "info";
        case balance_finding::severity::warning: return "warning";
        case balance_finding::severity::critical: return "critical";
    }
    return "info";
}

void balance_report::add(balance_finding finding) {
    findings_.push_back(std::move(finding));
}

void balance_report::add_info(const std::string& title, const std::string& detail,
                              const std::string& ref) {
    balance_finding f;
    f.level = balance_finding::severity::info;
    f.title = title;
    f.detail = detail;
    f.ref = ref;
    findings_.push_back(std::move(f));
}

void balance_report::add_warning(const std::string& title, const std::string& detail,
                                 const std::string& ref) {
    balance_finding f;
    f.level = balance_finding::severity::warning;
    f.title = title;
    f.detail = detail;
    f.ref = ref;
    findings_.push_back(std::move(f));
}

void balance_report::add_critical(const std::string& title, const std::string& detail,
                                  const std::string& ref) {
    balance_finding f;
    f.level = balance_finding::severity::critical;
    f.title = title;
    f.detail = detail;
    f.ref = ref;
    findings_.push_back(std::move(f));
}

std::string balance_report::render_text(const std::string& heading) const {
    std::ostringstream out;
    out << heading << "\n";
    out << std::string(heading.size(), '-') << "\n";
    if (findings_.empty()) {
        out << "  (no findings)\n";
        return out.str();
    }
    for (const auto& f : findings_) {
        out << "  [" << severity_name(f.level) << "] " << f.title;
        if (!f.ref.empty()) {
            out << "  (" << f.ref << ")";
        }
        out << "\n      " << f.detail;
        if (f.value != 0.0) {
            out << "  [value=" << nfmt::format_number(f.value) << "]";
        }
        out << "\n";
    }
    return out.str();
}

json balance_report::render_json() const {
    json arr = json::array();
    for (const auto& f : findings_) {
        json j;
        j["severity"] = severity_name(f.level);
        j["title"] = f.title;
        j["detail"] = f.detail;
        j["value"] = f.value;
        j["ref"] = f.ref;
        arr.push_back(std::move(j));
    }
    return arr;
}
