Notes For V3.3-1

1. New Features in 3.3-1

1.1 H316

TTY	- implemented paper-tape reader and punch
	- added ASCII file support

PTR,PTP	- added ASCII file support

1.2 HP2100

CPU	- added SET CPU 21MX-M, 21MX-E (from Dave Brian)
	- disabled TIMER/EXECUTE/DIAG instructions for 21MX-M (from Dave Bryan)
	- added post-processor to maintain T/M consistency (from Dave Bryan)

DS	- released 13037 disk controller

1.3 Interdata

MT	- added read-only file support

1.4 SDS

MT	- added read-only file support

1.5 PDP-11

TM,TS	- added read-only file support

2. Bugs Fixed in 3.3

2.1 H316

CPU	- fixed bug in divide

LPT	- fixed bug in DMA/DMC support

MT	- fixed bug in DMA/DMC support

DP	- fixed bug in skip on not seeking

TTY	- fixed bugs in SKS '104, '504

2.2 HP2100

CPU	- fixed DMA reset to clear alternate CTL flop (from Dave Bryan)
	- fixed bug in JPY (from Dave Bryan)
	- fixed bugs in CBS, SBS, TBS
	- separate A/B from M[0/1] for DMA (found by Dave Bryan)


LPS	- added restart when set online, etc. (from Dave Bryan)
	- fixed col count for non-printing chars (from Dave Bryan)

LPT	- added restart when set online, etc. (from Dave Bryan)


2.3 PDP-11

CPU	- fixed WAIT to work in all modes (from John Dundas)

