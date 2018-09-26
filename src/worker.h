thread_local std::vector<uint> local_nodes_next_round;
std::vector<uint> global_nodes_next_round;
std::atomic_flag nodes_next_round_mutex{ATOMIC_FLAG_INIT};

thread_local int32_t local_result = 0;
std::atomic<int64_t> global_result{0};

void flushLocalQueue(){
    spinlock(nodes_next_round_mutex);
    global_nodes_next_round.insert(
        std::end(global_nodes_next_round), 
        std::begin(local_nodes_next_round), 
        std::end(local_nodes_next_round)
    );
    spinunlock(nodes_next_round_mutex);
    local_nodes_next_round.clear();
}

inline bool is_eligible(const uint32_t id, const edge_t edge){
    if(graph.isEdgeAdorated(edge, id) || graph[edge.to].b_value == 0)
        return false;

    if(graph[edge.to].adorated_by_count < graph[edge.to].b_value)
        return true;
    
    const auto worst_edge = graph[edge.to].get_worst();
    if(worst_edge.weight == edge.weight)
        return graph[worst_edge.to].realId < graph[id].realId;

    return worst_edge.weight < edge.weight;
}

inline bool match(const uint32_t id){
    auto& node = graph[id];
    if(node.processed.test_and_set())
        return true;

    for(uint32_t i = 0; node.adorating_count < node.b_value && i < node.edges.size(); ++i){
        const auto& edge = node.edges[i];
        auto& node_to = graph[edge.to];
        if(is_eligible(id, edge)){
            spinlock(node_to.mutex);
            if(is_eligible(id, edge)){
                auto adoration_result = node_to.adorate(id, edge);
                
                const auto& result_change = adoration_result.second;
                local_result += result_change;
                if(local_result > 1e9 || local_result < -1e9){
                    global_result += local_result;
                    local_result = 0;
                }

                const auto& invalidated_node = adoration_result.first;
                if(invalidated_node != -1){
                    local_nodes_next_round.push_back(invalidated_node);
                    if(local_nodes_next_round.size() >= local_queue_size)
                        flushLocalQueue();
                }
            }
            spinunlock(node_to.mutex);
        }
    }

    return false;
}

void processingLoop(const std::vector<uint> &v, const uint32_t tid, const uint32_t tcount){
    const uint32_t piece_size = get_piece_size(v.size(), tcount);
    uint32_t i = (piece_size*tid) % v.size();

    while(true){
        if(match(v[i]))
            break;
        ++i;
        i %= v.size();
    }

    flushLocalQueue();
    global_result += local_result;
    local_result = 0;
}

std::atomic<uint> global_b_method{0};

void workerLoop(const std::vector<uint> &all_verticles, 
                const std::vector<uint> &round_verticles, 
                const uint32_t tid, 
                const uint32_t tcount)
{
    local_nodes_next_round.reserve(local_queue_size);

    while(true){

        ++how_many_ready_to_init;
        while(!can_init.load());

        if(do_init)
            graph.init(tid, tcount, global_b_method);

        ++how_many_ready_to_start;
        while(!can_start.load());

        const auto& verticles = (first_pass ? all_verticles : round_verticles);
        assert(!verticles.empty());

        processingLoop(verticles, tid, tcount);

        ++how_many_finished;
        while(!can_finish.load());
    }
}