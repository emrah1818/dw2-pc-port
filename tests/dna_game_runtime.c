/* Compile the exact live-game implementations without unrelated UI/task code.
 * PE/MinGW retains reference sections that ELF's garbage collector can discard. */
#include "common.h"
#include "main/game.h"
#include "host/host.h"
#include "src/main/digispecies.inc"
#include "src/stag2000/dna_offspring.inc"
