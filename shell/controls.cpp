#include "controls.h"

namespace shell {

void map_control(int idx, float v, spky::Instrument& inst)
{
    // Kein Clamp auf 0..1 an dieser Stelle, und das ist Absicht: der
    // Wertebereich ist Sache dessen, der den ADC liest, denn nur dort ist
    // bekannt, ob ein Wert ausserhalb ein Messfehler oder eine
    // Kalibrierfrage ist. Hier waere ein stiller Clamp genau die Art
    // Korrektur, die einen halb angeschlossenen Poti wie einen richtigen
    // aussehen laesst.
    switch(idx)
    {
        case 0: inst.set_rate(spky::PART_A, v); break;
        default: break;   // Kanal existiert nicht -> nichts tun
    }
}

} // namespace shell
