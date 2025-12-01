#ifndef CRACKING_ENGINE_H
#define CRACKING_ENGINE_H

#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <chrono>

/**
 * CrackingEngine - A self-contained adaptive indexing engine
 *
 * Based on the Stochastic Database Cracking algorithms from:
 * "Stochastic Database Cracking: Towards Robust Adaptive Indexing in Main-Memory Column-Stores"
 * Felix Halim, Stratos Idreos, Panagiotis Karras, Roland H. C. Yap (VLDB 2012)
 *
 * This class encapsulates all cracking state and provides a clean interface
 * for use in distributed storage nodes.
 */

namespace crackstore {



struct CrackingStats {
    // Cumulative statistics (lifetime of engine)
    long long queries_executed = 0;
    long long total_tuples_touched = 0;
    long long total_cracks_created = 0;
    double total_query_time_ms = 0.0;
    
    // Per-query statistics (reset after each query)
    int last_tuples_touched = 0;
    int last_cracks_created = 0;
    double last_query_time_ms = 0.0;
    int last_result_count = 0;
    
    void reset() {
        queries_executed = 0;
        total_tuples_touched = 0;
        total_cracks_created = 0;
        total_query_time_ms = 0.0;
        last_tuples_touched = 0;
        last_cracks_created = 0;
        last_query_time_ms = 0.0;
        last_result_count = 0;
    }
};


struct CrackIndex {
    int pos;        // the cracker index position
    int holes;      // the number of holes in front (for updates)
    bool sorted;    // is this piece sorted
    
    int prev_pos() const { return pos - holes; }
};

using ValueType = int;
using CrackMap = std::map<ValueType, CrackIndex>;
using CrackMapIter = CrackMap::iterator;



class CrackingEngine {
public:
    
    /**
     * Construct a CrackingEngine with a copy of the provided data.
     *
     * @param data    Pointer to integer array
     * @param size    Number of elements in array
     * @param extra_capacity  Additional capacity for inserts (default: size/10)
     */
    CrackingEngine(const int* data, int size, int extra_capacity = -1) {
        if (extra_capacity < 0) {
            extra_capacity = std::max(size / 10, 1000);
        }
        
        capacity_ = size + extra_capacity;
        size_ = size;
        arr_ = new int[capacity_];
        
        // Copy data
        std::memcpy(arr_, data, size * sizeof(int));
        
        // Initialize cracker index (empty)
        crack_index_.clear();
        
        // Initialize pending updates (empty)
        pending_inserts_.clear();
        pending_deletes_.clear();
        
        // Initialize stats
        stats_.reset();
    }
    
    ~CrackingEngine() {
        delete[] arr_;
        arr_ = nullptr;
    }
    
    // Disable copy (engine owns memory)
    CrackingEngine(const CrackingEngine&) = delete;
    CrackingEngine& operator=(const CrackingEngine&) = delete;
    
    // Enable move
    CrackingEngine(CrackingEngine&& other) noexcept {
        arr_ = other.arr_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        crack_index_ = std::move(other.crack_index_);
        pending_inserts_ = std::move(other.pending_inserts_);
        pending_deletes_ = std::move(other.pending_deletes_);
        stats_ = other.stats_;
        
        other.arr_ = nullptr;
        other.size_ = 0;
    }
    
