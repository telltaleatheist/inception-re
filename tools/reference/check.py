#!/usr/bin/env python3
"""Compare the rebuild with the original, screen by screen.

    check.py --chr BUILD/chr --game-dir DIR --data-dir DIR --work DIR [--full]

Runs the original headless once (run.py) through the cases below, renders
the same steps with `chr --script ... --screenshot`, and compares them pixel
for pixel (compare.py). Each case lists the rectangles the rebuild already
draws, which must be identical, and the differences still known, with their
cause in the original. A rectangle that stops matching fails the check; the
known differences are only reported, so that each one is fixed on purpose.
Each case also compares the game state (`chr --state` against the
original's memory, state.py): the random numbers, C-Bills and the
townspeople's records must be identical, and the cell window chr builds
where the original last built its own (window.py) must hold the same cells.

The quick cases run in ctest (`reference_compare`); --full adds the long
ones (thousands of passes of idling, a walk across MAP1), a few minutes.
--only NAME... runs the named sequences, every case; --affected runs the
ones the changed files exercise (AFFECTED).

The original's captures are cached (cache.py), keyed by the key script and
the rig, so DOSBox-X runs only for a script it has not run; --refresh runs
it anyway. The cache stays on this machine and is never committed.

Exit status: 0 all required rectangles match, 1 a mismatch, 77 skipped
(no DOSBox-X, nasm or game folder: the check needs all three).
"""
import argparse
import concurrent.futures
import fnmatch
import os
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cache  # noqa: E402
import compare  # noqa: E402
import memory  # noqa: E402
import run  # noqa: E402
import state  # noqa: E402
import window  # noqa: E402

SKIP = 77

# The original's keys before the first case. The start-up asks in text mode
# for the graphics adapter (3: 256K EGA/VGA) and the disk drives (3: hard
# disk), then waits on a note about saved games. The rebuild leaves those to
# the platform; its start-up begins with the Infocom screen (the first case).
START = "3 3 enter"

# Every case runs on from the one before it, in one run of the original.
# "keys" are the original's keys and idle steps since the previous case
# (run.py's script language); chr is given the same input with the feeder's
# idle passes spelled out (chr_script()).
WHOLE_PANEL = (0, 0, 104, 200)
WHOLE_MAP = (104, 0, 216, 200)

