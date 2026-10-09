"""Host test for qspi_placement.py: fed nm lines, it must refuse a hot
symbol in QSPI and accept a cold one. Plain script, exit code is the verdict."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import qspi_placement as q

FAILS = []
def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond: FAILS.append(name)

cold = "90110010 00000130 T daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)\n"
hot_in_qspi = "90100100 00000708 T spky::ModLane::follow(long, float, float)\n"
hot_in_sram = "2400a55c 00000708 T spky::ModLane::follow(long, float, float)\n"
check("cold symbol in QSPI passes", q.violations(cold) == [])
check("hot symbol in SRAM passes", q.violations(hot_in_sram) == [])
check("hot symbol in QSPI is refused",
      q.violations(hot_in_qspi) == ["spky::ModLane::follow(long, float, float)"])
check("engine switch path is hot",
      q.violations("90100200 00000010 T spky::FeedEngine::init(float)\n") != [])
check("the control law is hot",
      q.violations("90100300 00000010 T control::ControlLawT<spky::Instrument, control::NoHooks>::tick(float const*, control::Options const&, spky::Instrument&)\n") != [])
# Boot code is not all cold: whatever runs before the board's own QSPI init
# has returned (clocks, MPU, the QSPI driver itself) must stay in SRAM.
check("the system init is refused",
      q.violations("90110400 000001a4 T daisy::System::Init(daisy::System::Config const&)\n") != [])
check("the QSPI driver is refused",
      q.violations("90110500 000001b4 T daisy::QSPIHandle::Init(daisy::QSPIHandle::Config const&)\n") != [])
check("the board init is refused",
      q.violations("90110600 00000518 T daisy::patch_sm::DaisyPatchSM::Init()\n") != [])
check("a static constructor is refused",
      q.violations("90110700 00000050 t _GLOBAL__sub_I__ZN5shell6fx_memEv\n") != [])
sys.exit(1 if FAILS else 0)
