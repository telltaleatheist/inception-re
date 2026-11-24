# The screen's colours: a proposal

Nothing here is built. In the Amiga's cache the whole screen changes colours. The rebuild changes only
the cache's tiles (docs/formats/amiga.md, "Colours"). This page says what the Amiga program does, and
proposes how the rebuild could show it without the logic knowing a colour.

## What the Amiga does

The program sets the screen's 16 colours in one place, 0:1c4a0, the only caller of the system's call
(0:28402). It is called by absolute or relative calls at 16 places (searched by the bytes of the
call). Two of them, 0:1b574 and 0:1cfaa, are in functions nothing was found to call. The other 14:

| Called at | In | Colours |
|---|---|---|
| 0:2371e | 0:23690, the program's start | all black |
| 0:1cace, 0:1cb34 | the fades 0:1ca74 and 0:1caee, called by 0:23798 and 0:245b4 | INFOCOM.CMP's and BTTITLE.CMP's |
| 0:227c4, 0:228b6 | 0:2271e, after the title: a game, or the demo | the game's (1:1c1e) |
| 0:21bd8 | 0:218e2, the load, at its end | the game's, unless flag 3a is set. Also when the load was refused or failed |
| 0:034d2, 0:0355e | 0:02c4a, the stats screen: every 16th pass, and on leaving | the stats screen's (1:07a0, the same 16 words as the game's) |
| 0:0af30, 0:0af6c | 0:0ae12, going into the cache (DOS `1b5d:0004`), also by a load | black before the view is drawn, the cache's after it |
| 0:0b0fa, 0:0b136 | 0:0b000, the way up (DOS `1b5d:079c`) | black, then the cache's |
| 0:19694 | 0:17e60, special 45, after ENDMECH.CMP and STARLEAG.ICN loaded again | the cache's |
| 0:1b4aa | 0:1b3f8, the file loader, when a file can't be opened | two colours for its message |

"The cache's" are STARLEAG.ICN's 16 words, the file loaded last each time. They are ENDMECH.CMP's too, and
differ from the game's in 9 of the 16. The way down sets nothing: MAP.ICN's own words are read and not used.

So, apart from the black moments, the cache's colours go on when the party goes in, and stay until
something puts the game's back: the title, a load of a game saved outside (or a load refused or failed outside),
or the stats screen.

**Colours left on.** A new game from the game-over question (called at 0:008e0, in the main loop) is the same
function as the title's, 0:1c9c (DOS `1000:4dc7`), without the call before it that sets the colours. It clears
flag 3a with the other flags and sets no colours. So a game won in the cache and the question answered with
a new game should start in the town with the cache's colours, until one of the three above. This is read
from the code, not seen: no Amiga copy has been run to it. It looks like a bug of the Amiga version.

The stats screen would do the opposite, opened in the cache: it would leave the game's colours there.
It needs a 'Mech, and all four of the party's records are put aside on going in (docs/formats/save.md). That
it can't be opened there some other way is inferred, not tested.

## What DOS does

Nothing to show: DOS has one set of colours but for the stats screen's pulse and ENDMECH.CMP's own
(docs/formats/cmp.md), both rebuilt. Whatever is built here must leave every DOS screen as it is.

## The proposal

**1. A state, not an event.** docs/versions.md first proposed an event on entering and leaving. The colours
are better said like the rest of the screen, as what is so now:

- The logic has two names for the colours in force, the game's and the cache's (`ScreenColours`), and
  no colour values. `Town` keeps the one for now and sets it where the Amiga does:

| Where | To |
|---|---|
| a game's or the demo's start after the title | the game's |
| the load's end, with flag 3a clear, whatever came of the load | the game's |
| leaving the stats screen | the game's |
| `Town::enter_cache`, after the view is drawn and before `hold_screen(60)` | the cache's |
| `Town::cache_way_up`, and special 45 after its key | the cache's (it is already) |

