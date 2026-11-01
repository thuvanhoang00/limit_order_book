#include <gtest/gtest.h>
#include <sstream>
#include "../../tools/latency_histogram.hpp"
#include "../../tools/feed_outliers.hpp"

namespace order_book::measurement {
TEST(FeedLatency, ExactNearestRankIncludesSparseOutliersAndRepeatedValues) {
    LatencyHistogram histogram;
    for (unsigned i = 0; i != 990; ++i) histogram.observe(100);
    for (unsigned i = 0; i != 9; ++i) histogram.observe(500'000);
    histogram.observe(10'000'000);
    EXPECT_EQ(histogram.count(), 1000U);
    EXPECT_EQ(histogram.quantile(500), 100U);
    EXPECT_EQ(histogram.quantile(990), 100U);
    EXPECT_EQ(histogram.quantile(999), 500'000U);
    EXPECT_EQ(histogram.quantile(1000), 10'000'000U);
    std::ostringstream out;
    histogram.frequencies(out);
    EXPECT_EQ(out.str(), "[[100,990],[500000,9],[10000000,1]]");
}
TEST(FeedLatency, EmptyAndSingleObservationAndDenseBoundary) {
    LatencyHistogram empty;
    EXPECT_THROW((void)empty.quantile(500), std::invalid_argument);
    EXPECT_NE(empty.summary().find("\"p999_ns\":null"), std::string::npos);
    for (auto ns : {0U, 100'000U, 100'001U}) {
        LatencyHistogram histogram;
        histogram.observe(ns);
        EXPECT_EQ(histogram.quantile(500), ns);
        EXPECT_EQ(histogram.quantile(999), ns);
        EXPECT_THROW((void)histogram.quantile(0), std::invalid_argument);
        EXPECT_THROW((void)histogram.quantile(1001), std::invalid_argument);
    }
}
TEST(FeedLatency, OutlierRetentionIsBoundedAndTracksSchedulerEvidence) {
    FeedOutliers trace("AAPL", 100, 2);
    trace.observe({1, 0, 1, 50, 20, 0, 0, 0, 0, 'A'});
    trace.observe({2, 0, 2, 1000, 100, 0, 1, 0, 0, 'E'});
    trace.observe({3, 0, 3, 200, 190, 0, 0, 1, 0, 'X'});
    trace.observe({4, 0, 4, 300, 290, 0, 0, 0, 0, 'U'});
    const auto json = trace.json();
    EXPECT_NE(json.find("\"observations\":4"), std::string::npos);
    EXPECT_NE(json.find("\"slow_records\":3"), std::string::npos);
    EXPECT_NE(json.find("\"slow_off_cpu_with_switch\":1"), std::string::npos);
    EXPECT_NE(json.find("\"slow_cpu_dominant\":2"), std::string::npos);
    EXPECT_NE(json.find("\"retained\":2"), std::string::npos);
    EXPECT_EQ(json.find("\"record\":3,"), std::string::npos);
    EXPECT_LT(json.find("\"record\":2,"), json.find("\"record\":4,"));
}
}  // namespace order_book::measurement
