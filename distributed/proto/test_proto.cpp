#include <iostream>
#include "crackstore.pb.h"
#include "crackstore.grpc.pb.h"

int main() {
    std::cout << "=== Protobuf/gRPC Generation Test ===\n\n";
    
    // Test 1: Create and populate messages
    std::cout << "Test: Message creation...  ";
    
    crackstore::LoadColumnRequest load_req;
    load_req.set_column_name("test_column");
    load_req. add_data(10);
    load_req.add_data(20);
    load_req.add_data(30);
    
    if (load_req.column_name() != "test_column") {
        std::cerr << "FAILED\n";
        return 1;
    }
    if (load_req. data_size() != 3) {
        std::cerr << "FAILED\n";
        return 1;
    }
    std::cout << "PASSED\n";
    
    // Test 2: Range query message
    std::cout << "Test: RangeQueryRequest...  ";
    
    crackstore::RangeQueryRequest range_req;
    range_req.set_column_name("prices");
    range_req.set_low(100);
    range_req.set_high(500);
    
    if (range_req.low() != 100 || range_req. high() != 500) {
        std::cerr << "FAILED\n";
        return 1;
    }
    std::cout << "PASSED\n";
    
    // Test 3: Response with stats
    std::cout << "Test: RangeQueryResponse with stats... ";
    
    crackstore::RangeQueryResponse range_resp;
    range_resp.set_count(42);
    range_resp.set_node_id("node-1");
    range_resp.set_success(true);
    
    auto* stats = range_resp.mutable_stats();
    stats->set_tuples_touched(1000);
    stats->set_cracks_used(5);
    stats->set_query_time_ms(2.5);
    
    if (range_resp.count() != 42) {
        std::cerr << "FAILED\n";
        return 1;
    }
    if (range_resp.stats().tuples_touched() != 1000) {
        std::cerr << "FAILED\n";
        return 1;
    }
    std::cout << "PASSED\n";
    
    // Test 4: Distributed query response
    std::cout << "Test: DistributedRangeQueryResponse... ";
    
    crackstore::DistributedRangeQueryResponse dist_resp;
    dist_resp.set_total_count(100);
    dist_resp.set_nodes_queried(3);
    dist_resp. set_success(true);
    
    for (int i = 0; i < 3; ++i) {
        auto* node_result = dist_resp.add_node_results();
        node_result->set_node_id("node-" + std::to_string(i));
        node_result->set_count(33 + (i == 0 ? 1 : 0));  // 34 + 33 + 33 = 100
    }
    
    if (dist_resp.node_results_size() != 3) {
        std::cerr << "FAILED\n";
        return 1;
    }
    std::cout << "PASSED\n";
    
    // Test 5: Cluster status
    std::cout << "Test: ClusterStatusResponse... ";
    
    crackstore::ClusterStatusResponse cluster_status;
    cluster_status.set_total_nodes(5);
    cluster_status.set_healthy_nodes(4);
    
    for (int i = 0; i < 5; ++i) {
        auto* node = cluster_status.add_nodes();
        node->set_node_id("node-" + std::to_string(i));
        node->set_address("localhost");
        node->set_port(50051 + i);
        node->set_is_healthy(i != 2);  // node-2 is unhealthy
        node->add_columns("column_a");
    }
    
    if (cluster_status.nodes_size() != 5) {
        std::cerr << "FAILED\n";
        return 1;
    }
    std::cout << "PASSED\n";
    
    // Test 6: Verify service stubs exist (compile-time check)
    std::cout << "Test: Service stubs generated... ";
    
    // These will fail to compile if proto generation is broken
    using StorageStub = crackstore::StorageService::Stub;
    using CoordinatorStub = crackstore::CoordinatorService::Stub;
    
    std::cout << "PASSED\n";
    
    std::cout << "\n=== All Proto Tests Passed ===\n\n";
    return 0;
}