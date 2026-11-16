#!/usr/bin/env python3
"""make_routes.py - writes Ninja Gaiden's harvesting route (no game data in it).

  route_play.txt  START at 300 (skips the opening cutscene), START at 700 (title),
                  then a random-play tail from 1000 until --end.

Probe (2026-11-15, shots/probe_*.png): Tecmo logo 100-300, title 400-700, "Act-I"
card 800-900, Stage 1-1 playable by ~1000. A START in play pauses (timer froze at
143 after the START at 1500). Ninja Gaiden has no cheat code.

Recovery: every RECOVER frames the tail presses START twice, PAIR frames apart.
In play that is pause + unpause (harmless); after a game over at the title it
starts the game again (and skips the Act card).

Buttons: B = sword, A = jump, UP+B = the special (ninpo) item.

  python3 -I tools/make_routes.py [END]     (run in ~/Projects/NinjaGaiden1Recomp)
"""
import random
import sys

PRESS = 8
TAIL_START = 1000
END = 18500
RECOVER = 1500
PAIR = 30


def title():
    return ["300 START", f"{300 + PRESS} -", "700 START", f"{700 + PRESS} -"]


def tail(seed):
    """Mostly run right slashing, with jumps (wall clinging comes free), some
    backing up, crouch slashes and the special. Ticks of 6 frames."""
    rng = random.Random(seed)
    lines, f = [], TAIL_START
    move, crouch, jump_for, special_for = "RIGHT", False, 0, 0
    slash = False
    next_recover = TAIL_START + RECOVER
    while f < END:
        if f >= next_recover:
            lines += [f"{f} START", f"{f + PRESS} -",
                      f"{f + PAIR} START", f"{f + PAIR + PRESS} -"]
            f += PAIR + 2 * PRESS
            next_recover += RECOVER
            continue
        if rng.random() < 0.06:                       # change what we're doing
            r = rng.random()
            if r < 0.72: move, crouch = "RIGHT", False
            elif r < 0.84: move, crouch = "LEFT", False
            elif r < 0.92: move, crouch = None, False  # stand and fight
            else: move, crouch = None, True            # crouch slash
        if jump_for == 0 and rng.random() < 0.09:
            jump_for = rng.choice([1, 2, 3, 4])       # ticks holding A
        if special_for == 0 and rng.random() < 0.02:
            special_for = 1
        slash = not slash
        held = []
        if special_for:
            held.append("UP")
            special_for -= 1
        elif crouch:
            held.append("DOWN")
        if move:
            held.append(move)
        if jump_for:
            held.append("A")
            jump_for -= 1
        if slash or "UP" in held:
            held.append("B")
        lines.append(f"{f} {'+'.join(held) if held else '-'}")
        f += 6
    lines.append(f"{END} -")
    return lines


def write(path, lines):
    with open(path, "w") as out:
        out.write("\n".join(lines) + "\n")
    print(f"{path}: {len(lines)} lines")


def main():
    global END
    if len(sys.argv) > 1:
        END = int(sys.argv[1])
    write("route_play.txt", title() + tail(1))


if __name__ == "__main__":
    main()
