Has some bugs at the moment (might miss stuff and might "hallucinate" slightly), but works well enough.

The generation algo is 32-bit so we can get 16-bit AVX512. Not all calculations are 32 bit either, but its partial and I'll need to sit down and dig deeper into how overflows are leveraged, but it may be possible to crank this a bit if we get fancy.

Simple scanning solution right now where a region is built then run through the rectangle histogram algo. This is done lazily at the moment and it deals with edge cases by adding padding-overlap. Adding a subroutine for "looking into" valid lengths along edges would skip a lot of this.

Detects logical core count on host system to set # of threads.

Outputs results periodically to a text file. Tracks progress and prints it out in stdout periodically.

