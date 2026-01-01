#include "megaslimechunkfinder.h"
#include <iostream>
#include <iomanip>
#include <cmath>

// ==================== UNIT TESTS ====================

bool testOptimizations() {
    std::cout << "Testing PRNG optimizations...\n";
    std::cout << "========================================\n";

    const int64_t TEST_WORLD_SEED = 413563856LL;

    // Test a variety of chunk coordinates
    std::cout << "Testing individual chunks:\n";
    int64_t test_coords[][2] = {
        {1495, 8282},
        {1495, 8283},
        {1495, 8284},
        {1496, 8282},
        {1496, 8283},
        {1496, 8284},
        {1497, 8282},
        {1497, 8283},
        {1497, 8284},
    };

    bool allMatch = true;
    for (size_t i = 0; i < sizeof(test_coords) / sizeof(test_coords[0]); i++) {
        int64_t x = test_coords[i][0];
        int64_t z = test_coords[i][1];

        bool result = isSlimeChunk(x, z, TEST_WORLD_SEED);

        std::cout << "  Chunk (" << std::setw(8) << x << ", " << std::setw(8) << z << "): "
                  << (result ? "SLIME" : "NOT") << "\n";
    }

    // Test vectorized batch processing (16-way)
    std::cout << "\nTesting AVX-512 16-way batch processing:\n";
    alignas(64) int64_t batchX[16];
    alignas(64) int64_t batchZ[16];
    alignas(64) bool batchResults[16];

    // Fill batch arrays from test_coords (9 test coords, pad rest with zeros)
    int numTests = sizeof(test_coords) / sizeof(test_coords[0]);
    for (int i = 0; i < 16; i++) {
        if (i < numTests) {
            batchX[i] = test_coords[i][0];
            batchZ[i] = test_coords[i][1];
        } else {
            batchX[i] = 0;
            batchZ[i] = 0;
        }
    }

    isSlimeChunkVec16(batchX, batchZ, TEST_WORLD_SEED, batchResults);

    for (int i = 0; i < numTests; i++) {
        bool scalarResult = isSlimeChunk(batchX[i], batchZ[i], TEST_WORLD_SEED);
        bool matches = (scalarResult == batchResults[i]);

        std::cout << "  Chunk (" << batchX[i] << ", " << batchZ[i] << "): "
                  << "Scalar=" << scalarResult << " Vector=" << batchResults[i]
                  << " " << (matches ? "[MATCH]" : "[MISMATCH]") << "\n";

        if (!matches) {
            allMatch = false;
            std::cout << "    ERROR: Scalar and vector results do not match\n";
        }
    }

    if (allMatch) {
        std::cout << "\n[PASS] All optimization tests passed\n";
    } else {
        std::cout << "\n[FAIL] Some optimizations produced incorrect results\n";
    }

    std::cout << "========================================\n\n";
    return allMatch;
}

bool runUnitTests() {
    // Set test-specific configuration
    const int64_t TEST_WORLD_SEED = 413563856LL;
    const int64_t TEST_MINIMUM_RECT_DIMENSION = 3;

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
    int64_t testSearchMinX = 1200 * 16;  // Allow padding room before work unit
    int64_t testSearchMaxX = 1600 * 16;  // Allow padding room after work unit
    int64_t testSearchMinZ = 8100 * 16;  // Allow padding room
    int64_t testSearchMaxZ = 8500 * 16;  // Allow padding room

    std::cout << "Search bounds (blocks): X[" << testSearchMinX << " to " << testSearchMaxX
              << "] Z[" << testSearchMinZ << " to " << testSearchMaxZ << "]\n";
    std::cout << "Search bounds (chunks): X[" << (testSearchMinX/16) << " to " << (testSearchMaxX/16)
              << "] Z[" << (testSearchMinZ/16) << " to " << (testSearchMaxZ/16) << "]\n";

    // Create local state variables
    std::mutex resultsMutex;
    std::set<Rectangle> foundRectangles;
    std::atomic<bool> pauseFlag{false};
    std::atomic<int64_t> chunksProcessed{0};
    std::atomic<int64_t> maxDistanceReached{0};
    std::vector<std::pair<std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>> workQueue;
    std::atomic<int64_t> workQueueIndex{0};

    // Generate work queue for this small region
    std::cout << "Generating work queue...\n";
    generateWorkQueue(testSearchMinX, testSearchMaxX, testSearchMinZ, testSearchMaxZ, workQueue);
    std::cout << "Work units: " << workQueue.size() << "\n\n";

    // Run single-threaded for easier debugging (with debug mode enabled)
    std::cout << "Processing work units...\n";
    workerThread(0, 1, TEST_WORLD_SEED, TEST_MINIMUM_RECT_DIMENSION,
                 testSearchMinX, testSearchMaxX, testSearchMinZ, testSearchMaxZ,
                 resultsMutex, foundRectangles, pauseFlag, chunksProcessed, maxDistanceReached,
                 workQueue, workQueueIndex, true);  // Enable debug mode

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

int main(int argc, char* argv[]) {
    // First test PRNG optimizations
    bool optimizationsOK = testOptimizations();
    if (!optimizationsOK) {
        std::cout << "CRITICAL: PRNG optimizations failed. Aborting tests.\n";
        return 1;
    }

    // Then run full integration tests
    bool integrationOK = runUnitTests();

    return integrationOK ? 0 : 1;
}
