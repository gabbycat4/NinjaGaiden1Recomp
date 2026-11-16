#!/usr/bin/env python3
"""make_routes.py - writes Ninja Gaiden's harvesting routes (no game data in them).

  route_play.txt  1 player:  Konami code at the title (30 lives), START, then a
                  random-play tail until --end.
  route_2p.txt    2 players: Konami code, SELECT (2 PLAYERS), START, then random
                  play on both controllers ("2:" lines are controller 2).

Probe (2026-11-15): the title has finished scrolling in by frame 300; START there
reaches gameplay at ~900. The code starts at 340, each button held 8 frames then
released 8. START is never pressed after the title (it would pause the game).

  python3 -I tools/make_routes.py            (run in ~/Projects/NinjaGaiden1Recomp)
"""
import random
import sys

CODE = ["UP", "UP", "DOWN", "DOWN", "LEFT", "RIGHT", "LEFT", "RIGHT", "B", "A"]
CODE_START = 340
PRESS = 8
TAIL_START = 1000
END = 18500


def title(two_players):
    lines, f = [], CODE_START
    for b in CODE:
        lines += [f"{f} {b}", f"{f + PRESS} -"]
        f += 2 * PRESS
    if two_players:
        lines += [f"{f} SELECT", f"{f + PRESS} -"]
        f += 2 * PRESS
    lines += [f"{f} START", f"{f + PRESS} -"]
    return lines


def tail(seed, port=""):
    """Mostly run right firing, with jumps, aiming, lying down and some backing up.
    Ticks of 6 frames; B alternates on/off so the default gun keeps firing."""
    rng = random.Random(seed)
    lines, f = [], TAIL_START
    move, aim, left_for, jump_for = "RIGHT", None, 0, 0
    fire = False
    while f < END:
        if rng.random() < 0.06:                       # change what we're doing
            r = rng.random()
            if r < 0.70: move, aim = "RIGHT", None
            elif r < 0.80: move, aim = "LEFT", None
            elif r < 0.88: move, aim = "RIGHT", "UP"   # shoot diagonally up
            elif r < 0.93: move, aim = None, "UP"      # shoot straight up
            elif r < 0.97: move, aim = None, "DOWN"    # lie down
            else: move, aim = "RIGHT", "DOWN"          # shoot diagonally down
        if jump_for == 0 and rng.random() < 0.07:
            jump_for = rng.choice([1, 2, 3])          # ticks holding A
        fire = not fire
        held = [b for b in (aim, move) if b]
        if jump_for:
            held.append("A")
            jump_for -= 1
        if fire:
            held.append("B")
        lines.append(f"{f} {port}{'+'.join(held) if held else '-'}")
        f += 6
    lines.append(f"{END} {port}-")
    return lines


def write(path, lines):
    with open(path, "w") as out:
        out.write("\n".join(lines) + "\n")
    print(f"{path}: {len(lines)} lines")


def main():
    end = int(sys.argv[1]) if len(sys.argv) > 1 else END
    globals()["END"] = end
    write("route_play.txt", title(False) + tail(1))
    two = title(True) + tail(2) + tail(3, "2:")
    two.sort(key=lambda s: int(s.split()[0]))           # engine reads lines in frame order
    write("route_2p.txt", two)


if __name__ == "__main__":
    main()
