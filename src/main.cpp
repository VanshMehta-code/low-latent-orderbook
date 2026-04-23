#include "order_book.h"
#include <iostream>
#include <iomanip>
#include <x86intrin.h>
#include <vector>
#include <algorithm>

#define GREEN "\033[32m"
#define RED "\033[31m"
#define YELLOW "\033[33m"
#define CYAN "\033[36m"
#define MAGENTA "\033[35m"
#define RESET "\033[0m"

inline uint64_t rdtscp_start() {
    unsigned int aux;
    _mm_lfence();
    return __rdtscp(&aux);
}

inline uint64_t rdtscp_end() {
    unsigned int aux;
    uint64_t cycles = __rdtscp(&aux);
    _mm_lfence();
    return cycles;
}

class BenchmarkSuite {
    order_book* ob;
    uint32_t next_order_id;
    
    struct Stats {
        uint64_t min;
        uint64_t max;
        uint64_t avg;
        uint64_t p50;
        uint64_t p95;
        uint64_t p99;
    };
    
    void print_stats(const char* name, const Stats& s) {
        std::cout << CYAN << name << ":" << RESET << "\n";
        std::cout << "  Min: " << s.min << " cycles\n";
        std::cout << "  Avg: " << s.avg << " cycles\n";
        std::cout << "  P50: " << s.p50 << " cycles\n";
        std::cout << "  P95: " << s.p95 << " cycles\n";
        std::cout << "  P99: " << s.p99 << " cycles\n";
        std::cout << "  Max: " << s.max << " cycles\n\n";
    }
    
    Stats calculate_stats(std::vector<uint64_t>& latencies) {
        std::sort(latencies.begin(), latencies.end());
        
        uint64_t total = 0;
        for (auto l : latencies) total += l;
        
        Stats s;
        s.min = latencies[0];
        s.max = latencies[latencies.size() - 1];
        s.avg = total / latencies.size();
        s.p50 = latencies[latencies.size() / 2];
        s.p95 = latencies[(latencies.size() * 95) / 100];
        s.p99 = latencies[(latencies.size() * 99) / 100];
        
        return s;
    }
    
public:
    BenchmarkSuite() : next_order_id(1000) {
        ob = new order_book(order_range::mid, 100.0, next_order_id);
    }
    
    ~BenchmarkSuite() {
        delete ob;
    }
    
    uint32_t get_next_id() {
        return next_order_id++;
    }
    
    void reset() {
        delete ob;
        ob = new order_book(order_range::mid, 100.0, 1000);
        next_order_id = 1000;
    }
    
    // ========== EASY BENCHMARK ==========
    void benchmark_easy_batched() {
    std::cout << GREEN << "\n========== EASY BENCHMARK (BATCHED) ==========\n" << RESET;
    
    reset();
    
    // WARMUP - prime the cache
    for (int i = 0; i < 10000; i++) {
        ob->add_order(get_next_id(), 95.00 - i * 0.01, 100, true);
    }
    
    // MEASURE IN BATCHES
    constexpr int NUM_BATCHES = 100;
    constexpr int BATCH_SIZE = 1000;
    std::vector<uint64_t> batch_latencies;
    batch_latencies.reserve(NUM_BATCHES);
    
    for (int batch = 0; batch < NUM_BATCHES; batch++) {
        uint64_t start = rdtscp_start();
        
        // Do 1000 operations WITHOUT measuring each one
        for (int i = 0; i < BATCH_SIZE; i++) {
            ob->add_order(get_next_id(), 95.00 - i * 0.01, 100, true);
        }
        
        uint64_t end = rdtscp_end();
        
        // Store per-operation average
        batch_latencies.push_back((end - start) / BATCH_SIZE);
    }
    
    Stats stats = calculate_stats(batch_latencies);
    print_stats("Add Order (Batched)", stats);
}
    
