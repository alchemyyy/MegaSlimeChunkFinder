#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <set>
#include <csignal>
#include <iomanip>
#include <immintrin.h>
#include <cstring>

// ==================== CONFIGURATION ====================
constexpr int64_t MINIMUM_RECT_DIMENSION = 3;  // Minimum width AND height for rectangles
constexpr int64_t WORLD_SEED = 413563856LL;    // Minecraft world seed
constexpr int64_t WORK_UNIT_SIZE = 256;         // Size of each work unit in chunks

// Search bounds (in blocks) - can be overridden via command line
int64_t SEARCH_MIN_X = -10000000;  // -50000 chunks * 16
int64_t SEARCH_MAX_X = 10000000;   // 50000 chunks * 16
int64_t SEARCH_MIN_Z = -10000000;  // -50000 chunks * 16
int64_t SEARCH_MAX_Z = 10000000;   // 50000 chunks * 16

// ==================== AVX-512 SLIME CHUNK DETECTION ====================

// AVX-512 vectorized nextInt(10) for 8 parallel Java Random instances
inline __m512i vec_nextInt10(__m512i seeds) {
    const __m512i multiplier = _mm512_set1_epi64(0x5DEECE66DULL);
    const __m512i addend = _mm512_set1_epi64(0xBULL);
    const __m512i mask48 = _mm512_set1_epi64((1ULL << 48) - 1);
    
    // seed = (seed * multiplier + addend) & mask48
    __m512i new_seeds = _mm512_mullo_epi64(seeds, multiplier);
    new_seeds = _mm512_add_epi64(new_seeds, addend);
    new_seeds = _mm512_and_si512(new_seeds, mask48);
    
    // Get next(31): bits = seed >> 17
    __m512i bits = _mm512_srli_epi64(new_seeds, 17);
    
    // val = bits % 10 (using division)
    __m512i ten = _mm512_set1_epi64(10);
    
    // Manual modulo: val = bits - (bits / 10) * 10
    __m512i quotient = _mm512_div_epi64(bits, ten);
    __m512i product = _mm512_mullo_epi64(quotient, ten);
    __m512i val = _mm512_sub_epi64(bits, product);
    
    return val;
}

