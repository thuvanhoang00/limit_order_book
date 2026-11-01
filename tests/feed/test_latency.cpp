#include <gtest/gtest.h>
#include <sstream>
#include "../../tools/latency_histogram.hpp"

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
}  // namespace order_book::measurement
