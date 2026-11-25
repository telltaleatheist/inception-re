# Stopping and resuming the logic

How the game logic waits for the player, now and as planned. Nothing here is built yet beyond
the replay.

## Now: replay

The logic is plain C++ that runs straight through. Wherever the original reads the keyboard,
it asks a `KeySource` (game/town.h). The `next()` call is a question that waits; `poll()` is
the original's "is a key waiting?" (273d:002f), as in a computer-fought fight's once-a-round
check (203b:1015). When the keys run out at a question, the town stops there, and
`question_open()` (or `place_open()`, `start_up_open()`) says so. On the player's next key,
`main.cpp` builds a new `Town` and plays every key since the start again, with the new one at
the end (`replay`).

That keeps the logic one function per original function, in the original's order, and makes
the rig's runs and a player's run the same code. The cost grows with play: a whole campaign is
thousands of keys and many fights, all played again on every key.

**What a replay must not do again.** A replay is the sitting once more, so nothing in it may act outside
the `Town` twice:

- The saves. The `Town` has a `SaveStore` (game/save_store.h) and touches no file. The platform's
  `SaveJournal` (platform/save_files.h) lists the sitting's reads and writes in order. The nth is done the
  first time, and after that answered with what it answered then: a file found or not, a write that
  worked or failed. A file another program replaces is what the next new load reads. The journal lasts as
  long as the keys, the whole run of the program: a new game from the game-over question is in them too.
  A replay that asks for something else isn't the player's game: that action and every later write fail, and
  stderr says so. In 1.0.0 a replay read and wrote the files themselves, so a slot loaded and then
  saved over in one sitting took the walk between again with every key at a question.
- A sound, by the events already sounded (main.cpp); the title's tune, by whether it is on.
- The log goes to stdout after a script's one run. The game's files are only read. The logic reads no clock,
  and its random numbers start the same.

## Where the logic must stop

At every point the original reads input:
- a question's key (menus, yes/no, a press-a-key wait, the names);
- the fight's once-a-round poll (203b:1015). A key pressed during a computer-fought fight
  hands the fight back to the player, so the poll must see keys pressed while the
  previous round was shown;
- the pause after a shot (1e31:1dcc) when the message speed DG:015e is 5 or more: it waits for
  a key (273d:0259). At the starting speed, 2, it waits 24 retraces and reads no keys;
- the main loop's idle pass, where the town ticks on its own.

The waits that read no keys (273d:0006's retraces, the place pictures, the shot's poses,
missiles and explosions) are not stops. The logic records them as presentation events (draw
the view, a pose's frame, a shot's line, a sound, wait n retraces). At each stop the platform
plays the events so far at its own pace. Then it hands the logic whatever the player pressed
meanwhile and resumes it. A replay with the same keys gives the same events, so the rig can
ignore them.

## Options for resuming

1. **C++20 coroutines.** Every function that can reach a stop becomes a coroutine. Its
   callers `co_await` it, and `co_await keys.next()` / `keys.poll()` suspend at a stop. The
   call chain keeps its shape: `Town::run`, `turn`, the place scripts, `fight_rounds`,
   `Combat::round`, `fire`. The code still reads like the original's control flow, one
   function per original function. What changes: return types (a small `Task<T>`), each call
   on the way to a stop gains a `co_await`, and locals live in coroutine frames while
   suspended.
2. **An explicit state machine.** Each function that waits is split at its stops, and its
   locals become members: the fight's round, phase, figure and slot; a menu's bar; the place
   script's program counter. Easy to inspect and save at any stop, but
   the deep chains (a place script calling a fight calling a menu) become hand-written
   machines, far from the original's shape.
3. **A thread for the logic**, blocking on a channel at each stop. The smallest change to the
   code, but two threads share the game state, and a save would have to stop the thread at a
   known point.

The preference is (1). It keeps goal 1's "one function per original function" and needs no
rewrite of the rules, only of their signatures.

## Saved games

With (1), a save is taken only at a stop where no coroutine frame holds game state: the town's
main loop waiting for its next key (open question 1, answered). Everything there
lives in `Town` members, so a save writes those, and a load builds a `Town` from it. No frame
is restored. With (2), any stop could be saved, but only the town's matches the original.

The replay stays as a test: a run resumed stop by stop must match one played from the start
with the same keys, event for event and state for state.

## Open questions

1. (Answered.) Where the original saves a game: only from the command menu (`1000:2c50` calls
   the save, `1000:35d3`, at 1000:2d86), and the menu only from the main loop's Space key
   (1000:0108). So a save is always taken at the main loop, with nothing of a fight or a place
   under way, as this note assumed.
