#include <iostream>
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "crackstore.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::Channel;
using grpc::ClientContext;

using namespace crackstore;



std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signal) {
    std::cout << "\n[Coordinator] Received signal " << signal << ", shutting down.. .\n";
    g_shutdown_requested = true;
}


struct NodeInfo {
    std::string node_id;
    std::string address;
    int port;
    bool is_healthy;
    std::chrono::steady_clock::time_point last_heartbeat;
    std::unique_ptr<StorageService::Stub> stub;
};



class CoordinatorServiceImpl final : public CoordinatorService::Service {
public:
    CoordinatorServiceImpl() {
        std::cout << "[Coordinator] Service initialized\n";
    }

    
    Status RegisterNode(ServerContext* context,
                        const RegisterNodeRequest* request,
                        RegisterNodeResponse* response) override {
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string address = request->address();
        int port = request->port();
        std::string node_id = "node-" + std::to_string(next_node_id_++);
        
        std::cout << "[Coordinator] Registering node: " << node_id 
                  << " at " << address << ":" << port << "\n";
        
        // Create gRPC channel to storage node
        std::string target = address + ":" + std::to_string(port);
        auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
        
        NodeInfo node;
        node.node_id = node_id;
        node.address = address;
        node.port = port;
        node.is_healthy = true;
        node.last_heartbeat = std::chrono::steady_clock::now();
        node.stub = StorageService::NewStub(channel);
        
        nodes_[node_id] = std::move(node);
        
        response->set_success(true);
        response->set_assigned_node_id(node_id);
        response->set_message("Registered successfully");
        
        std::cout << "[Coordinator] Node " << node_id << " registered.  Total nodes: "
                  << nodes_. size() << "\n";
        
        return Status::OK;
    }

    Status Heartbeat(ServerContext* context,
                     const HeartbeatRequest* request,
                     HeartbeatResponse* response) override {
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        const std::string& node_id = request->node_id();
        
        auto it = nodes_.find(node_id);
        if (it != nodes_.end()) {
            it->second. last_heartbeat = std::chrono::steady_clock::now();
            it->second.is_healthy = true;
            response->set_acknowledged(true);
        } else {
            response->set_acknowledged(false);
        }
        
        return Status::OK;
    }

    
    Status LoadData(ServerContext* context,
                    const DistributedLoadRequest* request,
                    DistributedLoadResponse* response) override {
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::cout << "[Coordinator] LoadData request for column: " 
                  << request->column_name() << "\n";
        
        // Get list of healthy nodes
        std::vector<std::string> healthy_nodes;
        for (const auto& [id, node] : nodes_) {
            if (node.is_healthy) {
                healthy_nodes. push_back(id);
            }
        }
        
        if (healthy_nodes.empty()) {
            response->set_success(false);
            response->set_message("No healthy nodes available");
            return Status::OK;
        }
        
        // For now, just report success - actual loading done by client directly
        response->set_success(true);
        response->set_nodes_used(healthy_nodes. size());
        for (const auto& id : healthy_nodes) {
            response->add_node_ids(id);
        }
        response->set_message("Ready to load data to " + std::to_string(healthy_nodes.size()) + " nodes");
        
        return Status::OK;
    }

    
    Status RangeQuery(ServerContext* context,
                      const DistributedRangeQueryRequest* request,
                      DistributedRangeQueryResponse* response) override {
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::string column_name = request->column_name();
        int low = request->low();
        int high = request->high();
        
        std::cout << "[Coordinator] RangeQuery [" << low << ", " << high << ") on column: " 
                  << column_name << "\n";
        
        int total_count = 0;
        int nodes_queried = 0;
        
        
        for (auto& [node_id, node] : nodes_) {
            if (! node.is_healthy) continue;
            
            RangeQueryRequest node_request;
            node_request.set_column_name(column_name);
            node_request. set_low(low);
            node_request.set_high(high);
            
            RangeQueryResponse node_response;
            ClientContext client_context;
            client_context.set_deadline(
                std::chrono::system_clock::now() + std::chrono::seconds(30)
            );
            
            Status status = node.stub->RangeQuery(&client_context, node_request, &node_response);
            
            if (status.ok() && node_response. success()) {
                total_count += node_response.count();
                nodes_queried++;
                
                // Add to response
                auto* result = response->add_node_results();
                result->set_node_id(node_id);
                result->set_count(node_response.count());
                
                if (node_response. has_stats()) {
                    auto* stats = result->mutable_stats();
                    stats->set_tuples_touched(node_response. stats().tuples_touched());
                    stats->set_cracks_used(node_response. stats().cracks_used());
                    stats->set_query_time_ms(node_response.stats().query_time_ms());
                }
                
                std::cout << "[Coordinator]   " << node_id << ": count=" << node_response.count()
                          << ", touched=" << node_response.stats().tuples_touched() << "\n";
            } else {
                std::cerr << "[Coordinator]   " << node_id << ": FAILED - " 
                          << status.error_message() << "\n";
                node.is_healthy = false;
            }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        double total_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        
        response->set_total_count(total_count);
        response->set_nodes_queried(nodes_queried);
        response->set_total_time_ms(total_time_ms);
        response->set_success(nodes_queried > 0);
        
        if (nodes_queried == 0) {
            response->set_error_message("No nodes responded");
        }
        
        std::cout << "[Coordinator] Total count: " << total_count 
                  << " from " << nodes_queried << " nodes in " << total_time_ms << "ms\n";
        
        return Status::OK;
    }

    Status GetClusterStatus(ServerContext* context,
                            const ClusterStatusRequest* request,
                            ClusterStatusResponse* response) override {
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        int healthy_count = 0;
        auto now = std::chrono::steady_clock::now();
        
        for (const auto& [id, node] : nodes_) {
            auto* status = response->add_nodes();
            status->set_node_id(id);
            status->set_address(node.address);
            status->set_port(node. port);
            status->set_is_healthy(node.is_healthy);
            
            auto ms_since_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - node. last_heartbeat
            ).count();
            status->set_last_heartbeat_ms(ms_since_heartbeat);
            
            if (node. is_healthy) healthy_count++;
        }
        
        response->set_total_nodes(nodes_.size());
        response->set_healthy_nodes(healthy_count);
        
        return Status::OK;
    }

    std::vector<std::pair<std::string, int>> GetNodeAddresses() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<std::string, int>> addresses;
        for (const auto& [id, node] : nodes_) {
            if (node.is_healthy) {
                addresses.emplace_back(node.address, node. port);
            }
        }
        return addresses;
    }

private:
    std::map<std::string, NodeInfo> nodes_;
    std::mutex mutex_;
    int next_node_id_ = 1;
};

void health_check_loop(CoordinatorServiceImpl* service, int timeout_seconds) {
    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::seconds(timeout_seconds));
        // Could add node health checking here
    }
}


void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --port PORT      Port to listen on (default: 50050)\n"
              << "  --help           Show this help\n";
}

int main(int argc, char** argv) {
    int port = 50050;

    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[Coordinator] Starting on port " << port << "...\n";

    std::string server_address = "0.0.0. 0:" + std::to_string(port);
    CoordinatorServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    
    if (!server) {
        std::cerr << "[Coordinator] Failed to start server\n";
        return 1;
    }

    std::cout << "[Coordinator] Listening on " << server_address << "\n";

    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[Coordinator] Shutting down.. .\n";
    server->Shutdown();
    std::cout << "[Coordinator] Stopped\n";
    
    return 0;
}