- `Shown` carries the one that was in force when its screen was recorded. A turn's recorded screens
  can end with the cache's first (`hold_screen(60)`) after others from before it.
- A replayed town (docs/resuming.md) sets it again on the way. It is not in a save: the load sets it.

Every copy keeps it, DOS too. That is why it isn't an Amiga rule: the logic says which colours are
in force, and the copy says whether they look different.

It could be worked out from flag 3a instead, with nothing kept. The two agree but for the colours left on
after a game-over's new game (and the stats screen in the cache, if it can be had). Working it out would remove
that oddity without a word, and the rebuild keeps the original's bugs by default, so the kept one is proposed.
A fix switch could then set the game's colours in the new game too.

**2. The colours come from the asset provider.** {"COLOUR", n} stays the game's colour n (0-15).
{"COLOUR", 16 + n} is the cache's colour n:

- `OriginalAssets` answers 16-31 only for a copy whose cache has its own colours (the Amiga's, from
  `GameSource::picture_palette("STARLEAG.ICN")`), and has none for DOS.
- Without it, the cache's colour n is {"COLOUR", n}, asked of every provider. So a mod that changes
  colour 3 changes it in a DOS cache too, and a mod may give the cache its own colours for any copy.

**3. The renderer colours by number.** Every original picture but the font's glyphs comes with its colour
numbers (`Image::numbers`), and the glyphs are drawn in the text's colours. So the renderer can draw any
of them in either set, as `draw_stats` already does for its picture:

- `main.cpp` works out both sets once. Before drawing a screen it tells the renderer which is in force
  (`Shown`'s for a recorded screen, `Town`'s otherwise), and passes that set to the `draw_*` that take a
  palette (the text, the bars, the boxes, the shots, the overhead map).
- The renderer's texture cache is keyed by the picture and the set. A picture with numbers is
  recoloured for a set that differs from the one it was decoded with. One without numbers (a mod's new art)
  is drawn as it is.
- A picture with a palette of its own is never recoloured: the two start-up pictures, and ENDMECH.CMP.
  The provider marks it, since the renderer can't tell. (Proposed: a flag in `Image`.)
- The cache's tiles lose their special case: decoded with the game's colours like all tiles, they
  get the cache's from the set. The stats screen keeps its own.

`Renderer::xor_colours` finds a pixel's number by its colour. Both the game's and the cache's Amiga words have
two numbers with one colour, so for those it can take the wrong one, as it can now. The boxes over the map
are drawn by number where the numbers are known (`draw_xor_box`).

## What it wouldn't do

- The black moments. They last while the view is drawn, with no wait of their own, so how long they show
  depends on the machine.
- The fades, and the loader's message for a missing file.
- The XORs' look on the Amiga, which is another open item (docs/formats/amiga.md, "Colours").

## How it would be checked

- The whole reference set, DOS: every comparison as before. No colour may change, the cache
  sequences' (`chain_cache_*`, `chain_finale`) above all.
- A unit test of the state: a game's start, into the cache, a load out of it and into it, a new game from the
  game-over question, and a recorded screen from before the cache beside one from inside.
- A renderer test with made-up pictures: with numbers, without them, with a palette of its own, under both sets.
- An Amiga copy, headless: a screen in the cache holds no colour outside the cache's 16, and one in a town
  none outside the game's. A run with the two sets swapped must fail it.
- No one has compared the result with a real Amiga. A person's look is still needed.

## To be decided

- Whether the colours left on after a game-over's new game are kept (as proposed, with a fix switch) or not.
  On a DOS copy nothing shows either way.
- Whether it is worth building before an Amiga copy has been played in the cache.

## The size of it

The logic's part is one member of `Town` with six places that set it, and one of `Shown`. The
provider's is the 16 new colours and the flag. The renderer's, recolouring and its cache's key, is most of the work. Nothing is
imported anew, saves don't change, and no table or text is added.
