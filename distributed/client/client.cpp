#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

#include <grpcpp/grpcpp.h>
#include "crackstore.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using namespace crackstore;



class CrackStoreClient {
public:
    CrackStoreClient(const std::string& coordinator_address) 
        : coordinator_address_(coordinator_address) {
        auto channel = grpc::CreateChannel(coordinator_address, grpc::InsecureChannelCredentials());
        coordinator_stub_ = CoordinatorService::NewStub(channel);
    }

    
    bool GetClusterStatus() {
        ClusterStatusRequest request;
        ClusterStatusResponse response;
        ClientContext context;

        Status status = coordinator_stub_->GetClusterStatus(&context, request, &response);

        if (! status.ok()) {
            std::cerr << "Failed to get cluster status: " << status. error_message() << "\n";
            return false;
        }

        std::cout << "\n=== Cluster Status ===\n";
        std::cout << "Total nodes: " << response. total_nodes() << "\n";
        std::cout << "Healthy nodes: " << response.healthy_nodes() << "\n\n";

        for (const auto& node : response.nodes()) {
            std::cout << "  " << node.node_id() 
                      << " [" << node.address() << ":" << node.port() << "] "
                      << (node.is_healthy() ? "HEALTHY" : "UNHEALTHY")
                      << " (last heartbeat: " << node.last_heartbeat_ms() << "ms ago)\n";
        }
        std::cout << "\n";

        return true;
    }

    
    bool LoadColumnFromFile(const std::string& column_name, 
                            const std::string& file_path,
                            int num_partitions = 0) {
        
        std::cout << "Loading column '" << column_name << "' from " << file_path << "\n";

        // Read binary file
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (! file) {
            std::cerr << "Failed to open file: " << file_path << "\n";
            return false;
        }

        std::streamsize size = file.tellg();
        file. seekg(0, std::ios::beg);

        int num_elements = size / sizeof(int);
        std::vector<int> data(num_elements);

        if (! file.read(reinterpret_cast<char*>(data.data()), size)) {
            std::cerr << "Failed to read file\n";
            return false;
        }

        std::cout << "Read " << num_elements << " integers from file\n";

        // Get node list from coordinator
        ClusterStatusRequest status_request;
        ClusterStatusResponse status_response;
        ClientContext status_context;

        Status status = coordinator_stub_->GetClusterStatus(&status_context, status_request, &status_response);
        if (!status.ok()) {
            std::cerr << "Failed to get cluster status\n";
            return false;
        }

        std::vector<std::pair<std::string, std::unique_ptr<StorageService::Stub>>> nodes;
        for (const auto& node : status_response. nodes()) {
            if (node.is_healthy()) {
                std::string target = node.address() + ":" + std::to_string(node.port());
                auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
                nodes.emplace_back(node.node_id(), StorageService::NewStub(channel));
            }
        }

        if (nodes.empty()) {
            std::cerr << "No healthy nodes available\n";
            return false;
        }

        int actual_partitions = (num_partitions > 0) ? 
            std::min(num_partitions, (int)nodes.size()) : nodes.size();

        std::cout << "Distributing to " << actual_partitions << " nodes.. .\n";

        
        int elements_per_node = num_elements / actual_partitions;
        int remainder = num_elements % actual_partitions;

        int offset = 0;
        for (int i = 0; i < actual_partitions; ++i) {
            int count = elements_per_node + (i < remainder ?  1 : 0);

            LoadColumnRequest request;
            request.set_column_name(column_name);
            for (int j = 0; j < count; ++j) {
                request.add_data(data[offset + j]);
            }

            LoadColumnResponse response;
            ClientContext context;
            context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(60));

            Status status = nodes[i]. second->LoadColumn(&context, request, &response);

            if (status. ok() && response. success()) {
                std::cout << "  " << nodes[i].first << ": loaded " << response.rows_loaded() << " rows\n";
            } else {
                std::cerr << "  " << nodes[i].first << ": FAILED\n";
            }

            offset += count;
        }

