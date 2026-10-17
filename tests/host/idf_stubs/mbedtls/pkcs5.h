#pragma once
#include <cstddef>
constexpr int MBEDTLS_MD_SHA256 = 1;
int mbedtls_pkcs5_pbkdf2_hmac_ext(int, const unsigned char*, std::size_t,
    const unsigned char*, std::size_t, unsigned int, unsigned int, unsigned char*);
