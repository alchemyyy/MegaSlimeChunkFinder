// Debug test for AVX-512 implementation
#include <iostream>
#include <iomanip>
#include <cstdint>

// Scalar reference implementation
bool isSlimeChunk_Scalar(int64_t chunkX, int64_t chunkZ, int64_t worldSeed) {
    int32_t x = (int32_t)chunkX;
    int32_t z = (int32_t)chunkZ;
    
    int64_t seed = worldSeed +
                   (int64_t)(x * x * 0x4c1906) +
                   (int64_t)(x * 0x5ac0db) +
                   (int64_t)(z * z) * 0x4307a7LL +
                   (int64_t)(z * 0x5f24f) ^ 0x3ad8025fLL;
    
    std::cout << "  After initial calc: 0x" << std::hex << seed << std::dec << "\n";
    
    seed = (seed ^ 0x5DEECE66DULL) & 0xFFFFFFFFFFFFULL;
    std::cout << "  After XOR & mask:   0x" << std::hex << seed << std::dec << "\n";
    
    seed = (seed * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
    std::cout << "  After LCG step:     0x" << std::hex << seed << std::dec << "\n";
    
    int32_t bits = (int32_t)(seed >> 17);
    std::cout << "  Bits (seed >> 17):  " << bits << "\n";
    std::cout << "  Bits % 10:          " << (bits % 10) << "\n";
    
    return (bits % 10) == 0;
}

int main() {
    int64_t testX = 1495;
    int64_t testZ = 8282;
    int64_t worldSeed = 413563856LL;
    
    std::cout << "Testing chunk (" << testX << ", " << testZ << ") with seed " << worldSeed << "\n";
    std::cout << "Scalar implementation:\n";
    
    bool result = isSlimeChunk_Scalar(testX, testZ, worldSeed);
    
    std::cout << "\nFinal result: " << (result ? "SLIME" : "NOT") << "\n";
    
    return 0;
}