        std::cout << "Load complete\n\n";
        return true;
    }

    
    bool RangeQuery(const std::string& column_name, int low, int high) {
        std::cout << "Executing range query [" << low << ", " << high << ") on column '" 
                  << column_name << "'\n";

        DistributedRangeQueryRequest request;
        request.set_column_name(column_name);
        request.set_low(low);
        request.set_high(high);
        request.set_return_values(false);

        DistributedRangeQueryResponse response;
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(60));

        auto start = std::chrono::high_resolution_clock::now();
        Status status = coordinator_stub_->RangeQuery(&context, request, &response);
        auto end = std::chrono::high_resolution_clock::now();

        double client_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (! status.ok()) {
            std::cerr << "Query failed: " << status.error_message() << "\n";
            return false;
        }

        std::cout << "\n=== Query Results ===\n";
        std::cout << "Total count: " << response.total_count() << "\n";
        std::cout << "Nodes queried: " << response.nodes_queried() << "\n";
        std::cout << "Server time: " << response.total_time_ms() << " ms\n";
        std::cout << "Client time: " << client_time_ms << " ms\n\n";

        std::cout << "Per-node results:\n";
        for (const auto& result : response.node_results()) {
            std::cout << "  " << result.node_id() << ": count=" << result.count();
            if (result.has_stats()) {
                std::cout << ", touched=" << result.stats().tuples_touched()
                          << ", cracks=" << result.stats(). cracks_used()
                          << ", time=" << result.stats().query_time_ms() << "ms";
            }
            std::cout << "\n";
        }
        std::cout << "\n";

        return true;
    }

    
    bool RunBenchmark(const std::string& column_name, int low, int high, int iterations) {
        std::cout << "\n=== Running Benchmark ===\n";
        std::cout << "Query: [" << low << ", " << high << ") x " << iterations << " iterations\n\n";

        for (int i = 0; i < iterations; ++i) {
            DistributedRangeQueryRequest request;
            request.set_column_name(column_name);
            request.set_low(low);
            request.set_high(high);

            DistributedRangeQueryResponse response;
            ClientContext context;

            auto start = std::chrono::high_resolution_clock::now();
            Status status = coordinator_stub_->RangeQuery(&context, request, &response);
            auto end = std::chrono::high_resolution_clock::now();

            double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

            if (status.ok()) {
                int total_touched = 0;
                int total_cracks = 0;
                for (const auto& result : response.node_results()) {
                    if (result.has_stats()) {
                        total_touched += result.stats().tuples_touched();
                        total_cracks += result.stats().cracks_used();
                    }
                }

                std::cout << "Iteration " << i << ": count=" << response.total_count()
                          << ", touched=" << total_touched
                          << ", cracks=" << total_cracks
                          << ", time=" << time_ms << "ms\n";
            } else {
                std::cerr << "Iteration " << i << ": FAILED\n";
            }
        }

        std::cout << "\n(Tuples touched should decrease after first query)\n\n";
        return true;
    }

private:
    std::string coordinator_address_;
    std::unique_ptr<CoordinatorService::Stub> coordinator_stub_;
};


void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " [options] <command> [args]\n"
              << "\nOptions:\n"
              << "  --coordinator ADDR   Coordinator address (default: localhost:50050)\n"
              << "\nCommands:\n"
              << "  status                          Get cluster status\n"
              << "  load <column> <file>            Load binary data file to cluster\n"
              << "  query <column> <low> <high>     Execute range query\n"
              << "  benchmark <column> <low> <high> <iterations>  Run repeated queries\n"
              << "\nExamples:\n"
              << "  " << program << " status\n"
              << "  " << program << " load prices /app/data/100000000.data\n"
              << "  " << program << " query prices 1000000 2000000\n"
              << "  " << program << " benchmark prices 1000000 2000000 10\n";
}

int main(int argc, char** argv) {
    std::string coordinator_address = "localhost:50050";
    int arg_index = 1;

    // Parse options
    while (arg_index < argc && argv[arg_index][0] == '-') {
        std::string arg = argv[arg_index];
        if (arg == "--coordinator" && arg_index + 1 < argc) {
            coordinator_address = argv[++arg_index];
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
        ++arg_index;
    }

    if (arg_index >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[arg_index++];
    CrackStoreClient client(coordinator_address);

    if (command == "status") {
        return client.GetClusterStatus() ? 0 : 1;

    } else if (command == "load") {
        if (arg_index + 1 >= argc) {
            std::cerr << "Usage: load <column> <file>\n";
            return 1;
        }
        std::string column = argv[arg_index++];
        std::string file = argv[arg_index++];
        return client.LoadColumnFromFile(column, file) ? 0 : 1;

    } else if (command == "query") {
        if (arg_index + 2 >= argc) {
            std::cerr << "Usage: query <column> <low> <high>\n";
            return 1;
        }
        std::string column = argv[arg_index++];
        int low = std::stoi(argv[arg_index++]);
        int high = std::stoi(argv[arg_index++]);
        return client.RangeQuery(column, low, high) ? 0 : 1;

    } else if (command == "benchmark") {
        if (arg_index + 3 >= argc) {
            std::cerr << "Usage: benchmark <column> <low> <high> <iterations>\n";
            return 1;
        }
        std::string column = argv[arg_index++];
        int low = std::stoi(argv[arg_index++]);
        int high = std::stoi(argv[arg_index++]);
        int iterations = std::stoi(argv[arg_index++]);
        return client.RunBenchmark(column, low, high, iterations) ? 0 : 1;

    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(argv[0]);
        return 1;
    }
}