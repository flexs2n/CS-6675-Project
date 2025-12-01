#include <iostream>
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

#include <grpcpp/grpcpp.h>
#include "crackstore.grpc.pb.h"
#include "cracking_engine.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::Channel;
using grpc::ClientContext;

using namespace crackstore;



std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signal) {
    std::cout << "\n[StorageNode] Received signal " << signal << ", shutting down...\n";
    g_shutdown_requested = true;
}


class StorageServiceImpl final : public StorageService::Service {
public:
    StorageServiceImpl(const std::string& node_id) : node_id_(node_id) {
        std::cout << "[StorageNode:" << node_id_ << "] Service initialized\n";
    }

    
    Status LoadColumn(ServerContext* context,
                      const LoadColumnRequest* request,
                      LoadColumnResponse* response) override {
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        const std::string& column_name = request->column_name();
        int data_size = request->data_size();
        
        std::cout << "[StorageNode:" << node_id_ << "] LoadColumn: "
                  << column_name << " (" << data_size << " rows)\n";
        
        if (data_size == 0) {
            response->set_success(false);
            response->set_rows_loaded(0);
            response->set_node_id(node_id_);
            return Status::OK;
        }
        
        // Copy data from protobuf to vector
        std::vector<int> data(data_size);
        for (int i = 0; i < data_size; ++i) {
            data[i] = request->data(i);
        }
        
        // Create or replace cracking engine for this column
        columns_[column_name] = std::make_unique<CrackingEngine>(
            data.data(), data_size
        );
        
        response->set_success(true);
        response->set_rows_loaded(data_size);
        response->set_node_id(node_id_);
        
        std::cout << "[StorageNode:" << node_id_ << "] Column " << column_name 
                  << " loaded successfully\n";
        
        return Status::OK;
    }

 
    // RangeQuery - Execute cracking range query
    Status RangeQuery(ServerContext* context,
                      const RangeQueryRequest* request,
                      RangeQueryResponse* response) override {
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        const std::string& column_name = request->column_name();
        int low = request->low();
        int high = request->high();
        
       
        auto it = columns_.find(column_name);
        if (it == columns_.end()) {
            response->set_success(false);
            response->set_error_message("Column not found: " + column_name);
            response->set_node_id(node_id_);
            response->set_count(0);
            return Status::OK;
        }
        
        CrackingEngine* engine = it->second.get();
        
        
        int count = engine->range_query(low, high);
        CrackingStats stats = engine->get_stats();
        
       
        response->set_success(true);
        response->set_count(count);
        response->set_node_id(node_id_);
        
        auto* query_stats = response->mutable_stats();
        query_stats->set_tuples_touched(stats.last_tuples_touched);
        query_stats->set_cracks_used(engine->get_crack_count());
        query_stats->set_query_time_ms(stats.last_query_time_ms);
        
        std::cout << "[StorageNode:" << node_id_ << "] RangeQuery [" << low << ", " << high << "): "
                  << "count=" << count 
                  << ", touched=" << stats.last_tuples_touched
                  << ", cracks=" << engine->get_crack_count() << "\n";
        
        return Status::OK;
    }

    Status GetNodeInfo(ServerContext* context,
                       const NodeInfoRequest* request,
                       NodeInfoResponse* response) override {
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        response->set_node_id(node_id_);
        response->set_is_healthy(true);
        
        int total_rows = 0;
        int total_cracks = 0;
        
        for (const auto& [name, engine] : columns_) {
            response->add_columns(name);
            total_rows += engine->get_size();
            total_cracks += engine->get_crack_count();
        }
        
        response->set_total_rows(total_rows);
        response->set_total_cracks(total_cracks);
        
        return Status::OK;
    }


    Status HealthCheck(ServerContext* context,
                       const Empty* request,
                       StatusResponse* response) override {
        
        response->set_success(true);
        response->set_message("OK");
        return Status::OK;
    }

private:
    std::string node_id_;
    std::map<std::string, std::unique_ptr<CrackingEngine>> columns_;
    std::mutex mutex_;
};


class CoordinatorClient {
public:
    CoordinatorClient(std::shared_ptr<Channel> channel)
        : stub_(CoordinatorService::NewStub(channel)) {}

