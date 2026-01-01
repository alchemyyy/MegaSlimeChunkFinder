#include <iostream>
#include <iomanip>
#include <cstdint>
#include <immintrin.h>

// Scalar reference
bool isSlimeChunk_Scalar(int64_t chunkX, int64_t chunkZ, int64_t worldSeed) {
    int32_t x = (int32_t)chunkX;
    int32_t z = (int32_t)chunkZ;
    
    std::cout << "x=" << (int64_t)x << " z=" << (int64_t)z << " ws=" << worldSeed << "\n";
    
    // Show individual terms
    int64_t term1 = (int64_t)(x * x * 0x4c1906);
    int64_t term2 = (int64_t)(x * 0x5ac0db);
    int64_t term3 = (int64_t)(z * z) * 0x4307a7LL;
    int64_t term4 = (int64_t)(z * 0x5f24f);
    
    std::cout << "  term1 (x*x*c1):   0x" << std::hex << term1 << std::dec << "\n";
    std::cout << "  term2 (x*c2):     0x" << std::hex << term2 << std::dec << "\n";
    std::cout << "  term3 (z*z*c3):   0x" << std::hex << term3 << std::dec << "\n";
    std::cout << "  term4 (z*c4):     0x" << std::hex << term4 << std::dec << "\n";
    
    int64_t seed = worldSeed + term1 + term2 + term3 + term4 ^ 0x3ad8025fLL;
    
    std::cout << "After initial calc: 0x" << std::hex << seed << std::dec << "\n";
    
    seed = (seed ^ 0x5DEECE66DULL) & 0xFFFFFFFFFFFFULL;
    std::cout << "After XOR & mask:   0x" << std::hex << seed << std::dec << "\n";
    
    seed = (seed * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
    std::cout << "After LCG step:     0x" << std::hex << seed << std::dec << "\n";
    
    int32_t bits = (int32_t)(seed >> 17);
    std::cout << "Bits (seed >> 17):  " << bits << "\n";
    std::cout << "Bits % 10:          " << (bits % 10) << "\n";
    
    return (bits % 10) == 0;
}

// AVX-512 version - testing ONE value at a time to debug
bool isSlimeChunk_AVX512_Single(int64_t chunkX, int64_t chunkZ, int64_t worldSeed) {
    // Cast to int32_t - THIS IS CRITICAL!
    // The multiplications happen in 32-bit arithmetic, then cast to 64-bit
    int32_t x_i32 = (int32_t)chunkX;
    int32_t z_i32 = (int32_t)chunkZ;
    
    std::cout << "x=" << (int64_t)x_i32 << " z=" << (int64_t)z_i32 << " ws=" << worldSeed << "\n";
    
    // Storage for extracting values
    alignas(64) int64_t seed_arr[8];
    
    // Do the multiplications in SCALAR 32-bit arithmetic to match Java behavior!
    // term1: (int64_t)(x * x * c) - all 32-bit, then extend
    // term2: (int64_t)(x * c) - all 32-bit, then extend  
    // term3: (int64_t)(z * z) * c - z*z in 32-bit, extend to 64, then 64-bit mul!
    // term4: (int64_t)(z * c) - all 32-bit, then extend
    
    int32_t x_x_i32 = x_i32 * x_i32;  // 32-bit overflow!
    int32_t z_z_i32 = z_i32 * z_i32;  // 32-bit overflow!
    
    int32_t term1_i32 = x_x_i32 * 0x4c1906;  // 32-bit overflow!
    int32_t term2_i32 = x_i32 * 0x5ac0db;    // 32-bit overflow!
    int32_t term4_i32 = z_i32 * 0x5f24f;     // 32-bit overflow!
    
    // term3 is special: (int64_t)(z * z) * 0x4307a7LL
    // The z*z is 32-bit, then cast to 64-bit, THEN multiplied by 64-bit constant
    int64_t z_z_i64 = (int64_t)z_z_i32;  // Sign extend to 64-bit
    int64_t term3 = z_z_i64 * 0x4307a7LL;  // 64-bit multiplication!
    
    // Other terms: cast to 64-bit with sign extension
    int64_t term1 = (int64_t)term1_i32;
    int64_t term2 = (int64_t)term2_i32;
    int64_t term4 = (int64_t)term4_i32;
    
    // Load into SIMD
    __m512i term1_vec = _mm512_set1_epi64(term1);
    __m512i term2_vec = _mm512_set1_epi64(term2);
    __m512i term3_vec = _mm512_set1_epi64(term3);
    __m512i term4_vec = _mm512_set1_epi64(term4);
    __m512i ws_vec = _mm512_set1_epi64(worldSeed);
    
    // Debug: show individual terms
    _mm512_store_si512((__m512i*)seed_arr, term1_vec);
    std::cout << "  term1 (x*x*c1):   0x" << std::hex << seed_arr[0] << std::dec << "\n";
    _mm512_store_si512((__m512i*)seed_arr, term2_vec);
    std::cout << "  term2 (x*c2):     0x" << std::hex << seed_arr[0] << std::dec << "\n";
    _mm512_store_si512((__m512i*)seed_arr, term3_vec);
    std::cout << "  term3 (z*z*c3):   0x" << std::hex << seed_arr[0] << std::dec << "\n";
    _mm512_store_si512((__m512i*)seed_arr, term4_vec);
    std::cout << "  term4 (z*c4):     0x" << std::hex << seed_arr[0] << std::dec << "\n";
    
    // Sum
    __m512i seed = ws_vec;
    seed = _mm512_add_epi64(seed, term1_vec);
    seed = _mm512_add_epi64(seed, term2_vec);
    seed = _mm512_add_epi64(seed, term3_vec);
    seed = _mm512_add_epi64(seed, term4_vec);
    
    // XOR with constant
    seed = _mm512_xor_si512(seed, _mm512_set1_epi64(0x3ad8025f));
    
    // Extract first element to check
    _mm512_store_si512((__m512i*)seed_arr, seed);
    std::cout << "After initial calc: 0x" << std::hex << seed_arr[0] << std::dec << "\n";
    
    // Java Random steps
    __m512i mask48 = _mm512_set1_epi64(0xFFFFFFFFFFFFULL);
    seed = _mm512_xor_si512(seed, _mm512_set1_epi64(0x5DEECE66DULL));
    seed = _mm512_and_si512(seed, mask48);
    
    _mm512_store_si512((__m512i*)seed_arr, seed);
    std::cout << "After XOR & mask:   0x" << std::hex << seed_arr[0] << std::dec << "\n";
    
    seed = _mm512_mullo_epi64(seed, _mm512_set1_epi64(0x5DEECE66DULL));
    seed = _mm512_add_epi64(seed, _mm512_set1_epi64(0xBULL));
    seed = _mm512_and_si512(seed, mask48);
    
    _mm512_store_si512((__m512i*)seed_arr, seed);
    std::cout << "After LCG step:     0x" << std::hex << seed_arr[0] << std::dec << "\n";
    
    // Extract bits [17..47]
    __m512i bits = _mm512_srli_epi64(seed, 17);
    
    _mm512_store_si512((__m512i*)seed_arr, bits);
    int32_t bits_val = (int32_t)seed_arr[0];
    std::cout << "Bits (seed >> 17):  " << bits_val << "\n";
    std::cout << "Bits % 10:          " << (bits_val % 10) << "\n";
    
    // Check divisibility by 10 - MANUAL for now
    return (bits_val % 10) == 0;
}

int main() {
    int64_t testX = 1495;
    int64_t testZ = 8282;
    int64_t worldSeed = 413563856LL;
    
    std::cout << "=== SCALAR VERSION ===\n";
    bool scalar = isSlimeChunk_Scalar(testX, testZ, worldSeed);
    std::cout << "Result: " << (scalar ? "SLIME" : "NOT") << "\n\n";
    
    std::cout << "=== AVX-512 VERSION ===\n";
    bool avx512 = isSlimeChunk_AVX512_Single(testX, testZ, worldSeed);
    std::cout << "Result: " << (avx512 ? "SLIME" : "NOT") << "\n\n";
    
    if (scalar == avx512) {
        std::cout << "SUCCESS: Both match!\n";
        return 0;
    } else {
        std::cout << "FAILURE: Mismatch!\n";
        return 1;
    }
}