    CrackingEngine& operator=(CrackingEngine&& other) noexcept {
        if (this != &other) {
            delete[] arr_;
            
            arr_ = other.arr_;
            size_ = other. size_;
            capacity_ = other.capacity_;
            crack_index_ = std::move(other.crack_index_);
            pending_inserts_ = std::move(other.pending_inserts_);
            pending_deletes_ = std::move(other.pending_deletes_);
            stats_ = other.stats_;
            
            other.arr_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
    
    
    /*
     * This is the main operation that drives adaptive indexing:
     * - First query on a range is slow (scans data)
     * - Subsequent queries on similar ranges are faster (uses cracks)
     *
     * @param low   Lower bound (inclusive)
     * @param high  Upper bound (exclusive)
     * @return      Count of elements where low <= element < high
     */
    int range_query(int low, int high) {
        auto start_time = std::chrono::high_resolution_clock::now();
        
      
        stats_.last_tuples_touched = 0;
        stats_.last_cracks_created = 0;
        int initial_cracks = static_cast<int>(crack_index_.size());
        
        
        merge_pending_updates(low, high);
        
        
        int result = crack(low, high);
        
      
        auto end_time = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        
        stats_.last_cracks_created = static_cast<int>(crack_index_. size()) - initial_cracks;
        stats_. last_query_time_ms = elapsed_ms;
        stats_.last_result_count = result;
        
        stats_.queries_executed++;
        stats_.total_tuples_touched += stats_.last_tuples_touched;
        stats_.total_cracks_created += stats_. last_cracks_created;
        stats_.total_query_time_ms += elapsed_ms;
        
        return result;
    }
    
    /**
     * Queue an insert operation.
     * The value will be merged into the data during the next relevant query.
     *
     * @param value  Value to insert
     */
    void insert(int value) {
        // If value is pending delete, cancel the delete instead
        auto it = pending_deletes_.find(value);
        if (it != pending_deletes_.end()) {
            pending_deletes_.erase(it);
        } else {
            pending_inserts_.insert(value);
        }
    }
    
    /**
     * Queue a remove operation.
     * The value will be removed during the next relevant query. 
     * 
     * @param value  Value to remove
     */
    void remove(int value) {
        // If value is pending insert, cancel the insert instead
        auto it = pending_inserts_. find(value);
        if (it != pending_inserts_. end()) {
            pending_inserts_. erase(it);
        } else {
            pending_deletes_.insert(value);
        }
    }
    

    

    CrackingStats get_stats() const {
        return stats_;
    }
    

    void reset_stats() {
        stats_.reset();
    }
    

    int get_crack_count() const {
        return static_cast<int>(crack_index_. size());
    }
    

    int get_size() const {
        return size_;
    }
    

    int get_pending_inserts() const {
        return static_cast<int>(pending_inserts_.size());
    }
    

    int get_pending_deletes() const {
        return static_cast<int>(pending_deletes_.size());
    }

private:
    int* arr_ = nullptr;          // The data array
    int size_ = 0;                // Current number of elements
    int capacity_ = 0;            // Maximum capacity
    
    CrackMap crack_index_;        // The cracker index: value -> position
    
    std::multiset<int> pending_inserts_;  // Pending insert operations
    std::multiset<int> pending_deletes_;  // Pending delete operations
    
    CrackingStats stats_;         // Query statistics
    
    /**
     * Partition array segment [L, R) around value v.
     * After partitioning: all elements < v are before the returned position.
     *
     * @return  Position where elements >= v begin
     */
    int partition(int v, int L, int R) {
        return static_cast<int>(
            std::partition(arr_ + L, arr_ + R, [v](int x) { return x < v; }) - arr_
        );
    }
    
    /**
     * @param v    Value to find
     * @param L    Output: left bound of piece
     * @param R    Output: right bound of piece (exclusive)
     * @return     Iterator to the crack point at or after v
     */
    CrackMapIter find_piece(int v, int& L, int& R) {
        L = 0;
        R = size_;
        
        CrackMapIter it = crack_index_.lower_bound(v);
        
        if (it == crack_index_.end()) {
            if (it != crack_index_.begin()) {
                --it;
                L = it->second. pos;
                ++it;
            }
        } else if (it == crack_index_.begin()) {
            if (v < it->first) {
                R = it->second. prev_pos();
            } else {
                L = it->second.pos;
                ++it;
                if (it != crack_index_.end()) {
                    R = it->second.prev_pos();
                }
            }
        } else if (v < it->first) {
            R = it->second.prev_pos();
            --it;
            L = it->second.pos;
            ++it;
        } else {
            L = it->second.pos;
            ++it;
            if (it != crack_index_.end()) {
                R = it->second.prev_pos();
            }
        }
        
        return it;
    }
    
    /**
     * Add a crack point at value v with position p.
     *
     * @param v    Crack value
     * @param p    Position in array
     * @return     The position (unchanged)
     */
    int add_crack(int v, int p) {
        if (p == 0 || p >= size_) {
            return p;
        }
        
        // Check if crack already exists at this position
        CrackMapIter i = crack_index_. lower_bound(v);
        
        if (i != crack_index_.end()) {
            if (i->second. pos == p) return p;
            CrackMapIter j = i;
            if (j->first == v) ++j;
            if (j != crack_index_.end()) {
                if (j->second. prev_pos() == p) return p;
            }
        }
        
        if (i != crack_index_. begin()) {
            --i;
            if (i->second.pos == p) return p;
        }
        
        if (crack_index_. count(v)) {
            assert(crack_index_[v].pos == p);
            return p;
        }
        
        // Add new crack
        crack_index_[v] = CrackIndex{p, 0, false};
        return p;
    }
    

    void split_ab(int L, int R, int a, int b, int& i1, int& i2) {
        i1 = L;
        i2 = L;
        int end = R - 1;
        
        while (L <= end) {
            if (arr_[L] < a) {
                std::swap(arr_[L], arr_[i1]);
                if (i1 != i2) {
                    std::swap(arr_[L], arr_[i2]);
                }
                ++i1;
                ++i2;
                ++L;
            } else if (arr_[L] < b) {
                std::swap(arr_[L], arr_[i2]);
                ++i2;
                ++L;
            } else {
                std::swap(arr_[L], arr_[end]);
                --end;
            }
        }
    }
    

    int crack(int a, int b) {
        int L1, R1, L2, R2;
        int i1, i2;
        
        find_piece(a, L1, R1);
        find_piece(b, L2, R2);
        
        stats_.last_tuples_touched += (R1 - L1);
        
        if (L1 == L2) {
            // a and b are in the same piece - do 3-way split
            assert(R1 == R2);
            split_ab(L1, R1, a, b, i1, i2);
        } else {
            // a and b are in different pieces - partition each
            stats_.last_tuples_touched += (R2 - L2);
            i1 = partition(a, L1, R1);
            i2 = partition(b, L2, R2);
        }
        
        add_crack(a, i1);
        add_crack(b, i2);
        
        return i2 - i1;
    }

    void merge_pending_updates(int low, int high) {
        // Process pending inserts in range
        auto ins_low = pending_inserts_.lower_bound(low);
        auto ins_high = pending_inserts_.lower_bound(high);
        
        for (auto it = ins_low; it != ins_high; ) {
            int value = *it;
            it = pending_inserts_.erase(it);
            
            
            if (size_ < capacity_) {
                arr_[size_++] = value;
            }
            
        }
        
        
        auto del_low = pending_deletes_. lower_bound(low);
        auto del_high = pending_deletes_.lower_bound(high);
        
        for (auto it = del_low; it != del_high; ) {
            int value = *it;
            it = pending_deletes_.erase(it);
            
            
            for (int i = 0; i < size_; ++i) {
                if (arr_[i] == value) {
                    arr_[i] = arr_[--size_];
                    
                    crack_index_.clear();
                    break;
                }
            }
        }
    }
};


inline int naive_range_count(const int* data, int size, int low, int high) {
    int count = 0;
    for (int i = 0; i < size; ++i) {
        if (data[i] >= low && data[i] < high) {
            ++count;
        }
    }
    return count;
}

} 

#endif 