    // ========== MEDIUM BENCHMARK ==========
    void benchmark_medium() {
        std::cout << YELLOW << "\n========== MEDIUM BENCHMARK ==========\n" << RESET;
        std::cout << "Mixed operations, partial fills, multi-level walks\n\n";
        
        reset();
        
        // 1. Partial fills
        std::vector<uint64_t> partial_latencies;
        partial_latencies.reserve(5000);
        
        for (int i = 0; i < 5000; i++) {
            ob->add_order(get_next_id(), 100.00, 100, true);
            
            uint64_t start = rdtscp_start();
            ob->add_order(get_next_id(), 100.00, 60, false);
            uint64_t end = rdtscp_end();
            
            partial_latencies.push_back(end - start);
        }
        
        Stats partial_stats = calculate_stats(partial_latencies);
        print_stats("Partial Fill (60/100)", partial_stats);
        
        // 2. FIFO with 5 orders
        reset();
        std::vector<uint64_t> fifo_latencies;
        fifo_latencies.reserve(2000);
        
        for (int i = 0; i < 2000; i++) {
            for (int j = 0; j < 5; j++) {
                ob->add_order(get_next_id(), 100.00, 50, true);
            }
            
            uint64_t start = rdtscp_start();
            ob->add_order(get_next_id(), 100.00, 200, false);
            uint64_t end = rdtscp_end();
            
            fifo_latencies.push_back(end - start);
        }
        
        Stats fifo_stats = calculate_stats(fifo_latencies);
        print_stats("FIFO Match (4 orders)", fifo_stats);
        
        // 3. Walk 5 levels
        reset();
        std::vector<uint64_t> walk_latencies;
        walk_latencies.reserve(2000);
        
        for (int i = 0; i < 2000; i++) {
            for (int j = 0; j < 5; j++) {
                ob->add_order(get_next_id(), 101.00 + j * 0.01, 100, false);
            }
            
            uint64_t start = rdtscp_start();
            ob->add_order(get_next_id(), 105.00, 450, true);
            uint64_t end = rdtscp_end();
            
            walk_latencies.push_back(end - start);
        }
        
        Stats walk_stats = calculate_stats(walk_latencies);
        print_stats("Walk 5 Levels", walk_stats);
        
        // 4. Cancel from middle of queue
        reset();
        uint32_t middle_ids[1000];
        
        for (int i = 0; i < 500; i++) {
            ob->add_order(get_next_id(), 100.00, 100, true);
        }
        for (int i = 0; i < 1000; i++) {
            middle_ids[i] = get_next_id();
            ob->add_order(middle_ids[i], 100.00, 100, true);
        }
        for (int i = 0; i < 500; i++) {
            ob->add_order(get_next_id(), 100.00, 100, true);
        }
        
        std::vector<uint64_t> mid_cancel_latencies;
        mid_cancel_latencies.reserve(1000);
        
        for (int i = 0; i < 1000; i++) {
            uint64_t start = rdtscp_start();
            ob->cancel_order(middle_ids[i]);
            uint64_t end = rdtscp_end();
            
            mid_cancel_latencies.push_back(end - start);
        }
        
        Stats mid_cancel_stats = calculate_stats(mid_cancel_latencies);
        print_stats("Cancel from Middle (2k queue)", mid_cancel_stats);
    }
    
