# CHORD_GEN

Moduł VCV Rack do pracy z akordami z `INFINIT_MIDI_PLAYER`.

## Co jest zaimplementowane
- Wejście `Poly V/Oct` kompatybilne z `CHORD_POLY_VOCT_OUTPUT` (`poly_out`) z `INFINIT_MIDI_PLAYER`.
- Rozdział wejścia poly na 4 wyjścia mono (`Voice 1..4`).
- Wyświetlanie rozpoznanego typu akordu (`Chord: ...`).
- Wyświetlanie rozkładu dźwięków z wejścia (`Notes: C3 E3 G3 B3`).

## Build i deploy
- Lokalny deploy: `make deploy-local`
- Duży komputer: `make deploy-big`
- Dwa kompy: `make deploy-both`
- Jeśli `/Volumes/music` jest niedostępne lub bez zapisu, workflow ma fallback na `~/Volumes/music`.

## Następne kroki
- Opcjonalnie dodać osobne wejście/wyjścia gate (`CHORD_POLY_GATE_OUTPUT` -> 4x mono gate).
- Dopracować rozpoznawanie bardziej złożonych jakości akordów.
