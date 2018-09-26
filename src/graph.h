struct edge_state_t{
    private:
        uint32_t owner;
        std::atomic_char state;

    public:    
        edge_state_t(const uint32_t Owner) : owner(Owner), state(0) {}

        edge_state_t(const edge_state_t &e) : state(0){
            *this = e;
        }

        edge_state_t(edge_state_t &&e) : state(0) {
            *this = std::move(e);
        }

        edge_state_t& operator=(const edge_state_t &e){
            owner = e.owner;
            state.store(e.state.load());
            return *this;
        }

        edge_state_t& operator=(edge_state_t &&e){
            *this = e;
            return *this;
        }

        inline char getNodeFlag(const uint32_t nodeId){
            return (owner == nodeId ? 0x1 : 0x2);
        }
        
        inline bool is_adorating(const uint32_t nodeId){
            return (state.load() & getNodeFlag(nodeId));
        }

        inline bool set_adorating(const uint32_t id){
            const char nodeFlag = getNodeFlag(id);
            return (state.fetch_or(nodeFlag) | nodeFlag) == 0x3;
        }

        inline bool set_not_adorating(const uint32_t id){
            return state.fetch_and(~getNodeFlag(id)) == 0x3;
        }
        
        inline void clear_state(){
            state = 0;
        }
};


class edge_t{
    public:
        uint32_t to;
        uint32_t weight;
        uint32_t id;

        edge_t(uint32_t To, uint32_t Weight, uint32_t Id)
         : to(To), weight(Weight), id(Id) {
        }
};

class node_t {
    private:
        std::priority_queue<edge_t> adorated_by;
        std::atomic_flag queue_mutex{ATOMIC_FLAG_INIT};

        void cpy(node_t &e){
            realId = e.realId;
            b_value = e.b_value;
            if(e.next_round.test_and_set())
                next_round.test_and_set();
            else
                e.next_round.clear();
            if(e.processed.test_and_set())
                processed.test_and_set();
            else
                e.processed.clear();
            adorating_count = e.adorating_count;
            adorated_by_count = e.adorated_by_count;
            rem_adorating_count = e.rem_adorating_count.load();
        }

    public: 
        uint32_t realId;
        uint32_t b_value;

        std::atomic_flag next_round{ATOMIC_FLAG_INIT};
        std::atomic_flag processed{ATOMIC_FLAG_INIT};

        uint32_t adorated_by_count = 0;

        uint32_t adorating_count = 0;
        std::atomic<uint32_t> rem_adorating_count{0};
        
        std::atomic_flag mutex{ATOMIC_FLAG_INIT};
        std::vector<edge_t> edges;

        node_t(const uint32_t Real_id) : realId(Real_id) {}

        node_t(node_t &e){
            *this = e;
        }

        node_t(node_t &&e){
            *this = std::move(e);
        }

        node_t& operator=(node_t &e){
            cpy(e);
            edges = e.edges;
            adorated_by = e.adorated_by;
            return *this;
        }

        node_t& operator=(node_t &&e){
            cpy(e);
            edges = std::move(e.edges);
            adorated_by = std::move(e.adorated_by);
            return *this;
        }

        inline edge_t get_worst(){
            spinlock(queue_mutex);
            edge_t e = adorated_by.top();
            spinunlock(queue_mutex);
            return e;
        }
        
        std::pair<int32_t, int32_t> adorate(const uint32_t id, const edge_t &edge);

        inline void reset(const uint32_t b_value){
            this->b_value = b_value;

            next_round.clear();

            processed.clear();

            adorated_by = std::priority_queue<edge_t>();
            adorated_by_count = 0;

            adorating_count = 0;
            rem_adorating_count = 0;
        } 

        inline void addToAdorationQueue(const uint32_t nodeId, const edge_t& edge){
            spinlock(queue_mutex);
            if(adorated_by_count == b_value)
                adorated_by.pop();
            else 
                adorated_by_count++;
            adorated_by.push(edge_t(nodeId, edge.weight, edge.id));
            spinunlock(queue_mutex);

            assert(adorated_by_count <= b_value);
        }
};

