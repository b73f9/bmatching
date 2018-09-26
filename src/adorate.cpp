#include <iostream>
#include <thread>
#include <string>
#include <math.h>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <queue>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "config.h"
#include "blimit.hpp"

#include "utils.h"
#include "graph.h"

// barriers
std::atomic_bool can_start{false};
std::atomic<uint> how_many_ready_to_start{0};

std::atomic_bool can_finish{false};
std::atomic<uint> how_many_finished{0};

std::atomic_bool can_init{false};
std::atomic<uint> how_many_ready_to_init{0};

// misc round info
std::atomic_bool do_init{false};
std::atomic_bool first_pass{false};

#include "worker.h"

void read_input(std::ifstream &input_file){
    std::string line;
    while(true){
        char c = input_file.peek();
        if(c == '#' || c == '\n' || c == '\r'){
            getline(input_file, line);
            continue;
        }

        uint32_t u, v, weight;
        if(!(input_file >> u >> v >> weight))
            break;

        graph.add_edge(u, v, weight);
    }
}

int main(int argc, char* argv[]){
    #ifdef measure_time
    auto main_start = now();
    #endif

    if(argc != 4){
        std::cerr << "usage: " << argv[0] << " thread-count inputfile b-limit" << std::endl;
        return 1;
    }

    const uint32_t b_limit      = std::stoi(argv[3]);
    const uint32_t thread_count = std::stoi(argv[1]);
    std::ifstream input_file(argv[2]);
 
    #ifdef measure_time
    auto start_read = now();
    #endif

    read_input(input_file);

    // Handle a trivial case
    if(graph.size() == 0){
        for(uint32_t b_method=0;b_method<=b_limit;++b_method)
            std::cout << "0\n";
        return 0;
    }

    #ifdef measure_time
    double input_time = t_diff(start_read, now());
    auto start = now();
    auto start_init = now();
    double time_all{0};
    double time_all_init{0};
    #endif

    graph.sortNeighbourLists();
    
    #ifdef measure_time
    double sort_time = t_diff(start, now());
    start = now();
    #endif

    assert(graph.size());
    std::vector<uint> round_verticles;

    std::vector<uint> all_verticles;
    all_verticles.resize(graph.size());
    for(uint32_t i=0;i<graph.size();++i)
        all_verticles[i]=i;

    std::vector<std::thread> threads(thread_count-1);
    for(uint32_t i=0;i<thread_count-1;++i){
        threads[i] = std::thread(workerLoop, std::cref(all_verticles), std::ref(round_verticles), i+1, thread_count);
        threads[i].detach();
    }

    #ifdef measure_time
    std::cerr << "Processing b: ";
    #endif

    for(uint32_t b_method = 0; b_method <= b_limit; ++b_method) {

        #ifdef measure_time // print the progress
        std::cerr << b_method;
        if(b_method!=b_limit)
            std::cerr << ", ";
        else
            std::cerr << "\n\n";
        start_init = now();
        #endif

        while(how_many_ready_to_init.load() != thread_count-1);
        how_many_ready_to_init = 0;

        do_init = true;
        global_b_method = b_method;

        can_finish = false;
        can_init = true;

        graph.init(0, thread_count, global_b_method);
        first_pass = true;

        #ifdef measure_time
        time_all_init += t_diff(start_init, now());
        start = now();
        #endif

        while(round_verticles.size() || first_pass){

            while(how_many_ready_to_start.load() != thread_count-1);
            how_many_ready_to_start = 0;
            
            do_init = false;
            
            can_init = false;
            can_start = true;
            
            processingLoop(first_pass ? all_verticles : round_verticles, 0, thread_count);
            
            while(how_many_finished.load() != thread_count-1);
            how_many_finished = 0;

            first_pass = false;
            round_verticles = std::move(global_nodes_next_round);
            global_nodes_next_round.clear();
            graph.resetVerticles(round_verticles);

            can_start = false;
            can_finish = true;

            if(round_verticles.size()){
                while(how_many_ready_to_init.load()!=thread_count-1);
                how_many_ready_to_init = 0;
                
                can_finish = false;
                can_init = true;
            }
        }

        global_result += local_result;
        local_result = 0;
        
        std::cout << global_result << "\n";
        global_result = 0;

        #ifdef measure_time
        time_all += t_diff(start, now());
        #endif
    }

    #ifdef measure_time
    double overall_time = t_diff(main_start, now());
    std::cerr.setf(std::ios::fixed, std::ios::floatfield);
    std::cerr.setf(std::ios::showpoint);
    std::cerr << "Input			Sorting			Init			Algorithm		Overall\n";
    
    std::cerr << std::setprecision(3) << input_time << "s	";
    std::cerr << "(" << std::setprecision(1) << 100.0*input_time/overall_time << "%)		";
    std::cerr << std::setprecision(3) << sort_time << "s	";
    std::cerr << "(" << std::setprecision(1) << 100.0*sort_time/overall_time << "%)		";
    std::cerr << std::setprecision(3) << time_all_init << "s	";
    std::cerr << "(" << std::setprecision(1) << 100.0*time_all_init/overall_time << "%)		";
    std::cerr << std::setprecision(3) << time_all << "s	";
    std::cerr << "(" << std::setprecision(1) << 100.0*time_all/overall_time << "%)		";
    std::cerr << std::setprecision(3) << overall_time << "s\n\n";
    std::cerr << "Overall-input: " << std::setprecision(3) << time_all+time_all_init+sort_time << "s	";
    std::cerr << "(" << std::setprecision(1) << 100.0*(time_all+time_all_init+sort_time)/overall_time << "%)\n\n";
    #endif
}