    // ========== NIGHTMARE BENCHMARK ==========
    void benchmark_nightmare() {
        std::cout << RED << "\n========== NIGHTMARE BENCHMARK ==========\n" << RESET;
        std::cout << "Maximum stress, deep book, cache thrashing, worst-case patterns\n\n";
        
        // 1. Deep book walk - 100 levels
        reset();
        std::cout << MAGENTA << "[1/8] Deep Book Walk - 100 levels...\n" << RESET;
        
        std::vector<uint64_t> deep_walk_latencies;
        deep_walk_latencies.reserve(500);
        
        for (int round = 0; round < 500; round++) {
            for (int i = 0; i < 100; i++) {
                ob->add_order(get_next_id(), 101.00 + i * 0.01, 100, false);
            }
            
            uint64_t start = rdtscp_start();
            ob->add_order(get_next_id(), 200.00, 10000, true);
            uint64_t end = rdtscp_end();
            
            deep_walk_latencies.push_back(end - start);
        }
        
        Stats deep_walk_stats = calculate_stats(deep_walk_latencies);
        print_stats("Deep Walk (100 levels)", deep_walk_stats);
        
        // 2. Massive FIFO - 1000 orders same price
        reset();
        std::cout << MAGENTA << "[2/8] Massive FIFO - 1000 orders...\n" << RESET;
        
        std::vector<uint64_t> massive_fifo_latencies;
        massive_fifo_latencies.reserve(200);
        
        for (int round = 0; round < 200; round++) {
            for (int i = 0; i < 1000; i++) {
                ob->add_order(get_next_id(), 100.00, 10, true);
            }
            
            uint64_t start = rdtscp_start();
            ob->add_order(get_next_id(), 100.00, 5000, false);
            uint64_t end = rdtscp_end();
            
            massive_fifo_latencies.push_back(end - start);
        }
        
        Stats massive_fifo_stats = calculate_stats(massive_fifo_latencies);
        print_stats("Massive FIFO (500 orders)", massive_fifo_stats);
        
        // 3. Cancel from deep middle - 10k queue
        reset();
        std::cout << MAGENTA << "[3/8] Cancel from Deep Middle - 10k queue...\n" << RESET;
        
        uint32_t deep_ids[1000];
        
        for (int i = 0; i < 5000; i++) {
            ob->add_order(get_next_id(), 100.00, 100, true);
        }
        for (int i = 0; i < 1000; i++) {
            deep_ids[i] = get_next_id();
            ob->add_order(deep_ids[i], 100.00, 100, true);
        }
        for (int i = 0; i < 4000; i++) {
            ob->add_order(get_next_id(), 100.00, 100, true);
        }
        
        std::vector<uint64_t> deep_cancel_latencies;
        deep_cancel_latencies.reserve(1000);
        
        for (int i = 0; i < 1000; i++) {
            uint64_t start = rdtscp_start();
            ob->cancel_order(deep_ids[i]);
            uint64_t end = rdtscp_end();
            
            deep_cancel_latencies.push_back(end - start);
        }
        
        Stats deep_cancel_stats = calculate_stats(deep_cancel_latencies);
        print_stats("Cancel Deep Middle (10k queue)", deep_cancel_stats);
        
        // 4. Alternating add/cancel (cache thrash)
        reset();
        std::cout << MAGENTA << "[4/8] Cache Thrashing - Alternating ops...\n" << RESET;
        
        std::vector<uint64_t> thrash_latencies;
        thrash_latencies.reserve(10000);
        
        uint32_t thrash_id;
        for (int i = 0; i < 10000; i++) {
            if (i % 2 == 0) {
                thrash_id = get_next_id();
                
                uint64_t start = rdtscp_start();
                ob->add_order(thrash_id, 100.00 + (i % 100) * 0.01, 100, true);
                uint64_t end = rdtscp_end();
                
                thrash_latencies.push_back(end - start);
            } else {
                uint64_t start = rdtscp_start();
                ob->cancel_order(thrash_id);
                uint64_t end = rdtscp_end();
                
                thrash_latencies.push_back(end - start);
            }
        }
        
        Stats thrash_stats = calculate_stats(thrash_latencies);
        print_stats("Cache Thrashing", thrash_stats);
        
        // 5. Fragmented book spread
        reset();
        std::cout << MAGENTA << "[5/8] Fragmented Book - Max spread...\n" << RESET;
        
        for (int i = 0; i < 1000; i++) {
            ob->add_order(get_next_id(), 50.00 + i * 0.10, 100, true);
            ob->add_order(get_next_id(), 150.00 - i * 0.10, 100, false);
        }
        
        std::vector<uint64_t> frag_latencies;
        frag_latencies.reserve(5000);
        
        for (int i = 0; i < 5000; i++) {
            uint64_t start = rdtscp_start();
            ob->add_order(get_next_id(), 100.00 + (i % 100) * 0.50, 50, i % 2 == 0);
            uint64_t end = rdtscp_end();
            
            frag_latencies.push_back(end - start);
        }
        
        Stats frag_stats = calculate_stats(frag_latencies);
        print_stats("Fragmented Book", frag_stats);
        
        // 6. Worst-case partial fills
        reset();
        std::cout << MAGENTA << "[6/8] Worst Partials - 1 unit fills...\n" << RESET;
        
        std::vector<uint64_t> worst_partial_latencies;
        worst_partial_latencies.reserve(10000);
        
        for (int i = 0; i < 10000; i++) {
            ob->add_order(get_next_id(), 100.00, 1000, true);
            
            uint64_t start = rdtscp_start();
            ob->add_order(get_next_id(), 100.00, 1, false);
            uint64_t end = rdtscp_end();
            
            worst_partial_latencies.push_back(end - start);
        }
        
        Stats worst_partial_stats = calculate_stats(worst_partial_latencies);
        print_stats("Worst Partial (1/1000)", worst_partial_stats);
        
        // 7. Stress test - rapid fire adds
        reset();
        std::cout << MAGENTA << "[7/8] Rapid Fire - 50k sequential adds...\n" << RESET;
        
        std::vector<uint64_t> rapid_latencies;
        rapid_latencies.reserve(50000);
        
        for (int i = 0; i < 50000; i++) {
            uint64_t start = rdtscp_start();
            ob->add_order(get_next_id(), 100.00 - (i % 1000) * 0.01, 100, i % 2 == 0);
            uint64_t end = rdtscp_end();
            
            rapid_latencies.push_back(end - start);
        }
        
        Stats rapid_stats = calculate_stats(rapid_latencies);
        print_stats("Rapid Fire 50k Adds", rapid_stats);
        
        // 8. THE ULTIMATE - Pre-computed pattern
        reset();
        std::cout << MAGENTA << "[8/8] THE ULTIMATE - Pre-computed chaos...\n" << RESET;
        
        // Pre-generate operation pattern
        struct Op {
            uint8_t type;  // 0=add buy, 1=add sell, 2=cancel, 3=execute
            double price;
            uint32_t qty;
            uint32_t target_id;
        };
        
        Op ops[50000];
        uint32_t active_buy_ids[25000];
        uint32_t active_sell_ids[25000];
        int buy_count = 0;
        int sell_count = 0;
        
        // Pre-populate some orders
        for (int i = 0; i < 5000; i++) {
            if (i % 2 == 0) {
                active_buy_ids[buy_count++] = get_next_id();
                ob->add_order(active_buy_ids[buy_count-1], 95.00 + (i % 100) * 0.05, 100, true);
            } else {
                active_sell_ids[sell_count++] = get_next_id();
                ob->add_order(active_sell_ids[sell_count-1], 105.00 - (i % 100) * 0.05, 100, false);
            }
        }
        
        // Generate operation sequence
        for (int i = 0; i < 50000; i++) {
            int pattern = i % 10;
            
            if (pattern < 4) {  // 40% add
                ops[i].type = i % 2;
                ops[i].price = 100.00 + (i % 200 - 100) * 0.01;
                ops[i].qty = 100 + (i % 900);
            } else if (pattern < 6) {  // 20% cancel
                ops[i].type = 2;
                ops[i].target_id = (i % 2 == 0 && buy_count > 0) ? 
                    active_buy_ids[i % buy_count] : 
                    (sell_count > 0 ? active_sell_ids[i % sell_count] : 0);
            } else if (pattern < 8) {  // 20% aggressive match
                ops[i].type = i % 2;
                ops[i].price = (i % 2 == 0) ? 110.00 : 90.00;
                ops[i].qty = 500 + (i % 500);
            } else {  // 20% execute
                ops[i].type = 3;
                ops[i].target_id = (i % 2 == 0 && buy_count > 0) ? 
                    active_buy_ids[i % buy_count] : 
                    (sell_count > 0 ? active_sell_ids[i % sell_count] : 0);
                ops[i].qty = 50 + (i % 50);
            }
        }
        
        // Execute pre-computed pattern
        std::vector<uint64_t> ultimate_latencies;
        ultimate_latencies.reserve(50000);
        
        for (int i = 0; i < 50000; i++) {
            uint64_t start = rdtscp_start();
            
            if (ops[i].type == 0) {  // Add buy
                uint32_t id = get_next_id();
                ob->add_order(id, ops[i].price, ops[i].qty, true);
                if (buy_count < 25000) active_buy_ids[buy_count++] = id;
                
            } else if (ops[i].type == 1) {  // Add sell
                uint32_t id = get_next_id();
                ob->add_order(id, ops[i].price, ops[i].qty, false);
                if (sell_count < 25000) active_sell_ids[sell_count++] = id;
                
            } else if (ops[i].type == 2 && ops[i].target_id != 0) {  // Cancel
                ob->cancel_order(ops[i].target_id);
                
            } else if (ops[i].type == 3 && ops[i].target_id != 0) {  // Execute
                ob->execute_order(ops[i].target_id, ops[i].qty, i % 2 == 0);
            }
            
            uint64_t end = rdtscp_end();
            ultimate_latencies.push_back(end - start);
        }
        
        Stats ultimate_stats = calculate_stats(ultimate_latencies);
        print_stats("ULTIMATE (50k pre-computed)", ultimate_stats);
        
        std::cout << RED << "\n🔥 Nightmare complete. Your orderbook survived. 🔥\n" << RESET;
    }
    
    void run_all() {
        std::cout << "\n";
        std::cout << "╔════════════════════════════════════════════════════╗\n";
        std::cout << "║    ORDERBOOK BENCHMARK SUITE - RDTSCP EDITION      ║\n";
        std::cout << "╚════════════════════════════════════════════════════╝\n";
        
        benchmark_easy_batched();
        benchmark_medium();
        benchmark_nightmare();
        
        std::cout << "\n" << GREEN << "========== BENCHMARK COMPLETE ==========" << RESET << "\n\n";
    }
};

int main() {
    BenchmarkSuite bench;
    bench.run_all();
    return 0;
}
