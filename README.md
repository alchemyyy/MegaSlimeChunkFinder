Has some bugs at the moment, but works well enough.

Uses AVX-512 and multithreading. Detects logical core count on host system to set # of threads.

Outputs results periodically to a text file. Tracks progress and prints it out in stdout periodically.