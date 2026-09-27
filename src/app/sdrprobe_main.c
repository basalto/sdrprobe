#include "runtime.h"

/*
 * `sdrprobe`: the program without a window, and the one most runs want.
 *
 * It exists for the machine this one is not -- a box beside an antenna with
 * no graphics stack installed. `./sdrprobe server` never opens a window and
 * still *links* raylib, so serving from such a box meant installing raylib,
 * libGL and an X or Wayland client library to run something that draws
 * nothing (`.scratch/layer-boundaries/issues/04-*`, option B).
 *
 * The split is **window / no window**, not gui / server: `headless` moves in
 * here with `server`, because a scripted decode needs raylib for nothing
 * either and a decode beside an antenna is half of what this binary is for.
 *
 * There is no second set of command words to learn. Every flag, every
 * subcommand and every message is `app_main.c`'s, shared with `./sdrprobe`;
 * the only difference in the whole program is the NULL below, which
 * `sdrprobe_main()` reads as "no window in this build" and says so when a
 * windowed mode is asked for.
 */
int main(int argc, char **argv) {
    return sdrprobe_main(argc, argv, NULL);
}