CASES = [
    {
        # The start-up (docs/startup.md): the Infocom screen, up until a key
        # or 700 frames. No game state yet.
        "name": "start_infocom",
        "keys": "",
        "state": False,
        "match": {"the screen": (0, 0, 320, 200)},
    },
    {
        # The title, while its tune plays, until a key.
        "name": "start_title",
        "keys": "enter",
        "state": False,
        "match": {"the screen": (0, 0, 320, 200)},
    },
    {
        # A new game's panel, the notice and "Is this your first time
        # playing BattleTech?" in window 6, Yes picked.
        "name": "start_first_time",
        "keys": "enter",
        "state": False,
        "match": {"the panel": WHOLE_PANEL, "the window": WHOLE_MAP},
    },
    {
        # The first pass of the main loop is a game tick: 15 C-Bills (20 to
        # 35), the animated tiles on frame 1.
        "name": "town_start",
        "keys": "enter",
        "starts_town": True,
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # 12 passes: the second game tick (the fence's frame 2).
        "name": "town_idle",
        "keys": "idle:9",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # 120 more passes: the townsperson nearest the party (the fifth) walks
        # toward its next entrance, in view.
        "name": "people_walking",
        "keys": "idle:120",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Each key is a game tick too; the fifth asks its question first.
        "name": "citadel_question",
        "keys": "up*5",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Yes: the Citadel (CITADEL.BLD, docs/scripts.md): its picture in the
        # upper left box, its text and menu with the bar on the first item.
        "name": "citadel_inside",
        "keys": "y",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # The first item: the sentry's text, a key, then Katrina's (with
        # flag 00 clear), which sets flags 16 and 06.
        "name": "citadel_katrina",
        "keys": "enter enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Back at the menu, the bar where it was left; down to the Hall of
        # Legends, its first exhibit.
        "name": "citadel_hall",
        "keys": "enter down enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
]

# Run after CASES with --full, continuing from where they end.
FULL_CASES = [
    {
        # The Hall's last exhibit.
        "name": "citadel_hall_end",
        "keys": "enter*5",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # The combat class: the welcome (the first time, flag 07).
        "name": "citadel_class",
        "keys": "enter down enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # The courses with their prices (special 04), menu 9.
        "name": "citadel_class_menu",
        "keys": "enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # The first course: 75 C-Bills, and Jason has 35 (special 05).
        "name": "citadel_no_money",
        "keys": "enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Talk to others (special 12, 17dc:17b9): who is inside the Citadel
        # (nobody, at this point).
        "name": "citadel_talk",
        "keys": "enter down enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Nobody is inside at this point: a key goes back to the menu, the bar
        # on "talk" where it was left.
        "name": "citadel_talk_more",
        "keys": "enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Leave: special 14, the upper box cleared, the panel drawn again,
        # and the step goes on as after "no"; its game tick draws the map.
        "name": "citadel_leave",
        "keys": "down enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Out of the Citadel, then 250 more ticks: the 257th runs the money
        # step again (35 to 50, the floor), and the fence is on frame 2.
        "name": "money_step",
        "keys": "idle:2753",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # A walk west and south through MAP1: scrolling, fences, and the
        # townspeople's steps against where the party was last drawn.
        "name": "walk_west",
        "keys": "down*4 left*26 down*24 right*4 down*10",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        "name": "walk_idle",
        "keys": "idle:400",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Back, then east along the street, round the shops and west again.
        "name": "walk_east",
        "keys": "up*10 left*4 up*24 right*60 up*8 pgup*6 right*10 down*6 left*30 home*4 left*10",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Waiting by the Barracks door: the fourth townsperson reaches it and
        # goes in for a random number of rounds (24).
        "name": "door_in",
        "keys": "idle:600",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # ... and comes out again, for the next entrance.
        "name": "door_out",
        "keys": "idle:1200",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # The command menu's overhead map (1000:3d40, docs/overhead_map.md):
        # the seen town drawn small, the caption, the party's dot in the
        # colour of each pass's random number. The menu's Enter is a
        # blocking read with no flush after it, so all four polls before the
        # next item are the map's passes (arm:4).
        "name": "overhead_map",
        "keys": "space down*6 arm:4 enter",
        "match": {"the screen": (0, 0, 320, 200)},
    },
    {
        # In the town one key closes it: the nine maps loaded again without
        # placing the townspeople (no random numbers), the map drawn again.
        "name": "overhead_closed",
        "keys": "space",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
]

# Training mission 2 (docs/combat.md), a separate run from a new game.
# POKED: flag 00 = 2 and flag 13 = 1 stand in for passing missions 0-1 and
# resting in the barracks (BARRACKS.BLD +0247); docs/reference.md, "Test aids".
TO_TRAINING_CENTER = ("end*2 left*12 end*2 left*2 end*12 left*2 end down*6 end*2 down*6 end*3 "
                      "left*2 end down*2 end*6 down*49 pgdn right*19 pgup*4")
TRAINING_CASES = [dict(case, name="training_" + case["name"]) for case in CASES[:4]] + [
    {
        # Walk to the training center: "Will you enter the Training Center?"
        "name": "training_question",
        "keys": "flag:00=02 flag:13=01 " + TO_TRAINING_CENTER,
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Yes. The original first asks its quiz (a key, then three menus:
        # orig: only, docs/combat.md); then TRAINING.BLD greets and asks
        # about the third mission.
        "name": "training_greeting",
        "keys": "y orig:enter*4",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Yes: the mission's briefing (TRAINING.BLD +05b6).
        "name": "training_briefing",
        "keys": "enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # The 'Mech menu (menu 7).
        "name": "training_mech_menu",
        "keys": "enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # The Locust: special 1 walks it in, the mission places the target,
        # and the fight asks whether the computer should fight (No first).
        "name": "training_computer_question",
        "keys": "enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # No: the combat messages menu, on Verbose.
        "name": "training_messages",
        "keys": "enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Verbose: "See combat graphics", on Yes.
        "name": "training_graphics",
        "keys": "enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # Yes: the orders menu for Jason's Locust, the view on it with the
        # box, the bar on Begin Fight.
        "name": "training_orders",
        "keys": "enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
]

# The training center's quiz, shown (chr --show-quiz, docs/combat.md 7).
# The original asks it as built; the rebuild has no check, and its menu
# starts on the right part. So the original's bar is moved onto the right
# part (orig: keys, by DG:28ec's answers for the parts this run draws: 1, 6
# and 9) before each comparison, and Enter takes it on both sides.
QUIZ_CASES = TRAINING_CASES[:5] + [
    {
        # Yes: the message before the quiz, in the big box.
        "name": "quiz_message",
        "keys": "y",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # A key: BTSTATS.CMP, the first part boxed, the part names.
        "name": "quiz_part_1",
        "keys": "enter orig:down*4",
        "match": {"the screen": (0, 0, 320, 200)},
    },
    {
        "name": "quiz_part_2",
        "keys": "enter orig:down*14",
        "match": {"the screen": (0, 0, 320, 200)},
    },
    {
        "name": "quiz_part_3",
        "keys": "enter orig:up*2",
        "match": {"the screen": (0, 0, 320, 200)},
    },
    {
        # The last answer: the training center greets, as without the scene.
        "name": "quiz_greeting",
        "keys": "enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
]

# Training mission 2 fought by the computer, with no combat messages and no
# combat graphics: after the three answers it runs on random draws alone.
# With the computer fighting, the original asks once a round whether a key
# is waiting (203b:1015). "arm:1" before the last answer has the feeder
# answer the first such question, so the first capture is after round 1;
# each idle:1 after it is one more round. chr asks the same question and
# stops the fight at the first one its script has no item for; "arm:N"
# gives it N - 1 passes after the key. A capture after the fight has ended
# is taken at "You should return to base now.", where the original waits
# for a key (idle:99 lets the rest of the rounds go by first). The training
# cases before it are only captured: the training sequence compares them.
MISSION_ROUNDS = [
    {
        # The state after round n (the screen is not drawn while they run).
        "name": "mission_round_%d" % n,
        "keys": "arm:1 n" if n == 1 else "idle:1",
        "match": {},
        "fight": True,
    }
    for n in range(1, 9)
]
MISSION_CASES = [dict(case, compare=False) for case in TRAINING_CASES[:9]] + [
    {
        # Yes: the combat messages menu, on Verbose.
        "name": "mission_messages",
        "keys": "y",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
    {
        # None: "See combat graphics", on Yes.
        "name": "mission_graphics",
        "keys": "up*2 enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
    },
] + MISSION_ROUNDS[:1]
MISSION_FULL_CASES = MISSION_ROUNDS[1:] + [
    {
        # The fight is over: "You should return to base now."
        "name": "mission_end",
        "keys": "idle:99",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
        "fight": True,
    },
    # A key: the party is put where Jason's 'Mech stands (17dc:134b) and
    # the mission's walk begins; back to the door it ends, and the script
    # goes on; a key, and the town. The way back was found with chr's step
    # check.
    {"name": "mission_walk", "keys": "enter home*8", "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP}},
    {"name": "mission_over", "keys": "home*8 left*30", "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP}},
    {"name": "mission_in_town", "keys": "enter", "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP}},
    {"name": "mission_walk_on", "keys": "down", "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP}},
]

# The same fight with combat messages on (graphics still off): every shot's
# line in window 4, so each capture compares the last shot's text too.
def messages_cases(level, keys):
    """The mission with combat messages `level` ("verbose", "brief"):
    `keys` picks it in the messages menu. (quick cases, long cases)"""
    question = dict(MISSION_CASES[11 - 2], name=level + "_messages")
    graphics = {"name": level + "_graphics", "keys": keys,
                "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP}}
    rounds = [dict(case, name=case["name"].replace("mission", level),
                   match={"the panel": WHOLE_PANEL, "the map": WHOLE_MAP}) for case in MISSION_ROUNDS]
    end = dict(MISSION_FULL_CASES[-1], name=level + "_end")
    prefix = MISSION_CASES[:9]
    return prefix + [question, graphics] + rounds[:1], rounds[1:] + [end]


VERBOSE_CASES, VERBOSE_FULL_CASES = messages_cases("verbose", "enter")

# The same fight with no messages and combat graphics on: the shots' poses,
# missiles and explosions redraw the view, and every redraw writes the
# figures' terrain and screen places (docs/combat.md 3.14, quirk 36). The
# state is compared, not the screen.
DRAWN_ROUNDS = [dict(case, name=case["name"].replace("mission", "drawn"),
                     keys="arm:1 y" if n == 1 else case["keys"],
                     match={"the panel": WHOLE_PANEL, "the map": WHOLE_MAP})
                for n, case in enumerate(MISSION_ROUNDS, 1)]
DRAWN_CASES = [dict(case, compare=False) for case in MISSION_CASES[:11]] + DRAWN_ROUNDS[:1]
DRAWN_FULL_CASES = DRAWN_ROUNDS[1:] + [dict(MISSION_FULL_CASES[-1], name="drawn_end")]
BRIEF_CASES, BRIEF_FULL_CASES = messages_cases("brief", "up enter")

# The same mission fought by the player, Verbose messages and graphics on:
# Begin Fight with no orders, round after round, so the party stands and
# the enemy comes on (docs/combat.md 2.6). Each capture is the next round's
# orders menu. Then Flee: escaped, "You should return to base now.";
# not, "You do not escape!" and "Press a key.", both waiting for a key.
ORDERS_ROUNDS = [
    {
        "name": "orders_round_%d" % n,
        "keys": "enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
        "fight": True,
    }
    for n in range(1, 9)
]
ORDERS_CASES = [dict(case, compare=False) for case in TRAINING_CASES[:11]] + [
    dict(TRAINING_CASES[11], name="orders_menu", fight=True)] + ORDERS_ROUNDS[:1]
ORDERS_FULL_CASES = ORDERS_ROUNDS[1:] + [
    {
        "name": "orders_flee",
        "keys": "up enter",
        "match": {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP},
        "fight": True,
    },
]

# The player's move orders (203b:1c1f, docs/combat.md 2.6): Walk with the
# map cursor, Run over the walk orders (asked, then deleted), Jump with a
# Locust (no jump jets), then rounds fought on the run orders.
SCREEN = {"the panel": WHOLE_PANEL, "the map": WHOLE_MAP}
WHOLE_SCREEN = {"the screen": (0, 0, 320, 200)}
MOVE_CASES = [dict(case, compare=False) for case in ORDERS_CASES[:12]] + [
    # Walk: "You have N movement points left", the cursor at the unit.
    {"name": "move_walk", "keys": "up*9 enter", "match": SCREEN, "fight": True},
]
MOVE_FULL_CASES = [
    # A step east: a tentative waypoint and its path's arrows.
    {"name": "move_step", "keys": "right", "match": SCREEN, "fight": True},
    # Further, diagonally and past the box.
    {"name": "move_steps", "keys": "pgdn right*3 down", "match": SCREEN, "fight": True},
    # Back onto the first step's cell, and Enter: the menu, its bar on Walk.
    {"name": "move_accept", "keys": "up left*3 enter", "match": SCREEN, "fight": True},
    # Walk again: a second waypoint after the first.
    {"name": "move_walk_again", "keys": "enter left*2 up*3", "match": SCREEN, "fight": True},
    {"name": "move_accept_again", "keys": "enter", "match": SCREEN, "fight": True},
    # Run: "You are now Walking. Do you wish to Run instead?"
    {"name": "move_run_question", "keys": "down enter", "match": SCREEN, "fight": True},
    # Yes: the orders deleted, a key.
    {"name": "move_run_deleted", "keys": "y", "match": SCREEN, "fight": True},
    {"name": "move_run_cursor", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "move_run_steps", "keys": "right*4 down*2 enter", "match": SCREEN, "fight": True},
    # Jump with no jump jets: the reason and "Press a key."
    {"name": "move_jump", "keys": "down enter", "match": SCREEN, "fight": True},
    # A key, then Begin Fight: the rounds on the run orders.
    {"name": "move_round_1", "keys": "enter down*7 enter", "match": SCREEN, "fight": True},
    {"name": "move_round_2", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "move_round_3", "keys": "enter", "match": SCREEN, "fight": True},
]

# Thirteen Walk orders (bug 17), each on the unit's own cell (the cursor
# out and back): they cost nothing, and in this area (0xcc) bug 16 never
# finds a repeat. The thirteenth goes into figure 1's first order, in the
# original and, with fix_order_bound off (the default), in chr.
WAYPOINT_CASES = [dict(case, compare=False) for case in ORDERS_CASES[:12]] + [
    {"name": "waypoints_12", "keys": "up*9 enter right left enter " + " ".join(
        ["enter right left enter"] * 11), "match": SCREEN, "fight": True},
]
WAYPOINT_FULL_CASES = [
    {"name": "waypoints_13", "keys": "enter right left enter", "match": SCREEN, "fight": True},
    # Begin Fight: figure 1 is not active, so its order is never read.
    {"name": "waypoints_round", "keys": "down*9 enter", "match": SCREEN, "fight": True},
]

# Use Weapons (1d43:0004, docs/combat.md 2.6): the list, the first weapon
# at the enemy Locust through the target picker, Done, then rounds fought
# with it, and a Flee after the shots have drawn numbers.
WEAPON_CASES = [dict(case, compare=False) for case in ORDERS_CASES[:12]] + [
    # The list in window 5, the bar on the first weapon.
    {"name": "weapons_list", "keys": "up*6 enter", "match": SCREEN, "fight": True},
]
WEAPON_FULL_CASES = [
    # The first weapon: "Choose a target", the picker on the enemy Locust.
    # The view's last column is past the map file's east edge: terrain.
    {"name": "weapons_picker", "keys": "enter", "match": SCREEN, "fight": True},
    # Fire: back to the list, the weapon's target and range band shown.
    {"name": "weapons_targeted", "keys": "enter", "match": SCREEN, "fight": True},
    # Done (up from the first item wraps to it): the orders menu.
    {"name": "weapons_done", "keys": "up enter", "match": SCREEN, "fight": True},
] + [
    {"name": "weapons_round_%d" % n, "keys": "up enter" if n == 1 else "enter", "match": SCREEN, "fight": True}
    for n in range(1, 5)
]

# Kick (203b:2231): the picker for target slot 11, then Begin Fight and
# rounds with the kick ordered.
KICK_CASES = [dict(case, compare=False) for case in ORDERS_CASES[:12]] + [
    # "Choose the 'Mech to kick", the picker on the enemy Locust.
    # Its last map column is past the map file's edge, as in weapons_picker.
    {"name": "kick_picker", "keys": "up*5 enter", "match": SCREEN, "fight": True},
]
KICK_FULL_CASES = [
    # Target here: back to the orders menu, its bar on Kick.
    {"name": "kick_targeted", "keys": "enter", "match": SCREEN, "fight": True},
] + [
    {"name": "kick_round_%d" % n, "keys": "down*5 enter" if n == 1 else "enter", "match": SCREEN, "fight": True}
    for n in range(1, 4)
] + [
    # The left hip actuator lost (a test poke of byte 0x24 standing in for
    # a critical hit): Kick says it cannot, and a key.
    {"name": "kick_no_hip", "keys": "mech:0:24=c7 up*5 enter", "match": SCREEN, "fight": True},
    # The key: the orders menu, its bar on Kick.
    {"name": "kick_no_hip_menu", "keys": "enter", "match": SCREEN, "fight": True},
    # Begin Fight: the round drops the kick still in slot 11.
    {"name": "kick_no_hip_round", "keys": "down*5 enter", "match": SCREEN, "fight": True},
]

# Scan Unit (203b:2591) and the scan list (15ab:1467): Friends (the unit
# itself, Next comes back to it), Done, Enemies (the enemy Locust and its
# direction), Done, Cancel, then a round.
SCAN_CASES = [dict(case, compare=False) for case in ORDERS_CASES[:12]] + [
    # Friends / Enemies / Cancel in window 3.
    {"name": "scan_menu", "keys": "up*3 enter", "match": SCREEN, "fight": True},
    # Friends: the unit's heading and screen; Next, Detail, Done.
    {"name": "scan_friends", "keys": "enter", "match": SCREEN, "fight": True},
]
SCAN_FULL_CASES = [
    # Next: past the inactive figures and back to the unit.
    {"name": "scan_friends_next", "keys": "enter", "match": SCREEN, "fight": True},
    # Done: the orders menu, its bar on Scan Unit.
    {"name": "scan_friends_done", "keys": "down*2 enter", "match": SCREEN, "fight": True},
    # Enemies: the enemy Locust, its direction, the view on it. The view's
    # last column is past the map file's edge, as in weapons_picker.
    {"name": "scan_enemies", "keys": "enter down enter", "match": SCREEN, "fight": True},
    {"name": "scan_enemies_done", "keys": "down*2 enter", "match": SCREEN, "fight": True},
    # Cancel.
    {"name": "scan_cancel", "keys": "enter down*2 enter", "match": SCREEN, "fight": True},
    # Begin Fight from Scan Unit.
    {"name": "scan_round", "keys": "down*3 enter", "match": SCREEN, "fight": True},
]

# Detail Scan (15ab:1467): the stats screen (15ab:1afe, docs/town_loop.md),
# after a round of fire. A fight's orders menu never comes with heat left,
# so heat: sets Jason's Locust's (12: the bar blinks every 8 draws). arm:4
# has the feeder's fourth poll be the screen's fourth frame on both sides.
# The screen's pulsing colour starts from stack words the original never
# sets, so only a run's first showing is the same in every run, up to its
# 16th frame; a later showing compares the text column and the state.
DETAIL_CASES = [dict(case, compare=False) for case in WEAPON_CASES + WEAPON_FULL_CASES[:4]] + [
    {"name": "detail_friends", "keys": "up*3 enter enter", "match": SCREEN, "fight": True},
    {"name": "detail_friend", "keys": "heat:0=0c down arm:4 enter", "match": WHOLE_SCREEN, "fight": True},
    # Ten frames on: the heat bar in its second colour.
    {"name": "detail_friend_flash", "keys": "idle:10", "match": WHOLE_SCREEN, "fight": True},
    # A key: the screen drawn again, the same unit shown.
    {"name": "detail_friend_out", "keys": "enter", "match": SCREEN, "fight": True},
    # Again, with heat past 30: kept to 30 in place (the state's heat).
    {"name": "detail_friend_again", "keys": "heat:0=28 down arm:4 enter", "match": {"the text": WHOLE_PANEL},
     "fight": True, "known": ["the bars and pips: the pulsing colour depends on the run (docs/town_loop.md)"]},
    {"name": "detail_friend_again_out", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "detail_done", "keys": "down*2 enter", "match": SCREEN, "fight": True},
]
# The enemy Locust's, first in its run: "Unknown" for its crew.
DETAIL_ENEMY_CASES = [dict(case, compare=False) for case in WEAPON_CASES + WEAPON_FULL_CASES[:4]] + [
    {"name": "detail_enemies", "keys": "up*3 enter down enter", "match": SCREEN, "fight": True},
    {"name": "detail_enemy", "keys": "down arm:4 enter", "match": WHOLE_SCREEN, "fight": True},
    {"name": "detail_enemy_out", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "detail_enemy_done", "keys": "down*2 enter", "match": SCREEN, "fight": True},
]

# Mission 3 (training mode 3, docs/combat.md 1.3): three to six enemy
# people against Jason's Locust (17dc:0d49's people), then rounds fought.
PEOPLE_CASES = [dict(case, name=case["name"].replace("training_", "people_")) for case in TRAINING_CASES]
PEOPLE_CASES[4] = dict(PEOPLE_CASES[4], keys=PEOPLE_CASES[4]["keys"].replace("flag:00=02", "flag:00=03"))
PEOPLE_CASES = [dict(case, compare=False) for case in PEOPLE_CASES[:5]] + PEOPLE_CASES[5:]
# From round 6 the soldiers' lines of sight read memory before the cell
# window, the area grids (docs/combat.md 1.2.1). The fight goes on: they
# keep to the area's south edge, and none falls in 40 rounds (all 40
# match); 20 are kept here.
PEOPLE_FULL_CASES = [
    {"name": "people_round_%d" % n, "keys": "enter", "match": SCREEN, "fight": True}
    for n in range(1, 21)
]

# Mission 0 (training mode 0, docs/combat.md 1.3): the walking course.
# Flag 00 stays 0, as in a new game.
COURSE_CASES = [dict(case, name=case["name"].replace("training_", "course_")) for case in TRAINING_CASES[:9]]
# No pokes: the course run starts from the new game as it is.
COURSE_CASES[4] = dict(COURSE_CASES[4], keys=COURSE_CASES[4]["keys"].replace("flag:00=02 flag:13=01 ", ""))
COURSE_CASES = [dict(case, compare=False) for case in COURSE_CASES[:8]] + [
    # The Locust walks in, and the course starts: the party waits in its 'Mech.
    dict(COURSE_CASES[8], name="course_start", match=SCREEN)]
# The course's way to the southeast corner and back (found with chr's own
# step check, which the original agrees with along it).
COURSE_TO_CORNER = "pgdn*14 right*4 pgdn*2 down*8 pgdn*21 right*12"
COURSE_BACK_1 = "home*24 up*6 right down pgup down pgup down pgup down pgup*2 up*3"
COURSE_BACK_2 = "up*2 home*12 left*30"
COURSE_FULL_CASES = [
    {"name": "course_step_%d" % n, "keys": "right", "match": SCREEN} for n in range(1, 4)
] + [
    # The rest of the way to the corner: "You've made it to the southeast
    # corner", a key.
    {"name": "course_corner", "keys": COURSE_TO_CORNER, "match": SCREEN},
    # The key, and half the way back.
    {"name": "course_returning", "keys": "enter " + COURSE_BACK_1, "match": SCREEN},
    # The rest: at the door the mission ends and the script goes on.
    {"name": "course_ended", "keys": COURSE_BACK_2, "match": SCREEN},
    # A key: the script ends (special 3 hands the 'Mech back) and the party
    # is in town again, on foot.
    {"name": "course_in_town", "keys": "enter", "match": SCREEN},
    {"name": "course_walk_on", "keys": "down*3", "match": SCREEN},
]

# The computer hands the fight back (a key at the round's poll) and the
# player flees, and fails: 52 idle passes in town before the walk to the
# training center and six computer-fought rounds give a flee draw with
# r & 3 = 0 (docs/combat.md 2.7). The unit stays under the computer, so
# the orders screen shows its plan (1e31:03ab with show).
HANDOVER_CASES = [dict(case, compare=False) for case in MISSION_CASES[:4]] + [
    dict(MISSION_CASES[4], keys="idle:52 " + MISSION_CASES[4]["keys"], compare=False)] + [
    dict(case, compare=False) for case in MISSION_CASES[5:]] + [
    dict(case, name=case["name"].replace("mission", "handover")) for case in MISSION_ROUNDS[1:6]] + [
    # A key at the poll: the orders menu, "The computer is moving this unit".
    {"name": "handover_menu", "keys": "enter", "match": SCREEN, "fight": True},
    # Flee, and fail: "You do not escape!", "Press a key."
    {"name": "handover_flee_failed", "keys": "up enter", "match": SCREEN, "fight": True},
    # The key: the round goes on; the next orders menu.
    {"name": "handover_next_round", "keys": "enter", "match": SCREEN, "fight": True},
]

# Training mission 4 (flag 00 poked to 4): one enemy 'Mech, and no
# computer to fight for the party (F:0090), so no question about it. The
# Locust flees at the first orders menu; the walk back from Jason's 'Mech
# to the door, the verdict on running away, and the town.
FOURTH_CASES = [dict(case, compare=False) for case in TRAINING_CASES[:4]] + [
    dict(TRAINING_CASES[4], name="fourth_question",
         keys=TRAINING_CASES[4]["keys"].replace("flag:00=02", "flag:00=04"), match=SCREEN),
    dict(TRAINING_CASES[5], name="fourth_greeting", match=SCREEN),
    dict(TRAINING_CASES[6], name="fourth_briefing", match=SCREEN),
    dict(TRAINING_CASES[7], name="fourth_mech_menu", match=SCREEN),
    {"name": "fourth_messages", "keys": "enter", "match": SCREEN},
    {"name": "fourth_graphics", "keys": "up*2 enter", "match": SCREEN},
    {"name": "fourth_orders", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "fourth_fled", "keys": "up enter", "match": SCREEN, "fight": True},
    {"name": "fourth_walk", "keys": "enter", "match": SCREEN},
    {"name": "fourth_over", "keys": "left*4", "match": SCREEN},
    {"name": "fourth_in_town", "keys": "enter", "match": SCREEN},
]

# Training mission 7 (flag 00 poked to 7): always the raid. Its message,
# the options without the computer question, then Begin Fight round after
# round: a story line after each (the second with the place's picture,
# after the third map 11 in the town's place), until the Jenners bring the
# Locust down; the rest of the lines, the results and the script.
def raid_round(n, keys):
    return {"name": "raid_%02d" % n, "keys": keys, "match": SCREEN, "fight": True}
RAID_CASES = [dict(case, compare=False) for case in FOURTH_CASES[:4]] + [
    dict(FOURTH_CASES[4], name="raid_question",
         keys=FOURTH_CASES[4]["keys"].replace("flag:00=04", "flag:00=07")),
    dict(FOURTH_CASES[5], name="raid_greeting"),
    dict(FOURTH_CASES[6], name="raid_briefing"),
    dict(FOURTH_CASES[7], name="raid_message"),
    {"name": "raid_mechs", "keys": "enter", "match": SCREEN},
    {"name": "raid_computer", "keys": "enter", "match": SCREEN},
    {"name": "raid_messages", "keys": "enter", "match": SCREEN},
    {"name": "raid_graphics", "keys": "up*2 enter", "match": SCREEN},
    {"name": "raid_orders", "keys": "enter", "match": SCREEN, "fight": True},
    raid_round(1, "enter"), raid_round(3, "enter*2"), raid_round(5, "enter*2"), raid_round(6, "enter"),
    raid_round(9, "enter*3"), raid_round(13, "enter*4"), raid_round(17, "enter*4"),
    {"name": "raid_18", "keys": "enter", "match": SCREEN},
    {"name": "raid_20", "keys": "enter*2", "match": SCREEN},
    {"name": "raid_21", "keys": "enter", "match": SCREEN},
    {"name": "raid_22", "keys": "enter", "match": SCREEN},
]

# The third mission's first try (flag 13 not poked): the commandant stops
# Jason and takes the 'Mech back (TRAINING.BLD +04f3, special 3). He sleeps
# in the barracks (BARRACKS.BLD +0243: flag 13 + 1, the story, flag 14 = 0)
# and goes back for the briefing, the quiz asked again.
REST_CASES = [dict(case, compare=False) for case in TRAINING_CASES[:4]] + [
    dict(TRAINING_CASES[4], name="rest_question",
         keys=TRAINING_CASES[4]["keys"].replace("flag:13=01", "flag:01=01")),
    dict(TRAINING_CASES[5], name="rest_greeting"),
    {"name": "rest_commandant", "keys": "y", "match": SCREEN},
    {"name": "rest_handed_back", "keys": "enter", "match": SCREEN},
    # On foot to the Barracks door (MAP1 entrance 6).
    {"name": "rest_barracks_question", "keys": "up home*2 down*2 left*2 down right*2 pgup down*2 left*23 home up*85 left home up left home up*15 home", "match": SCREEN},
    {"name": "rest_barracks", "keys": "y", "match": SCREEN},
    {"name": "rest_sleep", "keys": "down enter", "match": SCREEN},
    {"name": "rest_story_2", "keys": "enter", "match": SCREEN},
    {"name": "rest_story_4", "keys": "enter*2", "match": SCREEN},
    {"name": "rest_story_7", "keys": "enter*3", "match": SCREEN},
    {"name": "rest_menu_again", "keys": "enter", "match": SCREEN},
    {"name": "rest_leave", "keys": "up enter", "match": SCREEN},
    # The same way back (each step turned round) to the Training Center.
    {"name": "rest_back_question", "keys": "pgdn down*15 pgdn right down pgdn right down*85 pgdn right*23 up*2 end left*2 up right*2 up*2 pgdn*2 down home up*2", "match": SCREEN},
    # Flag 14 is 0 again: the quiz first (orig: only), then the greeting.
    {"name": "rest_back_greeting", "keys": "y orig:enter*4", "match": SCREEN},
    # Flag 13 is set now: the third mission's briefing (TRAINING.BLD +05b6).
    {"name": "rest_briefing", "keys": "y", "match": SCREEN},
]

# Each sequence is one run of the original: (name, quick cases, --full
# cases, chr's own options).
# Mission 1, the marked spot, reached by keys alone after the course: flag
# 14 (set by TRAINING.BLD as a mission starts) runs down over 255 town
# ticks, an idle tick every 11 polls; then the training center again, the
# quiz on the original's side (the copy we read always passes it), the
# second mission in the Wasp, which has hands. The spot is drawn at random
# (spot 3 here); the way to it and back was found with chr's step check.
SPOT_CASES = [dict(case, compare=False) for case in COURSE_CASES + COURSE_FULL_CASES] + [
    {"name": "spot_waited", "keys": "idle:2900 up*5", "match": SCREEN},
    {"name": "spot_greeting", "keys": "y orig:enter*4", "match": SCREEN},
    {"name": "spot_briefing", "keys": "y", "match": SCREEN},
    {"name": "spot_start", "keys": "enter down enter", "match": SCREEN},
    {"name": "spot_reached", "keys": "pgup*10 right*21 pgdn right*3 pgup right*18", "match": SCREEN},
    {"name": "spot_back", "keys": "enter end*2 down*4 end left*55", "match": SCREEN},
    {"name": "spot_in_town", "keys": "enter", "match": SCREEN},
    {"name": "spot_walk_on", "keys": "down", "match": SCREEN},
]

# The long run with no pokes, from the new game on: the course and the
# spot (missions 0 and 1, as `course` and `spot`), then the third mission's
# first try: flag 14 runs down, the commandant sends Jason to rest, the
# Barracks, and back for the third mission.
CHAIN_CASES = [dict(case, compare=False) for case in SPOT_CASES] + [
    {"name": "chain_waited", "keys": "idle:2900 up*5", "match": SCREEN},
    {"name": "chain_greeting", "keys": "y orig:enter*4", "match": SCREEN},
    {"name": "chain_commandant", "keys": "y", "match": SCREEN},
    {"name": "chain_handed_back", "keys": "enter", "match": SCREEN},
    # One step east, then the `rest` sequence's ways there and back.
    {"name": "chain_barracks_question", "keys": "right up home*2 down*2 left*2 down right*2 pgup down*2 left*23 home up*85 left home up left home up*15 home", "match": SCREEN},
    {"name": "chain_barracks", "keys": "y", "match": SCREEN},
    {"name": "chain_sleep", "keys": "down enter", "match": SCREEN},
    {"name": "chain_story_4", "keys": "enter*3", "match": SCREEN},
    {"name": "chain_story_7", "keys": "enter*3", "match": SCREEN},
    {"name": "chain_menu_again", "keys": "enter", "match": SCREEN},
    {"name": "chain_leave", "keys": "up enter", "match": SCREEN},
    {"name": "chain_back_question", "keys": "pgdn down*15 pgdn right down pgdn right down*85 pgdn right*23 up*2 end left*2 up right*2 up*2 pgdn*2 down home up*2", "match": SCREEN},
    {"name": "chain_back_greeting", "keys": "y orig:enter*4", "match": SCREEN},
    {"name": "chain_briefing", "keys": "y", "match": SCREEN},
    # The third mission in the Wasp (the menu's bar is where mission 1 left
    # it), the computer fighting, graphics off: as in `mission`.
    {"name": "chain_mech_menu", "keys": "enter", "match": SCREEN},
    {"name": "chain_computer_question", "keys": "enter", "match": SCREEN},
    {"name": "chain_messages", "keys": "y", "match": SCREEN},
    {"name": "chain_graphics", "keys": "up*2 enter", "match": SCREEN},
    {"name": "chain_round_1", "keys": "arm:1 n", "match": SCREEN, "fight": True},
] + [{"name": "chain_round_%d" % n, "keys": "idle:1", "match": SCREEN, "fight": True} for n in range(2, 17)] + [
    {"name": "chain_end", "keys": "idle:99", "match": SCREEN, "fight": True},
    # The walk back to the door (found with chr), the verdict and the town.
    {"name": "chain_walk", "keys": "enter home*8", "match": SCREEN},
    {"name": "chain_over", "keys": "home*7 left*30", "match": SCREEN},
    {"name": "chain_in_town", "keys": "enter", "match": SCREEN},
    {"name": "chain_walk_on", "keys": "down", "match": SCREEN},
    # Flag 14 runs down again; mission 3 (enemy people) in the Wasp, the
    # computer fighting: the last falls in round 13.
    {"name": "chain_people_waited", "keys": "idle:2900 up*5", "match": SCREEN},
    {"name": "chain_people_greeting", "keys": "y orig:enter*4", "match": SCREEN},
    {"name": "chain_people_briefing", "keys": "y", "match": SCREEN},
    {"name": "chain_people_mech_menu", "keys": "enter", "match": SCREEN},
    {"name": "chain_people_computer", "keys": "enter", "match": SCREEN},
    {"name": "chain_people_messages", "keys": "y", "match": SCREEN},
    {"name": "chain_people_graphics", "keys": "up*2 enter", "match": SCREEN},
    {"name": "chain_people_round_1", "keys": "arm:1 n", "match": SCREEN, "fight": True},
] + [{"name": "chain_people_round_%d" % n, "keys": "idle:1", "match": SCREEN, "fight": True} for n in range(2, 14)] + [
    {"name": "chain_people_end", "keys": "idle:99", "match": SCREEN, "fight": True},
    {"name": "chain_people_walk", "keys": "enter home*13 left home*2 up*4 right down pgup down pgup down pgup down pgup*2 up*5 home*12 left*30", "match": SCREEN},
    {"name": "chain_people_in_town", "keys": "enter", "match": SCREEN},
    {"name": "chain_people_walk_on", "keys": "down", "match": SCREEN},
    # Mission 4: no computer question; the Locust flees at the first orders
    # menu, as in `fourth`.
    {"name": "chain_fourth_waited", "keys": "idle:2900 up*5", "match": SCREEN},
    {"name": "chain_fourth_greeting", "keys": "y orig:enter*4", "match": SCREEN},
    {"name": "chain_fourth_briefing", "keys": "y", "match": SCREEN},
    {"name": "chain_fourth_mech_menu", "keys": "enter", "match": SCREEN},
    {"name": "chain_fourth_messages", "keys": "enter", "match": SCREEN},
    {"name": "chain_fourth_graphics", "keys": "up*2 enter", "match": SCREEN},
    {"name": "chain_fourth_orders", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_fourth_fled", "keys": "up enter", "match": SCREEN, "fight": True},
    {"name": "chain_fourth_walk", "keys": "enter home*3", "match": SCREEN},
    {"name": "chain_fourth_over", "keys": "left", "match": SCREEN},
    {"name": "chain_fourth_in_town", "keys": "enter", "match": SCREEN},
    {"name": "chain_fourth_walk_on", "keys": "down", "match": SCREEN},
    # Mission 4 again, won by orders: the Wasp loses a fair fight with the
    # Locust, so the Chameleon, every weapon on the Locust, one waypoint.
    {"name": "chain_win_waited", "keys": "idle:2900 up*5", "match": SCREEN},
    {"name": "chain_win_greeting", "keys": "y orig:enter*4", "match": SCREEN},
    {"name": "chain_win_briefing", "keys": "y", "match": SCREEN},
    {"name": "chain_win_mech_menu", "keys": "enter", "match": SCREEN},
    {"name": "chain_win_messages", "keys": "down enter", "match": SCREEN},
    {"name": "chain_win_graphics", "keys": "up*2 enter", "match": SCREEN},
    {"name": "chain_win_orders", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_win_weapons", "keys": "up*6 enter enter enter down enter enter down enter enter down enter enter down enter enter down enter enter down enter enter down enter enter down enter enter down enter", "match": SCREEN, "fight": True},
    {"name": "chain_win_walk", "keys": "enter pgdn*4 right*4 enter", "match": SCREEN, "fight": True},
    {"name": "chain_win_round_1", "keys": "down*9 enter", "match": SCREEN, "fight": True},
] + [{"name": "chain_win_round_%d" % n, "keys": "enter", "match": SCREEN, "fight": True} for n in range(2, 7)] + [
    {"name": "chain_win_end", "keys": "idle:99", "match": SCREEN, "fight": True},
    {"name": "chain_win_walk_back", "keys": "enter home*7", "match": SCREEN},
    {"name": "chain_win_over", "keys": "left*5", "match": SCREEN},
    {"name": "chain_win_in_town", "keys": "enter", "match": SCREEN},
    {"name": "chain_win_walk_on", "keys": "down", "match": SCREEN},
    # Mission 5, two 'Mechs, by orders in the Chameleon. Fewer weapons, or
    # it overheats: the two Medium Lasers and the Machine Guns on figure 12,
    # one waypoint. 12 falls in round 8; the first Medium Laser is gone by
    # then, so the other three go on 13 with a waypoint south, where it can
    # be seen; 13 falls in round 15.
    {"name": "chain_five_waited", "keys": "idle:2900 up*5", "match": SCREEN},
    {"name": "chain_five_greeting", "keys": "y orig:enter*4", "match": SCREEN},
    {"name": "chain_five_briefing", "keys": "y", "match": SCREEN},
    {"name": "chain_five_mech_menu", "keys": "enter", "match": SCREEN},
    {"name": "chain_five_computer", "keys": "enter", "match": SCREEN},
    {"name": "chain_five_messages", "keys": "n", "match": SCREEN},
    {"name": "chain_five_graphics", "keys": "enter", "match": SCREEN},
    {"name": "chain_five_orders", "keys": "n", "match": SCREEN, "fight": True},
    {"name": "chain_five_weapons", "keys": "up*6 enter up*9 enter enter down down down down enter enter down down down enter enter down enter enter down enter", "match": SCREEN, "fight": True},
    {"name": "chain_five_walk", "keys": "enter pgdn*4 right*4 enter", "match": SCREEN, "fight": True},
    {"name": "chain_five_round_1", "keys": "down*9 enter", "match": SCREEN, "fight": True},
] + [{"name": "chain_five_round_%d" % n, "keys": "enter", "match": SCREEN, "fight": True} for n in range(2, 9)] + [
    {"name": "chain_five_round_9", "keys": "up*6 enter up*9 down*4 enter enter down*3 enter enter down enter enter down enter enter down*4 enter down*9 enter", "match": SCREEN, "fight": True},
] + [{"name": "chain_five_round_%d" % n, "keys": "enter", "match": SCREEN, "fight": True} for n in range(10, 16)] + [
    {"name": "chain_five_end", "keys": "idle:99", "match": SCREEN, "fight": True},
    {"name": "chain_five_walk_back", "keys": "enter home*11", "match": SCREEN},
    {"name": "chain_five_over", "keys": "left", "match": SCREEN},
    {"name": "chain_five_in_town", "keys": "enter", "match": SCREEN},
    {"name": "chain_five_walk_on", "keys": "down", "match": SCREEN},
    # Mission 6: its one draw lands on the raid here (r & 1), the keys as in
    # `raid`.
    {"name": "chain_raid_waited", "keys": "idle:2900 up*5", "match": SCREEN},
    {"name": "chain_raid_greeting", "keys": "y orig:enter*4", "match": SCREEN},
    {"name": "chain_raid_briefing", "keys": "y", "match": SCREEN},
    {"name": "chain_raid_mech_menu", "keys": "enter", "match": SCREEN},
    {"name": "chain_raid_message", "keys": "enter", "match": SCREEN},
    {"name": "chain_raid_computer", "keys": "enter", "match": SCREEN},
    {"name": "chain_raid_messages", "keys": "enter", "match": SCREEN},
    {"name": "chain_raid_graphics", "keys": "enter", "match": SCREEN},
    {"name": "chain_raid_options", "keys": "up*2 enter", "match": SCREEN},
    {"name": "chain_raid_orders", "keys": "enter", "match": SCREEN, "fight": True},
] + [{"name": "chain_raid_round_%d" % n, "keys": "enter", "match": SCREEN, "fight": True} for n in range(1, 6)] + [
    # Then Begin Fight (and the story lines' keys) on until the Jenners bring
    # the Chameleon down and the town comes back.
] + [{"name": "chain_raid_key_%d" % n, "keys": "enter*5", "match": SCREEN, "fight": True} for n in range(10, 46, 5)] + [
    {"name": "chain_raid_key_%d" % n, "keys": "enter", "match": SCREEN, "fight": True} for n in range(46, 58)] + [
    # Out of the town to the north: the town is map 11 now (outdoors,
    # flag 04), and past its top edge lies area 0xbc, all terrain. Random
    # encounters come up on the way (flag 24 is 0x3f), none in reach.
    {"name": "chain_out_1", "keys": "up*46 pgup up home up*3 left down home down home down home down home", "match": SCREEN},
    {"name": "chain_out_2", "keys": "home up left home up*5 left home up*5 left down home down home down home down home down home down home down home down home down home down home down home down home down home down home down home down pgdn right*12", "match": SCREEN},
    {"name": "chain_out_3", "keys": "right*2 down*2 home down home down home down home down home down home down home down home down home down home down home down home down home down home down pgdn right*13 pgdn*2 down home*2 down home down home down home down home down home", "match": SCREEN},
    {"name": "chain_out_4", "keys": "down home down home down home down home down home down home down home down home*3 down home down home down home down home down home down home down home*2 up left down pgdn*2 left home down end down home*2 up*11 pgup*5", "match": SCREEN},
    {"name": "chain_out_area", "keys": "up*10", "match": SCREEN},
]

# The chain's other branch from the walk out, its own sequence: a step west
# meets a random encounter in reach, four soldiers. Declining does not get
# away this time (r & 3 is 0), and the fight's questions follow.
CHAIN_DEATH_CASES = [dict(case, compare=False) for case in CHAIN_CASES] + [
    {"name": "chain_enc", "keys": "home", "match": SCREEN},
    {"name": "chain_enc_no", "keys": "n", "match": SCREEN},
    {"name": "chain_enc_computer", "keys": "enter", "match": SCREEN},
    # The computer fights (Yes), no combat messages, no combat graphics,
    # then round after round as in MISSION_ROUNDS. Jason, alone on foot,
    # is killed in round 5: the game is over (DG:014a is 0), a box and a
    # key. The question after the key is not compared: the original's
    # capture after Enter still shows the box.
] + [{"name": "chain_enc_round_%d" % n, "keys": "y enter arm:1 n" if n == 1 else "idle:1", "match": {}, "fight": True}
     for n in range(1, 6)] + [
    {"name": "chain_enc_over", "keys": "idle:1", "match": SCREEN},
]

# Poked: from chain_out_4, Jason is put in a 'Mech (docs/reference.md,
# "Pokes"): the raid's wrecked Chameleon record 0 gets a first letter, armour
# and structure, and character 0 byte 0x0c says he pilots record 0. The
# values are our own test values, not the template's (docs/reference.md).
# Then the walk west to the first
# encounter in reach, five soldiers, which the computer fights.
MECH_POKES = " ".join(["char:0:0c=00", "mech:0:00=43"] + ["mech:0:%02x=20" % b for b in range(0x11, 0x1c)]
                      + ["mech:0:%02x=10" % b for b in range(0x1c, 0x24)] + ["mech:0:24=ff"])
CHAIN_MECH_POKED_CASES = [dict(case, compare=False) for case in CHAIN_CASES[:CHAIN_CASES.index(
    next(c for c in CHAIN_CASES if c["name"] == "chain_out_4")) + 1]] + [
    # The state only: the original's screen still shows Jason on foot until
    # its next drawing, chr's draws the poked 'Mech at once.
    {"name": "chain_mech_poked", "keys": MECH_POKES + " idle:1", "match": {}},
    {"name": "chain_mech_walk", "keys": "left*29", "match": SCREEN},
    {"name": "chain_mech_enc", "keys": "left", "match": SCREEN},
    {"name": "chain_mech_round_1", "keys": "y y enter arm:1 n", "match": {}, "fight": True},
    {"name": "chain_mech_end_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_mech_end_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_mech_end_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_mech_loot", "keys": "enter", "match": SCREEN},
    {"name": "chain_mech_swap", "keys": "enter", "match": SCREEN},
    {"name": "chain_mech_return", "keys": "enter", "match": SCREEN},
]

# Poked: chain_mech_poked with Jason's hit points also lowered (character 0
# byte 0x0f, our own test value), so the same won fight ends in healing.
# On foot he loses to every group in reach, even with a poked rifle and the
# most hit points a byte allows, and the soldiers never hurt a 'Mech's pilot.
# Pokes draw no numbers, so the fight is the same; only the tail is compared.
CHAIN_HEAL_POKED_CASES = [dict(case, compare=False) for case in CHAIN_MECH_POKED_CASES
                          if case["name"] != "chain_mech_return"]
CHAIN_HEAL_POKED_CASES[[c["name"] for c in CHAIN_HEAL_POKED_CASES].index("chain_mech_poked")] = {
    "name": "chain_heal_poked", "keys": MECH_POKES + " char:0:0f=30 idle:1", "match": {}}
CHAIN_HEAL_POKED_CASES += [
    {"name": "chain_heal", "keys": "enter", "match": SCREEN},
    {"name": "chain_heal_return", "keys": "enter", "match": SCREEN},
]

# Poked: chain_mech_poked with Jason given Tech 4 (character 0 byte 9, our
# own test value). After the soldiers he walks on to a Wasp and a soldier;
# the soldier runs, the computer brings the Wasp down, and the repairs run
# (no scrap money: Jason is character 0, bug 24). Only the tail is compared.
CHAIN_REPAIR_POKED_CASES = [dict(case, compare=False) for case in CHAIN_MECH_POKED_CASES]
CHAIN_REPAIR_POKED_CASES[[c["name"] for c in CHAIN_REPAIR_POKED_CASES].index("chain_mech_poked")] = {
    "name": "chain_repair_poked", "keys": MECH_POKES + " char:0:09=04 idle:1", "match": {}}
CHAIN_REPAIR_POKED_CASES += [
    {"name": "chain_repair_walk", "keys": "enter", "match": SCREEN, "compare": False},
    {"name": "chain_repair_enc", "keys": "left*27", "match": SCREEN},
    # The key after round 1's start hands the fight back to the player
    # (203b:1015); each Begin Fight (Enter, the default) then runs a round
    # with the computer still moving the Chameleon. Round 1 ends with "Some
    # of the enemy humans have fled!" (2.10) and a key. The Wasp falls in
    # the round of the twelfth key; the next shows the scuffle box.
    {"name": "chain_repair_round_1", "keys": "y y enter arm:1 n", "match": {}, "fight": True},
    {"name": "chain_repair_player", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_repair_fled", "keys": "enter", "match": SCREEN, "fight": True},
] + [{"name": "chain_repair_key_%d" % n, "keys": "enter", "match": {}, "fight": True} for n in range(3, 12)] + [
    {"name": "chain_repair_key_12", "keys": "enter", "match": SCREEN},
    {"name": "chain_repair_over", "keys": "enter", "match": SCREEN},
    {"name": "chain_repair", "keys": "enter", "match": SCREEN},
    {"name": "chain_repair_return", "keys": "enter", "match": SCREEN},
    {"name": "chain_repair_back", "keys": "enter", "match": SCREEN},
]

# Poked: chain_mech_poked, then after the soldiers a second party member,
# Rex (character 1: name number 1, attributes, Piloting 2, on foot, hit
# points; our own test values), and the walk on to the same Wasp and
# soldier as chain_repair_poked. Rex fights on foot as figure 5. After the
# fight he is a pilot without a 'Mech: Yes to salvage, and Jason, the only
# one with Tech, salvages the Wasp into 'Mech record 1 for him (15ab:04f9).
# Only the tail is compared.
SALVAGE_POKES = " ".join(["char:1:00=01", "char:1:01=08", "char:1:02=09", "char:1:03=07",
                          "char:1:08=02", "char:1:0c=08", "char:1:0f=50"])
CHAIN_SALVAGE_POKED_CASES = [dict(case, compare=False) for case in CHAIN_MECH_POKED_CASES] + [
    {"name": "chain_salvage_walk", "keys": "enter", "match": SCREEN, "compare": False},
    {"name": "chain_salvage_poked", "keys": SALVAGE_POKES + " idle:1", "match": {}},
    {"name": "chain_salvage_enc", "keys": "left*27", "match": SCREEN},
    # As in chain_repair_poked: a key hands the fight back, then Begin
    # Fight each round. With Rex on foot the scuffle box comes three rounds
    # later.
    {"name": "chain_salvage_round_1", "keys": "y y enter arm:1 n", "match": {}, "fight": True},
    {"name": "chain_salvage_player", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_salvage_fled", "keys": "enter", "match": SCREEN, "fight": True},
] + [{"name": "chain_salvage_key_%d" % n, "keys": "enter", "match": {}, "fight": True} for n in range(3, 16)] + [
    {"name": "chain_salvage_over", "keys": "enter", "match": SCREEN},
    {"name": "chain_salvage_question", "keys": "enter", "match": SCREEN},
    {"name": "chain_salvage", "keys": "enter", "match": SCREEN},
    {"name": "chain_salvage_none", "keys": "enter", "match": SCREEN},
    {"name": "chain_salvage_back", "keys": "enter", "match": SCREEN},
]

# Poked: chain_salvage_poked with Rex also given Tech 2 (character 1 byte 9,
# our own test value), above Jason's. Rex makes the repairs, and the scrap
# money is multiplied by his character number, 1 (bug 24). The fight is the
# same; only the tail is compared.
CHAIN_SCRAP_POKED_CASES = [dict(case, compare=False) for case in
                           CHAIN_SALVAGE_POKED_CASES[:[c["name"] for c in CHAIN_SALVAGE_POKED_CASES].index("chain_salvage_key_15") + 1]]
CHAIN_SCRAP_POKED_CASES[[c["name"] for c in CHAIN_SCRAP_POKED_CASES].index("chain_salvage_poked")] = {
    "name": "chain_scrap_poked", "keys": SALVAGE_POKES + " char:1:09=02 idle:1", "match": {}, "compare": False}
CHAIN_SCRAP_POKED_CASES += [
    {"name": "chain_scrap_over", "keys": "enter", "match": SCREEN},
    {"name": "chain_scrap_repair", "keys": "enter", "match": SCREEN},
    {"name": "chain_scrap", "keys": "enter", "match": SCREEN},
    {"name": "chain_scrap_question", "keys": "enter", "match": SCREEN},
    {"name": "chain_scrap_salvage", "keys": "enter", "match": SCREEN},
    {"name": "chain_scrap_none", "keys": "enter", "match": SCREEN},
    {"name": "chain_scrap_back", "keys": "enter", "match": SCREEN},
]

# Poked: chain_salvage_poked's fight up to the hand-back, then the orders
# for Rex on foot (figure 5): the person menu (DG:3abc) and its orders.
# Only the tail is compared.
CHAIN_ORDERS_POKED_CASES = [dict(case, compare=False) for case in
                            CHAIN_SALVAGE_POKED_CASES[:[c["name"] for c in CHAIN_SALVAGE_POKED_CASES].index("chain_salvage_player") + 1]] + [
    {"name": "chain_orders_rex", "keys": "up*2 enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_weapon", "keys": "up*3 enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_weapon_2", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_weapon_3", "keys": "enter", "match": SCREEN, "fight": True},
    # Scan Unit: Friends (Jason's Chameleon, then Rex's condition, weapon
    # and armour), Done; Enemies ("humans only" while the Wasp stands, a
    # key, then the enemy person), Done.
    {"name": "chain_orders_scan", "keys": "down*2 enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_friends", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_friends_rex", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_friends_back", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_friends_done", "keys": "down*2 enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_scan_2", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_enemies", "keys": "down enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_enemy_human", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_enemies_done", "keys": "down enter", "match": SCREEN, "fight": True},
    # Clear Moves, Computer (the plan shown again), Move two cells left and
    # accept, then Begin Fight and two rounds.
    {"name": "chain_orders_clear", "keys": "up*3 enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_computer", "keys": "down*2 enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_move", "keys": "up*3 enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_move_left", "keys": "left*2", "match": SCREEN, "fight": True},
    {"name": "chain_orders_moved", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_begin", "keys": "down*7 enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_round_2", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_orders_round_3", "keys": "enter", "match": SCREEN, "fight": True},
]

# The unpoked chain on from the ruined town (docs/scripts.md, "After the
# raid"): branched at chain_raid_key_57 so chain_death and the poked
# sequences keep their prefix. With no idle there (N = 0 in the sweep of
# 0-255 idle passes) the walk out is the chain's own, so those cases are
# shared. Jason, alone on foot, flees every encounter in reach (2.7: three
# in four escape) on the way to map 2 (area 0x8a). Built a segment at a time.
CHAIN_TOWN_CASES = [dict(case, compare=False) for case in
                    CHAIN_CASES[:[c["name"] for c in CHAIN_CASES].index("chain_out_area") + 1]] + [
    # Segment 1: chain_death's encounter (four soldiers in reach), declined,
    # the computer not fighting, no messages, no graphics, then Flee from the
    # orders menu: eluded, a key, and the party back where it was.
    {"name": "chain_town_enc", "keys": "home", "match": SCREEN},
    {"name": "chain_town_enc_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_computer", "keys": "n", "match": SCREEN},
    {"name": "chain_town_messages", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_graphics", "keys": "n", "match": SCREEN},
    {"name": "chain_town_menu", "keys": "up enter", "match": SCREEN, "fight": True},
    {"name": "chain_town_orders", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_town_flee", "keys": "up enter", "match": SCREEN, "fight": True},
    {"name": "chain_town_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_back_2", "keys": "enter", "match": SCREEN},
    # Segment 2: 16 steps north-west (up-left and up in turn) to a group of
    # five in reach; declining it evades them this time (2.12): the escape,
    # its key and the return.
    {"name": "chain_town_walk_2", "keys": "home up " * 7 + "home up", "match": SCREEN},
    {"name": "chain_town_2_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_2_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_2_back", "keys": "enter", "match": SCREEN},
    # Segment 3: 64 steps west and north-west (left and up-left in turn),
    # with no encounter in reach; in halves.
    {"name": "chain_town_walk_3", "keys": "left home " * 15 + "left home", "match": SCREEN},
    {"name": "chain_town_walk_3b", "keys": "left home " * 15 + "left home", "match": SCREEN},
    # Segments 4 and 5: up-left to a group in reach (five, then three),
    # declined and evaded each time.
    {"name": "chain_town_walk_4", "keys": "home*9", "match": SCREEN},
    {"name": "chain_town_4_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_4_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_4_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_walk_5", "keys": "home*27", "match": SCREEN},
    {"name": "chain_town_5_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_5_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_5_back", "keys": "enter", "match": SCREEN},
    # Segment 6: 51 steps up-left into area 0xaa, where map 4 (area 0xb9)
    # comes into the grids (docs/wilderness.md, the map loads).
    {"name": "chain_town_walk_6", "keys": "home*26", "match": SCREEN},
    {"name": "chain_town_walk_6b", "keys": "home*25", "match": SCREEN},
    # Segments 7-9: on up-left, then north, to groups of six, two and four
    # in reach, each declined and evaded.
    {"name": "chain_town_walk_7", "keys": "home*30", "match": SCREEN},
    {"name": "chain_town_walk_7b", "keys": "home*31", "match": SCREEN},
    {"name": "chain_town_7_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_7_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_7_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_walk_8", "keys": "up home " * 4 + "up", "match": SCREEN},
    {"name": "chain_town_8_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_8_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_8_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_walk_9", "keys": "up*28", "match": SCREEN},
    {"name": "chain_town_9_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_9_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_9_back", "keys": "enter", "match": SCREEN},
    # Segment 10: 12 steps north, until map 2 (area 0x8a) is loaded into
    # the grids.
    {"name": "chain_town_walk_10", "keys": "up*12", "match": SCREEN},
    # Segments 11-14: map 2 is walled on its south and east sides; on foot
    # (tiles from 0x55 block) its only way in is the west side at rows
    # 27-31, which leads to one door, entrance 10 (the 'Mech Garage). North
    # to the wall, west along it and round (the planner's walks, some steps
    # blocked), then north up the west side to the door: one group evaded,
    # one fled, one evaded on the way. Ends at the door's question.
    {"name": "chain_town_walk_11", "keys": "up*32", "match": SCREEN},
    {"name": "chain_town_walk_11b", "keys": "up*32", "match": SCREEN},
    {"name": "chain_town_walk_11c", "keys": "up*17", "match": SCREEN},
    {"name": "chain_town_11_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_11_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_11_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_walk_12", "keys": "up*32", "match": SCREEN},
    {"name": "chain_town_walk_12b", "keys": "up*8 home left home left home left home left home left home left home left home left home left home left home left home left", "match": SCREEN},
    {"name": "chain_town_walk_12c", "keys": "home left home left home left home left home left home left home left home left home left home left home left home left home left home left home left home left", "match": SCREEN},
    {"name": "chain_town_walk_12d", "keys": "home left home left home left home left home left home left home left home left home left home left home left home left home left home left home left home left", "match": SCREEN},
    {"name": "chain_town_walk_12e", "keys": "home left home left home left home left home left home left home left home left home left home left home left home left home left home left home left home left", "match": SCREEN},
    {"name": "chain_town_walk_12f", "keys": "home left home left home left home left right up right up right up right up right up right up right up right up right up right up right up right up", "match": SCREEN},
    {"name": "chain_town_walk_12g", "keys": "right up right up right up right up right up right up right up right up right up right up right up right up right up right up right up right up", "match": SCREEN},
    {"name": "chain_town_walk_12h", "keys": "right up right up right up right up left*4 up*11", "match": SCREEN},
    {"name": "chain_town_12_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_12_computer", "keys": "n", "match": SCREEN},
    {"name": "chain_town_12_messages", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_12_graphics", "keys": "n", "match": SCREEN},
    {"name": "chain_town_12_menu", "keys": "up enter", "match": SCREEN, "fight": True},
    {"name": "chain_town_12_orders", "keys": "enter", "match": SCREEN, "fight": True},
    {"name": "chain_town_12_flee", "keys": "up enter", "match": SCREEN, "fight": True},
    {"name": "chain_town_12_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_12_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_12_back_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_walk_13", "keys": "up*2", "match": SCREEN},
    {"name": "chain_town_13_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_13_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_13_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_walk_14", "keys": "up*32", "match": SCREEN},
    {"name": "chain_town_walk_14b", "keys": "up right*8", "match": SCREEN},
    # The garage (GARAGE.BLD) on foot: special 11 finds no 'Mech records in
    # use, so flag 18 is set and the party is put inside, at the rear exit.
    {"name": "chain_town_garage", "keys": "y", "match": SCREEN},
    {"name": "chain_town_garage_key", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_garage_out", "keys": "up", "match": SCREEN},
    {"name": "chain_town_garage_out_2", "keys": "up", "match": SCREEN},
    # Time in town before setting out: outdoors only steps draw, so the walk
    # to map 3 meets a group Jason can neither evade nor flee (twice) at a
    # fixed draw; 1280 or more idle passes here use that draw up in town
    # (swept 0-2500; docs/reference.md). 1300 for a margin.
    {"name": "chain_town_wait", "keys": "idle:1300", "match": SCREEN},
    # Out by the garage's front door (entrance 0) towards map 3's jail, the
    # recruit open to a party of one (docs/scripts.md): with flag 18 set,
    # special 1d finds no 'Mech held, so flag 18 is cleared and the party is
    # put outside the front door.
    {"name": "chain_town_to_garage", "keys": "down down left left left", "match": SCREEN},
    {"name": "chain_town_garage_door", "keys": "left", "match": SCREEN},
    {"name": "chain_town_garage_2", "keys": "y", "match": SCREEN},
    {"name": "chain_town_garage_2_key", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_outside", "keys": "left", "match": SCREEN},
    # Segments 15-17: north into the row of map 3's area, then east along
    # it towards the jail; two groups evaded.
    {"name": "chain_town_walk_15", "keys": "up*32", "match": SCREEN},
    {"name": "chain_town_walk_15b", "keys": "up*32", "match": SCREEN},
    {"name": "chain_town_walk_15c", "keys": "up*32", "match": SCREEN},
    {"name": "chain_town_walk_15d", "keys": "up", "match": SCREEN},
    {"name": "chain_town_15_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_15_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_15_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_walk_16", "keys": "pgup*32", "match": SCREEN},
    {"name": "chain_town_walk_16b", "keys": "pgup*32", "match": SCREEN},
    {"name": "chain_town_walk_16c", "keys": "right*32", "match": SCREEN},
    {"name": "chain_town_walk_16d", "keys": "right*32", "match": SCREEN},
    {"name": "chain_town_walk_16e", "keys": "right*32", "match": SCREEN},
    {"name": "chain_town_walk_16f", "keys": "right*14", "match": SCREEN},
    {"name": "chain_town_16_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_16_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_16_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_town_walk_17", "keys": "right*32", "match": SCREEN},
    {"name": "chain_town_walk_17b", "keys": "right*32", "match": SCREEN},
    # Segment 18: east into map 3. Most step patterns meet three soldiers
    # in reach 37 steps in (neither evaded nor fled); right, right, up-right
    # is in among the buildings by then and comes to the Armor Shop's door
    # instead. No there, then inside the town to the jail's door (entrance 6).
    {"name": "chain_town_walk_18", "keys": "right*32", "match": SCREEN},
    {"name": "chain_town_walk_18b", "keys": "right*32", "match": SCREEN},
    {"name": "chain_town_walk_18c", "keys": "right*32", "match": SCREEN},
    {"name": "chain_town_walk_18d", "keys": "right*32", "match": SCREEN},
    {"name": "chain_town_walk_18e", "keys": "right*2 pgup right*2 pgup right*2 pgup right*2 pgup right*2 pgup right*2 pgup right*2 pgup right*2 pgup right*2 pgup right*2 pgup right*2", "match": SCREEN},
    {"name": "chain_town_walk_18f", "keys": "pgup right*2 pgup right*2 pgup right*2 pgup right*2 pgup right*2 pgup right*2 pgup right*2 pgup", "match": SCREEN},
    {"name": "chain_town_armor_no", "keys": "n", "match": SCREEN},
    {"name": "chain_town_to_jail", "keys": "left*2 end*8 down*5 end*7", "match": SCREEN},
    {"name": "chain_town_jail_door", "keys": "end", "match": SCREEN},
    {"name": "chain_town_jail", "keys": "y", "match": SCREEN},
]

# From chain_town_garage_out_2 (inside map 2, flag 18 set): the first
# companion (docs/scripts.md, "The first companion"). Three groups evaded on
# the way to CLOTHES, where Jason buys clothes (flag 1e); PARTY's first visit
# sets flag 1c and the timer, flag 1d = ff. The timer drops by one a game
# tick, one per 11 idle passes: in chr it reaches 0 after 2792 passes from
# chain_party_party_out (it does not depend on the random numbers); 2800 for
# a margin. The ticks' encounter checks fire too (map 2 counts as outdoors):
# every group up to 3000 passes is out of reach. Then off the door and back
# on, and PARTY's scene up to special 1e (not rebuilt yet: it makes the
# companion, then a fight of kind -1).
CHAIN_PARTY_CASES = [dict(case, compare=False) for case in
                     CHAIN_TOWN_CASES[:[c["name"] for c in CHAIN_TOWN_CASES].index("chain_town_garage_out_2") + 1]] + [
    {"name": "chain_party_walk", "keys": "down pgdn*4 right*52 pgdn*8 right*2", "match": SCREEN},
    {"name": "chain_party_1_no", "keys": "n", "match": SCREEN},
    {"name": "chain_party_1_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_1_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_walk_2", "keys": "right", "match": SCREEN},
    {"name": "chain_party_2_no", "keys": "n", "match": SCREEN},
    {"name": "chain_party_2_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_2_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_walk_3", "keys": "right", "match": SCREEN},
    {"name": "chain_party_3_no", "keys": "n", "match": SCREEN},
    {"name": "chain_party_3_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_3_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_clothes_door", "keys": "right", "match": SCREEN},
    {"name": "chain_party_clothes", "keys": "y", "match": SCREEN},
    {"name": "chain_party_clothes_buy", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_clothes_out", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_to_party", "keys": "down pgdn*3 down*6 pgdn*6 down*4 pgdn*8 right*7", "match": SCREEN},
    {"name": "chain_party_party", "keys": "y", "match": SCREEN},
    {"name": "chain_party_party_key", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_party_key_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_party_out", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_wait", "keys": "idle:2800", "match": SCREEN},
    {"name": "chain_party_off", "keys": "left", "match": SCREEN},
    {"name": "chain_party_door_2", "keys": "right", "match": SCREEN},
    {"name": "chain_party_party_2", "keys": "y", "match": SCREEN},
    {"name": "chain_party_scene_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_5", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_6", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_7", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_8", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_9", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_10", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_11", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_12", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_scene_13", "keys": "enter", "match": SCREEN},
    # Special 1e: the companion (record 1), the soldiers, then the fight
    # (kind -1) at its first question (docs/combat.md 1.6).
    {"name": "chain_party_member", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_member_2", "keys": "enter", "match": SCREEN},
    # Messages as they were, no graphics, then Flee from the first orders
    # menu: kind -1 may flee (combat.md 1.6), and this draw escapes. Jason
    # and the companion are walked back to the alley's entry position.
    {"name": "chain_party_messages", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_graphics", "keys": "n", "match": SCREEN},
    {"name": "chain_party_flee", "keys": "up enter", "match": SCREEN, "fight": True},
    {"name": "chain_party_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_back", "keys": "enter", "match": SCREEN},
    # West to REPAIR (entrance 3; the door is entered from its own row,
    # one below the entrance's), whose first recruit needs a member and
    # flag 18 (docs/scripts.md): new_member 5. The recruit's wait draws a
    # number for each poll, then its key is flushed and another read. It
    # starts right after the script's blocking read, with no flush between:
    # the feeder's key comes on the 4th poll, so 3 draws (arm:4).
    {"name": "chain_party_to_repair", "keys": "left*19 home*7 left*8 home*10 left*4", "match": SCREEN},
    {"name": "chain_party_repair_door", "keys": "left*4 home*2 left*28 home*6 up left pgup up right home right*2", "match": SCREEN},
    {"name": "chain_party_repair", "keys": "y", "match": SCREEN},
    {"name": "chain_party_repair_tech", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_recruit", "keys": "arm:4 enter", "match": SCREEN},
    {"name": "chain_party_recruit_key", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_recruit_key_2", "keys": "enter", "match": SCREEN},
    # Not the apprentice course (specials 0f and 10, not rebuilt): leave.
    {"name": "chain_party_repair_leave", "keys": "up", "match": SCREEN},
    {"name": "chain_party_repair_out", "keys": "enter", "match": SCREEN},
    # To the garage (entrance 0): with the Commando held, leaving costs 150
    # and the party has 135 (docs/scripts.md, the garage fee).
    {"name": "chain_party_to_garage", "keys": "right pgdn right*4 pgup up right up pgup up*7 home*10", "match": SCREEN},
    {"name": "chain_party_garage", "keys": "y", "match": SCREEN},
    {"name": "chain_party_garage_fee", "keys": "y", "match": SCREEN},
    {"name": "chain_party_garage_out", "keys": "enter", "match": SCREEN},
    # Money for the fee: wait at the garage door (flag 24 is 7f here: an
    # encounter can come one tick in 128). The planner's bounded search
    # (docs/reference.md) evades five groups in reach and lets the computer
    # fight the sixth, two soldiers: all three live, and the loot leaves 167.
    # Two weapons are found: the first to Jason (his cudgel, weapon 0, ends
    # the offer), the second to Edward (Jason again would hand the first back).
    {"name": "chain_party_wait_1", "keys": "idle:4970", "match": SCREEN},
    {"name": "chain_party_evade_1", "keys": "n", "match": SCREEN},
    {"name": "chain_party_eluded_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_back_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_wait_2", "keys": "idle:1989", "match": SCREEN},
    {"name": "chain_party_evade_2", "keys": "n", "match": SCREEN},
    {"name": "chain_party_eluded_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_back_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_wait_3", "keys": "idle:14188", "match": SCREEN},
    {"name": "chain_party_evade_3", "keys": "n", "match": SCREEN},
    {"name": "chain_party_eluded_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_back_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_wait_4", "keys": "idle:9", "match": SCREEN},
    {"name": "chain_party_evade_4", "keys": "n", "match": SCREEN},
    {"name": "chain_party_eluded_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_back_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_wait_5", "keys": "idle:6356", "match": SCREEN},
    {"name": "chain_party_evade_5", "keys": "n", "match": SCREEN},
    {"name": "chain_party_eluded_5", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_back_5", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_wait_fight", "keys": "idle:6840", "match": SCREEN},
    {"name": "chain_party_fight", "keys": "y y enter arm:1 n", "match": {}, "fight": True},
]  + [{"name": "chain_party_round_%d" % n, "keys": "idle:1", "match": {}, "fight": True} for n in range(1, 11)] + [
    {"name": "chain_party_won", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_pockets", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_offer_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_offer_2", "keys": "down down enter", "match": SCREEN},
    {"name": "chain_party_healing", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_return", "keys": "enter", "match": SCREEN},
    # The fee paid: flag 18 clear, the party outside at 0a00,8039, then
    # special 2e gives the 'Mechs back and the crew screen (docs/scripts.md).
    # Rex already pilots the Commando, so Done (the last item) ends it.
    {"name": "chain_party_fee_door", "keys": "down up", "match": SCREEN},
    {"name": "chain_party_fee", "keys": "y", "match": SCREEN},
    {"name": "chain_party_fee_pay", "keys": "y", "match": SCREEN},
    {"name": "chain_party_crew", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_crew_bar", "keys": "up", "match": SCREEN},
    {"name": "chain_party_crew_done", "keys": "enter", "match": SCREEN},
    # With the Commando, out to map 3's Hospital (entrance 1, 0d11,700b): the
    # on-foot route's first step north is blocked for the 'Mech's figure, so
    # the planner's search (docs/reference.md) goes by that route's
    # positions, evading four groups, and ends at the Hospital's question.
    {"name": "chain_party_map3_walk_1", "keys": "home left home*10", "match": SCREEN},
    {"name": "chain_party_map3_no_1", "keys": "n", "match": SCREEN},
    {"name": "chain_party_map3_eluded_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_map3_back_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_map3_walk_2", "keys": "home*38 pgup*109 pgdn*45", "match": SCREEN},
    {"name": "chain_party_map3_no_2", "keys": "n", "match": SCREEN},
    {"name": "chain_party_map3_eluded_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_map3_back_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_map3_walk_3", "keys": "pgdn*10 pgup*55 pgdn*96 pgup*14", "match": SCREEN},
    {"name": "chain_party_map3_no_3", "keys": "n", "match": SCREEN},
    {"name": "chain_party_map3_eluded_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_map3_back_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_map3_walk_4", "keys": "pgup*27", "match": SCREEN},
    {"name": "chain_party_map3_no_4", "keys": "n", "match": SCREEN},
    {"name": "chain_party_map3_eluded_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_map3_back_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_map3_walk_5", "keys": "pgup*55 pgdn*11 pgup*21 pgdn pgup*2 right*5 pgup*2 home", "match": SCREEN},
    # The Hospital: Search medical records (the third item), the nurse and the
    # doctor, then new_member 6 (docs/scripts.md): its wait follows the
    # scene's blocking read, so arm:4 as at REPAIR. Then leave (the last item).
    {"name": "chain_party_hospital", "keys": "y", "match": SCREEN},
    {"name": "chain_party_hospital_records", "keys": "down down", "match": SCREEN},
    {"name": "chain_party_hospital_nurse", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_hospital_doctor", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_hospital_recruit", "keys": "arm:4 enter", "match": SCREEN},
    {"name": "chain_party_hospital_recruit_key", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_hospital_recruit_key_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_hospital_leave", "keys": "up", "match": SCREEN},
    {"name": "chain_party_hospital_out", "keys": "enter", "match": SCREEN},
    # On to the jail (entrance 6): ask about Rex's friend (the jailkeeper's menu,
    # item 07dc), the scenes, new_member 4 (arm:4 after the scene's read), and
    # No at the impounded 'Mech's question: Yes runs special 0x28, mode 9.
    {"name": "chain_party_to_jail", "keys": "down*13 end*7", "match": SCREEN},
    {"name": "chain_party_jail", "keys": "y", "match": SCREEN},
    {"name": "chain_party_jail_ask", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_keeper", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_cubicle", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_rick", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_rick_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_recruit", "keys": "arm:4 enter", "match": SCREEN},
    {"name": "chain_party_jail_recruit_key", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_holocard", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_keys", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_barrier", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_alarm", "keys": "enter", "match": SCREEN},
    # Yes at the impounded 'Mech's question (the only chance: docs/scripts.md,
    # "JAIL after its recruit"): the jail break, mode 9 (docs/combat.md 1.7).
    # Jason alone walks to the bays on row 702d before the first fight (the
    # 81st tick): the first two won't start, the third does. The escape, then
    # the crew screen with everyone on foot: Jason pilots the STINGER, Rex the
    # COMMANDO, the rest on foot.
    {"name": "chain_party_jail_yes", "keys": "y", "match": SCREEN},
    {"name": "chain_party_jail_gunfire", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_break", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_walk_1", "keys": "pgdn*4", "match": SCREEN},
    {"name": "chain_party_jail_walk_2", "keys": "down*5", "match": SCREEN},
    {"name": "chain_party_jail_bay_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_bay_1_key", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_walk_3", "keys": "right*4", "match": SCREEN},
    {"name": "chain_party_jail_bay_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_bay_2_key", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_walk_4", "keys": "right*4", "match": SCREEN},
    {"name": "chain_party_jail_bay_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_escape", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_jail_crew", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_crew_jason", "keys": "up up up", "match": SCREEN},
    {"name": "chain_party_crew_jason_seat", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_crew_jason_stinger", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_crew_rex", "keys": "down", "match": SCREEN},
    {"name": "chain_party_crew_rex_seat", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_crew_rex_commando", "keys": "up", "match": SCREEN},
    {"name": "chain_party_crew_rex_pick", "keys": "enter", "match": SCREEN},
    {"name": "chain_party_crew_to_done", "keys": "down*4", "match": SCREEN},
    {"name": "chain_party_crew_done_2", "keys": "enter", "match": SCREEN},
]

# The jail's other answer, its own sequence: No at the impounded 'Mech's
# question gives the 'Mech up for good (docs/scripts.md, "JAIL after its
# recruit"); the script ends and the party is outside.
CHAIN_JAIL_NO_CASES = [dict(case, compare=False) for case in
                       CHAIN_PARTY_CASES[:[c["name"] for c in CHAIN_PARTY_CASES].index("chain_party_jail_alarm") + 1]] + [
    {"name": "chain_jail_no", "keys": "n", "match": SCREEN},
    {"name": "chain_jail_no_out", "keys": "enter", "match": SCREEN},
]

# The jail break's first fight, its own sequence: from the start of mode 9,
# 80 steps off the bays' row (each key is a tick), then the 81st brings a
# fight of kind 3, eight people against Jason alone (docs/combat.md 1.7).
# The computer fights. On the original Jason falls in this first round
# (DG:014a = 0, his record emptied), as in chr; then one pass, the
# townspeople back and walking.
CHAIN_JAIL_FIGHT_CASES = [dict(case, compare=False) for case in
                          CHAIN_PARTY_CASES[:[c["name"] for c in CHAIN_PARTY_CASES].index("chain_party_jail_break") + 1]] + [
    {"name": "chain_jail_fight_steps", "keys": " ".join(["up down"] * 40), "match": SCREEN},
    {"name": "chain_jail_fight", "keys": "up", "match": SCREEN},
    {"name": "chain_jail_fight_round_1", "keys": "y enter arm:1 n", "match": {}, "fight": True},
    {"name": "chain_jail_fight_round_2", "keys": "idle:1", "match": SCREEN, "fight": True},
]

# The main chain from the jail on (chain_party is its prefix, with Rick on
# foot): the double agent found out, its own sequence: on the jail break's crew
# screen Rick is seated as the COMMANDO's rider, which shows his tell and sets
# F:374a (docs/scripts.md). The first random encounter after it (one person,
# 119 passes out) is fought by the computer; after round 1 he acts from the
# rider's seat (the box; docs/combat.md 2.9). Round 1 without arm:1, so the
# box's key is not fed to 273d:086a's flush; the box's key with arm:2, so both
# programs take the next key at the same poll (docs/reference.md).
CHAIN_AGENT_CASES = [dict(case, compare=False) for case in
                     CHAIN_PARTY_CASES[:[c["name"] for c in CHAIN_PARTY_CASES].index("chain_party_crew_rex_pick") + 1]] + [
    {"name": "chain_agent_rick", "keys": "down*3", "match": SCREEN},
    {"name": "chain_agent_vehicle", "keys": "enter", "match": SCREEN},
    {"name": "chain_agent_commando", "keys": "enter", "match": SCREEN},
    {"name": "chain_agent_tell", "keys": "n", "match": SCREEN},
    {"name": "chain_agent_tell_key", "keys": "enter", "match": SCREEN},
    {"name": "chain_agent_done", "keys": "down enter", "match": SCREEN},
    {"name": "chain_agent_encounter", "keys": "idle:119", "match": SCREEN},
    {"name": "chain_agent_fight", "keys": "y y enter n", "match": {}, "fight": True},
    {"name": "chain_agent_acted", "keys": "arm:2 enter", "match": {}, "fight": True},
] + [{"name": "chain_agent_key_%d" % n, "keys": "enter", "match": {}, "fight": True} for n in range(2, 6)] + [
    # Nobody can reach the last soldier past the buildings, so the party
    # flees from the orders menu: escaped, and back in town with Rick gone
    # (flag 27 = 0, flag 24 = 0x7f, his record empty). The same soldier is
    # in reach again straight after.
    {"name": "chain_agent_flee", "keys": "up enter", "match": SCREEN, "fight": True},
    {"name": "chain_agent_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_agent_back", "keys": "enter", "match": SCREEN},
    # Then map 3's Lounge (entrance 4), the one reopened place that can set
    # a story flag (2f, after Talk's let-out: docs/scripts.md). One step
    # east is map 3; the 'Mechs ask at the Weapons Shop three times on the
    # way (No each time). No route passes the jail's doors (5-7): any visit
    # there now ends the game (flag 2a).
    {"name": "chain_lounge_walk", "keys": "right*25 pgup pgdn right*5", "match": SCREEN},
    {"name": "chain_lounge_weapon_no", "keys": "n", "match": SCREEN},
    {"name": "chain_lounge_walk_2", "keys": "right", "match": SCREEN},
    {"name": "chain_lounge_weapon_no_2", "keys": "n", "match": SCREEN},
    {"name": "chain_lounge_walk_3", "keys": "right", "match": SCREEN},
    {"name": "chain_lounge_weapon_no_3", "keys": "n", "match": SCREEN},
    {"name": "chain_lounge_walk_4", "keys": "right*4 pgdn*5", "match": SCREEN},
    {"name": "chain_lounge_door", "keys": "right right", "match": SCREEN},
    {"name": "chain_lounge_in", "keys": "enter", "match": SCREEN},
    {"name": "chain_lounge_talk", "keys": "enter", "match": SCREEN},
    {"name": "chain_lounge_talked", "keys": "enter", "match": SCREEN},
    {"name": "chain_lounge_leave", "keys": "down enter", "match": SCREEN},
    # Then south to the ruined home town (map 11, area 0xcc) and its only
    # door, BARRACK2 (entrance 0, 0c11,c009), which with a party runs
    # VIEWDISK after it (docs/scripts.md). Planned in segments of up to 64
    # steps with chr judging each; two groups in reach on the way, both
    # evaded. Since the raid area 0xcc is map 11 (27e8:00fc), whose north
    # wall is broken: map 1's would block the 'Mechs along the whole edge.
    {"name": "chain_barrack_walk_1", "keys": " ".join(["down end"] * 32), "match": SCREEN},
    {"name": "chain_barrack_walk_2", "keys": " ".join(["down end"] * 32), "match": SCREEN},
    {"name": "chain_barrack_walk_3", "keys": "end*64", "match": SCREEN},
    {"name": "chain_barrack_walk_4", "keys": "end*64", "match": SCREEN},
    {"name": "chain_barrack_walk_5", "keys": "down*64", "match": SCREEN},
    {"name": "chain_barrack_walk_6", "keys": "down*64", "match": SCREEN},
    {"name": "chain_barrack_walk_7", "keys": " ".join(["down pgdn"] * 15) + " down", "match": SCREEN},
    {"name": "chain_barrack_7_no", "keys": "n", "match": SCREEN},
    {"name": "chain_barrack_7_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_barrack_7_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_barrack_walk_8", "keys": "down*64", "match": SCREEN},
    {"name": "chain_barrack_walk_9", "keys": "down*64", "match": SCREEN},
    {"name": "chain_barrack_walk_10", "keys": "down*45", "match": SCREEN},
    {"name": "chain_barrack_10_no", "keys": "n", "match": SCREEN},
    {"name": "chain_barrack_10_eluded", "keys": "enter", "match": SCREEN},
    {"name": "chain_barrack_10_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_barrack_walk_11", "keys": "down*64", "match": SCREEN},
    {"name": "chain_barrack_door", "keys": "end left end left end", "match": SCREEN},
    # Yes: BARRACK2 with a party (flag 0e) is one page and a key, and after
    # it 17dc:0008 runs VIEWDISK (place 0x12): thirteen pages, flag 32 set
    # at its end, then area 0x34 becomes map 12 (27e8:0064). The step that
    # asked goes on as after No.
    {"name": "chain_barrack_in", "keys": "enter", "match": SCREEN},
] + [{"name": "chain_viewdisk_%d" % n, "keys": "enter", "match": SCREEN} for n in range(1, 14)] + [
    {"name": "chain_viewdisk_done", "keys": "enter", "match": SCREEN},
    # Money for the courses: 'Mech wrecks. Flag 24 is 0x7f here (combat.md 1.1), so an encounter
    # can start one tick in 128; idle passes and blocked steps draw nothing, so only the route
    # chooses the fight. This one, 57 steps north of BARRACK2's door, is 2 'Mechs and 4
    # soldiers, won by the computer with everyone alive; Edward (character 2) is the tech.
    {"name": "chain_money_walk_1", "keys": "right*6 pgup*4 up*47", "match": SCREEN},
    {"name": "chain_money_1_fight", "keys": "y y enter arm:1 n", "match": {}, "fight": True},
] + [{"name": "chain_money_1_key_%d" % n, "keys": "enter", "match": {}, "fight": True} for n in range(1, 5)] + [
    {"name": "chain_money_1_won", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_1_tech", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_1_scrap", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_1_pockets", "keys": "enter", "match": SCREEN},
    # The loot: the bar starts on Edward. Giving him the Rifle offers his Crossbow, and
    # giving that back offers the Rifle again, for as long as the default is taken.
    {"name": "chain_money_1_loot", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_1_swap", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_1_swap_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_1_rifle_russ", "keys": "down enter", "match": SCREEN},
    {"name": "chain_money_1_rifle_edward", "keys": "up enter", "match": SCREEN},
    {"name": "chain_money_1_drop", "keys": "down down enter", "match": SCREEN},
    {"name": "chain_money_1_drop_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_1_healed", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_1_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_1_end", "keys": "enter", "match": SCREEN},
    # Space: the command menu (1000:2c50), eight items with 'Mechs. Allocate men in 'Mechs
    # (1000:4d57) opens the crew screen: Edward and Russ, who can't pilot, become riders in
    # the Commando and the Stinger, out of the soldiers' reach.
    {"name": "chain_space_menu", "keys": "space", "match": SCREEN},
    {"name": "chain_space_crew", "keys": "down down enter", "match": SCREEN},
    {"name": "chain_crew_edward", "keys": "down down enter", "match": SCREEN},
    {"name": "chain_crew_edward_rides", "keys": "enter", "match": SCREEN},
    {"name": "chain_crew_russ", "keys": "down enter", "match": SCREEN},
    {"name": "chain_crew_russ_rides", "keys": "down enter", "match": SCREEN},
    {"name": "chain_crew_done", "keys": "down enter", "match": SCREEN},
    # Fight 2: 2 'Mechs and 7 soldiers. The soldiers run, both 'Mechs are wrecked, Edward
    # (riding) is still the tech.
    {"name": "chain_money_walk_2", "keys": "end*6 pgdn*183", "match": SCREEN},
    {"name": "chain_money_2_fight", "keys": "y y enter arm:1 n", "match": {}, "fight": True},
] + [{"name": "chain_money_2_key_%d" % n, "keys": "enter", "match": {}, "fight": True} for n in range(1, 5)] + [
    {"name": "chain_money_2_won", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_2_tech", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_2_scrap", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_2_healed", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_2_return", "keys": "enter", "match": SCREEN},
    {"name": "chain_money_2_back", "keys": "enter", "match": SCREEN},
    # Back to map 3 for the courses. Encounters with no 'Mechs are fought only while
    # Edward and Russ ride (no one steps out); every 'Mech encounter is evaded
    # (n, then a key for each of two pages).
    {"name": "chain_return_walk_1", "keys": "left*22 up*32", "match": SCREEN},
    {"name": "chain_return_met", "keys": "n", "match": SCREEN},
    {"name": "chain_return_computer", "keys": "enter", "match": SCREEN},
    {"name": "chain_return_fight", "keys": "y enter arm:2 n", "match": {}, "fight": True},
    {"name": "chain_return_won", "keys": "enter", "match": SCREEN},
    {"name": "chain_return_pockets", "keys": "enter", "match": SCREEN},
    {"name": "chain_return_aid", "keys": "enter", "match": SCREEN},
    {"name": "chain_return_healed", "keys": "enter", "match": SCREEN},
    {"name": "chain_return_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_return_walk_2", "keys": "up*6", "match": SCREEN},
    {"name": "chain_return_evade_1", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_return_walk_3", "keys": "up*119", "match": SCREEN},
    {"name": "chain_return_evade_2", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_return_walk_4", "keys": "up*5", "match": SCREEN},
    {"name": "chain_return_evade_3", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_return_walk_5", "keys": "up*62 " + " ".join(["up home"] * 15) + " up*14", "match": SCREEN},
    {"name": "chain_return_evade_4", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_return_walk_6", "keys": "up*115 pgup*31", "match": SCREEN},
    {"name": "chain_return_evade_5", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_return_walk_7", "keys": "pgup*43 " + " ".join(["up pgup"] * 3) + " up", "match": SCREEN},
    # Heal from the command menu (the bar starts on the item last chosen, Allocate).
    # It changes the draws, so the walk on misses a 'Mech fight.
    {"name": "chain_return_space", "keys": "space", "match": SCREEN},
    {"name": "chain_return_heal", "keys": "down down enter", "match": SCREEN},
    {"name": "chain_return_heal_done", "keys": "enter", "match": SCREEN},
    {"name": "chain_return_walk_8", "keys": " ".join(["pgup up"] * 56), "match": SCREEN},
    {"name": "chain_return_walk_9", "keys": "right*34 pgup*8", "match": SCREEN},
    # REPAIR's question comes on a step from its door cell, not onto it.
    {"name": "chain_return_repair_door", "keys": "right", "match": SCREEN},
    {"name": "chain_return_repair", "keys": "enter", "match": SCREEN},
    {"name": "chain_tech_who", "keys": "up*5 enter", "match": SCREEN},
    {"name": "chain_tech_fee", "keys": "down down enter", "match": SCREEN},
    {"name": "chain_tech_paid", "keys": "enter", "match": SCREEN},
    {"name": "chain_tech_menu", "keys": "enter", "match": SCREEN},
    {"name": "chain_tech_leave", "keys": "up enter", "match": SCREEN},
    {"name": "chain_hospital_walk_1", "keys": "left*8 home up left up home up home up*16 home*5 up left home up left "
     "home end up left home end up left home end up left home left*5 home*8 up left up home up home up*3 home*3",
     "match": SCREEN},
    {"name": "chain_hospital_evade", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hospital_walk_2", "keys": "home", "match": SCREEN},
    {"name": "chain_hospital_door", "keys": "down", "match": SCREEN},
    {"name": "chain_hospital", "keys": "enter", "match": SCREEN},
    {"name": "chain_seminar_fee", "keys": "up*5 enter", "match": SCREEN},
    {"name": "chain_seminar_who", "keys": "enter", "match": SCREEN},
    {"name": "chain_seminar_paid", "keys": "down*3 enter", "match": SCREEN},
    {"name": "chain_seminar_menu", "keys": "enter", "match": SCREEN},
    # To the HUT (area 0x34, map 12 since VIEWDISK): out of map 3 to the west, north along a
    # lake's east shore, west along the blocks' row 21. 'Mech encounters are evaded; the one
    # group with no 'Mechs that cannot be evaded is fought with everyone mounted. The HUT's
    # script wants Tech 4 (Edward) and Medical 4 (Russ), then sets flags 35 and 36.
    {"name": "chain_hut_leave_hospital", "keys": "up up enter", "match": SCREEN},
    {"name": "chain_hut_walk_1", "keys": "down*2 end left*80 home*130 left*140 home*3 pgup*8 up*36 pgup*16 up*21", "match": SCREEN},
    {"name": "chain_hut_evade_1", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_2", "keys": "up*8", "match": SCREEN},
    {"name": "chain_hut_evade_2", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_3", "keys": "up*3 pgup*16 up*70", "match": SCREEN},
    {"name": "chain_hut_evade_3", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_4", "keys": "up*170 home*37", "match": SCREEN},
    {"name": "chain_hut_evade_4", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_5", "keys": "home*8", "match": SCREEN},
    {"name": "chain_hut_evade_5", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_6", "keys": "home*42 left*138", "match": SCREEN},
    {"name": "chain_hut_evade_6", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_7", "keys": "left*90", "match": SCREEN},
    {"name": "chain_hut_evade_7", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_8", "keys": "left*69", "match": SCREEN},
    {"name": "chain_hut_evade_8", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_9", "keys": "left*45", "match": SCREEN},
    {"name": "chain_hut_met", "keys": "n", "match": SCREEN},
    {"name": "chain_hut_computer", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_fight", "keys": "y enter arm:2 n", "match": {}, "fight": True},
    {"name": "chain_hut_fight_key_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_fight_key_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_fight_key_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_fight_key_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_fight_key_5", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_fight_key_6", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_walk_10", "keys": "left*45", "match": SCREEN},
    {"name": "chain_hut_evade_9", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_11", "keys": "left*57", "match": SCREEN},
    {"name": "chain_hut_evade_10", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_12", "keys": "left*10", "match": SCREEN},
    {"name": "chain_hut_evade_11", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_13", "keys": "left*54", "match": SCREEN},
    {"name": "chain_hut_evade_12", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_14", "keys": "left*47", "match": SCREEN},
    {"name": "chain_hut_evade_13", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_15", "keys": "left*135", "match": SCREEN},
    {"name": "chain_hut_evade_14", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_hut_walk_16", "keys": "left*16 end*46 left up*6 pgdn down*4 pgdn end left end left*3", "match": SCREEN},
    {"name": "chain_hut_door", "keys": "left", "match": SCREEN},
    {"name": "chain_hut_in", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_5", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_6", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_7", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_8", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_9", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_10", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_11", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_12", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_13", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_14", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_15", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_16", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_17", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_18", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_19", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_20", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_21", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_22", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_23", "keys": "enter", "match": SCREEN},
    {"name": "chain_hut_page_24", "keys": "enter", "match": SCREEN},
]


# The main chain on from the HUT to ENTRANCE: one encounter evaded, then the
# forced 'Mech encounter (2 'Mechs, 3 people, at 0471,4059) that cannot be
# evaded, fought by the computer to the win and Edward's salvage (no party
# figure dies, so no enemy person's target is ff: docs/reference.md). Then
# on past one more encounter (map 13 loads on the way) to ENTRANCE's door
# and into the cache, as chain_cache_poked goes without its poke.
CHAIN_ENTRANCE_CASES = [dict(case, compare=False) for case in CHAIN_AGENT_CASES] + [
    {"name": "chain_entrance_out", "keys": "end down*2 end*2 down*2", "match": SCREEN},
    {"name": "chain_entrance_walk_1", "keys": "right pgdn down end*2 down*84", "match": SCREEN},
    {"name": "chain_entrance_evade_1", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_entrance_walk_2", "keys": "down*9 pgdn*105", "match": SCREEN},
    {"name": "chain_entrance_met", "keys": "n", "match": SCREEN},
    {"name": "chain_entrance_computer", "keys": "enter", "match": SCREEN},
    {"name": "chain_entrance_fight", "keys": "y enter arm:2 n", "match": {}, "fight": True},
] + [{"name": "chain_entrance_fight_key_%d" % n, "keys": "enter", "match": SCREEN} for n in range(1, 13)] + [
    {"name": "chain_entrance_walk_3", "keys": "pgdn*99", "match": SCREEN},
    {"name": "chain_entrance_evade_2", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_entrance_walk_4",
     "keys": "pgdn*36 down*16 pgdn*16 down*3 left*2 end down pgdn*3 down left up end up end down*2 pgdn", "match": SCREEN},
    {"name": "chain_entrance_door", "keys": "right", "match": SCREEN},
    {"name": "chain_entrance_enter", "keys": "y", "match": SCREEN},
    {"name": "chain_entrance_going_in", "keys": "enter", "match": SCREEN},
    {"name": "chain_entrance_instruct", "keys": "enter", "match": SCREEN},
    {"name": "chain_entrance_instruct_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_entrance_inside", "keys": "enter", "match": SCREEN},
]


# Poked: chain_entrance's forced fight with party 'Mech 1's armour and
# internal bytes set to 1 (our own test values) before it starts. The 'Mech
# falls, the enemy people aimed at it get target ff, and the run (combat.md
# 2.10) measures them from figure -1 (docs/sound.md). Each fight key compared.
# Key 1 answers the fled message, whose key read (273d:0259) has no flush after
# it: the original lets three of the round's polls go by before the fourth
# gets the next key, so arm:4 gives chr three passes (the feeder's own count).
# From key 6 the repair mends the SRM 6s from the count left on the stack
# (bug 23, kept: combat.md 2.15).
FLEE_POKES = " ".join("mech:1:%02x=01" % b for b in range(0x11, 0x24))
_FLEE_FROM = [case["name"] for case in CHAIN_ENTRANCE_CASES].index("chain_entrance_fight")
CHAIN_FLEE_CASES = [dict(case, compare=False) for case in CHAIN_ENTRANCE_CASES[:_FLEE_FROM]] + [
    {"name": "chain_flee_fight", "keys": FLEE_POKES + " y enter arm:2 n", "match": {}, "fight": True},
    {"name": "chain_flee_fight_key_1", "keys": "arm:4 enter", "match": SCREEN},
] + [{"name": "chain_flee_fight_key_%d" % n, "keys": "enter", "match": SCREEN} for n in range(2, 13)]


# Poked: from the HUT's door, the generator's state set to 0f1e2d (our own
# test value) in place of the walk's draws. From there every way to ENTRANCE
# meets a 'Mech encounter that cannot be evaded (docs/reference.md, open
# items); this side branch reaches ENTRANCE's door to build map 14 on, until
# the main chain finds a clean way. One encounter on the way, evaded.
CHAIN_CACHE_POKED_CASES = [dict(case, compare=False) for case in CHAIN_AGENT_CASES] + [
    {"name": "chain_cache_poked", "keys": "random:0f1e2d end down*2 end*2 down*2", "match": SCREEN},
    {"name": "chain_cache_evade", "keys": "n enter enter", "match": SCREEN},
    {"name": "chain_cache_walk_1", "keys": "right pgdn down end*2 down*93", "match": SCREEN},
    {"name": "chain_cache_walk_2", "keys": "pgdn*120", "match": SCREEN},
    {"name": "chain_cache_walk_3", "keys": "pgdn*120", "match": SCREEN},
    {"name": "chain_cache_walk_4", "keys": "down*16 pgdn*16 down*3 left*2 end down pgdn*3 down left up end up end down*2 pgdn",
     "match": SCREEN},
    # ENTRANCE's question comes on a step from its door cell.
    {"name": "chain_cache_door", "keys": "right", "match": SCREEN},
    {"name": "chain_cache_enter", "keys": "y", "match": SCREEN},
    # Leaving ENTRANCE with flag 36 set goes into the cache (1b5d:0004).
    {"name": "chain_cache_going_in", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_instruct", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_instruct_2", "keys": "enter", "match": SCREEN},
    # The step that asked at ENTRANCE's door goes on inside.
    {"name": "chain_cache_inside", "keys": "enter", "match": SCREEN},
    # Up, east and down to the corridor under door 0, then east below it.
    {"name": "chain_cache_to_door", "keys": "up up up up up up right right right right right right right right right right right right right right right right right right right right down down down down down right right right right right right right right right right right right right right right right right right right right right right", "match": SCREEN},
    # A door tile stops the step: without its three items it says which are
    # wrong and waits for a key (1000:1c12, 1b5d:0ab6).
    {"name": "chain_cache_door_wrong", "keys": "up", "match": SCREEN},
    {"name": "chain_cache_door_shut", "keys": "enter", "match": SCREEN},
    # Back west and up to the terminals of the first room.
    {"name": "chain_cache_to_terminal", "keys": "left left left left left left left left left left left left left left left left left left left left left left up up up up up left left left left up up up up", "match": SCREEN},
    # An item tile: the item asks to be taken (1b5d:03aa); yes puts it in its slot.
    {"name": "chain_cache_item_question", "keys": "up", "match": SCREEN},
    {"name": "chain_cache_item_taken", "keys": "enter", "match": SCREEN},
    # Saved in the cache (1000:35d3; without 'Mechs there is no crew item, and the menu starts on
    # the healing, the item last chosen, one above the save), two steps back down, and loaded
    # (1000:32b3, one above the save): the cache's own load, 1b5d:0004.
    {"name": "chain_cache_save_menu", "keys": "space down enter", "match": SCREEN},
    {"name": "chain_cache_saved", "keys": "enter", "match": SCREEN, "saved": 0},
    {"name": "chain_cache_save_walked", "keys": "down down", "match": SCREEN},
    {"name": "chain_cache_load_menu", "keys": "space up enter", "match": SCREEN},
    {"name": "chain_cache_loaded", "keys": "enter", "match": SCREEN},
    # The cache's load goes through INSTRUCT as the way in does; then the load's end.
    {"name": "chain_cache_load_instruct", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_load_instruct_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_load_inside", "keys": "enter", "match": SCREEN},
]


# Poked: chain_cache_poked to ENTRANCE's door, with doors 0-10 marked open
# (flags 43-4d, our own test values) in place of the walk through them with
# their items, about 7,800 steps. Going in shows the marked doors open
# (1b5d:0004), so the walks below reach the cache's text tiles
# (docs/cache.md).
_CACHE_DOOR = next(i for i, c in enumerate(CHAIN_CACHE_POKED_CASES) if c["name"] == "chain_cache_door")
CHAIN_CACHE_OPEN_CASES = [dict(case, compare=False) for case in CHAIN_CACHE_POKED_CASES[:_CACHE_DOOR]] + [
    {"name": "chain_cache_open", "keys": "flag:43=01 flag:44=01 flag:45=01 flag:46=01 flag:47=01 flag:48=01 flag:49=01 flag:4a=01 flag:4b=01 flag:4c=01 flag:4d=01 right", "match": SCREEN},
    {"name": "chain_cache_open_enter", "keys": "y", "match": SCREEN},
    {"name": "chain_cache_open_going_in", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_open_instruct", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_open_instruct_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_open_inside", "keys": "enter", "match": SCREEN},
    # Through doors 1 and 2 to the tiles at x 4e, y 0c-10 (1b5d:02d2).
    {"name": "chain_cache_to_hint", "keys": "up*6 right*10 down*2 right*2 down*3 right*62 up*10 right*34 up*6 right*8 up*68 left*21 up left up*7 right*4 up*14 left*19 up left up*3 left*6", "match": SCREEN},
    {"name": "chain_cache_hint", "keys": "left", "match": SCREEN},
    {"name": "chain_cache_hint_read", "keys": "enter", "match": SCREEN},
    # East to a screen tile (1b5d:02a8).
    {"name": "chain_cache_to_screen", "keys": "down*5 right*26 down*14 left*3 down left down*6 right*12 up*18 right*2 up*7 right*6", "match": SCREEN},
    {"name": "chain_cache_screen", "keys": "right", "match": SCREEN},
    {"name": "chain_cache_screen_read", "keys": "enter", "match": SCREEN},
    # Across the cache to tile f5 at x 0c01-0c04, y c054: the wall opens (1b5d:01e9).
    {"name": "chain_cache_to_wall", "keys": "left*3 down left down*17 left down left down*7 right*8 down*68 left*5 down left down*5 left*33 down left down*8 left*31 up left up*14 right*6 down*7 right*16 up*28 right*6 up*4 right*8 up*10 right*2 up*8 left*7 up left up*7 right*6 up*10 left*27 up left up*6 left*25 down left down*21 left*5 down left down*7 left*23 down left down*9", "match": SCREEN},
    {"name": "chain_cache_wall", "keys": "down", "match": SCREEN},
    {"name": "chain_cache_wall_read", "keys": "enter", "match": SCREEN},
    # To tile 28 at x 48, y 38: down to the second level (1b5d:055a).
    {"name": "chain_cache_to_down", "keys": "up*13 right*12 up*6 right*16 up*21 right*26 down*8 right*30 down*10 left*5 down left down*7 right*8 down*18 left*7 down left down*3 left*5 down left down*26 left*15 up left up*59 right*10", "match": SCREEN},
    {"name": "chain_cache_down", "keys": "right", "match": SCREEN},
    {"name": "chain_cache_down_read", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_down_read_2", "keys": "enter", "match": SCREEN},
    # A cell that is not one of the seven (1b5d:0913).
    {"name": "chain_cache_to_cell_wrong", "keys": "up right", "match": SCREEN},
    {"name": "chain_cache_cell_wrong", "keys": "right", "match": SCREEN},
    # The check: wrong, every cell off again (1b5d:0980).
    {"name": "chain_cache_to_check_wrong", "keys": "up*4 left*2", "match": SCREEN},
    {"name": "chain_cache_check_wrong", "keys": "left", "match": SCREEN},
    {"name": "chain_cache_check_wrong_read", "keys": "enter", "match": SCREEN},
    # The seven cells of the table at DG:241e.
    {"name": "chain_cache_to_cell_1", "keys": "up*25 right*24 down right*4 up right*4 down*3 right*4 up*2", "match": SCREEN},
    {"name": "chain_cache_cell_1", "keys": "up", "match": SCREEN},
    {"name": "chain_cache_to_cell_2", "keys": "down*8 left*2", "match": SCREEN},
    {"name": "chain_cache_cell_2", "keys": "left", "match": SCREEN},
    {"name": "chain_cache_to_cell_3", "keys": "down*4 left*10", "match": SCREEN},
    {"name": "chain_cache_cell_3", "keys": "left", "match": SCREEN},
    {"name": "chain_cache_to_cell_4", "keys": "down*2", "match": SCREEN},
    {"name": "chain_cache_cell_4", "keys": "left", "match": SCREEN},
    {"name": "chain_cache_to_cell_5", "keys": "up right*6 down*2 right*2 down*2 right*2 down*6 right*2 down*6 left down left*6 up left up*4", "match": SCREEN},
    {"name": "chain_cache_cell_5", "keys": "left", "match": SCREEN},
    {"name": "chain_cache_to_cell_6", "keys": "down*5 right*4 up*2 right*4 up*4 right*4", "match": SCREEN},
    {"name": "chain_cache_cell_6", "keys": "right", "match": SCREEN},
    {"name": "chain_cache_to_cell_7", "keys": "left*4", "match": SCREEN},
    {"name": "chain_cache_cell_7", "keys": "left", "match": SCREEN},
    # The check: solved, flag 3e.
    {"name": "chain_cache_to_check_solved", "keys": "down*5 left down left*6 up left up*3 left*9 up left up left*18", "match": SCREEN},
    {"name": "chain_cache_check_solved", "keys": "left", "match": SCREEN},
    {"name": "chain_cache_check_solved_read", "keys": "enter", "match": SCREEN},
    # Tile 94: back up (1b5d:079c).
    {"name": "chain_cache_to_way_up", "keys": "down*5", "match": SCREEN},
    {"name": "chain_cache_way_up", "keys": "left", "match": SCREEN},
    {"name": "chain_cache_way_up_read", "keys": "enter", "match": SCREEN},
    # Tile 28 again: the puzzle solved, it only says so.
    {"name": "chain_cache_down_again", "keys": "right", "match": SCREEN},
    {"name": "chain_cache_down_again_read", "keys": "enter", "match": SCREEN},
    # To the tiles above f5 at cells 1-2, 49-50: ENDMECH (1b5d:0288).
    {"name": "chain_cache_to_endmech", "keys": "down*3 left*7 down left down*7 left down left down*48 right*14 up*28 right*6 up*4 right*8 up*10 right*2 up*8 left*7 up left up*7 right*6 up*10 left*27 up left up*6 left*25 down left down*21 left*5 down left down*7 left*23 down left down*23", "match": SCREEN},
    {"name": "chain_cache_endmech", "keys": "down", "match": SCREEN},
    {"name": "chain_cache_endmech_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_endmech_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_endmech_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_endmech_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_endmech_5", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_endmech_6", "keys": "enter", "match": SCREEN},
    # To tile 28 at x 7c, y 04: FINDIT (1b5d:055a).
    {"name": "chain_cache_to_findit", "keys": "up*27 right*12 up*6 right*16 up*21 right*26 down*8 right*30 down*10 left*5 down left down*7 right*8 down*18 left*7 down left down*3 left*5 down left down*26 left*15 up left up*6 left*7 down left down*14 right*30 up*10 right*34 up*6 right*8 up*68 left*7 up left up*35 right*6", "match": SCREEN},
    {"name": "chain_cache_findit", "keys": "right", "match": SCREEN},
    {"name": "chain_cache_findit_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_findit_2", "keys": "enter", "match": SCREEN},
    # To tile 4d at x 02, y 0e with the hint, the puzzle and FINDIT behind
    # it: WINSCENE (1b5d:04ab), then the main loop's question (1000:049d).
    {"name": "chain_cache_to_way_out", "keys": "down*11 left*3 down left down*17 left down left down*6 left*39 up left up*12 left*7 down left down*4 left*43 up left up*7 left up left up*5 left*7 up left up left up left up*2 left*8", "match": SCREEN},
    {"name": "chain_cache_way_out", "keys": "left", "match": SCREEN},
    {"name": "chain_cache_winscene_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_winscene_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_winscene_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_winscene_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_winscene_5", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_winscene_6", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_winscene_7", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_winscene_8", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_winscene_9", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_winscene_10", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_winscene_11", "keys": "enter", "match": SCREEN},
    {"name": "chain_cache_won", "keys": "enter", "match": SCREEN},
    # Yes: a new game (1000:4dc7) and the main loop on map 1 again.
    {"name": "chain_new_game", "keys": "enter", "match": SCREEN},
    {"name": "chain_new_game_walk", "keys": "down down down left", "match": SCREEN},
    {"name": "chain_new_game_waited", "keys": "idle:40", "match": SCREEN},
]


# The main chain on from chain_entrance's first step inside the cache, unpoked, to the
# game's end (docs/cache.md, "The way through"): each door's three items taken from their
# terminals and the door opened, in the order the route plan found (chr --walks), walks over
# 200 steps split so no capture is far from the last; a save and load after the doors; the
# hint, a screen tile, the wall and the way down; then from the second level on as
# chain_cache_open goes, since the way down and the way up put the party at fixed places.
CHAIN_FINALE_CASES = [dict(case, compare=False) for case in CHAIN_ENTRANCE_CASES] + [
    {"name": "chain_finale_door_1_item_0", "keys": "home*2 pgup*5 right*2 pgup*3 right pgdn pgup", "match": SCREEN},
    {"name": "chain_finale_door_1_item_0_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_1_item_2", "keys": "right*4 pgdn*8 right*60 pgup", "match": SCREEN},
    {"name": "chain_finale_door_1_item_2_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_1_item_4", "keys": "right*5 pgup right pgdn right*5 pgup", "match": SCREEN},
    {"name": "chain_finale_door_1_item_4_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_1", "keys": "left*5 home left end left*9 home", "match": SCREEN},
    {"name": "chain_finale_door_2_item_6", "keys": "home*4 up pgup*5 right*23 pgup", "match": SCREEN},
    {"name": "chain_finale_door_2_item_6_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_2_item_1", "keys": "left*23 end*5 down*4 end left*51 home*7 up pgup", "match": SCREEN},
    {"name": "chain_finale_door_2_item_1_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_2_item_17_a", "keys": "down pgdn*7 right*51 pgup up*4 pgup*5 right*28 pgup*6 right*2 pgup up*39 home up pgup up*23 home*2 left*27", "match": SCREEN},
    {"name": "chain_finale_door_2_item_17_b", "keys": "left*17 home*4 up*8 home*2 left*5 end*6 left*3 home*6 left*6 home*4 up*17 pgup*2", "match": SCREEN},
    {"name": "chain_finale_door_2_item_17_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_2", "keys": "end down*17 pgdn*4 right*6 pgdn*6 right*3 pgup*6 right*5 pgdn*2 down*8 pgdn*4 right*44 pgdn*2 down*23 end down pgdn down*39 end left*2 end*6 left*17 home", "match": SCREEN},
    {"name": "chain_finale_door_0_item_10", "keys": "home*4 up*2 home*2 left*6 home*4 up pgup*8", "match": SCREEN},
    {"name": "chain_finale_door_0_item_10_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_0_item_13", "keys": "end*7 down pgdn*4 right*15 pgup up*5 home*4 up*2 home*2 up*6 pgup*3", "match": SCREEN},
    {"name": "chain_finale_door_0_item_13_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_0_item_14", "keys": "right*7 pgup*2 right*6 pgup up*4 pgup*2", "match": SCREEN},
    {"name": "chain_finale_door_0_item_14_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_0", "keys": "end down*4 end*4 down*3 pgdn*6 down*5 end left*8 end*6 left*2 end*5 down*2 end left*6 end*5 down*4 end left*27 home", "match": SCREEN},
    {"name": "chain_finale_door_6_item_3", "keys": "right*27 pgup*2 right pgdn*2 right*9 pgup right pgdn pgup", "match": SCREEN},
    {"name": "chain_finale_door_6_item_3_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_6_item_12", "keys": "home left end left*9 home*4 up pgup*5 right*6 pgup up*2 pgup*5 right*2 pgup*6 right*8 pgup up*5 home pgup*2", "match": SCREEN},
    {"name": "chain_finale_door_6_item_12_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_6_item_30_a", "keys": "end pgdn down*5 end left*8 end*6 left*2 end*4 down pgdn*3 right*17 pgup*6 right*2 pgup up*39 home up pgup up*23 home*2 left*44 home*4 up*8 home*2 left*5 end*6 left*3 home*3", "match": SCREEN},
    {"name": "chain_finale_door_6_item_30_b", "keys": "home*3 left*5 end*6 left*15 home*4 left end*4 left*4 end*7 down pgdn*2 down*7 end left home", "match": SCREEN},
    {"name": "chain_finale_door_6_item_30_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_6_a", "keys": "right pgup up*7 home*2 up pgup*7 right*4 pgup*4 right pgdn*4 right*15 pgup*6 right*5 pgdn*6 right*3 pgup*6 right*5 pgdn*2 down*8 pgdn*4 right*44 pgdn*2 down*23 end down pgdn down*36", "match": SCREEN},
    {"name": "chain_finale_door_6_b", "keys": "down*3 end left*2 end*6 left*17 home*2 left end*2 left*6 end*5 down*4 end left*27 home*4 up*7 pgup*5 right pgdn*2 down*2 pgdn*4 right*11 pgup up*22 pgup*9 right*4 pgup up*10", "match": SCREEN},
    {"name": "chain_finale_door_10_item_23", "keys": "right pgup up*5 home*2 left*4 home*4 up pgup*4", "match": SCREEN},
    {"name": "chain_finale_door_10_item_23_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_10_item_27", "keys": "right*4 pgup up*7 home*2 left*16 home*4 up pgup*2", "match": SCREEN},
    {"name": "chain_finale_door_10_item_27_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_10_item_15", "keys": "end down pgdn*4 right*16 pgdn*2 down*7 end left*2 end*4 down pgdn*3 right*4 pgdn*2 down*5 end*2 down*8 end left*4 end*9 down*22 end left*11 home*4 up*2 home up pgup up*4 pgup*5 right*2 pgup up*21 pgup", "match": SCREEN},
    {"name": "chain_finale_door_10_item_15_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_10_a", "keys": "down*21 end left*2 end*5 down*4 end left*2 end*5 down*7 pgdn*4 right*27 pgup up*4 pgup*5 right*6 pgup*2 right pgdn*2 right*17 pgup*6 right*2 pgup up*39 home up pgup up*23 home*2 left*7", "match": SCREEN},
    {"name": "chain_finale_door_10_b", "keys": "left*11 home", "match": SCREEN},
    {"name": "chain_finale_door_8_item_32", "keys": "home*2 left end*2 left*21 home*3 up pgup*4 right*4 pgup up*3 pgup", "match": SCREEN},
    {"name": "chain_finale_door_8_item_32_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_8_item_9_a", "keys": "down*3 end left*4 end*4 down pgdn*3 right*21 pgup*2 right pgdn*2 right*18 pgdn*2 down*23 end down pgdn down*39 end left*2 end*6 left*17 home*2 left end*2 left*6 end*5 down*4 end left*26", "match": SCREEN},
    {"name": "chain_finale_door_8_item_9_b", "keys": "left home*4 up*7 pgup*5 right pgdn*2 down*2 pgdn*4 right*11 pgup up*27 pgup", "match": SCREEN},
    {"name": "chain_finale_door_8_item_9_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_8_item_24", "keys": "right*5 pgup*4 right*4 pgup up*8 pgup*2 up*2 pgup*5 right*4 pgup up*5 home*4 up*4 pgup*3", "match": SCREEN},
    {"name": "chain_finale_door_8_item_24_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_8_a", "keys": "end*2 down*4 pgdn*4 down*5 end left*4 end*5 down*2 end*2 down*8 end left*4 end*9 down*22 end left*11 home*4 up*2 home*2 left end*5 down*7 pgdn*4 right*27 pgup up*4 pgup*5 right*6 pgup*2 right pgdn*2 right*17 pgup*6 right*2 pgup up*16", "match": SCREEN},
    {"name": "chain_finale_door_8_b", "keys": "up*23 home up pgup up*23 home*2 left*4 home", "match": SCREEN},
    {"name": "chain_finale_door_4_item_11", "keys": "right*4 pgdn*2 down*23 end down pgdn down*39 end left*2 end*6 left*17 home*3 up pgup*4 right*2 pgup up*5 home*3 pgup*4", "match": SCREEN},
    {"name": "chain_finale_door_4_item_11_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_4_item_5", "keys": "end*3 pgdn*3 down*5 end left*2 end*5 down*2 end left*6 end*5 down pgdn*4 right*9 pgup right pgdn right*23 pgup*3 up*2 pgup*2", "match": SCREEN},
    {"name": "chain_finale_door_4_item_5_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_4_item_28_a", "keys": "end down*2 end*3 left*23 home left end left*9 home*2 left end*2 left*27 home*4 up*7 pgup*5 right pgdn*2 down*2 pgdn*4 right*11 pgup up*22 pgup*9 right*4 pgup up*8 pgup*2 up*5 home*2 left*4 home*3 up pgup*4 right*2 pgup up*7 home*2 left*13", "match": SCREEN},
    {"name": "chain_finale_door_4_item_28_b", "keys": "left*3 home*2 left end*2 left*3 home*4 up*2 home*2 left*19 end*6 left*3 home*5", "match": SCREEN},
    {"name": "chain_finale_door_4_item_28_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_4", "keys": "pgdn*4 right*2 pgdn*2 down*13 end*5 down pgdn*3 right*4 pgdn*2 down*2", "match": SCREEN},
    {"name": "chain_finale_door_5_item_21", "keys": "up home*2 left*4 home*7 up pgup*3", "match": SCREEN},
    {"name": "chain_finale_door_5_item_21_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_5_item_19", "keys": "end*2 down pgdn*7 right*4 pgdn*2 down*5 end left*8 end*4 left*3 home*2 left*2 home", "match": SCREEN},
    {"name": "chain_finale_door_5_item_19_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_5_item_26_a", "keys": "right*2 pgdn*2 right*3 pgup*4 right*8 pgup up*5 home*2 left*4 home*3 up pgup*5 up*13 home up pgup*6 right*19 pgdn*2 down*2 pgdn*4 right*3 pgup*2 right pgdn*2 right*16 pgdn*2 down*7 end left*2 end*4 down pgdn*3 right*4 pgdn*2 down*5 end*2 down*8 end left*4 end*9 down*22 end left*10", "match": SCREEN},
    {"name": "chain_finale_door_5_item_26_b", "keys": "left home*4 up*2 home*2 left end*5 down*7 pgdn*4 right*27 pgup up*4 pgup*5 right*6 pgup*2 right pgdn*2 right*17 pgup*6 right*2 pgup up*39 home up pgup up*23 home up pgup up*7 pgup", "match": SCREEN},
    {"name": "chain_finale_door_5_item_26_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_5_a", "keys": "down*7 end down pgdn down*23 end down pgdn down*39 end left*2 end*6 left*17 home*2 left end*2 left*6 end*5 down*4 end left*27 home*4 up*7 pgup*5 right pgdn*2 down*2 pgdn*4 right*11 pgup up*14", "match": SCREEN},
    {"name": "chain_finale_door_5_b", "keys": "up*8 pgup*9 right*4 pgup up*8 pgup*2 up*5 home*2 left*4 home*3 up pgup*4 right*2 pgup up*7 home*2 left*16 home*2 left end*2 left*3 home*4 up*2 home*2 left*19 end*6 down pgdn down*13 end*5 down pgdn*3 right*7 pgup", "match": SCREEN},
    {"name": "chain_finale_door_3_item_25", "keys": "left*7 home*3 up pgup*5 up*13 home up pgup*6 right*19 pgdn*2 down*2 pgdn*4 right*3 pgup*2 right pgdn*2 right*16 pgdn*2 down*7 end left*2 end*4 down pgdn*3 right*15 pgup up*5 home*4 up pgup*5 right*4 pgup", "match": SCREEN},
    {"name": "chain_finale_door_3_item_25_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_3_item_16", "keys": "left*4 end*5 down pgdn*4 down*5 end left*4 end*5 down*2 end*2 down*8 end left*4 end*9 down*22 end left*11 home*4 up*2 home up pgup up*4 pgup*5 right*2 pgup up*16 pgup*6", "match": SCREEN},
    {"name": "chain_finale_door_3_item_16_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_3_item_18", "keys": "right*4 pgup right pgdn pgup", "match": SCREEN},
    {"name": "chain_finale_door_3_item_18_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_3_a", "keys": "home left end left*2 end*7 down*14 end left*2 end*5 down*4 end down pgdn down*2 pgdn*4 right*11 pgup up*22 pgup*9 right*4 pgup up*8 pgup*2 up*5 home*2 left*4 home*3 up pgup*4 right*2 pgup up*7 home*2 left*16 home*2 left end*2 left*3 home*4 up*2 home*2 left*19 end*6 down pgdn down*5", "match": SCREEN},
    {"name": "chain_finale_door_3_b", "keys": "down*8 end*5 down pgdn*3 right*4 pgdn*2 down*4 pgdn*4 down*2", "match": SCREEN},
    {"name": "chain_finale_door_9_item_8", "keys": "down*4 end*6 down pgdn*4 right*2 pgdn*2 pgup", "match": SCREEN},
    {"name": "chain_finale_door_9_item_8_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_9_item_7", "keys": "home*2 left*2 home*5 left end*7 left*3 home*4 pgup", "match": SCREEN},
    {"name": "chain_finale_door_9_item_7_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_9_item_20", "keys": "pgdn*4 right*3 pgup*12 right*2 pgup up*5 home*4 up*4 home*2 left*4 home*8 left end*6 left*9 home", "match": SCREEN},
    {"name": "chain_finale_door_9_item_20_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_9_a", "keys": "right*9 pgup*6 right*2 pgup*2 right pgdn*2 right*3 pgup up*13 home up pgup*6 right*19 pgdn*2 down*2 pgdn*4 right*3 pgup*2 right pgdn*2 right*16 pgdn*2 down*7 end left*2 end*4 down pgdn*3 right*4 pgdn*2 down*5 end*2 down*8 end left*4 end*9 down*22 end left*11 home*4 up*2 home*2 left end*4", "match": SCREEN},
    {"name": "chain_finale_door_9_b", "keys": "end down*7 pgdn*4 right*27 pgup up*4 pgup*5 right*6 pgup*2 right pgdn*2 right*17 pgup*6 right*2 pgup up*39 home up pgup up*23 home*2 left*4 home*4 up*15", "match": SCREEN},
    {"name": "chain_finale_door_7_item_29", "keys": "right pgup up*5 home*2 up*5 pgup*6", "match": SCREEN},
    {"name": "chain_finale_door_7_item_29_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_7_item_31", "keys": "end*5 down*5 pgdn*2 down*5 end*2 down*16 end left*9 home*4 up*2 home*4 pgup*3", "match": SCREEN},
    {"name": "chain_finale_door_7_item_31_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_7_item_22_a", "keys": "end*2 pgdn*4 down*2 pgdn*4 right*9 pgup*2 right pgdn*2 right*4 pgdn*2 down*23 end down pgdn down*39 end left*2 end*6 left*17 home*2 left end*2 left*6 end*5 down*4 end left*27 home*4 up*7 pgup*5 right pgdn*2 down*2 pgdn*4 right*4", "match": SCREEN},
    {"name": "chain_finale_door_7_item_22_b", "keys": "right*7 pgup up*22 pgup*9 right*4 pgup up*8 pgup*2 up*5 home*2 left*4 home*3 up pgup*4 right*2 pgup up*7 home*2 left*16 home*2 left end*2 left*3 home*4 up*2 home*2 left*19 end*6 down pgdn down*13 end*5 down pgdn*3 right*9 pgup up*7 pgup", "match": SCREEN},
    {"name": "chain_finale_door_7_item_22_taken", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_door_7_a", "keys": "down*7 end left*9 home*3 up pgup*5 up*13 home up pgup*6 right*19 pgdn*2 down*2 pgdn*4 right*3 pgup*2 right pgdn*2 right*16 pgdn*2 down*7 end left*2 end*4 down pgdn*3 right*4 pgdn*2 down*5 end*2 down*8 end left*4 end*9 down*22 end left*11 home*4 up*2 home up pgup up*4", "match": SCREEN},
    {"name": "chain_finale_door_7_b", "keys": "up*38", "match": SCREEN},
    {"name": "chain_finale_doors_save_menu", "keys": "space down enter", "match": SCREEN},
    {"name": "chain_finale_doors_saved", "keys": "enter", "match": SCREEN, "saved": 0},
    {"name": "chain_finale_doors_load_menu", "keys": "space up enter", "match": SCREEN},
    {"name": "chain_finale_doors_loaded", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_doors_load_instruct", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_doors_load_instruct_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_doors_load_inside", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_to_hint_a", "keys": "home up pgup*5 right*2 pgup*3 right pgdn right*4 pgdn*8 right*19 pgup*2 right pgdn*2 right*27 pgup up*4 pgup*5 right*6 pgup*2 right pgdn*2 right*17 pgup*6 right*2 pgup up*39 home up pgup up*23 home*2 left*4 home*2 left end*2", "match": SCREEN},
    {"name": "chain_finale_to_hint_b", "keys": "left*9 home*3 up pgup*5 up*11 home*2 left*16 home*6 left*2", "match": SCREEN},
    {"name": "chain_finale_hint", "keys": "end", "match": SCREEN},
    {"name": "chain_finale_hint_read", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_to_screen", "keys": "right*2 pgdn*6 right*16 pgdn*2 down*11 end*5 down pgdn*3 right*9 pgup up*16 pgup*2 up*2 pgup*6", "match": SCREEN},
    {"name": "chain_finale_screen", "keys": "pgup", "match": SCREEN},
    {"name": "chain_finale_screen_read", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_to_wall_a", "keys": "end*6 down*2 end*2 down*13 pgdn*4 right*4 pgdn*2 down*23 end down pgdn down*39 end left*2 end*6 left*17 home*2 left end*2 left*6 end*5 down*4 end left*27 home*4 up*7 pgup*5 right pgdn*2 down*2 pgdn*4 right*3", "match": SCREEN},
    {"name": "chain_finale_to_wall_b", "keys": "right*8 pgup up*22 pgup*9 right*4 pgup up*8 pgup*2 up*5 home*2 left*4 home*3 up pgup*4 right*2 pgup up*7 home*2 left*16 home*2 left end*2 left*3 home*4 up*2 home*2 left*19 end*6 down pgdn down*13 end left*3 home*2 left end*2 left*2 end*6 left*6 end*7 down*6", "match": SCREEN},
    {"name": "chain_finale_wall", "keys": "down", "match": SCREEN},
    {"name": "chain_finale_wall_read", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_to_down_a", "keys": "up*6 pgup*7 right*6 pgup*6 right*2 pgup*2 right pgdn*2 right*3 pgup up*13 home up pgup*6 right*19 pgdn*2 down*2 pgdn*4 right*3 pgup*2 right pgdn*2 right*16 pgdn*2 down*7 end left*2 end*4 down pgdn*3 right*4 pgdn*2 down*5 end*2 down*8 end left*4 end*9 down*22 end left*11 home*3", "match": SCREEN},
    {"name": "chain_finale_to_down_b", "keys": "home up*2 home up pgup up*40 pgup*2 up*2 pgup*7 right*3", "match": SCREEN},
    {"name": "chain_finale_down", "keys": "right", "match": SCREEN},
    {"name": "chain_finale_down_read", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_down_read_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_to_cell_wrong", "keys": "up right", "match": SCREEN},
    {"name": "chain_finale_cell_wrong", "keys": "right", "match": SCREEN},
    {"name": "chain_finale_to_check_wrong", "keys": "up*4 left*2", "match": SCREEN},
    {"name": "chain_finale_check_wrong", "keys": "left", "match": SCREEN},
    {"name": "chain_finale_check_wrong_read", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_to_cell_1", "keys": "up*25 right*24 down right*4 up right*4 down*3 right*4 up*2", "match": SCREEN},
    {"name": "chain_finale_cell_1", "keys": "up", "match": SCREEN},
    {"name": "chain_finale_to_cell_2", "keys": "down*8 left*2", "match": SCREEN},
    {"name": "chain_finale_cell_2", "keys": "left", "match": SCREEN},
    {"name": "chain_finale_to_cell_3", "keys": "down*4 left*10", "match": SCREEN},
    {"name": "chain_finale_cell_3", "keys": "left", "match": SCREEN},
    {"name": "chain_finale_to_cell_4", "keys": "down*2", "match": SCREEN},
    {"name": "chain_finale_cell_4", "keys": "left", "match": SCREEN},
    {"name": "chain_finale_to_cell_5", "keys": "up right*6 down*2 right*2 down*2 right*2 down*6 right*2 down*6 left down left*6 up left up*4", "match": SCREEN},
    {"name": "chain_finale_cell_5", "keys": "left", "match": SCREEN},
    {"name": "chain_finale_to_cell_6", "keys": "down*5 right*4 up*2 right*4 up*4 right*4", "match": SCREEN},
    {"name": "chain_finale_cell_6", "keys": "right", "match": SCREEN},
    {"name": "chain_finale_to_cell_7", "keys": "left*4", "match": SCREEN},
    {"name": "chain_finale_cell_7", "keys": "left", "match": SCREEN},
    {"name": "chain_finale_to_check_solved", "keys": "down*5 left down left*6 up left up*3 left*9 up left up left*18", "match": SCREEN},
    {"name": "chain_finale_check_solved", "keys": "left", "match": SCREEN},
    {"name": "chain_finale_check_solved_read", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_to_way_up", "keys": "down*5", "match": SCREEN},
    {"name": "chain_finale_way_up", "keys": "left", "match": SCREEN},
    {"name": "chain_finale_way_up_read", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_down_again", "keys": "right", "match": SCREEN},
    {"name": "chain_finale_down_again_read", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_to_endmech_a", "keys": "down*3 left*7 down left down*7 left down left down*48 right*14 up*28 right*6 up*4 right*8 up*10 right*2 up*8 left*7 up left up*7 right*6 up*10 left*18", "match": SCREEN},
    {"name": "chain_finale_to_endmech_b", "keys": "left*9 up left up*6 left*25 down left down*21 left*5 down left down*7 left*23 down left down*23", "match": SCREEN},
    {"name": "chain_finale_endmech", "keys": "down", "match": SCREEN},
    {"name": "chain_finale_endmech_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_endmech_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_endmech_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_endmech_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_endmech_5", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_endmech_6", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_to_findit_a", "keys": "up*27 right*12 up*6 right*16 up*21 right*26 down*8 right*30 down*10 left*5 down left down*7 right*8 down*18 left*4", "match": SCREEN},
    {"name": "chain_finale_to_findit_b", "keys": "left*3 down left down*3 left*5 down left down*26 left*15 up left up*6 left*7 down left down*14 right*30 up*10 right*34 up*6 right*8 up*25", "match": SCREEN},
    {"name": "chain_finale_to_findit_c", "keys": "up*43 left*7 up left up*35 right*6", "match": SCREEN},
    {"name": "chain_finale_findit", "keys": "right", "match": SCREEN},
    {"name": "chain_finale_findit_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_findit_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_to_way_out", "keys": "down*11 left*3 down left down*17 left down left down*6 left*39 up left up*12 left*7 down left down*4 left*43 up left up*7 left up left up*5 left*7 up left up left up left up*2 left*8", "match": SCREEN},
    {"name": "chain_finale_way_out", "keys": "left", "match": SCREEN},
    {"name": "chain_finale_winscene_1", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_winscene_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_winscene_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_winscene_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_winscene_5", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_winscene_6", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_winscene_7", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_winscene_8", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_winscene_9", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_winscene_10", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_winscene_11", "keys": "enter", "match": SCREEN},
    {"name": "chain_finale_won", "keys": "enter", "match": SCREEN},

]


# Poked: from chain_lounge_leave, 1000 C-Bills (our own test value) and the
# two courses that raise a skill, both on map 3. REPAIR's Apprentice item
# (menu 0x17's who list, special 0d) asks who, then the fee (special 0f);
# the second try for the same member is refused (special 10). HOSPITAL's
# seminar asks the fee first, then who (specials 0d, 16, 15). With the
# money gone, special 15 sends the member away unpaid. Each menu comes back
# with the bar on the item last chosen.
CHAIN_COURSES_POKED_CASES = [dict(case, compare=False) for case in
                             CHAIN_AGENT_CASES[:[c["name"] for c in CHAIN_AGENT_CASES].index("chain_lounge_leave") + 1]] + [
    {"name": "chain_courses_walk", "keys": "end down*25 pgdn*3 right pgdn down right pgup pgdn right*8 pgup up right",
     "match": SCREEN},
    {"name": "chain_courses_repair", "keys": "cbills:03e8 enter", "match": SCREEN},
    {"name": "chain_courses_tech_who", "keys": "down enter", "match": SCREEN},
    {"name": "chain_courses_tech_fee", "keys": "down down enter", "match": SCREEN},
    {"name": "chain_courses_tech_paid", "keys": "enter", "match": SCREEN},
    {"name": "chain_courses_tech_menu", "keys": "enter", "match": SCREEN},
    {"name": "chain_courses_tech_again", "keys": "enter down down enter", "match": SCREEN},
    {"name": "chain_courses_tech_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_courses_repair_leave", "keys": "up enter", "match": SCREEN},
    {"name": "chain_courses_walk_2", "keys": "left*8 home up left up home up home up*16 home*5 up left home up left home end",
     "match": SCREEN},
    {"name": "chain_courses_walk_3", "keys": "up left home end up left home end up left home left*5 home*8 up left up "
     "home up home up*3 home*5", "match": SCREEN},
    {"name": "chain_courses_hospital", "keys": "enter", "match": SCREEN},
    {"name": "chain_courses_seminar_fee", "keys": "down down enter", "match": SCREEN},
    {"name": "chain_courses_seminar_who", "keys": "enter", "match": SCREEN},
    {"name": "chain_courses_seminar_paid", "keys": "down*3 enter", "match": SCREEN},
    {"name": "chain_courses_seminar_menu", "keys": "enter", "match": SCREEN},
    {"name": "chain_courses_seminar_again", "keys": "enter enter down*3 enter", "match": SCREEN},
    {"name": "chain_courses_seminar_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_courses_seminar_short", "keys": "enter enter down enter", "match": SCREEN},
    {"name": "chain_courses_seminar_short_back", "keys": "enter", "match": SCREEN},
]

# REPAIR's shops (specials 18, 19, 20 and 44; docs/scripts.md, "The repair shop"), from
# chain_courses_poked's walk to map 3's REPAIR. At the door the Commando is damaged by
# pokes: armour and structure points, a heat sink, both weapons of slot code 0x1f (its own
# and one written into an empty slot: bug 40) and its SRM 6 (0x20: bug 41), a hip actuator,
# and shots of the two 0x1f weapons (the poked one given 10 at most); 5000 C-Bills. Every
# repair is bought; bug 40's second payment mends nothing, and bug 41 calls the weapons all
# fixed with the SRM 6 still destroyed. Then 10 C-Bills with armour and a heat sink damaged
# again: two points bought, the heat sink and the SRM 6 refused for money. Ammunition with
# 10000: 25 typed for the first (20 missing), 5 for the second, the destroyed SRM 6 named.
# The speed shop's case 3 short of money, then bought with 20000; then its second upgrade
# (case 7) offered and declined.
LUBE_POKES = " ".join(["cbills:1388", "mech:0:11=02", "mech:0:12=00", "mech:0:1c=02", "mech:0:3a=a2",
                       "mech:0:41=9f", "mech:0:52=a0", "mech:0:53=9f", "mech:0:24=00", "mech:0:28=05",
                       "mech:0:2a=02", "mech:0:6e=0a"])
CHAIN_LUBE_POKED_CASES = [dict(case, compare=False) for case in
                          CHAIN_COURSES_POKED_CASES[:[c["name"] for c in CHAIN_COURSES_POKED_CASES].index(
                              "chain_courses_walk") + 1]] + [
    {"name": "chain_lube_door", "keys": LUBE_POKES + " enter", "match": SCREEN},
    {"name": "chain_lube_repair_which", "keys": "down down down enter", "match": SCREEN},
    {"name": "chain_lube_armour", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_structure", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_sinks", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_weapons", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_weapon_paid", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_weapon_again", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_actuators", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_actuators_done", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_repaired", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_short_which", "keys": "cbills:000a mech:0:13=00 mech:0:3a=a2 enter", "match": SCREEN},
    {"name": "chain_lube_short_armour", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_short_bought", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_short_sinks", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_short_refused", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_short_weapons", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_short_weapon", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_short_out", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_ammo_which", "keys": "cbills:2710 down enter", "match": SCREEN},
    {"name": "chain_lube_ammo_first", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_ammo_too_many", "keys": "2 5 enter", "match": SCREEN},
    {"name": "chain_lube_ammo_bought", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_ammo_second", "keys": "5 enter", "match": SCREEN},
    {"name": "chain_lube_ammo_out", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_speed_which", "keys": "up up enter", "match": SCREEN},
    {"name": "chain_lube_speed_offer", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_speed_price", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_speed_short", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_speed_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_buy_which", "keys": "cbills:4e20 enter", "match": SCREEN},
    {"name": "chain_lube_buy_offer", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_buy_price", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_bought", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_buy_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_second_which", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_second_offer", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_second_price", "keys": "enter", "match": SCREEN},
    {"name": "chain_lube_second_no", "keys": "n", "match": SCREEN},
    {"name": "chain_lube_leave", "keys": "up up enter", "match": SCREEN},
]

# HOSPITAL's treatment and kits (specials 24, 25 and 26; docs/scripts.md), from the end of
# chain_courses_poked inside map 3's Hospital, whose seminar left a member able to use both
# kits (menu 0x1c). 10000 C-Bills: the fee, a Full surgeon (level 5); Jason's hit points
# poked down and the doctor declined (level 0, the party's own Field Surgery); again with
# 100 C-Bills (Jason still hurt) and the Neurosurgeon (level 7), more than is left: refused,
# the 25 given back. The MedKit bought, the field surgery kit refused for money and then
# bought with 1000, and the MedKit asked for again (already held). The original draws the
# party box only at 1000:4cac, so until the healing draws it again the poked hit points show
# in chr's bars alone: those two cases compare the map (and the state).
CHAIN_WARD_POKED_CASES = [dict(case, compare=False) for case in CHAIN_COURSES_POKED_CASES] + [
    {"name": "chain_ward_fee", "keys": "cbills:2710 up enter", "match": SCREEN},
    {"name": "chain_ward_caliber", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_treated", "keys": "down*4 enter", "match": SCREEN},
    {"name": "chain_ward_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_fee_2", "keys": "char:0:0f=05 enter", "match": {"the map": WHOLE_MAP}},
    {"name": "chain_ward_declined", "keys": "n", "match": {"the map": WHOLE_MAP}},
    {"name": "chain_ward_back_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_fee_3", "keys": "cbills:0064 enter", "match": SCREEN},
    {"name": "chain_ward_caliber_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_short", "keys": "down*2 enter", "match": SCREEN},
    {"name": "chain_ward_back_3", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_medkit", "keys": "down*4 enter", "match": SCREEN},
    {"name": "chain_ward_medkit_bought", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_back_4", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_kit", "keys": "down enter", "match": SCREEN},
    {"name": "chain_ward_kit_short", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_back_5", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_kit_2", "keys": "cbills:03e8 enter", "match": SCREEN},
    {"name": "chain_ward_kit_bought", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_back_6", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_medkit_held", "keys": "up enter", "match": SCREEN},
    {"name": "chain_ward_back_7", "keys": "enter", "match": SCREEN},
    {"name": "chain_ward_leave", "keys": "down down enter", "match": SCREEN},
]

# JAIL's traitor (specials 36 and 37; docs/scripts.md), from chain_town's walk to the jail's
# door, with flag 27 (the late game) and flag 25 (the traitor's record, here 0) poked there:
# the name printed twice, then the game over: Jason's record emptied and DG:014a cleared, and
# the main loop's game-over box (1000:04a4) with the leader's text. The original draws the
# party box only at 1000:4cac, so after the record is emptied it still shows Jason's bars
# while chr's are drawn from the records: that case compares the map (and the state).
CHAIN_TRAITOR_POKED_CASES = [dict(case, compare=False) for case in
                             CHAIN_TOWN_CASES[:[c["name"] for c in CHAIN_TOWN_CASES].index(
                                 "chain_town_to_jail") + 1]] + [
    {"name": "chain_traitor_door", "keys": "flag:27=01 flag:25=00 end", "match": SCREEN},
    {"name": "chain_traitor_named", "keys": "y", "match": SCREEN},
    {"name": "chain_traitor_named_2", "keys": "enter", "match": SCREEN},
    {"name": "chain_traitor_over", "keys": "enter", "match": {"the map": WHOLE_MAP}},
    {"name": "chain_traitor_box", "keys": "enter", "match": SCREEN},
]

# The game settings (1000:3bd0, docs/town_loop.md) from the town start: the
# command menu's item 1 and each of its items. The menus remember their last
# item, so each case's keys count from there. Steps a key is walked at 2 and
# at 4, the sound flipped (no sound is played: only its state shows), and the
# quit question answered No.
SETTINGS_CASES = [dict(case, compare=False) for case in CASES[:4]] + [
    {"name": "settings_open", "keys": "space down enter", "match": SCREEN},
    {"name": "settings_sound_off", "keys": "down*2 enter", "match": SCREEN},
    {"name": "settings_sound_shown", "keys": "space enter", "match": SCREEN},
    {"name": "settings_back", "keys": "down*3 enter", "match": SCREEN},
    {"name": "settings_steps_menu", "keys": "space enter down enter", "match": SCREEN},
    {"name": "settings_steps_2", "keys": "down enter", "match": SCREEN},
    {"name": "settings_steps_2_walk", "keys": "left*2", "match": SCREEN},
    {"name": "settings_steps_4_menu", "keys": "space enter enter", "match": SCREEN},
    {"name": "settings_steps_4", "keys": "down enter", "match": SCREEN},
    {"name": "settings_steps_4_walk", "keys": "right", "match": SCREEN},
    {"name": "settings_speed_menu", "keys": "space enter down enter", "match": SCREEN},
    {"name": "settings_speed", "keys": "down*3 enter", "match": SCREEN},
    {"name": "settings_pictures_menu", "keys": "space enter down*2 enter", "match": SCREEN},
    {"name": "settings_pictures", "keys": "down enter", "match": SCREEN},
    {"name": "settings_quit", "keys": "space enter down enter", "match": SCREEN},
    {"name": "settings_quit_no", "keys": "n", "match": SCREEN},
]

# From the Weapons Shop's door to the Armor Shop's (map 1 entrances 3 and 4).
SHOP_MAP = {"the map": WHOLE_MAP}
SHOP_ARMOUR_WALK = "right down*15 left*5 down*8 left*6 down*52 right*31 down*5 left*3"

# The shops on map 1 (docs/scripts.md, "The shops") from the town start.
# WEAPON (entrance 3): the cheapest blade bought with the town's money, then
# the dearest anti-'Mech weapon refused for want of it. The menus remember
# their last item, so each case's keys count from there.
SHOP_CASES = [dict(case, compare=False) for case in CASES[:4]] + [
    {"name": "shop_weapon_door", "keys": "left*25 down*24 left*12 up*5 left", "match": SCREEN},
    {"name": "shop_weapon_inside", "keys": "y", "match": SCREEN},
    {"name": "shop_weapon_counters", "keys": "enter", "match": SCREEN},
    {"name": "shop_weapon_blades", "keys": "down enter", "match": SCREEN},
    {"name": "shop_weapon_first", "keys": "down enter", "match": SCREEN},
    {"name": "shop_weapon_bought", "keys": "y", "match": SCREEN},
    {"name": "shop_weapon_again", "keys": "enter", "match": SCREEN},
    {"name": "shop_weapon_missiles", "keys": "enter down*2 enter", "match": SCREEN},
    {"name": "shop_weapon_dearest", "keys": "down*2 enter", "match": SCREEN},
    {"name": "shop_weapon_refused", "keys": "y", "match": SCREEN},
    {"name": "shop_weapon_out", "keys": "enter", "match": SCREEN},
    # ARMOR (entrance 4), from the weapon shop's door. At the door a second
    # character (SALVAGE_POKES) and 2000 C-Bills are poked: the cheapest
    # armour is bought and offered round the party, Jason takes it; then
    # his points are poked down and topped up. The original draws the party
    # box only at 1000:4cac, so until leaving it shows no bars for the poked
    # character while chr's are drawn from the records: those cases compare
    # the map (and the state); leaving redraws the box, compared whole.
    {"name": "shop_armour_door", "keys": SHOP_ARMOUR_WALK + " " + SALVAGE_POKES + " cbills:07d0",
     "match": SHOP_MAP},
    {"name": "shop_armour_inside", "keys": "y", "match": SHOP_MAP},
    {"name": "shop_armour_list", "keys": "enter", "match": SHOP_MAP},
    {"name": "shop_armour_first", "keys": "down enter", "match": SHOP_MAP},
    {"name": "shop_armour_offer", "keys": "y", "match": SHOP_MAP},
    {"name": "shop_armour_taken", "keys": "enter", "match": SHOP_MAP},
    {"name": "shop_armour_again", "keys": "enter", "match": SHOP_MAP},
    {"name": "shop_armour_who", "keys": "char:0:0e=05 down enter", "match": SHOP_MAP},
    {"name": "shop_armour_top_up", "keys": "enter", "match": SHOP_MAP},
    {"name": "shop_armour_topped_up", "keys": "y", "match": SHOP_MAP},
    {"name": "shop_armour_out", "keys": "enter", "match": SHOP_MAP},
    {"name": "shop_armour_leave", "keys": "down*2 enter", "match": SCREEN},
]

# The long unpoked run: its own tier (`--campaign`, ctest label campaign).
# The save (1000:35d3, docs/formats/save.md), from the agent's chain once the crew is set:
# the command menu's item 6 (the menu starts on the crew item, 2) and slot 0. The file the
# original wrote is compared byte for byte. Then the load (1000:32b3).
CHAIN_SAVE_CASES = [dict(case, compare=False) for case in CHAIN_AGENT_CASES[:[
    c["name"] for c in CHAIN_AGENT_CASES].index("chain_crew_done") + 1]] + [
    {"name": "chain_save_menu", "keys": "space down*4 enter", "match": SCREEN},
    {"name": "chain_save_written", "keys": "enter", "match": SCREEN, "saved": 0},
    # The load (1000:32b3): item 5 (the menu starts on the save), slot 1, which has no file:
    # the name and the key; then time passes and slot 0 is loaded back.
    {"name": "chain_load_missing", "keys": "space up enter down enter", "match": SCREEN},
    {"name": "chain_load_missing_back", "keys": "enter", "match": SCREEN},
    {"name": "chain_load_waited", "keys": "idle:60", "match": SCREEN},
    {"name": "chain_load_menu", "keys": "space enter", "match": SCREEN},
    {"name": "chain_load_done", "keys": "up enter", "match": SCREEN},
]

# ComStar (COMSTAR, specials 6, 7 and 8; docs/scripts.md): the account, investing 15 typed
# with a Backspace, selling from an empty stock, a sale capped at what is held, an investment
# cancelled with Esc, the prospectus, and out. Menu 7 is both the buy/sell question and the
# stock list, so the two share a remembered item.
COMSTAR_CASES = [dict(case, compare=False) for case in CASES[:4]] + [
    {"name": "comstar_door", "keys": "left right up right*12 up*2 right*20 up*2 right", "match": SCREEN},
    {"name": "comstar_inside", "keys": "y", "match": SCREEN},
    {"name": "comstar_terminal", "keys": "enter", "match": SCREEN},
    {"name": "comstar_account", "keys": "enter", "match": SCREEN},
    {"name": "comstar_change", "keys": "enter", "match": SCREEN},
    {"name": "comstar_buy_which", "keys": "enter", "match": SCREEN},
    {"name": "comstar_buy_how_much", "keys": "enter", "match": SCREEN},
    {"name": "comstar_buy_typed", "keys": "1 2 backspace 5", "match": SCREEN},
    {"name": "comstar_bought", "keys": "enter", "match": SCREEN},
    {"name": "comstar_account_2", "keys": "enter", "match": SCREEN},
    {"name": "comstar_sell_which", "keys": "enter down enter", "match": SCREEN},
    {"name": "comstar_sell_empty", "keys": "enter", "match": SCREEN},
    {"name": "comstar_account_3", "keys": "enter", "match": SCREEN},
    {"name": "comstar_sell_which_2", "keys": "enter enter", "match": SCREEN},
    {"name": "comstar_sell_how_much", "keys": "up enter", "match": SCREEN},
    {"name": "comstar_sell_capped", "keys": "9 9 9 enter", "match": SCREEN},
    {"name": "comstar_account_4", "keys": "enter", "match": SCREEN},
    {"name": "comstar_buy_which_2", "keys": "enter enter", "match": SCREEN},
    {"name": "comstar_buy_how_much_2", "keys": "enter", "match": SCREEN},
    {"name": "comstar_buy_cancelled", "keys": "4 esc enter", "match": SCREEN},
    {"name": "comstar_account_5", "keys": "enter", "match": SCREEN},
    {"name": "comstar_prospectus", "keys": "down enter enter", "match": SCREEN},
    {"name": "comstar_account_6", "keys": "enter", "match": SCREEN},
    {"name": "comstar_out", "keys": "down enter", "match": SCREEN},
]

# The Arena (ARENA, docs/scripts.md "The Arena"), from inside map 2 as chain_party starts: the
# walk to its door (entrance 1), 500 C-Bills poked there (the chain has 170, renting costs 250),
# renting the Arena's 'Mech, and a fight the computer fights (no combat graphics) to the
# verdict, the prize and the way out. The poke is not drawn by the original until the script's
# `pay` redraws the C-Bills, so the cases before it match the map only.
ARENA_WALK = "pgdn*19 down*9 right*20 down*2 left*4 down*16 right*4"
CHAIN_ARENA_CASES = [dict(case, compare=False) for case in
                     CHAIN_TOWN_CASES[:[c["name"] for c in CHAIN_TOWN_CASES].index("chain_town_garage_out_2") + 1]] + [
    {"name": "chain_arena_door", "keys": ARENA_WALK + " cbills:01f4", "match": SHOP_MAP},
    {"name": "chain_arena_welcome", "keys": "y", "match": SHOP_MAP},
    {"name": "chain_arena_attendant", "keys": "y", "match": SHOP_MAP},
    {"name": "chain_arena_deposit", "keys": "enter", "match": SCREEN},
    {"name": "chain_arena_first_time", "keys": "enter", "match": SCREEN},
    {"name": "chain_arena_computer", "keys": "enter", "match": SCREEN},
    {"name": "chain_arena_messages", "keys": "y", "match": SCREEN},
    {"name": "chain_arena_graphics", "keys": "enter", "match": SCREEN},
    {"name": "chain_arena_verdict", "keys": "n idle:400", "match": SCREEN},
    {"name": "chain_arena_prize", "keys": "enter", "match": SCREEN},
    {"name": "chain_arena_out", "keys": "enter", "match": SCREEN},
]

# FROB (docs/scripts.md, "Frob's"): from chain_town's garage door, the walk to Frob's door
# (map 2, entrance 11), 20000 C-Bills poked there, Jason alone buying the holoviewer (flag 30),
# then a second visit buying the terrain mapper (flag 31), and three steps after it, which the
# seen map marks a whole column at a time. Until the first `pay` redraws the C-Bills the cases
# match the map only.
FROB_WALK = "up left up*29 left*11"
CHAIN_FROB_CASES = [dict(case, compare=False) for case in
                    CHAIN_TOWN_CASES[:[c["name"] for c in CHAIN_TOWN_CASES].index("chain_town_garage_out_2") + 1]] + [
    {"name": "chain_frob_door", "keys": FROB_WALK + " cbills:4e20", "match": SHOP_MAP},
    {"name": "chain_frob_welcome", "keys": "y", "match": SHOP_MAP},
    {"name": "chain_frob_inside", "keys": "enter", "match": SHOP_MAP},
    {"name": "chain_frob_choice", "keys": "enter", "match": SHOP_MAP},
    {"name": "chain_frob_salesman", "keys": "enter", "match": SHOP_MAP},
    {"name": "chain_frob_price", "keys": "enter", "match": SHOP_MAP},
    {"name": "chain_frob_holoviewer", "keys": "y", "match": SCREEN},
    {"name": "chain_frob_holoviewer_out", "keys": "enter", "match": SCREEN},
    {"name": "chain_frob_again", "keys": "right left", "match": SCREEN},
    {"name": "chain_frob_again_welcome", "keys": "y", "match": SCREEN},
    {"name": "chain_frob_again_inside", "keys": "enter", "match": SCREEN},
    {"name": "chain_frob_again_choice", "keys": "enter", "match": SCREEN},
    {"name": "chain_frob_mappers", "keys": "down enter", "match": SCREEN},
    {"name": "chain_frob_mapper", "keys": "y", "match": SCREEN},
    {"name": "chain_frob_mapper_out", "keys": "enter", "match": SCREEN},
    {"name": "chain_frob_seen", "keys": "down*3", "match": SCREEN},
]

# MAYOR (docs/scripts.md, "The mayor"): from chain_town's segment 6, south-west across the
# wilderness to the Mayor's house (map 4, entrance 6), four groups in reach declined and evaded
# on the way (the planner's walk). The first visit takes menu item 0 (which loops back) and then
# item 1 through to the end, special 47 among the last texts; the second visit is the short one
# flag 2b gives.
CHAIN_MAYOR_CASES = [dict(case, compare=False) for case in
                     CHAIN_TOWN_CASES[:[c["name"] for c in CHAIN_TOWN_CASES].index("chain_town_walk_6b") + 1]]
for i, walk in enumerate(["end*61", "end*9", "end*28", "end*40 home*61"]):
    CHAIN_MAYOR_CASES += [
        {"name": "chain_mayor_walk_%d" % i, "keys": walk, "match": SCREEN},
        {"name": "chain_mayor_%d_no" % i, "keys": "n", "match": SCREEN},
        {"name": "chain_mayor_%d_eluded" % i, "keys": "enter", "match": SCREEN},
        {"name": "chain_mayor_%d_back" % i, "keys": "enter", "match": SCREEN},
    ]
CHAIN_MAYOR_CASES += [
    {"name": "chain_mayor_door", "keys": "home*8", "match": SCREEN},
    {"name": "chain_mayor_inside", "keys": "y", "match": SCREEN},
    {"name": "chain_mayor_menu", "keys": "enter", "match": SCREEN},
    {"name": "chain_mayor_item_0", "keys": "enter", "match": SCREEN},
    {"name": "chain_mayor_menu_again", "keys": "enter", "match": SCREEN},
    {"name": "chain_mayor_item_1", "keys": "down enter", "match": SCREEN},
] + [{"name": "chain_mayor_text_%d" % i, "keys": "enter", "match": SCREEN} for i in range(8)] + [
    {"name": "chain_mayor_out", "keys": "enter", "match": SCREEN},
    {"name": "chain_mayor_again", "keys": "pgdn pgdn home", "match": SCREEN},
    {"name": "chain_mayor_again_inside", "keys": "y", "match": SCREEN},
    {"name": "chain_mayor_again_out", "keys": "enter", "match": SCREEN},
]

# THEATER (docs/scripts.md, "The theater"): from chain_agent's end, south and east across the
# wilderness to map 10's theater (entrance 7), the planner's walk: two groups in reach declined
# and evaded, one fled from (Flee from the fight's menu, then its texts), one more evaded and one
# other door declined. Yes at the door and Yes to the show, its texts, the last of them ending in
# special 42; then three steps with the audience coming out.
_THEATER_FLEE = ["n", "n", "enter", "n", "up enter", "enter", "up enter", "enter", "enter", "enter"]
CHAIN_THEATER_CASES = [dict(case, compare=False) for case in CHAIN_AGENT_CASES]
for i, (walk, after) in enumerate([("down*96", "evade"), ("down*47 end*67", "flee"),
                                   ("end*15 pgdn*7 down*15 end*8 down pgdn*8 down*15 end*40 down pgdn*8 down*15 end*55", "evade"),
                                   ("end*54 home*4", "evade"), ("home*6 left*11 home*8 pgup home", "no")]):
    CHAIN_THEATER_CASES.append({"name": "chain_theater_walk_%d" % i, "keys": walk, "match": SCREEN})
    if after == "evade":
        CHAIN_THEATER_CASES += [{"name": "chain_theater_%d_%s" % (i, n), "keys": k, "match": SCREEN}
                                for n, k in [("no", "n"), ("eluded", "enter"), ("back", "enter")]]
    elif after == "flee":
        CHAIN_THEATER_CASES += [{"name": "chain_theater_%d_flee_%d" % (i, j), "keys": k, "match": SCREEN}
                                for j, k in enumerate(_THEATER_FLEE)]
    else:
        CHAIN_THEATER_CASES.append({"name": "chain_theater_%d_no" % i, "keys": "n", "match": SCREEN})
CHAIN_THEATER_CASES += [
    {"name": "chain_theater_door", "keys": "pgup home*5 pgup home pgup home*2 pgup up*4 home*2 end home end home*7 left",
     "match": SCREEN},
    {"name": "chain_theater_inside", "keys": "y", "match": SCREEN},
    {"name": "chain_theater_yes", "keys": "y", "match": SCREEN},
] + [{"name": "chain_theater_show_%d" % i, "keys": "enter", "match": SCREEN} for i in range(9)] + [
    {"name": "chain_theater_out", "keys": "enter", "match": SCREEN},
] + [{"name": "chain_theater_step_%d" % i, "keys": "down", "match": SCREEN} for i in range(3)]

# The command menu's inspect item (1000:378d, docs/town_loop.md "Inspect a character"), from the
# armour shop's end: two characters and no 'Mechs, so no 'Mech question; the menu's bar stays on
# the item after the first time. Jason's armour whole, then its points poked to 5 and to 0 (our
# own test values), flag 31 poked set for the mapper's line; the poked second character has no
# armour.
INSPECT_CASES = [dict(case, compare=False) for case in SHOP_CASES] + [
    {"name": "inspect_who", "keys": "space down*2 enter", "match": SCREEN},
    {"name": "inspect_whole", "keys": "enter", "match": SCREEN},
    {"name": "inspect_whole_out", "keys": "enter", "match": SCREEN},
    {"name": "inspect_worn", "keys": "char:0:0e=05 space enter enter", "match": SCREEN},
    {"name": "inspect_worn_out", "keys": "enter", "match": SCREEN},
    {"name": "inspect_ruined", "keys": "char:0:0e=00 flag:31=01 space enter enter", "match": SCREEN},
    {"name": "inspect_ruined_out", "keys": "enter", "match": SCREEN},
    {"name": "inspect_second", "keys": "space enter down enter", "match": SCREEN},
    {"name": "inspect_second_out", "keys": "enter", "match": SCREEN},
]

# The same from chain_party's end, with 'Mechs and the double agent aboard: the 'Mech question
# (No), Jason, then Rick (the tell: F:374a set); last the question's Yes and the 'Mech chooser.
CHAIN_INSPECT_CASES = [dict(case, compare=False) for case in CHAIN_PARTY_CASES] + [
    {"name": "chain_inspect_question", "keys": "space down*3 enter", "match": SCREEN},
    {"name": "chain_inspect_who", "keys": "n", "match": SCREEN},
    {"name": "chain_inspect_jason", "keys": "enter", "match": SCREEN},
    {"name": "chain_inspect_jason_out", "keys": "enter", "match": SCREEN},
    {"name": "chain_inspect_again", "keys": "space enter", "match": SCREEN},
    {"name": "chain_inspect_again_who", "keys": "n", "match": SCREEN},
    {"name": "chain_inspect_rick", "keys": "down*4 enter", "match": SCREEN},
    {"name": "chain_inspect_rick_out", "keys": "enter", "match": SCREEN},
    {"name": "chain_inspect_mech", "keys": "space enter", "match": SCREEN},
    {"name": "chain_inspect_which_mech", "keys": "y", "match": SCREEN},
    # The first 'Mech: the stats screen (15ab:1afe, docs/town_loop.md). The
    # feeder's fourth poll is the screen's fourth frame, on both sides
    # (arm:4).
    {"name": "chain_inspect_stats", "keys": "arm:4 enter", "match": WHOLE_SCREEN},
    # A key: the panel drawn again, window 4 cleared, the map back.
    {"name": "chain_inspect_stats_out", "keys": "enter", "match": SCREEN},
]

# The start-up's load (1000:50c8, docs/startup.md): "No" to the first-time question, "Yes" to
# loading, slot 0. Its Game1 is the one chain_save's original run wrote (SEEDED), on both drives.
START_LOAD_CASES = [dict(case, compare=False) for case in CASES[:3]] + [
    {"name": "startload_question", "keys": "n", "state": False,
     "match": {"the panel": WHOLE_PANEL, "the window": WHOLE_MAP}},
    {"name": "startload_menu", "keys": "y", "state": False, "match": SCREEN},
    {"name": "startload_done", "keys": "enter", "starts_town": True, "match": SCREEN},
    {"name": "startload_idle", "keys": "idle:9", "match": SCREEN},
]

# Sequences that start with a file another sequence's original run wrote: the sequence, the
# file's name in its capture folder, and the name on the drive. The other sequence runs first
# (it is added to the run when needed); the file is never kept anywhere else.
SEEDED = {"save_startup": ("chain_save", "Game1", "Game1")}

# A run of the original with no new capture for this long is stuck (a
# dialog, a hang): DOSBox-X is stopped and the sequence is a rig error. The
# longest sequence, chain_cache_open, takes about 30 minutes for its cases,
# so the whole-script limit alone would leave a stuck run going far longer.
STALL_SECONDS = 600

# Campaign sequences longer than the campaign's limit (2400 s for the whole
# script): chain_finale's original runs about 35 minutes alone. The stall
# limit still stops one that hangs.
LONG_SEQUENCES = {"chain_finale": 3600}

CAMPAIGN = {"chain_save", "save_startup", "chain", "chain_death", "chain_mech_poked", "chain_heal_poked", "chain_repair_poked",
            "chain_salvage_poked", "chain_scrap_poked", "chain_orders_poked", "chain_town", "chain_party",
            "chain_jail_no", "chain_jail_fight", "chain_agent", "chain_courses_poked", "chain_cache_poked",
            "chain_cache_open", "chain_finale", "chain_entrance", "chain_arena", "chain_frob", "chain_mayor",
            "chain_theater", "chain_inspect", "chain_flee", "chain_lube_poked", "chain_ward_poked",
            "chain_traitor_poked"}

# Sequences with a known mismatch, written up as an open item in
# docs/reference.md: in neither tier (not --full, not --campaign), run by
# name only. Seeded from a campaign chain, one would also pull that whole
# chain into --full.
KNOWN_OPEN = set()

SEQUENCES = [("town", CASES, FULL_CASES, []), ("training", TRAINING_CASES, [], []),
             ("quiz", QUIZ_CASES, [], ["--show-quiz"]), ("mission", MISSION_CASES, MISSION_FULL_CASES, []),
             ("verbose", VERBOSE_CASES, VERBOSE_FULL_CASES, []), ("drawn", DRAWN_CASES, DRAWN_FULL_CASES, []),
             ("brief", [], BRIEF_CASES + BRIEF_FULL_CASES, []),
             ("orders", ORDERS_CASES, ORDERS_FULL_CASES, []),
             ("moves", MOVE_CASES, MOVE_FULL_CASES, []),
             ("waypoints", [], WAYPOINT_CASES + WAYPOINT_FULL_CASES, []),
             ("weapons", WEAPON_CASES, WEAPON_FULL_CASES, []),
             ("kick", KICK_CASES, KICK_FULL_CASES, []),
             ("scan", SCAN_CASES, SCAN_FULL_CASES, []),
             ("detail_poked", [], DETAIL_CASES, []),
             ("detail_enemy", [], DETAIL_ENEMY_CASES, []),
             ("people", PEOPLE_CASES, PEOPLE_FULL_CASES, []),
             ("course", COURSE_CASES, COURSE_FULL_CASES, []),
             ("spot", [], SPOT_CASES, []),
             ("fourth", [], FOURTH_CASES, []),
             ("raid", [], RAID_CASES, []),
             ("rest", [], REST_CASES, []),
             # The longest campaign sequence first, so it runs beside the others.
             ("chain_finale", [], CHAIN_FINALE_CASES, []),
             ("chain", [], CHAIN_CASES, []),
             ("chain_death", [], CHAIN_DEATH_CASES, []),
             ("chain_mech_poked", [], CHAIN_MECH_POKED_CASES, []),
             ("chain_save", [], CHAIN_SAVE_CASES, []),
             ("save_startup", [], START_LOAD_CASES, []),
             ("chain_heal_poked", [], CHAIN_HEAL_POKED_CASES, []),
             ("chain_repair_poked", [], CHAIN_REPAIR_POKED_CASES, []),
             ("chain_salvage_poked", [], CHAIN_SALVAGE_POKED_CASES, []),
             ("chain_scrap_poked", [], CHAIN_SCRAP_POKED_CASES, []),
             ("chain_orders_poked", [], CHAIN_ORDERS_POKED_CASES, []),
             ("chain_town", [], CHAIN_TOWN_CASES, []),
             ("chain_party", [], CHAIN_PARTY_CASES, []),
             ("chain_jail_no", [], CHAIN_JAIL_NO_CASES, []),
             ("chain_jail_fight", [], CHAIN_JAIL_FIGHT_CASES, []),
             ("chain_agent", [], CHAIN_AGENT_CASES, []),
             ("chain_courses_poked", [], CHAIN_COURSES_POKED_CASES, []),
             ("chain_lube_poked", [], CHAIN_LUBE_POKED_CASES, []),
             ("chain_ward_poked", [], CHAIN_WARD_POKED_CASES, []),
             ("chain_traitor_poked", [], CHAIN_TRAITOR_POKED_CASES, []),
             ("chain_cache_poked", [], CHAIN_CACHE_POKED_CASES, []),
             ("chain_cache_open", [], CHAIN_CACHE_OPEN_CASES, []),
             ("chain_entrance", [], CHAIN_ENTRANCE_CASES, []),
             ("chain_flee", [], CHAIN_FLEE_CASES, []),
             ("settings", [], SETTINGS_CASES, []),
             ("shops", [], SHOP_CASES, []),
             ("comstar", [], COMSTAR_CASES, []),
             ("chain_arena", [], CHAIN_ARENA_CASES, []),
             ("chain_frob", [], CHAIN_FROB_CASES, []),
             ("chain_mayor", [], CHAIN_MAYOR_CASES, []),
             ("chain_theater", [], CHAIN_THEATER_CASES, []),
             ("inspect", [], INSPECT_CASES, []),
             ("chain_inspect", [], CHAIN_INSPECT_CASES, []),
             ("handover", [], HANDOVER_CASES, [])]

# The feeder (tools/capture/snap.asm, docs/reference.md) hands over a key at
# every 4th poll of the keyboard. The main loop polls once a pass, and after
# a key it flushes the keyboard with one more poll, so the town starts with
# 3 passes before the first item, and a key is followed by 2. idle:N is N.
FIRST_PASSES = 3
PASSES_AFTER_KEY = 2


# A test aid in a case's keys: "flag:NN=VV" (hex) sets a flag in both
# programs at that point: a test poke in the original (run.py, data only)
# and set:NN=VV in chr. A case that uses one says so and what play it stands
# in for (docs/reference.md).
FLAGS_AT = 0xD30C  # F:d30c, the flags


def flag_poke(token):
    """The original's poke for a flag:NN=VV token."""
    flag, _, value = token[len("flag:"):].partition("=")
    return "@poke:F:%04x=%02x" % (FLAGS_AT + int(flag, 16), int(value, 16))


RECORDS_AT = 0xC724  # F:c724, the eight 'Mech records (docs/formats/mech.md)


def mech_poke(token):
    """The original's poke for a mech:R:BB=VV token: one byte inside the
    eight 'Mech records, nothing else."""
    try:
        record, _, rest = token[len("mech:"):].partition(":")
        byte, _, value = rest.partition("=")
        r, b, v = int(record, 16), int(byte, 16), int(value, 16)
    except ValueError:
        raise ValueError(f"bad record poke {token!r}: use mech:R:BB=VV (hex)")
    if not (0 <= r < 8 and 0 <= b < 0x7d and 0 <= v <= 0xff):
        raise ValueError(f"refused record poke {token!r}: record 0-7, byte below 7d, value one byte")
    return "@poke:F:%04x=%02x" % (RECORDS_AT + 0x7d * r + b, v)


CHARACTERS_AT = 0xC614  # F:c614, the 16 character records of 0x11 bytes


def char_poke(token):
    """The original's poke for a char:R:BB=VV token: one byte inside the 16
    character records, nothing else."""
    try:
        record, _, rest = token[len("char:"):].partition(":")
        byte, _, value = rest.partition("=")
        r, b, v = int(record, 16), int(byte, 16), int(value, 16)
    except ValueError:
        raise ValueError(f"bad record poke {token!r}: use char:R:BB=VV (hex)")
    if not (0 <= r < 16 and 0 <= b < 0x11 and 0 <= v <= 0xff):
        raise ValueError(f"refused record poke {token!r}: record 0-f, byte below 11, value one byte")
    return "@poke:F:%04x=%02x" % (CHARACTERS_AT + 0x11 * r + b, v)


CBILLS_AT = 0xD370  # F:d370, the C-Bills, 32 bits


def cbills_poke(token):
    """The original's poke for a cbills:VVVV token: the four bytes of the
    C-Bills, nothing else."""
    try:
        v = int(token[len("cbills:"):], 16)
    except ValueError:
        raise ValueError(f"bad C-Bills poke {token!r}: use cbills:VVVV (hex)")
    if not 0 <= v <= 0xffff:
        raise ValueError(f"refused C-Bills poke {token!r}: at most ffff")
    return "@poke:F:%04x=%02x%02x0000" % (CBILLS_AT, v & 0xff, v >> 8)


RANDOM_AT = 0x4FC0  # DG:4fc0, the generator's three bytes (docs/random.md)


def random_poke(token):
    """The original's poke for a random:AABBCC token: the generator's three
    bytes, nothing else."""
    body = token[len("random:"):]
    try:
        if len(body) != 6:
            raise ValueError
        int(body, 16)
    except ValueError:
        raise ValueError(f"bad random poke {token!r}: use random:AABBCC (hex)")
    return "@poke:DG:%04x=%s" % (RANDOM_AT, body.lower())


HEAT_AT = 0x006E  # F:006e, the eight 'Mech records' heat


def heat_poke(token):
    """The original's poke for a heat:M=VV token: one 'Mech's heat byte,
    nothing else."""
    try:
        record, _, value = token[len("heat:"):].partition("=")
        if len(record) != 1:
            raise ValueError
        m, v = int(record, 16), int(value, 16)
    except ValueError:
        raise ValueError(f"bad heat poke {token!r}: use heat:M=VV (hex)")
    if not (0 <= m < 8 and 0 <= v <= 0xff):
        raise ValueError(f"refused heat poke {token!r}: record 0-7, value one byte")
    return "@poke:F:%04x=%02x" % (HEAT_AT + m, v)


def original_token(k):
    """A case's key as the original's script has it: flag: is a poke, and
    orig: marks input that only the original asks for (the training quiz,
    which the rebuild does not show: docs/combat.md)."""
    if k.startswith("flag:"):
        return flag_poke(k)
    if k.startswith("mech:"):
        return mech_poke(k)
    if k.startswith("char:"):
        return char_poke(k)
    if k.startswith("cbills:"):
        return cbills_poke(k)
    if k.startswith("random:"):
        return random_poke(k)
    if k.startswith("heat:"):
        return heat_poke(k)
    if k.startswith("orig:"):
        return k[len("orig:"):]
    if k.startswith("arm:"):
        return "@" + k
    return k


def expand(keys):
    out = []
    for token in keys.replace(",", " ").split():
        name, _, count = token.partition("*")
        out += [name] * (int(count) if count else 1)
    return out


def chr_script(cases, upto):
    """The chr script for the case at index `upto`: every item so far."""
    items = []
    in_town = False
    passes_after = None
    for case in cases[:upto + 1]:
        for k in expand(case["keys"]):
            if k.startswith("orig:"):
                continue  # input only the original asks for (docs/reference.md)
            if k.startswith("arm:"):
                passes_after = int(k[len("arm:"):]) - 1  # for the next key only
                continue
            if k.startswith("flag:"):
                items.append("set:" + k[len("flag:"):])
                continue
            if k.startswith(("mech:", "char:", "random:", "heat:")):
                items.append(k)  # applied as the next key is read; no passes of its own
                continue
            items.append(k)
            if case.get("starts_town"):
                # The answer to the first-time question: the town begins.
                items.append("idle:%d" % FIRST_PASSES)
                in_town = True
            elif in_town and not k.startswith("idle:"):
                n = PASSES_AFTER_KEY if passes_after is None else passes_after
                passes_after = None
                if n > 0:
                    items.append("idle:%d" % n)
    return " ".join(items) or "idle:1"


def original_script(cases):
    tokens = [START]
    for case in cases:
        if case.get("keys"):
            tokens.append(" ".join(original_token(k) for k in case["keys"].replace(",", " ").split()))
        tokens.append("@snap:" + case["name"])
    return " ".join(tokens)


def disk_prompt_case(ref_dir, cases):
    """The first case whose capture shows the original in its disk prompt,
    or None."""
    for case in cases:
        path = os.path.join(ref_dir, case["name"] + ".bin")
        try:
            if os.path.exists(path) and memory.Memory(path).disk_prompt():
                return case["name"]
        except ValueError:
            continue  # a capture without memory (before the game's town)
    return None


# The saved game's parts by where they are in the file (docs/formats/save.md):
# a difference is reported by part, with its count of bytes, not its values.
SAVE_PARTS = [(0, "the mark"), (1 + 0xc614 - 0xc614, "the character records"),
              (1 + 0xc724 - 0xc614, "the 'Mech records"), (1 + 0xcb0c - 0xc614, "the seen map"),
              (1 + 0xd30c - 0xc614, "the flags"), (1 + 0xd370 - 0xc614, "C-Bills"),
              (1 + 0xd374 - 0xc614, "the investments"), (1 + 0xd380 - 0xc614, "F:d380-d38f"),
              (1 + 0xd390 - 0xc614, "the townspeople's records"), (1 + 0xd450 - 0xc614, "the kits"),
              (1 + 0xd452 - 0xc614, "the held 'Mechs"), (1 + 0xd456 - 0xc614, "the next name"),
              (1 + 0xd457 - 0xc614, "the marks"), (1 + 0xd557 - 0xc614, "the marks' next slot"),
              (1 + 0xf44, "x"), (3 + 0xf44, "y"), (5 + 0xf44, "past the end")]


def save_differences(ref, ours):
    """Each part of a saved game where the two files differ, as a line."""
    if len(ref) != len(ours):
        return ["the files are %d and %d bytes" % (len(ref), len(ours))]
    lines = []
    for (start, what), (end, _) in zip(SAVE_PARTS, SAVE_PARTS[1:]):
        offsets = [k for k in range(start, end) if ref[k] != ours[k]]
        if offsets:
            lines.append("%s: %d bytes, the first at file offset 0x%x" % (what, len(offsets), offsets[0]))
    return lines


def run_original(a, sequence, cases, ref_dir, seed, say):
    """One run of the original through `cases` into `ref_dir`; False (with
    the reason said) when it did not give every capture."""
    timeout = LONG_SEQUENCES.get(sequence, 2400 if sequence in CAMPAIGN else 900 if a.full else 240)
    for attempt in (1, 2):
        log_to = os.path.join(ref_dir, "run")
        why = []
        finished, _ = run.run(a.game_dir, original_script(cases), ref_dir, timeout=timeout, log_to=log_to,
                              seed=seed, stall=STALL_SECONDS, why=why)
        if not finished:
            took = len([f for f in os.listdir(ref_dir) if f.endswith(".bin")])
            if why == ["stalled"]:
                say(f"rig error: the original stalled in the {sequence} script: no new capture for "
                    f"{STALL_SECONDS} s after {took} of {len(cases)}; DOSBox-X stopped (log in {log_to}-dosbox.log)")
            elif why == ["timeout"]:
                say(f"rig error: the {sequence} script ran past its {timeout} s limit after {took} of "
                    f"{len(cases)} captures; DOSBox-X stopped (log in {log_to}-dosbox.log)")
            else:
                say(f"the original did not reach the end of the {sequence} script")
            return False
        stuck = disk_prompt_case(ref_dir, cases)
        if stuck is None:
            for end in ("-dosbox.log", "-files.txt"):
                os.remove(log_to + end)
            return True
        # A file the original could not read on the host (the rig, not the
        # game: docs/reference.md): its captures from there on are the prompt.
        kept = os.path.join(ref_dir, f"rig-error-{attempt}")
        for end in ("-dosbox.log", "-files.txt"):
            os.replace(log_to + end, kept + end)
        say(f"rig error: the original showed its disk prompt at {stuck} in the {sequence} script "
            f"(DOSBox-X's log and the drive's files in {kept}-*)")
        if attempt == 2:
            say(f"rig error: the same on the second try; re-run the {sequence} sequence")
            return False
        say(f"rig error: running the {sequence} script once more")


def compare_case(a, cases, i, ref_dir, chr_options, seed, out_dir):
    """chr through the case at index `i`, compared with the original's
    capture; its screenshot, diff and saves go in `out_dir`. (True if every
    required rectangle and the state match, the report's lines.)"""
    report = []
    say = report.append
    case = cases[i]
    name = case["name"]
    shot = os.path.join(out_dir, name + ".bmp")
    # The cell window too, built where the original last built its own.
    at, ref_window = window.original_window(os.path.join(ref_dir, name + ".bin"))
    # Each run saves into its own folder, never the data folder's.
    saves = os.path.join(out_dir, name + "-saves")
    shutil.rmtree(saves, ignore_errors=True)
    os.makedirs(saves)
    for source, file in seed:
        shutil.copyfile(source, os.path.join(saves, file))
    env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy")
    out = subprocess.run([a.chr, "--game-dir", a.game_dir, "--data-dir", a.data_dir,
                          "--save-dir", saves,
                          "--script", chr_script(cases, i), "--screenshot", shot,
                          "--fight-state" if case.get("fight") else "--state",
                          "--window", "%x,%x" % at, *chr_options],
                         check=True, env=env, stdout=subprocess.PIPE, text=True).stdout
    ours_state = [line for line in out.splitlines() if line.startswith(("state ", "window "))]
    ref_state = state.original_state(os.path.join(ref_dir, name + ".bin"), fight=case.get("fight", False))
    ref_state += ref_window
    ref = compare.reference_pixels(os.path.join(ref_dir, name + ".bin"))
    ours = compare.bmp_pixels(shot)
    compare.write_diff(os.path.join(out_dir, name + "-diff.png"), ref, ours)
    ok = True
    say(f"== {name}")
    for region in ("panel", "play"):
        n = len(compare.compare(ref, ours, region))
        say(f"  {region}: {n} pixels differ")
    for label, rect in case["match"].items():
        n = len(compare.compare_rect(ref, ours, rect))
        say(f"  {'ok  ' if n == 0 else 'FAIL'} {label}: {n} pixels differ")
        ok = ok and n == 0
    if not case.get("state", True):
        ref_state = ours_state = []
    differ = [f"original: {r}\n         chr: {c}" for r, c in zip(ref_state, ours_state) if r != c]
    if len(ref_state) != len(ours_state):
        differ.append("different number of state lines")
    say(f"  {'ok  ' if not differ else 'FAIL'} the game state: {len(differ)} lines differ")
    for d in differ:
        say("    " + d)
    ok = ok and not differ
    if "saved" in case:
        # The file the original wrote, byte for byte (docs/formats/save.md).
        game = "Game%d" % (case["saved"] + 1)
        ref_file, ours_file = os.path.join(ref_dir, game), os.path.join(saves, game)
        if not os.path.exists(ref_file) or not os.path.exists(ours_file):
            say(f"  FAIL the saved game {game}: missing from "
                + ("the original" if not os.path.exists(ref_file) else "chr"))
            ok = False
        else:
            bad = save_differences(open(ref_file, "rb").read(), open(ours_file, "rb").read())
            say(f"  {'ok  ' if not bad else 'FAIL'} the saved game {game}: {len(bad)} parts differ")
            for d in bad:
                say("    " + d)
            ok = ok and not bad
    for known in case.get("known", []):
        say(f"  known: {known}")
    return ok, report


def run_sequence(a, sequence, cases, chr_options=(), seed=()):
    """The original's captures through `cases` (from the cache, or one run
    of the original), then chr for each case, both with the `seed` files
    ((source, name) pairs) on their drives from the start.
    (True if every required rectangle and the state match, the report.)"""
    report = []
    say = report.append
    for source, _ in seed:
        if not os.path.exists(source):
            say(f"the {sequence} sequence needs {source}, which its first sequence did not write")
            return False, report
    ref_dir = os.path.join(a.work, "original-" + sequence)
    key = a.cache.key(original_script(cases), seed) if a.cache else None
    if key and not a.refresh and a.cache.fetch(key, ref_dir):
        say(f"-- {sequence}: the original's captures from the cache")
    else:
        # Never into a folder filled from the cache: its files are the cache's own.
        shutil.rmtree(ref_dir, ignore_errors=True)
        start = time.time()
        with a.dosbox:
            if not run_original(a, sequence, cases, ref_dir, seed, say):
                return False, report
        took = time.time() - start
        if key:
            a.cache.store(key, ref_dir, sequence, took)
        say(f"-- {sequence}: the original ran ({took / 60:.1f} min)" + ("; cached" if key else ""))

    # Each case is its own chr run, so the cases are compared side by side.
    # A case another sequence also compares, under the same name, writes
    # into a folder of this sequence's own (main()).
    compared = [i for i, case in enumerate(cases) if case.get("compare", True)]  # the rest: by another sequence
    ok = True
    for passed, lines in a.case_pool.map(
            lambda i: compare_case(a, cases, i, ref_dir, chr_options, seed,
                                   a.outputs.get((sequence, cases[i]["name"]), a.work)),
            compared):
        report += lines
        ok = ok and passed
    return ok, report


# --affected: the sequences that exercise each part of the rebuild, by path
# (fnmatch, first match wins). Coarse on purpose: a file not listed, or a
# change outside these folders, runs everything; docs and unit tests run
# nothing. ALL is every sequence of the --full and --campaign tiers.
ALL = "all"
_NO_FIGHT = {"town", "quiz", "settings", "shops", "comstar"}
_FIGHTS = {sequence for sequence, *_ in SEQUENCES} - _NO_FIGHT - KNOWN_OPEN
AFFECTED = [
    ("docs/*", set()), ("*.md", set()), ("tests/*", set()), (".gitignore", set()),
    ("src/game/training_quiz.*", {"quiz"}), ("src/platform/quiz_layer.*", {"quiz"}),
    ("src/platform/stats_layer.*", {"inspect", "chain_inspect", "detail_poked", "detail_enemy"}),
    ("src/game/cache.cpp", {"chain_cache_poked", "chain_cache_open", "chain_finale"}),
    ("src/game/save.*", {"chain_save", "save_startup", "settings"}),
    ("src/game/fight.*", _FIGHTS), ("src/game/combat.*", _FIGHTS), ("src/game/figures.*", _FIGHTS),
    ("src/game/movement.*", _FIGHTS), ("src/game/overhead.*", _FIGHTS), ("src/game/terrain.*", _FIGHTS),
    ("src/platform/overhead_layer.*", _FIGHTS),
]


def changed_files(base):
    """The repo's files changed since its merge-base with `base`, with the
    working tree's changes and new files."""
    root = os.path.dirname(os.path.dirname(HERE))

    def git(*args):
        return subprocess.run(["git", "-C", root, *args], check=True, stdout=subprocess.PIPE,
                              text=True).stdout.split("\n")

    since = git("merge-base", "HEAD", base)[0]
    files = git("diff", "--name-only", since) + git("ls-files", "--others", "--exclude-standard")
    return sorted({f for f in files if f})


def affected_sequences(files):
    """The sequences the changed `files` need, or ALL."""
    chosen = set()
    for path in files:
        for pattern, sequences in AFFECTED:
            if fnmatch.fnmatch(path, pattern):
                chosen |= sequences
                break
        else:
            return ALL
    return chosen


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--chr", required=True)
    ap.add_argument("--game-dir", required=True)
    ap.add_argument("--data-dir", required=True)
    ap.add_argument("--work", required=True, help="where the captures, screenshots and diffs go")
    ap.add_argument("--full", action="store_true", help="the long cases too")
    ap.add_argument("--campaign", action="store_true",
                    help="only the campaign sequences (the long unpoked run), all their cases")
    ap.add_argument("--sequence", action="append", help="run only this sequence (may be repeated)")
    ap.add_argument("--only", nargs="+", metavar="NAME",
                    help="only these sequences, all their cases (a KNOWN_OPEN one too)")
    ap.add_argument("--affected", nargs="?", const="main", metavar="BASE",
                    help="only the sequences the files changed since BASE (default main) exercise, "
                         "all their cases (AFFECTED)")
    ap.add_argument("--jobs", type=int, default=4,
                    help="at most this many runs of the original (DOSBox-X) at once (default 4)")
    ap.add_argument("--cache", help="the original's captures kept between runs (cache.py); "
                                    "default: $CHR_REFERENCE_CACHE, else reference-cache beside --chr")
    ap.add_argument("--no-cache", action="store_true", help="always run the original, keep nothing")
    ap.add_argument("--refresh", action="store_true", help="run the original even when cached, and re-cache")
    ap.add_argument("--prune-cache", action="store_true",
                    help="first drop the cache's entries from another rig and the files no entry uses")
    a = ap.parse_args()
    run.stop_on_signals()  # a stopped run leaves no DOSBox-X (or its quit dialog)

    names = {sequence for sequence, *_ in SEQUENCES}
    unknown = sorted(set(a.only or []) - names)
    if unknown:
        ap.error("no such sequence: " + ", ".join(unknown))

    missing = [what for what, ok in (("DOSBox-X", os.path.exists(run.DOSBOX)),
                                     ("nasm", shutil.which("nasm")),
                                     ("the game folder", os.path.exists(os.path.join(a.game_dir, "BTECH.EXE"))))
               if not ok]
    if missing:
        print("skipped: missing " + ", ".join(missing))
        return SKIP

    os.makedirs(a.work, exist_ok=True)
    a.cache = None if a.no_cache else cache.Cache(
        a.cache or os.environ.get("CHR_REFERENCE_CACHE")
        or os.path.join(os.path.dirname(os.path.abspath(a.chr)), "reference-cache"), a.game_dir)
    if a.prune_cache and a.cache:
        dropped, files, freed = a.cache.prune()
        print(f"cache: dropped {dropped} entries from another rig and {files} unused files ({freed >> 20} MB)")

    # The sequences are separate runs of the original, each in its own
    # copy of the game folder (run.py), so they run side by side, --jobs
    # at a time.
    # The campaign sequences run only with --campaign (or by name): they
    # would make the everyday --full run too long.
    every = {sequence for sequence in names if sequence not in KNOWN_OPEN}
    whole = bool(a.only or a.affected)  # every case of the chosen sequences
    if a.only:
        chosen = set(a.only)
    elif a.affected:
        files = changed_files(a.affected)
        chosen = affected_sequences(files)
        chosen = every if chosen == ALL else chosen
        print(f"affected: {len(files)} files changed since {a.affected}: "
              + (", ".join(sorted(chosen)) if chosen else "no sequence"), flush=True)
    elif a.sequence:
        chosen = set(a.sequence)
    else:
        chosen = {sequence for sequence in every if (sequence in CAMPAIGN) == a.campaign}
    chosen |= {SEEDED[s][0] for s in chosen if s in SEEDED}
    if whole:
        a.full = True  # their time limits too
    # SEQUENCES lists each seeded sequence after its first, so the first is
    # always submitted (and given a worker) before the one that waits on it.
    jobs = [(sequence, quick + (full if a.full or a.campaign else []), chr_options)
            for sequence, quick, full, chr_options in SEQUENCES if sequence in chosen]
    if not jobs:
        return 0
    # Two sequences may compare a case of the same name (training's first
    # cases are quiz's too), and they run at once: the first in SEQUENCES
    # keeps the work folder, a later one writes in work/SEQUENCE.
    a.outputs, claimed = {}, set()
    for sequence, cases, _ in jobs:
        for case in cases:
            if case.get("compare", True):
                if case["name"] in claimed:
                    a.outputs[(sequence, case["name"])] = os.path.join(a.work, sequence)
                    os.makedirs(os.path.join(a.work, sequence), exist_ok=True)
                claimed.add(case["name"])
    # DOSBox-X runs are limited to --jobs; a sequence from the cache needs
    # none, so the sequences (and their cases, each one chr run) share the
    # machine's processors.
    cpus = os.cpu_count() or 4
    a.dosbox = threading.BoundedSemaphore(max(1, a.jobs))
    ok = True
    with concurrent.futures.ThreadPoolExecutor(max_workers=cpus) as case_pool, \
            concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(max(a.jobs, cpus), len(jobs)))) as pool:
        a.case_pool = case_pool
        futures = {}

        def seeded(sequence, cases, chr_options):
            first, capture, name = SEEDED[sequence]
            futures[first].result()
            source = os.path.join(a.work, "original-" + first, capture)
            return run_sequence(a, sequence, cases, chr_options, seed=[(source, name)])

        for sequence, cases, chr_options in jobs:
            futures[sequence] = (pool.submit(seeded, sequence, cases, chr_options) if sequence in SEEDED
                                 else pool.submit(run_sequence, a, sequence, cases, chr_options))
        results = list(futures.values())
        for result in results:
            passed, report = result.result()
            print("\n".join(report), flush=True)
            ok = ok and passed
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
