// Standalone PRNG optimization test
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cmath>

// Original implementation
bool isSlimeChunk_Original(int64_t chunkX, int64_t chunkZ, int64_t worldSeed) {
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
    
    return (bits % 10) == 0;
}

// Optimized implementation with clearer masking
bool isSlimeChunk_Optimized(int64_t chunkX, int64_t chunkZ, int64_t worldSeed) {
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
    
    return (bits % 10) == 0;
}

int main() {
    std::cout << "PRNG Optimization Verification\n";
    std::cout << "========================================\n\n";
    
    const int64_t TEST_SEED = 413563856LL;
    
    // Test coordinates
    int64_t test_coords[][2] = {
        {0, 0},
        {1495, 8282},
        {1496, 8283},
        {1497, 8284},
        {-100, -100},
        {50000, 50000},
        {-50000, -50000},
        {12345, 67890},
        {-12345, -67890},
        {(int64_t)(INT32_MAX / 16), (int64_t)(INT32_MAX / 16)},
        {(int64_t)(INT32_MIN / 16), (int64_t)(INT32_MIN / 16)}
    };
    
    bool allMatch = true;
    int testCount = sizeof(test_coords) / sizeof(test_coords[0]);
    
    std::cout << "Testing " << testCount << " coordinates:\n\n";
    
    for (int i = 0; i < testCount; i++) {
        int64_t x = test_coords[i][0];
        int64_t z = test_coords[i][1];
        
        bool original = isSlimeChunk_Original(x, z, TEST_SEED);
        bool optimized = isSlimeChunk_Optimized(x, z, TEST_SEED);
        bool matches = (original == optimized);
        
        std::cout << "Chunk (" << std::setw(10) << x << ", " << std::setw(10) << z << "): ";
        std::cout << "Orig=" << original << " Opt=" << optimized << " ";
        std::cout << (matches ? "[PASS]" : "[FAIL]") << "\n";
        
        if (!matches) allMatch = false;
    }
    
    std::cout << "\n========================================\n";
    if (allMatch) {
        std::cout << "SUCCESS: All tests passed!\n";
        std::cout << "The optimizations are correct.\n";
        return 0;
    } else {
        std::cout << "FAILURE: Some tests failed!\n";
        std::cout << "The optimizations have bugs.\n";
        return 1;
    }
}
