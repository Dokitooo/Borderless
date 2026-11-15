Borderless 1.0.1 - NBA 2K14: Video Settings > Window Mode = Windowed / Fullscreen / Borderless

SLAM:  copy SLAM\Borderless\ (asi + ini) into the game's SLAM folder.
plain: plain\Borderless.asi next to your other .asi files (any ASI loader),
       plus plain\Borderless\ (the ini) next to it.

Pick the mode in Video Settings; it is applied (and saved) when the game asks
"Video settings have been changed. Do you want to apply the new settings?" -> Yes.
The mode you picked sticks: if the game (or a crash / restart) tries to switch the window back
to windowed, Borderless puts it back.
Also fixes the black / old blocks in windowed mode (KeepFrame=1) and makes VSync work windowed (VSyncFix=1).
Log: Borderless\Borderless.log

1.0.1: works next to ReShade / ENB (a d3d9.dll in the game folder): Borderless uses the Windows d3d9.dll
       underneath it and leaves the wrapper alone. Borderless.log says which one was used.
