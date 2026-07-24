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
              "0163e3ba4988f5769eece514be01fbf48e134af4b407b0710762f81356f20f82")
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
