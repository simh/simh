Notes For V3.10

3.10 is mostly an attempt to get aligned with the current head of the
GitHub 4.0 sources. While the core libraries and SCP have diverged too
far for real forward and backward compatibility, enough 4.0 workalikes
have been added to allow much closer convergence of the two streams.

3.10 will provide the basis for my future simulation work.


1. New Features

1.1 SCP and libraries

- -n added in ATTACH, meaning "force new file"

1.2 1401

- option to read cards from the console terminal window
- option to print line printer output in the console terminal window

1.3 1620

- tab stop support added to console terminal

1.4 PDP-8

- LOAD command now supports multi-segment binary tapes

1.5 PDP11

- added RS03/RS04 Massbus fixed head disk support
        

2. Bugs Fixed

Please see the revision history on http://simh.trailing-edge.com or
in the source module sim_rev.h.


3. Status Report

The main branch of SimH is maintained in a public repository

	https://github.com/simh/simh

under the general editorship of Dave Hittner and Mark Pizzolato. The
3.X branch provides a simpler environment for debugging, for my taste.

Because of divergences between 3.X and 4.0, certain features in 4.0
cannot work in 3.X and are not present:

PDP11/VAX/PDP10 - DUP11 and DMC11 synchronous network interfaces

PDP10 - front- end keep-alive timer

H316 - IMP network interface

In addition, the PDP11 DZ, VH, XQ, and XU implementations are significantly
"down rev" compared to 4.0, due to differences in supporting libraries.

