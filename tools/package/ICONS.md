# The package icon

`chi.png` (512x512), `chi.ico` (16 to 256) and `chi.icns` (16 to 512, at 1x
and 2x) are one picture: a hawk's head inside a crescent, on a riveted steel
and crimson patch. It is this project's own art and contains nothing from the
game.

- Made 2026-11-19 with Qwen-Image 2.1 (`qwen-image-2.1`), 768x768, 40 steps,
  seed 32.
- Prompt: "A round military unit patch badge: the head of a hawk in side
  profile facing right, sharp hooked beak, fierce eye, nested inside the curve
  of a thin crescent moon, embossed brushed steel with deep crimson enamel
  inlays, inside a thick circular brushed steel rim with small rivets around
  it, symmetrical, front view, centred on a pure black background, no text, no
  letters."
- The black surround was made transparent (near-black, 28 or less, flooded in
  from the edges; MinFilter 3, blur 1.2), the picture cropped square with a 4%
  margin, and each size resized with Lanczos. The .icns was made with
  iconutil.

Where each is used:

- `chi.icns`: the Mac app (`CFBundleIconFile`).
- `chi.ico`: chi.exe's icon resource (CMakeLists.txt) and Setup.exe
  (`SetupIconFile` in installer.iss).
- `chi.png`: the AppImage's desktop entry (`chi.desktop`, `Icon=chi`).
