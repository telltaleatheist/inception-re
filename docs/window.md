# The window

The original has one screen, 320x200 (VGA and EGA mode 0Dh), and a monitor that stretches it. The
rebuild shows the same screen in a window of any size (`src/platform/renderer.cpp`). It has square
pixels: 16:10, not a monitor's 4:3.

## The picture

- The picture is as large as the window allows at 16:10, in the middle (`picture_rect`). Up to 3
  pixels of rounding are filled, not left as a bar.
- A frame is drawn off the window, at a whole number of pixels for each screen unit: the smallest
  that isn't below the window's (`frame_scale`; less if the renderer's largest texture is smaller).
  Everything the layers draw, and the XOR box's reading back, therefore meets a whole scale. SDL's
  software renderer cuts a fractional rectangle's position and size separately, so tiles drawn
  straight to the window at such a scale have gaps between them.
- The frame is then scaled once to the window, nearest-neighbour. Nothing is smoothed. A window
  pixel shows the screen pixel its middle falls in, as if a 320x200 screen had been scaled, because the
  frame is a whole multiple of one. Where the middle falls on the edge of two, the scaler may take
  either (the checks allow a twentieth of a window pixel).
- At a size that isn't a whole multiple, some screen pixels are one window pixel wider than others.
  The window has the display's own pixels (`SDL_WINDOW_HIGH_PIXEL_DENSITY`), so on a dense display
  that is hard to see.
- A mod's finer art is drawn into the frame at its own resolution, up to the frame's. The XOR box
  works on the frame's pixels, so it doesn't coarsen it.
- A screenshot is the frame at 3 pixels a unit, 960x600, whatever the window or the display.

- A frame that can't be drawn (a lost device, a refused target) is skipped, the error is said once, and
  the next frame starts again from nothing. The game goes on. Only a screenshot run ends with it. A
  window with no pixels (minimised) is not an error. A picture whose texture is refused is left out,
  said once the same way, and asked for again when it is next drawn.
- When SDL says the device has been reset (`SDL_EVENT_RENDER_DEVICE_RESET`), every texture is gone:
  they are all dropped, and each is made again from the provider's picture when it is next drawn.
  From SDL 3.4.12's source: Direct3D 11 and 12 and Vulkan say it (the driver was updated or
  restarted, the GPU removed), after a present that failed; Direct3D 9 keeps its textures and says
  only that the targets' contents are gone, and the frame is drawn from nothing each time anyway.
  `SDL_EVENT_RENDER_DEVICE_LOST`, a device that can't be made again, leaves the window as it is:
  nothing more is drawn. No one has seen any of these on a real GPU; the checks send the event
  (`rendertests`, and `chr --loop-resets` in `keep_places`).

## The window's shape

The window is held to 16:10 while it is resized (`SDL_SetWindowAspectRatio`), and is at least
320x200. Where the shape can't be 16:10, bars remain at two sides. From SDL 3.4.12's source, and not
tried on a screen:

| System | While resized | Maximised, tiled, full screen |
|---|---|---|
| macOS | AppKit holds it (`contentAspectRatio`) | SDL adds nothing: AppKit decides whether the shape or the space wins |
| Windows | held (`WM_SIZING`) | not held: bars at two sides |
| X11 | a hint, for the window manager to follow or not | the same |
| Wayland | held, but the compositor's maximised and tiled sizes are limits | as while resized |

Whichever of those happens, the picture follows the window: only the two bars can differ.

## Not built

A switch for whole-number scaling only. It would be one setting, and `picture_rect` rounding down to a
multiple of 320x200. It would bring the bars back at most sizes, which is the complaint this answers,
and on a dense display the uneven pixels it removes are one part in six or more.

## How it is checked

`rendertests` (tests/test_renderer.cpp) draws with SDL's software renderer on a surface, with no
window: 18 sizes, among them 1000x625, 1884x1098, twice that, and shapes that aren't 16:10. Every
window pixel must be the right pixel of the 1x frame, tiles, text, lines and XOR box alike.