    bool RegisterNode(const std::string& address, int port, std::string& assigned_id) {
        RegisterNodeRequest request;
        request.set_address(address);
        request.set_port(port);

        RegisterNodeResponse response;
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

        Status status = stub_->RegisterNode(&context, request, &response);

        if (status.ok() && response.success()) {
            assigned_id = response.assigned_node_id();
            std::cout << "[StorageNode] Registered with coordinator as: " << assigned_id << "\n";
            return true;
        } else {
            std::cerr << "[StorageNode] Failed to register: " 
                      << (status.ok() ? response.message() : status.error_message()) << "\n";
            return false;
        }
    }

    bool SendHeartbeat(const std::string& node_id) {
        HeartbeatRequest request;
        request.set_node_id(node_id);

        HeartbeatResponse response;
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));

        Status status = stub_->Heartbeat(&context, request, &response);
        return status.ok() && response.acknowledged();
    }

private:
    std::unique_ptr<CoordinatorService::Stub> stub_;
};


void heartbeat_loop(CoordinatorClient* client, 
                    const std::string& node_id,
                    int interval_seconds) {
    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
        
        if (g_shutdown_requested) break;
        
        if (!client->SendHeartbeat(node_id)) {
            std::cerr << "[StorageNode] Heartbeat failed\n";
        }
    }
    std::cout << "[StorageNode] Heartbeat thread stopped\n";
}



void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --port PORT           Port to listen on (default: 50051)\n"
              << "  --coordinator ADDR    Coordinator address (default: localhost:50050)\n"
              << "  --node-id ID          Node identifier (default: auto-assigned)\n"
              << "  --heartbeat SEC       Heartbeat interval in seconds (default: 5)\n"
              << "  --standalone          Run without coordinator\n"
              << "  --help                Show this help\n";
}

int main(int argc, char** argv) {
    // Default configuration
    int port = 50051;
    std::string coordinator_address = "localhost:50050";
    std::string node_id = "";
    int heartbeat_interval = 5;
    bool standalone = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--coordinator" && i + 1 < argc) {
            coordinator_address = argv[++i];
        } else if (arg == "--node-id" && i + 1 < argc) {
            node_id = argv[++i];
        } else if (arg == "--heartbeat" && i + 1 < argc) {
            heartbeat_interval = std::stoi(argv[++i]);
        } else if (arg == "--standalone") {
            standalone = true;
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Set up signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Generate node ID if not provided
    if (node_id.empty()) {
        node_id = "node-" + std::to_string(port);
    }

    std::cout << "[StorageNode] Starting...\n"
              << "  Node ID: " << node_id << "\n"
              << "  Port: " << port << "\n"
              << "  Coordinator: " << (standalone ? "standalone mode" : coordinator_address) << "\n";

    // Create coordinator client and register (unless standalone)
    std::unique_ptr<CoordinatorClient> coordinator_client;
    std::thread heartbeat_thread;

    if (!standalone) {
        auto channel = grpc::CreateChannel(coordinator_address, grpc::InsecureChannelCredentials());
        coordinator_client = std::make_unique<CoordinatorClient>(channel);

        std::string assigned_id;
        if (coordinator_client->RegisterNode("localhost", port, assigned_id)) {
            if (!assigned_id.empty()) {
                node_id = assigned_id;
            }
            
            // Start heartbeat thread
            heartbeat_thread = std::thread(
                heartbeat_loop, 
                coordinator_client.get(), 
                node_id, 
                heartbeat_interval
            );
        } else {
            std::cerr << "[StorageNode] Warning: Could not register with coordinator, running standalone\n";
            standalone = true;
        }
    }

    // Create and start gRPC server
    std::string server_address = "0.0.0.0:" + std::to_string(port);
    StorageServiceImpl service(node_id);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    
    if (!server) {
        std::cerr << "[StorageNode] Failed to start server on " << server_address << "\n";
        g_shutdown_requested = true;
        if (heartbeat_thread.joinable()) {
            heartbeat_thread.join();
        }
        return 1;
    }

    std::cout << "[StorageNode] Listening on " << server_address << "\n";

    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Graceful shutdown
    std::cout << "[StorageNode] Shutting down...\n";
    server->Shutdown();
    
    if (heartbeat_thread.joinable()) {
        heartbeat_thread.join();
    }

    std::cout << "[StorageNode] Stopped\n";
    return 0;
}