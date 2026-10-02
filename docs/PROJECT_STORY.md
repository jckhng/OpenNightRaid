# Why I Rebuilt Night Raid

This was my pa's favorite game. His high-score table is still in the original
save: six `LY` entries, with a best score of 4,855. I wanted to keep the game
playable on modern computers without losing those scores.

I began with a small problem: pressing Space sometimes failed to fire the
cannon. The original samples fire only at fixed intervals, so a short press
between firing gates can be missed entirely.

I kept that behavior in faithful mode and fixed it in enhanced mode by
buffering new presses until the next firing gate. The firing rate stays the
same. The comparison video shows the original missing four short taps while
enhanced mode fires all four, followed by a held-fire control case.

From there, I worked through decompiled code and original DOSBox-X runs to
recover the rules, menus, scoring, resource handling, audio, and intermissions.
The native game reads original sprites and sounds directly from a
player-supplied archive; it does not run the DOS executable.

My [DOS RE Harness](https://github.com/jckhng/dos-re-harness) made that work
repeatable. Ghidra helped recover the original logic; Unicorn tested isolated
original routines without running the whole game; DOSBox-X provided the
running original for state, pixel, and audio comparisons. These tools are
separate from the game and are not required to play it.

Enhanced mode shares the simulation, while adding gamepad controls, smoother
presentation, lighting, screen shake, and persistent cosmetic debris. Its
input fixes are deliberate differences, not something I claim is identical
to the original. Known timing/audio limits remain documented.

The screenshot shows my pa's original high-score table in DOS. I have kept the
raw save private and unchanged.
