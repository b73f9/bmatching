auto now(){
    return std::chrono::system_clock::now();
}

auto t_diff(auto start, auto end){
    return ((double)std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count())/1000.0;
}

inline void spinlock(std::atomic_flag &b){
    while(b.test_and_set());
}

inline void spinunlock(std::atomic_flag &b){
    b.clear();
}

uint32_t get_piece_size(uint32_t wsize, uint32_t tcount){
    if(tcount>1)
        return std::max((uint)ceil((double)wsize/(double)tcount), 1U);
    else
        return wsize;
}