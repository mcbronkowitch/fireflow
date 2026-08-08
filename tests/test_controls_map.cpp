// Die Abbildung Mux-Kanal -> Engine-Setter ist reine Datenlogik und gehoert
// auf den Host getestet, bevor Hardware im Spiel ist. Auf dem Board waere
// eine falsche Zuordnung nur als "der Poti macht das Falsche" sichtbar, und
// das ist teuer zu debuggen; hier ist es eine Zeile.
//
// Der Umfang ist absichtlich klein: belegt ist genau EIN Kanal. Die
// vollstaendige Tabelle folgt erst, wenn Task 2 entschieden hat, welche
// Kanaele es ueberhaupt gibt (docs/hardware/io-budget.md fuehrt die 82
// Parameter mit noch leerer Einstufungsspalte). Was hier schon steht, ist
// das Verhalten an den Raendern -- und das aendert sich durch Task 2 nicht.
#include <doctest/doctest.h>
#include "../shell/controls.h"
#include "instrument.h"

TEST_CASE("controls: channel zero drives part A rate") {
    spky::Instrument inst;
    inst.init(48000.0f);
    shell::map_control(0, 0.75f, inst);
    CHECK(inst.rate(spky::PART_A) == doctest::Approx(0.75f));
}

TEST_CASE("controls: channel zero leaves part B alone") {
    // Ein Mapping, das versehentlich beide Decks anfasst, waere auf dem
    // Board als "der Poti macht zu viel" hoerbar, aber schwer zuzuordnen.
    spky::Instrument inst;
    inst.init(48000.0f);
    const float before = inst.rate(spky::PART_B);
    shell::map_control(0, 0.75f, inst);
    CHECK(inst.rate(spky::PART_B) == doctest::Approx(before));
}

TEST_CASE("controls: an out-of-range channel changes nothing") {
    // Der Mux liefert im Fehlerfall (falsche Kanalzahl, halb angeschlossener
    // Chip) Indizes, die es nicht gibt. Die dann still zu ignorieren ist die
    // richtige Antwort -- ein Zugriff daneben waere ein Absturz im
    // Audio-Callback.
    spky::Instrument inst;
    inst.init(48000.0f);
    const float before = inst.rate(spky::PART_A);
    shell::map_control(9999, 1.0f, inst);
    CHECK(inst.rate(spky::PART_A) == doctest::Approx(before));
    shell::map_control(-1, 1.0f, inst);
    CHECK(inst.rate(spky::PART_A) == doctest::Approx(before));
}
