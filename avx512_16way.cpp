#include <iostream>
#include <iomanip>
#include <cstdint>
#include <immintrin.h>

// Scalar reference
bool isSlimeChunk_Scalar(int64_t chunkX, int64_t chunkZ, int64_t worldSeed) {
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

// AVX-512 16-way using 32-bit operations!
void isSlimeChunk_AVX512_16way(const int64_t* chunkX, const int64_t* chunkZ, int64_t worldSeed, bool* results) {
    // Convert to int32_t (16 values)
    alignas(64) int32_t x_i32[16];
    alignas(64) int32_t z_i32[16];
    
    for (int i = 0; i < 16; i++) {
        x_i32[i] = (int32_t)chunkX[i];
        z_i32[i] = (int32_t)chunkZ[i];
    }
    
    // Load as 32-bit integers (16 per register)
    __m512i x = _mm512_load_si512((__m512i*)x_i32);
    __m512i z = _mm512_load_si512((__m512i*)z_i32);
    
    // All intermediate calculations in 32-bit!
    __m512i x_x = _mm512_mullo_epi32(x, x);  // 32-bit multiply
    __m512i z_z = _mm512_mullo_epi32(z, z);
    
    __m512i term1 = _mm512_mullo_epi32(x_x, _mm512_set1_epi32(0x4c1906));
    __m512i term2 = _mm512_mullo_epi32(x, _mm512_set1_epi32(0x5ac0db));
    __m512i term4 = _mm512_mullo_epi32(z, _mm512_set1_epi32(0x5f24f));
    
    // term3 is special: need to extend z_z to 64-bit, then multiply
    // Split into low and high halves for 64-bit processing
    __m256i z_z_lo256 = _mm512_extracti32x8_epi32(z_z, 0);  // Lower 8 values
    __m256i z_z_hi256 = _mm512_extracti32x8_epi32(z_z, 1);  // Upper 8 values
    
    // Sign-extend 32-bit to 64-bit
    __m512i z_z_lo64 = _mm512_cvtepi32_epi64(z_z_lo256);
    __m512i z_z_hi64 = _mm512_cvtepi32_epi64(z_z_hi256);
    
    // 64-bit multiply with constant
    __m512i term3_lo = _mm512_mullo_epi64(z_z_lo64, _mm512_set1_epi64(0x4307a7));
    __m512i term3_hi = _mm512_mullo_epi64(z_z_hi64, _mm512_set1_epi64(0x4307a7));
    
    // Now process each half separately (8 chunks at a time) since we need 64-bit arithmetic
    alignas(64) int32_t term1_arr[16], term2_arr[16], term4_arr[16];
    alignas(64) int64_t term3_lo_arr[8], term3_hi_arr[8];
    
    _mm512_store_si512((__m512i*)term1_arr, term1);
    _mm512_store_si512((__m512i*)term2_arr, term2);
    _mm512_store_si512((__m512i*)term4_arr, term4);
    _mm512_store_si512((__m512i*)term3_lo_arr, term3_lo);
    _mm512_store_si512((__m512i*)term3_hi_arr, term3_hi);
    
    // Process all 16 chunks
    for (int i = 0; i < 16; i++) {
        int64_t term1_64 = (int64_t)term1_arr[i];
        int64_t term2_64 = (int64_t)term2_arr[i];
        int64_t term3_64 = (i < 8) ? term3_lo_arr[i] : term3_hi_arr[i - 8];
        int64_t term4_64 = (int64_t)term4_arr[i];
        
        int64_t seed = worldSeed + term1_64 + term2_64 + term3_64 + term4_64 ^ 0x3ad8025fLL;
        
        seed = (seed ^ 0x5DEECE66DULL) & 0xFFFFFFFFFFFFULL;
        seed = (seed * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
        int32_t bits = (int32_t)(seed >> 17);
        
        results[i] = (bits % 10) == 0;
    }
}

int main() {
    std::cout << "Testing 16-way AVX-512 implementation\n";
    std::cout << "========================================\n";
    
    const int64_t TEST_SEED = 413563856LL;
    
    // Test 16 chunks
    int64_t testX[16] = {1495, 1496, 1497, 1495, 1496, 1497, 1495, 1496, 
                         0, 100, -100, 5000, -5000, 12345, -12345, 99999};
    int64_t testZ[16] = {8282, 8282, 8282, 8283, 8283, 8283, 8284, 8284,
                         0, 100, -100, 5000, -5000, 67890, -67890, 88888};
    
    bool scalar_results[16];
    bool avx512_results[16];
    
    // Compute with scalar
    for (int i = 0; i < 16; i++) {
        scalar_results[i] = isSlimeChunk_Scalar(testX[i], testZ[i], TEST_SEED);
    }
    
    // Compute with AVX-512 16-way
    isSlimeChunk_AVX512_16way(testX, testZ, TEST_SEED, avx512_results);
    
    // Compare
    bool allMatch = true;
    for (int i = 0; i < 16; i++) {
        bool match = (scalar_results[i] == avx512_results[i]);
        std::cout << "Chunk " << std::setw(2) << i << " (" << std::setw(6) << testX[i] << "," << std::setw(6) << testZ[i] << "): ";
        std::cout << "Scalar=" << scalar_results[i] << " AVX512=" << avx512_results[i];
        std::cout << " " << (match ? "[MATCH]" : "[FAIL]") << "\n";
        if (!match) allMatch = false;
    }
    
    std::cout << "\n";
    if (allMatch) {
        std::cout << "SUCCESS: All 16 chunks match!\n";
        std::cout << "16-way AVX-512 parallelism working!\n";
        return 0;
    } else {
        std::cout << "FAILURE: Some mismatches found\n";
        return 1;
    }
}
