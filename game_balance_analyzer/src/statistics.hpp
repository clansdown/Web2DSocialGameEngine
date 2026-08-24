#pragma once

#include <nlohmann/json.hpp>
#include <cstddef>
#include <string>
#include <vector>

// Descriptive statistics of a population of scores (e.g. the repeated outcomes
// of one heuristic strategy). All quantities are derived from the population.
struct descriptive_stats {
    size_t count = 0;     // number of outcomes
    double mean = 0.0;    // arithmetic mean
    double median = 0.0;  // 50th percentile
    double p10 = 0.0;     // 10th percentile
    double p90 = 0.0;     // 90th percentile
    double min = 0.0;     // minimum
    double max = 0.0;     // maximum
    double stddev = 0.0;  // sample standard deviation (n-1)

    std::string render_line() const;
    nlohmann::json render_json() const;
};

// Computes descriptive_stats from a population of scores.
descriptive_stats compute_descriptive_stats(const std::vector<double>& scores);

// A single scored strategy result: a label plus a numeric score (higher is
// better). Used to compute optimality distributions.
struct strategy_result {
    std::string label;
    double score = 0.0;
};

// Computes the "within X% of optimal" distribution for a set of scored
// strategies.
//
// The optimal score is the maximum over the set. A strategy is "within P% of
// optimal" when its score is at least (1 - P/100) * optimal. The percentages
// reported are {0, 1, 2, 3, 4, 5, 10}, where 0% means exactly the optimal
// score (ties included).
//
// Returns a list of {percent, count} pairs for each threshold in ascending
// order. Counts are cumulative (a strategy within 2% is also within 3%).
class optimality_distribution {
public:
    optimality_distribution() = default;

    // Builds the distribution from scored strategies.
    explicit optimality_distribution(std::vector<strategy_result> results);

    double optimal_score() const { return optimal_score_; }
    std::string optimal_label() const { return optimal_label_; }
    size_t strategy_count() const { return results_.size(); }

    // Number of strategies at-or-better than the given percent-of-optimal.
    size_t count_within(double percent) const;

    // Renders a human-readable summary line.
    std::string render_line() const;

    // Renders as JSON: { optimal: {...}, thresholds: [...] }.
    nlohmann::json render_json() const;

private:
    std::vector<strategy_result> results_;
    double optimal_score_ = 0.0;
    std::string optimal_label_;
    std::vector<double> thresholds_ = {0, 1, 2, 3, 4, 5, 10};
};
