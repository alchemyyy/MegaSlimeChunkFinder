#ifndef MEGASLIMECHUNKFINDER_H
#define MEGASLIMECHUNKFINDER_H

#include <vector>
#include <set>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <immintrin.h>

// CONFIGURATION
constexpr int64_t WORK_UNIT_SIZE = 1024;

// DATA STRUCTURES
struct Rectangle {
    int64_t x, z;
    int64_t width, height;
    int64_t area;
    int64_t distanceSquared;

    bool operator<(const Rectangle& other) const;
};

// SLIME CHUNK DETECTION
bool isSlimeChunk(int64_t chunkX, int64_t chunkZ, int64_t worldSeed);
void isSlimeChunkVec16(const int64_t* chunkX, const int64_t* chunkZ, int64_t worldSeed, bool* results);

// RECTANGLE FINDING
void findMaximalRectangles(const std::vector<std::vector<bool>>& grid,
                           int64_t startRow, int64_t endRow,
                           int64_t offsetX, int64_t offsetZ,
                           int64_t minimumRectDimension,
                           std::mutex& resultsMutex,
                           std::set<Rectangle>& foundRectangles,
                           bool debugMode = false);

void processRegion(int64_t minX, int64_t maxX, int64_t minZ, int64_t maxZ,
                   int64_t worldSeed,
                   int64_t minimumRectDimension,
                   int64_t searchMinX, int64_t searchMaxX, int64_t searchMinZ, int64_t searchMaxZ,
                   std::mutex& resultsMutex,
                   std::set<Rectangle>& foundRectangles,
                   std::atomic<int64_t>& chunksProcessed,
                   bool debugMode = false);

void generateWorkQueue(int64_t searchMinX, int64_t searchMaxX, int64_t searchMinZ, int64_t searchMaxZ,
                       std::vector<std::pair<std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>>& workQueue);

void workerThread(int64_t threadId, int64_t numThreads,
                  int64_t worldSeed,
                  int64_t minimumRectDimension,
                  int64_t searchMinX, int64_t searchMaxX, int64_t searchMinZ, int64_t searchMaxZ,
                  std::mutex& resultsMutex,
                  std::set<Rectangle>& foundRectangles,
                  std::atomic<bool>& pauseFlag,
                  std::atomic<int64_t>& chunksProcessed,
                  std::atomic<int64_t>& maxDistanceReached,
                  std::vector<std::pair<std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>>& workQueue,
                  std::atomic<int64_t>& workQueueIndex,
                  bool debugMode = false);

void printStats(const std::atomic<int64_t>& chunksProcessed,
                const std::atomic<int64_t>& maxDistanceReached,
                const std::set<Rectangle>& foundRectangles,
                bool toFile = false);

#endif // MEGASLIMECHUNKFINDER_H
