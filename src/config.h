// ============================    build settings    ============================
// Whether or not to collect the time specific parts of the program take
#define measure_time

// Disable asserts
#define NDEBUG


// ============================ performance settings ============================

// How many nodes are stored locally before being pushed to the global queue
const uint local_queue_size = 256;


// ============================       warnings       ============================

#ifdef measure_time
    #warning "Time measurement enabled!"
    #include <chrono>
    #include <iomanip>
#endif

#include <cassert>