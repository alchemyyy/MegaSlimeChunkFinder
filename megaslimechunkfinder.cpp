#include "megaslimechunkfinder.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <iomanip>

// SIGNAL HANDLING
std::atomic<bool>* g_pauseFlag = nullptr;

void signalHandler(int signal) {
    if (signal == SIGINT && g_pauseFlag != nullptr) {
        g_pauseFlag->store(true);
    }
}

// MAIN
int main(int argc, char* argv[]) {
    // Configuration
    const int64_t WORLD_SEED = 413563856LL;
    const int64_t MINIMUM_RECT_DIMENSION = 3;

    // Search bounds (in blocks) - can be overridden via command line
    int64_t searchMinX = -30000000;
    int64_t searchMaxX = 30000000;
    int64_t searchMinZ = -30000000;
    int64_t searchMaxZ = 30000000;

    // State variables
    std::mutex resultsMutex;
    std::set<Rectangle> foundRectangles;
    std::atomic<bool> pauseFlag{false};
    std::atomic<int64_t> chunksProcessed{0};
    std::atomic<int64_t> maxDistanceReached{0};
    std::vector<std::pair<std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>> workQueue;
    std::atomic<int64_t> workQueueIndex{0};

    // Set up signal handler
    g_pauseFlag = &pauseFlag;
    signal(SIGINT, signalHandler);

    // Detect number of logical cores
    int64_t NUM_THREADS = std::thread::hardware_concurrency();
    if (NUM_THREADS == 0) {
        NUM_THREADS = 8;
    }

    std::cout << "Minecraft Slime Chunk Rectangle Finder (AVX-512 Optimized)\n";
    std::cout << "==========================================================\n";
    std::cout << "World Seed: " << WORLD_SEED << "\n";
    std::cout << "CPU Cores Detected: " << NUM_THREADS << "\n";
    std::cout << "Search Bounds (blocks): X[" << searchMinX << " to " << searchMaxX
              << "] Z[" << searchMinZ << " to " << searchMaxZ << "]\n";
    std::cout << "Search Bounds (chunks): X[" << (searchMinX/16) << " to " << (searchMaxX/16)
              << "] Z[" << (searchMinZ/16) << " to " << (searchMaxZ/16) << "]\n";
    std::cout << "Work Unit Size: " << WORK_UNIT_SIZE << " chunks\n";
    std::cout << "Min Rectangle Dimension: " << MINIMUM_RECT_DIMENSION << "x" << MINIMUM_RECT_DIMENSION << "\n";
    std::cout << "SIMD: AVX-512 16-wide vectorization enabled\n";
    std::cout << "Press Ctrl+C to pause and view stats\n\n";

    // Generate work queue sorted by distance from origin
    std::cout << "Generating work queue...\n";
    generateWorkQueue(searchMinX, searchMaxX, searchMinZ, searchMaxZ, workQueue);
    std::cout << "Work queue ready: " << workQueue.size() << " units\n\n";

    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int64_t i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(workerThread, i, NUM_THREADS, WORLD_SEED, MINIMUM_RECT_DIMENSION,
                           searchMinX, searchMaxX, searchMinZ, searchMaxZ,
                           std::ref(resultsMutex), std::ref(foundRectangles),
                           std::ref(pauseFlag), std::ref(chunksProcessed), std::ref(maxDistanceReached),
                           std::ref(workQueue), std::ref(workQueueIndex), false);
    }

    // Monitor thread
    std::thread monitor([&]() {
        int64_t totalWorkUnits = workQueue.size();
        while (!pauseFlag) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!pauseFlag) {
                int64_t completed = workQueueIndex.load();
                double percentage = (totalWorkUnits > 0) ? (100.0 * completed / totalWorkUnits) : 0.0;
                std::cout << "[Progress] " << std::fixed << std::setprecision(2) << percentage << "% "
                          << "(" << completed << "/" << totalWorkUnits << " units) | "
                          << "Chunks: " << chunksProcessed.load()
                          << " | Distance: " << maxDistanceReached.load()
                          << " | Found: " << foundRectangles.size() << "    \r" << std::flush;

                // Write current results to file
                printStats(chunksProcessed, maxDistanceReached, foundRectangles, true);
            }
        }
    });

    for (auto& t : threads) {
        t.join();
    }

    pauseFlag = true;
    monitor.join();

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    printStats(chunksProcessed, maxDistanceReached, foundRectangles);

    std::cout << "Total time: " << duration.count() / 1000.0 << " seconds\n";
    std::cout << "Throughput: " << (chunksProcessed.load() * 1000.0 / duration.count()) << " chunks/sec\n";

    // Clean up signal handler
    g_pauseFlag = nullptr;

    return 0;
}
