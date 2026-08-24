#include "statistics.hpp"
#include "fmt.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

using json = nlohmann::json;

descriptive_stats compute_descriptive_stats(const std::vector<double>& scores) {
    descriptive_stats s;
    s.count = scores.size();
    if (scores.empty()) {
        return s;
    }

    std::vector<double> sorted = scores;
    std::sort(sorted.begin(), sorted.end());
    s.min = sorted.front();
    s.max = sorted.back();

    double sum = 0.0;
    for (double v : sorted) {
        sum += v;
    }
    s.mean = sum / static_cast<double>(sorted.size());

    auto percentile = [&](double p) {
        if (sorted.size() == 1) {
            return sorted[0];
        }
        double rank = (p / 100.0) * static_cast<double>(sorted.size() - 1);
        size_t lo = static_cast<size_t>(std::floor(rank));
        size_t hi = static_cast<size_t>(std::ceil(rank));
        double frac = rank - static_cast<double>(lo);
        return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
    };
    s.p10 = percentile(10.0);
    s.median = percentile(50.0);
    s.p90 = percentile(90.0);

    if (sorted.size() >= 2) {
        double sq = 0.0;
        for (double v : sorted) {
            double d = v - s.mean;
            sq += d * d;
        }
        s.stddev = std::sqrt(sq / static_cast<double>(sorted.size() - 1));
    }
    return s;
}

std::string descriptive_stats::render_line() const {
    std::ostringstream out;
    out << "n=" << nfmt::format_int(static_cast<long long>(count))
        << " mean=" << nfmt::format_number(mean)
        << " median=" << nfmt::format_number(median)
        << " p10=" << nfmt::format_number(p10)
        << " p90=" << nfmt::format_number(p90)
        << " min=" << nfmt::format_number(min)
        << " max=" << nfmt::format_number(max)
        << " stddev=" << nfmt::format_number(stddev);
    return out.str();
}

json descriptive_stats::render_json() const {
    json j;
    j["count"] = count;
    j["mean"] = mean;
    j["median"] = median;
    j["p10"] = p10;
    j["p90"] = p90;
    j["min"] = min;
    j["max"] = max;
    j["stddev"] = stddev;
    return j;
}

optimality_distribution::optimality_distribution(std::vector<strategy_result> results)
    : results_(std::move(results)) {
    if (results_.empty()) {
        optimal_score_ = 0.0;
        return;
    }
    auto best = std::max_element(
        results_.begin(), results_.end(),
        [](const strategy_result& a, const strategy_result& b) {
            return a.score < b.score;
        });
    optimal_score_ = best->score;
    optimal_label_ = best->label;
}

size_t optimality_distribution::count_within(double percent) const {
    if (optimal_score_ == 0.0) {
        return results_.size();
    }
    double threshold = optimal_score_ * (1.0 - percent / 100.0);
    size_t count = 0;
    for (const auto& r : results_) {
        if (r.score >= threshold) {
            ++count;
        }
    }
    return count;
}

std::string optimality_distribution::render_line() const {
    std::ostringstream out;
    out << "strategies=" << nfmt::format_int(static_cast<long long>(results_.size()))
        << " optimal=" << nfmt::format_number(optimal_score_) << " (" << optimal_label_ << ")\n";
    for (double p : thresholds_) {
        size_t count = count_within(p);
        double pct = results_.empty() ? 0.0 : 100.0 * static_cast<double>(count) / static_cast<double>(results_.size());
        out << "  within " << static_cast<int>(p) << "% of optimal: "
            << nfmt::format_int(static_cast<long long>(count)) << " ("
            << nfmt::format_number(pct) << "%)\n";
    }
    return out.str();
}

json optimality_distribution::render_json() const {
    json j;
    j["strategy_count"] = results_.size();
    j["optimal_score"] = optimal_score_;
    j["optimal_label"] = optimal_label_;
    json thresholds = json::array();
    for (double p : thresholds_) {
        json t;
        t["percent"] = p;
        t["count"] = count_within(p);
        thresholds.push_back(std::move(t));
    }
    j["thresholds"] = std::move(thresholds);
    return j;
}
