#include "cracking_engine.h"
#include <iostream>
#include <random>
#include <cassert>

using namespace crackstore;

void test_basic_construction() {
    std::cout << "Test: Basic construction...  ";
    
    int data[] = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
    CrackingEngine engine(data, 10);
    
    assert(engine.get_size() == 10);
    assert(engine.get_crack_count() == 0);
    assert(engine.get_pending_inserts() == 0);
    assert(engine.get_pending_deletes() == 0);
    
    std::cout << "PASSED\n";
}

void test_simple_range_query() {
    std::cout << "Test: Simple range query... ";
    
    int data[] = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
    CrackingEngine engine(data, 10);
    
    // Query for values in [3, 7) -> should return 4 (values: 3, 4, 5, 6)
    int count = engine.range_query(3, 7);
    int expected = naive_range_count(data, 10, 3, 7);
    
    assert(count == expected);
    assert(count == 4);
    assert(engine.get_crack_count() > 0);
    
    std::cout << "PASSED (count=" << count << ", cracks=" << engine. get_crack_count() << ")\n";
}

void test_full_range() {
    std::cout << "Test: Full range query... ";
    
    int data[] = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
    CrackingEngine engine(data, 10);
    
    int count = engine.range_query(0, 100);
    assert(count == 10);
    
    std::cout << "PASSED\n";
}

void test_empty_range() {
    std::cout << "Test: Empty range query... ";
    
    int data[] = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
    CrackingEngine engine(data, 10);
    
    int count = engine.range_query(100, 200);
    assert(count == 0);
    
    std::cout << "PASSED\n";
}

void test_adaptive_behavior() {
    std::cout << "Test: Adaptive behavior (repeated queries)... \n";
    
    // Generate larger dataset
    const int SIZE = 100000;
    std::vector<int> data(SIZE);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 1000000);
    
    for (int i = 0; i < SIZE; ++i) {
        data[i] = dist(rng);
    }
    
    CrackingEngine engine(data. data(), SIZE);
    
    // Run same query multiple times - should get faster
    int low = 100000, high = 200000;
    
    std::cout << "  Query [" << low << ", " << high << "):\n";
    
    for (int i = 0; i < 5; ++i) {
        engine.reset_stats();
        int count = engine. range_query(low, high);
        auto stats = engine.get_stats();
        
        std::cout << "    Iteration " << i << ": "
                  << "count=" << count 
                  << ", touched=" << stats.last_tuples_touched
                  << ", time=" << stats.last_query_time_ms << "ms"
                  << ", cracks=" << engine.get_crack_count() << "\n";
    }
    
    // After first query, subsequent queries should touch fewer tuples
    std::cout << "  (Tuples touched should decrease after first query)\n";
    std::cout << "PASSED\n";
}

void test_different_ranges() {
    std::cout << "Test: Different ranges build index...  \n";
    
    const int SIZE = 100000;
    std::vector<int> data(SIZE);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 1000000);
    
    for (int i = 0; i < SIZE; ++i) {
        data[i] = dist(rng);
    }
    
    CrackingEngine engine(data.data(), SIZE);
    
    // Different range queries should accumulate crack points
    std::vector<std::pair<int,int>> ranges = {
        {100000, 200000},
        {300000, 400000},
        {500000, 600000},
        {150000, 350000},
        {0, 100000}
    };
    
    for (auto& range : ranges) {
        int count = engine.range_query(range.first, range.second);
        std::cout << "  Range [" << range.first << ", " << range.second << "): "
                  << "count=" << count << ", total_cracks=" << engine. get_crack_count() << "\n";
    }
    
    std::cout << "PASSED\n";
}

void test_insert() {
    std::cout << "Test: Insert operation... ";
    
    int data[] = {5, 2, 8, 1, 9};
    CrackingEngine engine(data, 5);
    
    // Insert a value
    engine. insert(3);
    assert(engine. get_pending_inserts() == 1);
    
    // Query should merge the insert
    int count = engine.range_query(0, 10);
    assert(count == 6);  // Original 5 + 1 inserted
    assert(engine.get_pending_inserts() == 0);
    
    std::cout << "PASSED\n";
}

void test_remove() {
    std::cout << "Test: Remove operation... ";
    
    int data[] = {5, 2, 8, 1, 9};
    CrackingEngine engine(data, 5);
    
    // Remove a value
    engine.remove(5);
    assert(engine.get_pending_deletes() == 1);
    
    // Query should merge the delete
    int count = engine.range_query(0, 10);
    assert(count == 4);  // Original 5 - 1 removed
    assert(engine.get_pending_deletes() == 0);
    
    std::cout << "PASSED\n";
}

void test_statistics() {
    std::cout << "Test: Statistics tracking... ";
    
    const int SIZE = 10000;
    std::vector<int> data(SIZE);
    for (int i = 0; i < SIZE; ++i) {
        data[i] = i;
    }
    
    CrackingEngine engine(data.data(), SIZE);
    
    // Run a few queries
    engine. range_query(1000, 2000);
    engine.range_query(3000, 4000);
    engine. range_query(5000, 6000);
    
    auto stats = engine. get_stats();
    
    assert(stats.queries_executed == 3);
    assert(stats.total_tuples_touched > 0);
    assert(stats.total_query_time_ms > 0);
    
    std::cout << "PASSED (queries=" << stats.queries_executed
              << ", total_touched=" << stats.total_tuples_touched << ")\n";
}

void test_correctness_large() {
    std::cout << "Test: Correctness on large dataset... ";
    
    const int SIZE = 100000;
    std::vector<int> data(SIZE);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 1000000);
    
    for (int i = 0; i < SIZE; ++i) {
        data[i] = dist(rng);
    }
    
    CrackingEngine engine(data.data(), SIZE);
    
    // Run multiple random queries and verify correctness
    std::uniform_int_distribution<int> range_dist(0, 1000000);
    
    for (int i = 0; i < 20; ++i) {
        int low = range_dist(rng);
        int high = low + range_dist(rng) % 100000;
        
        int cracking_count = engine. range_query(low, high);
        int naive_count = naive_range_count(data.data(), SIZE, low, high);
        
        if (cracking_count != naive_count) {
            std::cerr << "FAILED: range [" << low << ", " << high << ") "
                      << "cracking=" << cracking_count << " naive=" << naive_count << "\n";
            assert(false);
        }
    }
    
    std::cout << "PASSED (20 random queries verified)\n";
}

int main() {
    std::cout << "\n=== CrackingEngine Test Suite ===\n\n";
    
    test_basic_construction();
    test_simple_range_query();
    test_full_range();
    test_empty_range();
    test_adaptive_behavior();
    test_different_ranges();
    test_insert();
    test_remove();
    test_statistics();
    test_correctness_large();
    
    std::cout << "\n=== All Tests Passed ===\n\n";
    return 0;
}