#include "megaslimechunkfinder.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <cstring>
#include <cmath>

// AVX-512 SLIME CHUNK DETECTION

// AVX-512 optimized slime chunk detection for 16 chunks in parallel.
// Uses 32-bit SIMD operations for 2x throughput.
inline void isSlimeChunkVec16(const int64_t* chunkX, const int64_t* chunkZ, int64_t worldSeed, bool* results) {
    // Convert to int32_t
    alignas(64) int32_t x_i32[16];
    alignas(64) int32_t z_i32[16];
    
    for (int i = 0; i < 16; i++) {
        x_i32[i] = (int32_t)chunkX[i];
        z_i32[i] = (int32_t)chunkZ[i];
    }
    
    // Load as 32-bit integers
    __m512i x = _mm512_load_si512((__m512i*)x_i32);
    __m512i z = _mm512_load_si512((__m512i*)z_i32);
    
    // All intermediate calculations in 32-bit
    __m512i x_x = _mm512_mullo_epi32(x, x);
    __m512i z_z = _mm512_mullo_epi32(z, z);
    
    __m512i term1 = _mm512_mullo_epi32(x_x, _mm512_set1_epi32(0x4c1906));
    __m512i term2 = _mm512_mullo_epi32(x, _mm512_set1_epi32(0x5ac0db));
    __m512i term4 = _mm512_mullo_epi32(z, _mm512_set1_epi32(0x5f24f));
    
    // term3 requires 64-bit multiply: (int64_t)(z*z) * 0x4307a7LL
    __m256i z_z_lo256 = _mm512_extracti32x8_epi32(z_z, 0);
    __m256i z_z_hi256 = _mm512_extracti32x8_epi32(z_z, 1);
    __m512i z_z_lo64 = _mm512_cvtepi32_epi64(z_z_lo256);
    __m512i z_z_hi64 = _mm512_cvtepi32_epi64(z_z_hi256);
    __m512i term3_lo = _mm512_mullo_epi64(z_z_lo64, _mm512_set1_epi64(0x4307a7));
    __m512i term3_hi = _mm512_mullo_epi64(z_z_hi64, _mm512_set1_epi64(0x4307a7));
    
    // Store intermediate results
    alignas(64) int32_t term1_arr[16], term2_arr[16], term4_arr[16];
    alignas(64) int64_t term3_lo_arr[8], term3_hi_arr[8];
    
    _mm512_store_si512((__m512i*)term1_arr, term1);
    _mm512_store_si512((__m512i*)term2_arr, term2);
    _mm512_store_si512((__m512i*)term4_arr, term4);
    _mm512_store_si512((__m512i*)term3_lo_arr, term3_lo);
    _mm512_store_si512((__m512i*)term3_hi_arr, term3_hi);
    
    // Process all 16 chunks
    for (int i = 0; i < 16; i++) {
        int64_t t1 = (int64_t)term1_arr[i];
        int64_t t2 = (int64_t)term2_arr[i];
        int64_t t3 = (i < 8) ? term3_lo_arr[i] : term3_hi_arr[i - 8];
        int64_t t4 = (int64_t)term4_arr[i];
        
        int64_t seed = worldSeed + t1 + t2 + t3 + t4 ^ 0x3ad8025fLL;
        seed = (seed ^ 0x5DEECE66DULL) & 0xFFFFFFFFFFFFULL;
        seed = (seed * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
        int32_t bits = (int32_t)(seed >> 17);
        
        results[i] = (bits % 10) == 0;
    }
}

// Scalar slime chunk detection.
// Only bits [17..47] of the final seed matter for divisibility check.
bool isSlimeChunk(int64_t chunkX, int64_t chunkZ, int64_t worldSeed) {
    // Cast to int32_t to match Java's int behavior
    int32_t x = (int32_t)chunkX;
    int32_t z = (int32_t)chunkZ;
    
    // Calculate initial seed. XOR has lower precedence than addition.
    int64_t seed = worldSeed +
                   (int64_t)(x * x * 0x4c1906) +
                   (int64_t)(x * 0x5ac0db) +
                   (int64_t)(z * z) * 0x4307a7LL +
                   (int64_t)(z * 0x5f24f) ^ 0x3ad8025fLL;
    
    // XOR with Java Random constant and mask to 48 bits
    seed = (seed ^ 0x5DEECE66DULL) & 0xFFFFFFFFFFFFULL;
    
    // Java Random LCG step
    seed = (seed * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
    
    // Extract bits [17..47] as signed 32-bit
    int32_t bits = (int32_t)(seed >> 17);
    
    return (bits % 10) == 0;
}

// Check divisibility by 10 using bit tricks.
// x % 10 == 0 iff x % 2 == 0 AND x % 5 == 0
inline bool isDivisibleBy10(int32_t x) {
    if (x & 1) return false;
    return (x % 5) == 0;
}

// Alternative slime chunk detection using isDivisibleBy10
inline bool isSlimeChunkFast(int64_t chunkX, int64_t chunkZ, int64_t worldSeed) {
    int32_t x = (int32_t)chunkX;
    int32_t z = (int32_t)chunkZ;
    
    int64_t seed = worldSeed +
                   (int64_t)(x * x * 0x4c1906) +
                   (int64_t)(x * 0x5ac0db) +
                   (int64_t)(z * z) * 0x4307a7LL +
                   (int64_t)(z * 0x5f24f) ^ 0x3ad8025fLL;
    
    seed = (seed ^ 0x5DEECE66DULL) & 0xFFFFFFFFFFFFULL;
    seed = (seed * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
    
    int32_t bits = (int32_t)(seed >> 17);
    
    return isDivisibleBy10(bits);
}

// RECTANGLE STRUCTURE
bool Rectangle::operator<(const Rectangle& other) const {
    // Sort by area (largest first)
    if (area != other.area) return area > other.area;
    
    // Sort by distance from spawn (closest first)
    if (distanceSquared != other.distanceSquared) return distanceSquared < other.distanceSquared;
    
    // Consistent ordering by coordinates
    if (x != other.x) return x < other.x;
    return z < other.z;
}

// RECTANGLE FINDING

// Maximal rectangle detection using histogram algorithm
void findMaximalRectangles(const std::vector<std::vector<bool>>& grid, 
                           int64_t startRow, int64_t endRow,
                           int64_t offsetX, int64_t offsetZ,
                           int64_t minimumRectDimension,
                           std::mutex& resultsMutex,
                           std::set<Rectangle>& foundRectangles,
                           bool debugMode) {
    
    if (grid.empty()) return;
    
    int64_t rows = endRow - startRow;
    int64_t cols = grid[0].size();
    
    if (debugMode) {
        bool isTestRegion = (offsetX <= 1495 && offsetX + cols > 1495 && 
                             offsetZ <= 8282 && offsetZ + rows > 8282);
        if (isTestRegion) {
            std::lock_guard<std::mutex> lock(resultsMutex);
            std::cout << "[DEBUG] findMaximalRectangles called: offsetX=" << offsetX 
                      << " offsetZ=" << offsetZ << " rows=" << rows << " cols=" << cols << "\n";
        }
    }
    
    // Build height matrix for histogram-based algorithm
    std::vector<std::vector<int64_t>> heights(rows, std::vector<int64_t>(cols, 0));
    
    for (int64_t i = 0; i < rows; i++) {
        for (int64_t j = 0; j < cols; j++) {
            if (grid[i][j]) {
                heights[i][j] = (i == 0) ? 1 : heights[i-1][j] + 1;
            }
        }
    }
    
    // Find all maximal rectangles using histogram algorithm
    for (int64_t row = 0; row < rows; row++) {
        std::vector<int64_t>& height = heights[row];
        
        // For each position, find all maximal rectangles ending at this row
        for (int64_t i = 0; i < cols; i++) {
            if (height[i] == 0) continue;
            
            int64_t minHeight = height[i];
            
            // Extend rectangle to the right
            for (int64_t j = i; j < cols && height[j] > 0; j++) {
                minHeight = std::min(minHeight, height[j]);
                int64_t width = j - i + 1;
                int64_t h = minHeight;
                
                // Report all rectangles that meet minimum size requirement
                if (width >= minimumRectDimension && h >= minimumRectDimension) {
                    Rectangle rect;
                    rect.x = offsetX + i;
                    rect.z = offsetZ + (row - h + 1);
                    rect.width = width;
                    rect.height = h;
                    rect.area = width * h;
                    
                    // Calculate distance from spawn (0,0) using rectangle center in BLOCK coordinates
                    int64_t centerChunkX = rect.x + rect.width / 2;
                    int64_t centerChunkZ = rect.z + rect.height / 2;
                    int64_t centerBlockX = centerChunkX * 16;
                    int64_t centerBlockZ = centerChunkZ * 16;
                    rect.distanceSquared = centerBlockX * centerBlockX + centerBlockZ * centerBlockZ;
                    
                    if (debugMode && rect.x == 1495 && rect.z == 8282 && rect.width == 3 && rect.height == 3) {
                        std::lock_guard<std::mutex> lock(resultsMutex);
                        std::cout << "[DEBUG] Found target 3x3! offsetX=" << offsetX << " offsetZ=" << offsetZ 
                                  << " i=" << i << " row=" << row << " h=" << h << "\n";
                    }
                    
                    std::lock_guard<std::mutex> lock(resultsMutex);
                    foundRectangles.insert(rect);
                }
            }
        }
    }
}

// Process a rectangular region with overlap padding to catch boundary rectangles
void processRegion(int64_t minX, int64_t maxX, int64_t minZ, int64_t maxZ,
                   int64_t worldSeed,
                   int64_t minimumRectDimension,
                   int64_t searchMinX, int64_t searchMaxX, int64_t searchMinZ, int64_t searchMaxZ,
                   std::mutex& resultsMutex,
                   std::set<Rectangle>& foundRectangles,
                   std::atomic<int64_t>& chunksProcessed,
                   bool debugMode) {
    
    if (debugMode) {
        bool isDebugRegion = (minX <= 1495 && maxX > 1495 && minZ <= 8284 && maxZ > 8284);
        if (isDebugRegion) {
            std::lock_guard<std::mutex> lock(resultsMutex);
            std::cout << "[DEBUG] processRegion input: X[" << minX << "-" << maxX << "] Z[" << minZ << "-" << maxZ << "]\n";
            std::cout << "[DEBUG] SEARCH bounds (chunks): X[" << (searchMinX/16) << "-" << (searchMaxX/16) 
                      << "] Z[" << (searchMinZ/16) << "-" << (searchMaxZ/16) << "]\n";
        }
    }
    
    // Add padding to ensure rectangles on boundaries aren't missed
    int64_t paddedMinX = minX - minimumRectDimension + 1;
    int64_t paddedMaxX = maxX + minimumRectDimension - 1;
    int64_t paddedMinZ = minZ - minimumRectDimension + 1;
    int64_t paddedMaxZ = maxZ + minimumRectDimension - 1;
    
    // Clamp to search bounds (convert block bounds to chunk bounds)
    int64_t searchMinChunkX = searchMinX / 16;
    int64_t searchMaxChunkX = searchMaxX / 16;
    int64_t searchMinChunkZ = searchMinZ / 16;
    int64_t searchMaxChunkZ = searchMaxZ / 16;
    
    paddedMinX = std::max(paddedMinX, searchMinChunkX);
    paddedMaxX = std::min(paddedMaxX, searchMaxChunkX);
    paddedMinZ = std::max(paddedMinZ, searchMinChunkZ);
    paddedMaxZ = std::min(paddedMaxZ, searchMaxChunkZ);
    
    int64_t width = paddedMaxX - paddedMinX;
    int64_t height = paddedMaxZ - paddedMinZ;
    
    if (width < minimumRectDimension || height < minimumRectDimension) {
        return;
    }
    
    // Build slime chunk grid using AVX-512 vectorized detection
    std::vector<std::vector<bool>> grid(height, std::vector<bool>(width, false));
    
    // Batch arrays for AVX-512 16-way processing
    alignas(64) int64_t chunkXBatch[16];
    alignas(64) int64_t chunkZBatch[16];
    alignas(64) bool resultsBatch[16];
    int64_t gridXBatch[16];
    int64_t gridZBatch[16];
    
    int64_t batchIdx = 0;
    
    for (int64_t z = 0; z < height; z++) {
        for (int64_t x = 0; x < width; x++) {
            int64_t chunkX = paddedMinX + x;
            int64_t chunkZ = paddedMinZ + z;
            
            chunkXBatch[batchIdx] = chunkX;
            chunkZBatch[batchIdx] = chunkZ;
            gridXBatch[batchIdx] = x;
            gridZBatch[batchIdx] = z;
            batchIdx++;
            
            // Process batch of 16 chunks with AVX-512
            if (batchIdx == 16) {
                isSlimeChunkVec16(chunkXBatch, chunkZBatch, worldSeed, resultsBatch);
                
                for (int64_t i = 0; i < 16; i++) {
                    grid[gridZBatch[i]][gridXBatch[i]] = resultsBatch[i];
                }
                
                batchIdx = 0;
            }
        }
    }
    
    // Process remaining chunks (less than 16)
    if (batchIdx > 0) {
        // Pad with dummy values
        for (int64_t i = batchIdx; i < 16; i++) {
            chunkXBatch[i] = 0;
            chunkZBatch[i] = 0;
        }
        
        isSlimeChunkVec16(chunkXBatch, chunkZBatch, worldSeed, resultsBatch);
        
        for (int64_t i = 0; i < batchIdx; i++) {
            grid[gridZBatch[i]][gridXBatch[i]] = resultsBatch[i];
        }
    }
    
    // Find rectangles in this grid
    findMaximalRectangles(grid, 0, height, paddedMinX, paddedMinZ, minimumRectDimension, resultsMutex, foundRectangles, debugMode);
    
    // Only count the non-padded region for progress tracking
    chunksProcessed += (maxX - minX) * (maxZ - minZ);
}

// Generate work queue sorted by distance from origin
void generateWorkQueue(int64_t searchMinX, int64_t searchMaxX, int64_t searchMinZ, int64_t searchMaxZ,
                       std::vector<std::pair<std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>>& workQueue) {
    struct WorkUnit {
        int64_t minX, maxX, minZ, maxZ;
        int64_t distSquared;
    };
    
    std::vector<WorkUnit> units;
    
    // Convert block bounds to chunk bounds
    int64_t searchMinChunkX = searchMinX / 16;
    int64_t searchMaxChunkX = searchMaxX / 16;
    int64_t searchMinChunkZ = searchMinZ / 16;
    int64_t searchMaxChunkZ = searchMaxZ / 16;
    
    // Generate all work units (in chunks)
    for (int64_t x = searchMinChunkX; x < searchMaxChunkX; x += WORK_UNIT_SIZE) {
        for (int64_t z = searchMinChunkZ; z < searchMaxChunkZ; z += WORK_UNIT_SIZE) {
            WorkUnit unit;
            unit.minX = x;
            unit.maxX = std::min(x + WORK_UNIT_SIZE, searchMaxChunkX);
            unit.minZ = z;
            unit.maxZ = std::min(z + WORK_UNIT_SIZE, searchMaxChunkZ);
            
            // Calculate distance from origin (center of work unit)
            int64_t centerX = (unit.minX + unit.maxX) / 2;
            int64_t centerZ = (unit.minZ + unit.maxZ) / 2;
            unit.distSquared = centerX * centerX + centerZ * centerZ;
            
            units.push_back(unit);
        }
    }
    
    // Sort by distance from origin (closest first)
    std::sort(units.begin(), units.end(), [](const WorkUnit& a, const WorkUnit& b) {
        return a.distSquared < b.distSquared;
    });
    
    // Convert to work queue
    workQueue.reserve(units.size());
    for (const auto& unit : units) {
        workQueue.push_back({{unit.minX, unit.maxX}, {unit.minZ, unit.maxZ}});
    }
}

// Worker thread - grabs work from queue dynamically
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
                  bool debugMode) {
    while (!pauseFlag) {
        // Atomically grab next work unit
        int64_t idx = workQueueIndex.fetch_add(1, std::memory_order_relaxed);
        
        if (idx >= (int64_t)workQueue.size()) {
            break;
        }
        
        auto& work = workQueue[idx];
        int64_t minX = work.first.first;
        int64_t maxX = work.first.second;
        int64_t minZ = work.second.first;
        int64_t maxZ = work.second.second;
        
        if (debugMode) {
            if (minX <= 1495 && maxX > 1495 && minZ <= 8282 && maxZ > 8282) {
                std::lock_guard<std::mutex> lock(resultsMutex);
                std::cout << "[DEBUG] Processing work unit containing test 3x3: X[" << minX << "-" << maxX 
                          << "] Z[" << minZ << "-" << maxZ << "]\n";
            }
        }
        
        processRegion(minX, maxX, minZ, maxZ, worldSeed, minimumRectDimension,
                     searchMinX, searchMaxX, searchMinZ, searchMaxZ,
                     resultsMutex, foundRectangles, chunksProcessed, debugMode);
        
        // Update max distance
        int64_t centerX = (minX + maxX) / 2;
        int64_t centerZ = (minZ + maxZ) / 2;
        int64_t dist = (int64_t)std::sqrt(centerX * centerX + centerZ * centerZ);
        
        int64_t currentMax = maxDistanceReached.load(std::memory_order_relaxed);
        while (dist > currentMax && 
               !maxDistanceReached.compare_exchange_weak(currentMax, dist, std::memory_order_relaxed)) {
        }
    }
}

void printStats(const std::atomic<int64_t>& chunksProcessed,
                const std::atomic<int64_t>& maxDistanceReached,
                const std::set<Rectangle>& foundRectangles,
                bool toFile) {
    std::ostream* out = &std::cout;
    std::ofstream fileOut;
    
    if (toFile) {
        fileOut.open("slimechunkfinder.txt", std::ios::out | std::ios::trunc);
        if (fileOut.is_open()) {
            out = &fileOut;
        }
    }
    
    *out << "\n========================================\n";
    *out << "CURRENT STATISTICS\n";
    *out << "========================================\n";
    *out << "Chunks processed: " << chunksProcessed.load() << "\n";
    *out << "Max distance: " << maxDistanceReached.load() << " chunks\n";
    *out << "Rectangles found: " << foundRectangles.size() << "\n\n";
    
    if (!foundRectangles.empty()) {
        *out << "All rectangles (sorted by size, then distance from spawn):\n";
        *out << std::setw(12) << "Area" 
             << std::setw(10) << "Width" 
             << std::setw(10) << "Height"
             << std::setw(12) << "Block X"
             << std::setw(12) << "Block Z"
             << std::setw(14) << "Euclidean"
             << std::setw(14) << "Manhattan" << "\n";
        *out << std::string(84, '-') << "\n";
        
        for (const auto& rect : foundRectangles) {
            // Calculate center point in chunk coordinates, then convert to blocks
            int64_t centerChunkX = rect.x + rect.width / 2;
            int64_t centerChunkZ = rect.z + rect.height / 2;
            int64_t centerBlockX = centerChunkX * 16;
            int64_t centerBlockZ = centerChunkZ * 16;
            
            // Euclidean distance from spawn (already calculated in block coords)
            int64_t euclidean = (int64_t)std::sqrt(rect.distanceSquared);
            
            // Manhattan distance from spawn (in block coordinates)
            int64_t manhattan = std::abs(centerBlockX) + std::abs(centerBlockZ);
            
            *out << std::setw(12) << rect.area
                 << std::setw(10) << rect.width
                 << std::setw(10) << rect.height
                 << std::setw(12) << (rect.x * 16)
                 << std::setw(12) << (rect.z * 16)
                 << std::setw(14) << euclidean
                 << std::setw(14) << manhattan << "\n";
        }
    }
    *out << "========================================\n\n";
    
    if (toFile && fileOut.is_open()) {
        fileOut.close();
    }
}
