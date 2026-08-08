#pragma once

// Mux-Kanal -> Engine-Parameter.
//
// Diese Datei enthaelt bewusst KEINEN Hardwaretyp. Der Mux-Scan selbst
// (Adressleitungen, ADC, Einschwingzeit) gehoert woanders hin; hier steht nur
// die Abbildung, und genau deshalb laesst sie sich auf dem Host testen --
// tests/test_controls_map.cpp haengt in spky_tests und sieht nie ein Board.
//
// Ein Poti, der das Falsche tut, ist auf der Hardware nur als "klingt
// komisch" sichtbar und teuer zu suchen. Hier ist es eine Zeile.

#include "instrument.h"

namespace shell {

// Setzt den Parameter, der an Kanal `idx` haengt, auf den normalisierten
// Wert `v` (0..1, so wie der ADC ihn liefert). Ein Kanal, den es nicht gibt,
// aendert nichts -- der Mux liefert im Fehlerfall Indizes ausserhalb des
// Bereichs, und ein Zugriff daneben waere ein Absturz im Audio-Callback.
//
// STAND: genau EIN Kanal ist belegt. Die vollstaendige Tabelle folgt, wenn
// Task 2 des Phase-0-Plans entschieden hat, welche Kanaele es gibt
// (docs/hardware/io-budget.md, Einstufungsspalte noch leer). Diese Funktion
// vorher zu fuellen hiesse, die Entscheidung im Code vorwegzunehmen.
void map_control(int idx, float v, spky::Instrument& inst);

} // namespace shell