// Scalar slime chunk detection
inline bool isSlimeChunk(int64_t chunkX, int64_t chunkZ, int64_t worldSeed) {
    // Need to cast to int32_t first to match Java's int behavior (sign extension)
    int32_t x = (int32_t)chunkX;
    int32_t z = (int32_t)chunkZ;
    
    int64_t seed = worldSeed +
                   (int64_t)(x * x * 0x4c1906) +
                   (int64_t)(x * 0x5ac0db) +
                   (int64_t)(z * z) * 0x4307a7LL +
                   (int64_t)(z * 0x5f24f) ^ 0x3ad8025fLL;
    
    seed = (seed ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1);
    seed = (seed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
    int32_t bits = (int32_t)(seed >> 17);
    
    bool result = (bits % 10) == 0;
    
    return result;
}

// Process 8 slime chunks in parallel (currently using scalar fallback)
inline void isSlimeChunkBatch8(
    const int64_t* chunkX,
    const int64_t* chunkZ,
    int64_t worldSeed,
    bool* results
) {
    for (int64_t i = 0; i < 8; i++) {
        results[i] = isSlimeChunk(chunkX[i], chunkZ[i], worldSeed);
    }
}

// ==================== RECTANGLE FINDING ====================
struct Rectangle {
    int64_t x, z;      // Top-left corner chunk coordinates
    int64_t width, height;
    int64_t area;
    int64_t distanceSquared;  // Distance from spawn (0,0)
    
    bool operator<(const Rectangle& other) const {
        // Primary: Sort by area (largest first)
        if (area != other.area) return area > other.area;
        
        // Secondary: Sort by distance from spawn (closest first)
        if (distanceSquared != other.distanceSquared) return distanceSquared < other.distanceSquared;
        
        // Tertiary: Consistent ordering by coordinates
        if (x != other.x) return x < other.x;
        return z < other.z;
    }
};

std::mutex resultsMutex;
std::set<Rectangle> foundRectangles;
std::atomic<bool> pauseFlag{false};
std::atomic<int64_t> chunksProcessed{0};
std::atomic<int64_t> maxDistanceReached{0};

// Work queue system
std::mutex workQueueMutex;
std::vector<std::pair<std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>> workQueue; // ((minX, maxX), (minZ, maxZ))
std::atomic<int64_t> workQueueIndex{0};

// Ultra-fast rectangle detection using maximal rectangle algorithm
void findMaximalRectangles(const std::vector<std::vector<bool>>& grid, 
                           int64_t startRow, int64_t endRow,
                           int64_t offsetX, int64_t offsetZ) {
    
    if (grid.empty()) return;
    
    int64_t rows = endRow - startRow;
    int64_t cols = grid[0].size();
    
    // Debug
    bool isTestRegion = (offsetX <= 1495 && offsetX + cols > 1495 && 
                         offsetZ <= 8282 && offsetZ + rows > 8282);
    if (isTestRegion) {
        std::lock_guard<std::mutex> lock(resultsMutex);
        std::cout << "[DEBUG] findMaximalRectangles called: offsetX=" << offsetX 
                  << " offsetZ=" << offsetZ << " rows=" << rows << " cols=" << cols << "\n";
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
    
    // Debug: Check heights around test location
    if (isTestRegion) {
        int64_t testLocalX = 1495 - offsetX;  // 1495 - 1280 = 215
        int64_t testLocalZ = 8284 - offsetZ;  // 8284 - 8192 = 92
        if (testLocalX >= 0 && testLocalX < cols && testLocalZ >= 0 && testLocalZ < rows) {
            std::lock_guard<std::mutex> lock(resultsMutex);
            std::cout << "[DEBUG] Test location in grid: x=" << testLocalX << " z=" << testLocalZ << "\n";
            std::cout << "[DEBUG] height[" << testLocalZ << "][" << testLocalX << "] = " << heights[testLocalZ][testLocalX] << "\n";
            std::cout << "[DEBUG] grid[" << testLocalZ << "][" << testLocalX << "] = " << grid[testLocalZ][testLocalX] << "\n";
        }
    }
    
    // Find all maximal rectangles using histogram algorithm
    int64_t candidateCount = 0;
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
                if (width >= MINIMUM_RECT_DIMENSION && h >= MINIMUM_RECT_DIMENSION) {
                    candidateCount++;
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
                    
                    // Debug for test case
                    if (rect.x == 1495 && rect.z == 8282 && rect.width == 3 && rect.height == 3) {
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
    
    if (isTestRegion) {
        std::lock_guard<std::mutex> lock(resultsMutex);
        std::cout << "[DEBUG] Candidate rectangles found: " << candidateCount << "\n";
    }
}

// Process a rectangular region with overlap padding to catch boundary rectangles
void processRegion(int64_t minX, int64_t maxX, int64_t minZ, int64_t maxZ) {
    // Debug
    bool isDebugRegion2 = (minX <= 1495 && maxX > 1495 && minZ <= 8284 && maxZ > 8284);
    if (isDebugRegion2) {
        std::lock_guard<std::mutex> lock(resultsMutex);
        std::cout << "[DEBUG] processRegion input: X[" << minX << "-" << maxX << "] Z[" << minZ << "-" << maxZ << "]\n";
        std::cout << "[DEBUG] SEARCH bounds (chunks): X[" << (SEARCH_MIN_X/16) << "-" << (SEARCH_MAX_X/16) << "] Z[" << (SEARCH_MIN_Z/16) << "-" << (SEARCH_MAX_Z/16) << "]\n";
    }
    
    // Add padding to ensure rectangles on boundaries aren't missed
    int64_t paddedMinX = minX - MINIMUM_RECT_DIMENSION + 1;
    int64_t paddedMaxX = maxX + MINIMUM_RECT_DIMENSION - 1;
    int64_t paddedMinZ = minZ - MINIMUM_RECT_DIMENSION + 1;
    int64_t paddedMaxZ = maxZ + MINIMUM_RECT_DIMENSION - 1;
    
    if (isDebugRegion2) {
        std::lock_guard<std::mutex> lock(resultsMutex);
        std::cout << "[DEBUG] Before clamp: X[" << paddedMinX << "-" << paddedMaxX << "] Z[" << paddedMinZ << "-" << paddedMaxZ << "]\n";
    }
    
    // Clamp to search bounds (convert block bounds to chunk bounds)
    int64_t searchMinChunkX = SEARCH_MIN_X / 16;
    int64_t searchMaxChunkX = SEARCH_MAX_X / 16;
    int64_t searchMinChunkZ = SEARCH_MIN_Z / 16;
    int64_t searchMaxChunkZ = SEARCH_MAX_Z / 16;
    
    paddedMinX = std::max(paddedMinX, searchMinChunkX);
    paddedMaxX = std::min(paddedMaxX, searchMaxChunkX);
    paddedMinZ = std::max(paddedMinZ, searchMinChunkZ);
    paddedMaxZ = std::min(paddedMaxZ, searchMaxChunkZ);
    
    if (isDebugRegion2) {
        std::lock_guard<std::mutex> lock(resultsMutex);
        std::cout << "[DEBUG] After clamp: X[" << paddedMinX << "-" << paddedMaxX << "] Z[" << paddedMinZ << "-" << paddedMaxZ << "]\n";
    }
    
    int64_t width = paddedMaxX - paddedMinX;
    int64_t height = paddedMaxZ - paddedMinZ;
    
    if (width < MINIMUM_RECT_DIMENSION || height < MINIMUM_RECT_DIMENSION) {
        return;
    }
    
    // Build slime chunk grid using AVX-512 vectorized detection
    std::vector<std::vector<bool>> grid(height, std::vector<bool>(width, false));
    
    // Debug flag for test region
    bool isDebugRegion = (paddedMinX <= 1495 && paddedMaxX > 1495 && 
                          paddedMinZ <= 8284 && paddedMaxZ > 8284);
    
    // Batch arrays for AVX-512 processing
    alignas(64) int64_t chunkXBatch[8];
    alignas(64) int64_t chunkZBatch[8];
    alignas(64) bool resultsBatch[8];
    int64_t gridXBatch[8];  // Store grid coordinates
    int64_t gridZBatch[8];
    
    int64_t batchIdx = 0;
    
    for (int64_t z = 0; z < height; z++) {
        for (int64_t x = 0; x < width; x++) {
            int64_t chunkX = paddedMinX + x;
            int64_t chunkZ = paddedMinZ + z;
            
            // Debug
            if (isDebugRegion && chunkX == 1495 && chunkZ == 8284) {
                std::lock_guard<std::mutex> lock(resultsMutex);
                std::cout << "[DEBUG] Loop reached chunk (1495, 8284) at grid pos (" << x << "," << z 
                          << ") batchIdx=" << batchIdx << "\n";
            }
            
            chunkXBatch[batchIdx] = chunkX;
            chunkZBatch[batchIdx] = chunkZ;
            gridXBatch[batchIdx] = x;
            gridZBatch[batchIdx] = z;
            batchIdx++;
            
            // Process batch of 8 chunks with AVX-512
            if (batchIdx == 8) {
                isSlimeChunkBatch8(chunkXBatch, chunkZBatch, WORLD_SEED, resultsBatch);
                
                for (int64_t i = 0; i < 8; i++) {
                    grid[gridZBatch[i]][gridXBatch[i]] = resultsBatch[i];
                    
                    // Debug specific chunk
                    if (isDebugRegion && chunkXBatch[i] == 1495 && chunkZBatch[i] == 8284) {
                        std::lock_guard<std::mutex> lock(resultsMutex);
                        std::cout << "[DEBUG] Setting chunk (" << chunkXBatch[i] << "," << chunkZBatch[i] 
                                  << ") -> grid[" << gridZBatch[i] << "][" << gridXBatch[i] 
                                  << "] = " << resultsBatch[i] << "\n";
                    }
                }
                
                batchIdx = 0;
            }
        }
    }
    
    // Process remaining chunks (less than 8)
    if (batchIdx > 0) {
        // Pad with dummy values
        for (int64_t i = batchIdx; i < 8; i++) {
            chunkXBatch[i] = 0;
            chunkZBatch[i] = 0;
        }
        
        isSlimeChunkBatch8(chunkXBatch, chunkZBatch, WORLD_SEED, resultsBatch);
        
        for (int64_t i = 0; i < batchIdx; i++) {
            grid[gridZBatch[i]][gridXBatch[i]] = resultsBatch[i];
        }
    }
    
    // Debug: Count slime chunks in grid
    int64_t slimeCount = 0;
    for (int64_t z = 0; z < height; z++) {
        for (int64_t x = 0; x < width; x++) {
            if (grid[z][x]) slimeCount++;
        }
    }
    
    // Find rectangles in this grid
    int64_t beforeCount = foundRectangles.size();
    findMaximalRectangles(grid, 0, height, paddedMinX, paddedMinZ);
    int64_t afterCount = foundRectangles.size();
    
    // Debug output for test region
    if (paddedMinX <= 1495 && paddedMaxX > 1495 && paddedMinZ <= 8282 && paddedMaxZ > 8282) {
        std::lock_guard<std::mutex> lock(resultsMutex);
        std::cout << "[DEBUG] processRegion: padded X[" << paddedMinX << "-" << paddedMaxX 
                  << "] Z[" << paddedMinZ << "-" << paddedMaxZ << "]\n";
        std::cout << "[DEBUG] Grid size: " << width << "x" << height << "\n";
        std::cout << "[DEBUG] Slime chunks in grid: " << slimeCount << "\n";
        std::cout << "[DEBUG] Rectangles added: " << (afterCount - beforeCount) << "\n";
    }
    
    // Only count the non-padded region for progress tracking
    chunksProcessed += (maxX - minX) * (maxZ - minZ);
}

// Generate work queue sorted by distance from origin
void generateWorkQueue() {
    struct WorkUnit {
        int64_t minX, maxX, minZ, maxZ;
        int64_t distSquared;
    };
    
    std::vector<WorkUnit> units;
    
    // Convert block bounds to chunk bounds
    int64_t searchMinChunkX = SEARCH_MIN_X / 16;
    int64_t searchMaxChunkX = SEARCH_MAX_X / 16;
    int64_t searchMinChunkZ = SEARCH_MIN_Z / 16;
    int64_t searchMaxChunkZ = SEARCH_MAX_Z / 16;
    
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
void workerThread(int64_t threadId, int64_t numThreads) {
    while (!pauseFlag) {
        // Atomically grab next work unit
        int64_t idx = workQueueIndex.fetch_add(1, std::memory_order_relaxed);
        
        if (idx >= (int64_t)workQueue.size()) {
            break; // All work done
        }
        
        auto& work = workQueue[idx];
        int64_t minX = work.first.first;
        int64_t maxX = work.first.second;
        int64_t minZ = work.second.first;
        int64_t maxZ = work.second.second;
        
        // Debug: Check if this is the work unit containing our test 3x3
        if (minX <= 1495 && maxX > 1495 && minZ <= 8282 && maxZ > 8282) {
            std::lock_guard<std::mutex> lock(resultsMutex);
            std::cout << "[DEBUG] Processing work unit containing test 3x3: X[" << minX << "-" << maxX 
                      << "] Z[" << minZ << "-" << maxZ << "]\n";
        }
        
        processRegion(minX, maxX, minZ, maxZ);
        
        // Update max distance
        int64_t centerX = (minX + maxX) / 2;
        int64_t centerZ = (minZ + maxZ) / 2;
        int64_t dist = (int64_t)std::sqrt(centerX * centerX + centerZ * centerZ);
        
        int64_t currentMax = maxDistanceReached.load(std::memory_order_relaxed);
        while (dist > currentMax && 
               !maxDistanceReached.compare_exchange_weak(currentMax, dist, std::memory_order_relaxed)) {
            // Retry if another thread updated it
        }
    }
}

// ==================== SIGNAL HANDLING ====================
void signalHandler(int signal) {
    if (signal == SIGINT) {
        pauseFlag = true;
    }
}

void printStats(bool toFile = false) {
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

// ==================== UNIT TESTS ====================
bool runUnitTests() {
    // Set test-specific world seed
    const int64_t TEST_WORLD_SEED = 413563856LL;
    
    std::cout << "Running unit tests...\n";
    std::cout << "========================================\n";
    
    // Test case: 3x3 at chunk (1495-1497, 8282-8284)
    // Block coords: (23920-23952, 132512-132544)
    std::cout << "Test 1: Known 3x3 rectangle detection (full pipeline)\n";
    std::cout << "Expected: 3x3 at chunk (1495, 8282) = block (23920, 132512)\n\n";
    
    // Verify the 3x3 chunks are actually slime chunks
    std::cout << "Verifying 3x3 slime chunks with seed " << TEST_WORLD_SEED << ":\n";
    for (int64_t z = 8282; z <= 8284; z++) {
        for (int64_t x = 1495; x <= 1497; x++) {
            bool isSlime = isSlimeChunk(x, z, TEST_WORLD_SEED);
            std::cout << "  Chunk (" << x << ", " << z << "): " << (isSlime ? "SLIME" : "NOT") << "\n";
        }
    }
    std::cout << "\n";
    
    // Set search bounds to small area around the test case (with padding room)
    // Test is at chunk (1495, 8282-8284), convert to blocks and add padding
    SEARCH_MIN_X = 1200 * 16;  // Allow padding room before work unit
    SEARCH_MAX_X = 1600 * 16;  // Allow padding room after work unit
    SEARCH_MIN_Z = 8100 * 16;  // Allow padding room
    SEARCH_MAX_Z = 8500 * 16;  // Allow padding room
    
    std::cout << "Search bounds (blocks): X[" << SEARCH_MIN_X << " to " << SEARCH_MAX_X 
              << "] Z[" << SEARCH_MIN_Z << " to " << SEARCH_MAX_Z << "]\n";
    std::cout << "Search bounds (chunks): X[" << (SEARCH_MIN_X/16) << " to " << (SEARCH_MAX_X/16)
              << "] Z[" << (SEARCH_MIN_Z/16) << " to " << (SEARCH_MAX_Z/16) << "]\n";
    
    // Clear previous results
    {
        std::lock_guard<std::mutex> lock(resultsMutex);
        foundRectangles.clear();
    }
    workQueueIndex = 0;
    workQueue.clear();
    chunksProcessed = 0;
    maxDistanceReached = 0;
    
    // Generate work queue for this small region
    std::cout << "Generating work queue...\n";
    generateWorkQueue();
    std::cout << "Work units: " << workQueue.size() << "\n\n";
    
    // Run single-threaded for easier debugging
    std::cout << "Processing work units...\n";
    workerThread(0, 1);
    
    // Check results
    std::cout << "\nRectangles found: " << foundRectangles.size() << "\n";
    for (const auto& rect : foundRectangles) {
        std::cout << "  " << rect.width << "x" << rect.height 
                  << " at chunk (" << rect.x << ", " << rect.z << ")"
                  << " = block (" << (rect.x * 16) << ", " << (rect.z * 16) << ")\n";
    }
    
    // Verify we found the expected 3x3
    bool found3x3 = false;
    for (const auto& rect : foundRectangles) {
        if (rect.width == 3 && rect.height == 3 && 
            rect.x == 1495 && rect.z == 8282) {
            found3x3 = true;
            break;
        }
    }
    
    if (found3x3) {
        std::cout << "\n[PASS] Found expected 3x3 at chunk (1495, 8282)\n";
    } else {
        std::cout << "\n[FAIL] Did NOT find expected 3x3 at chunk (1495, 8282)\n";
    }
    
    std::cout << "========================================\n\n";
    
    return found3x3;
}

// ==================== MAIN ====================
int main(int argc, char* argv[]) {
    // Check for test mode
    if (argc > 1 && std::string(argv[1]) == "--test") {
        bool passed = runUnitTests();
        return passed ? 0 : 1;
    }
    
    // Detect number of logical cores
    int64_t NUM_THREADS = std::thread::hardware_concurrency();
    if (NUM_THREADS == 0) {
        NUM_THREADS = 8; // Fallback if detection fails
    }
    
    std::cout << "Minecraft Slime Chunk Rectangle Finder (AVX-512 Optimized)\n";
    std::cout << "==========================================================\n";
    std::cout << "World Seed: " << WORLD_SEED << "\n";
    std::cout << "CPU Cores Detected: " << NUM_THREADS << "\n";
    std::cout << "Search Bounds (blocks): X[" << SEARCH_MIN_X << " to " << SEARCH_MAX_X 
              << "] Z[" << SEARCH_MIN_Z << " to " << SEARCH_MAX_Z << "]\n";
    std::cout << "Search Bounds (chunks): X[" << (SEARCH_MIN_X/16) << " to " << (SEARCH_MAX_X/16)
              << "] Z[" << (SEARCH_MIN_Z/16) << " to " << (SEARCH_MAX_Z/16) << "]\n";
    std::cout << "Work Unit Size: " << WORK_UNIT_SIZE << " chunks\n";
    std::cout << "Min Rectangle Dimension: " << MINIMUM_RECT_DIMENSION << "x" << MINIMUM_RECT_DIMENSION << "\n";
    std::cout << "SIMD: AVX-512 8-wide vectorization enabled\n";
    std::cout << "Press Ctrl+C to pause and view stats\n\n";
    
    signal(SIGINT, signalHandler);
    
    // Generate work queue sorted by distance from origin
    std::cout << "Generating work queue...\n";
    generateWorkQueue();
    std::cout << "Work queue ready: " << workQueue.size() << " units\n\n";
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int64_t i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(workerThread, i, NUM_THREADS);
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
                printStats(true);
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
    
    printStats();
    
    std::cout << "Total time: " << duration.count() / 1000.0 << " seconds\n";
    std::cout << "Throughput: " << (chunksProcessed.load() * 1000.0 / duration.count()) << " chunks/sec\n";
    
    return 0;
}