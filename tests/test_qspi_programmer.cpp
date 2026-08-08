#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "../bench/qspi_programmer/program_core.h"

namespace {

struct FakeQspi {
    bool erase_ok = true;
    bool write_ok = true;
    bool corrupt_after_write = false;
    uint32_t erased_offset = 0;
    uint32_t written_offset = 0;
    std::size_t written_size = 0;
    std::size_t invalidated_size = 0;
    std::array<uint8_t, 0xfe00> mapped = {};

    bool erase_block(uint32_t offset)
    {
        erased_offset = offset;
        return erase_ok;
    }

    bool write(uint32_t offset, std::size_t size, const uint8_t* source)
    {
        written_offset = offset;
        written_size = size;
        if(!write_ok)
            return false;
        std::copy(source, source + size, mapped.begin());
        if(corrupt_after_write)
            mapped.back() ^= 0xff;
        return true;
    }

    void invalidate(const volatile uint8_t*, std::size_t size)
    {
        invalidated_size = size;
    }
};

std::array<uint8_t, 0xfe00> payload()
{
    std::array<uint8_t, 0xfe00> result = {};
    for(std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<uint8_t>(i * 13 + 5);
    return result;
}

} // namespace

TEST_CASE("QSPI programmer erases writes and compares the exact payload")
{
    FakeQspi device;
    const auto source = payload();

    const auto result = bench::program_qspi_payload(
        device,
        source.data(),
        reinterpret_cast<const volatile uint8_t*>(device.mapped.data()));

    CHECK(result == bench::QspiProgramResult::ok);
    // Der Offset wird gegen die KONSTANTE geprueft, nicht gegen eine Zahl:
    // dieser Test stand seit c905971 ("the bank stops squatting on the
    // bootloader's app address") auf 0x00040000 und war rot, weil der Wert
    // an zwei Stellen gepflegt werden musste und eine davon vergessen wurde.
    //
    // Damit ein blosser Spiegel der Konstante nicht jede Aenderung
    // durchwinkt, ist der Wert selbst hier einmal festgenagelt -- MIT
    // Begruendung, denn er ist eine Entscheidung und kein Detail: 0x90040000
    // ist die App-Adresse, an die der Daisy-Bootloader das Programm laedt.
    // Laege die Bank dort, ueberschriebe jedes Flashen sie.
    static_assert(bench::kQspiPayloadOffset == 0x00100000u,
                  "QSPI bank offset is a decision: 0x90100000, clear of the "
                  "bootloader's app address at 0x90040000");
    CHECK(device.erased_offset == bench::kQspiPayloadOffset);
    CHECK(device.written_offset == bench::kQspiPayloadOffset);
    CHECK(device.written_size == 0xfe00);
    CHECK(device.invalidated_size == 0xfe00);
    CHECK(std::equal(source.begin(), source.end(), device.mapped.begin()));
}

TEST_CASE("QSPI programmer reports an erase failure without writing")
{
    FakeQspi device;
    device.erase_ok = false;
    const auto source = payload();

    const auto result = bench::program_qspi_payload(
        device,
        source.data(),
        reinterpret_cast<const volatile uint8_t*>(device.mapped.data()));

    CHECK(result == bench::QspiProgramResult::erase_failed);
    CHECK(device.written_size == 0);
}

TEST_CASE("QSPI programmer reports a write failure without comparing")
{
    FakeQspi device;
    device.write_ok = false;
    const auto source = payload();

    const auto result = bench::program_qspi_payload(
        device,
        source.data(),
        reinterpret_cast<const volatile uint8_t*>(device.mapped.data()));

    CHECK(result == bench::QspiProgramResult::write_failed);
    CHECK(device.invalidated_size == 0);
}

TEST_CASE("QSPI programmer rejects a byte mismatch after cache invalidation")
{
    FakeQspi device;
    device.corrupt_after_write = true;
    const auto source = payload();

    const auto result = bench::program_qspi_payload(
        device,
        source.data(),
        reinterpret_cast<const volatile uint8_t*>(device.mapped.data()));

    CHECK(result == bench::QspiProgramResult::compare_failed);
    CHECK(device.invalidated_size == 0xfe00);
}
