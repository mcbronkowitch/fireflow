#include "doctest/doctest.h"

#include <cstdint>
#include <cstring>

#include "../bench/qspi_digest.h"
#include "synth/wt_bank.h"

TEST_CASE("bench QSPI digest matches the independently baked bank SHA") {
    char digest[65] = {};
    bench::sha256_hex(
        reinterpret_cast<const uint8_t*>(spky::wt::kBankSamples),
        sizeof(spky::wt::kBankSamples),
        digest);
    CHECK(std::strcmp(
              digest,
              "ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27")
          == 0);
}

TEST_CASE("bench QSPI digest implements the standard SHA-256 vector") {
    static constexpr char kInput[] = "abc";
    char digest[65] = {};
    bench::sha256_hex(
        reinterpret_cast<const uint8_t*>(kInput),
        3,
        digest);
    CHECK(std::strcmp(
              digest,
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
          == 0);
}