class graph_t {
    private:
        std::unordered_map<uint, uint> id2index;
        std::vector<edge_state_t> edge_state;
        std::vector<node_t> nodes;
        
        uint32_t getNormalizedId(const uint32_t id){
            if(id2index.count(id))
                return id2index[id];

            id2index[id] = nodes.size();
            nodes.emplace_back(id);

            return nodes.size()-1;
        }

    public:
        inline node_t& operator[] (const size_t id) {
          return nodes[id];
        }

        inline size_t size(){
            return nodes.size();
        }
        
        void add_edge(uint32_t u, uint32_t v, const uint32_t weight){
            u = getNormalizedId(u);
            v = getNormalizedId(v);

            nodes[u].edges.push_back(edge_t(v, weight, edge_state.size()));
            edge_state.push_back(edge_state_t(v));
            const uint32_t edge_id = nodes[u].edges.back().id;

            nodes[v].edges.push_back(edge_t(u, weight, edge_id));
        }

        void sortNeighbourLists(){
            for(auto& node : nodes)
                std::sort(node.edges.begin(), node.edges.end());
        }

        inline bool setNodeNextRound(const uint32_t nodeId){
            return nodes[nodeId].next_round.test_and_set();
        }
        
        inline bool setEdgeNotAdorated(const edge_t& edge){
            nodes[edge.to].rem_adorating_count++;
            return edge_state[edge.id].set_not_adorating(edge.to);
        }
        
        inline bool setEdgeAdorated(const uint32_t nodeId, const edge_t& edge){
            nodes[nodeId].adorating_count++;
            return edge_state[edge.id].set_adorating(nodeId);
        }

        inline bool isEdgeAdorated(const edge_t &edge, const uint32_t id){
            return edge_state[edge.id].is_adorating(id);
        }

        void init(const uint32_t tid, const uint32_t tcount, const uint32_t b_method){
            const uint32_t graph_piece_size = get_piece_size(nodes.size(), tcount);
            const uint32_t graph_start = graph_piece_size*tid;
            const uint32_t graph_end = std::min((size_t)graph_piece_size*(tid+1), nodes.size());

            for(uint32_t i=graph_start;i<graph_end;++i)
                nodes[i].reset(bvalue(b_method, nodes[i].realId));
            
            const uint32_t edges_piece_size = get_piece_size(edge_state.size(), tcount);
            const uint32_t edges_start = edges_piece_size*tid;
            const uint32_t edges_end = std::min((size_t)edges_piece_size*(tid+1), edge_state.size());
            for(uint32_t i=edges_start;i<edges_end;++i)
                edge_state[i].clear_state();
        }

        void resetVerticles(const std::vector<uint32_t> &verticle_list){
            for(auto x : verticle_list){
                nodes[x].next_round.clear();
                nodes[x].processed.clear();
                nodes[x].adorating_count -= nodes[x].rem_adorating_count.load();
                nodes[x].rem_adorating_count = 0;
            }
        }
};

graph_t graph;

std::pair<int32_t, int32_t> node_t::adorate(const uint32_t id, const edge_t &edge){
    int32_t result_change = 0;
    int32_t invalidated_node = -1;
    if(adorated_by_count == b_value){
        const auto& worst_edge = get_worst();

        if(graph.setEdgeNotAdorated(worst_edge))
            result_change -= worst_edge.weight;

        if(!graph.setNodeNextRound(worst_edge.to))
            invalidated_node = worst_edge.to;
    }
    if(graph.setEdgeAdorated(id, edge))
        result_change += edge.weight; 

    addToAdorationQueue(id, edge);
    return {invalidated_node, result_change};
}

bool operator<(const edge_t& lhs, const edge_t& rhs){
    if(lhs.weight == rhs.weight)
        return graph[lhs.to].realId > graph[rhs.to].realId;
    return lhs.weight > rhs.weight;
}