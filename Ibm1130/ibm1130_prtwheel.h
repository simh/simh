/*
 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to sim@ibm1130.org
 */

struct tag_codewheel {
	unsigned char ascii;
	unsigned char ebcdic;
};

static struct tag_codewheel codewheel1132[] =
{										/* characters and EBCDIC codes in printwheel order */
	{'A',	0xC1},
	{'B',	0xC2},
	{'C',	0xC3},
	{'D',	0xC4},
	{'F',	0xC6},
	{'H',	0xC8},
	{'I',	0xC9},
	{'S',	0xE2},
	{'T',	0xE3},
	{'U',	0xE4},
	{'V',	0xE5},
	{'1',	0xF1},
	{'2',	0xF2},
	{'3',	0xF3},
	{'4',	0xF4},
	{'5',	0xF5},
	{'6',	0xF6},
	{'7',	0xF7},
	{'8',	0xF8},
	{'9',	0xF9},
	{'0',	0xF0},
	{'=',	0x7E},
	{'$',	0x5B},
	{'.',	0x4B},
	{'\'',	0x7D},
	{',',	0x6B},
	{')',	0x5D},
	{'-',	0x60},
	{'(',	0x4D},
	{'+',	0x4E},
	{'/',	0x61},
	{'*',	0x5C},
	{'&',	0x50},
	{'J',	0xD1},
	{'K',	0xD2},
	{'L',	0xD3},
	{'M',	0xD4},
	{'N',	0xD5},
	{'O',	0xD6},
	{'P',	0xD7},
	{'Q',	0xD8},
	{'R',	0xD9},
	{'E',	0xC5},
	{'G',	0xC7},
	{'W',	0xE6},
	{'X',	0xE7},
	{'Y',	0xE8},
	{'Z',	0xE9},
};

#define WHEELCHARS_1132 (sizeof(codewheel1132)/sizeof(codewheel1132[0]))

static struct tag_codewheel codewheel1403[] =
{
	{'A',	0x64},
	{'B',	0x25},
	{'C',	0x26},
	{'D',	0x67},
	{'E',	0x68},
	{'F',	0x29},
	{'G',	0x2A},
	{'H',	0x6B},
	{'I',	0x2C},
	{'J',	0x58},
	{'K',	0x19},
	{'L',	0x1A},
	{'M',	0x5B},
	{'N',	0x1C},
	{'O',	0x5D},
	{'P',	0x5E},
	{'Q',	0x1F},
	{'R',	0x20},
	{'S',	0x0D},
	{'T',	0x0E},
	{'U',	0x4F},
	{'V',	0x10},
	{'W',	0x51},
	{'X',	0x52},
	{'Y',	0x13},
	{'Z',	0x54},
	{'0',	0x49},
	{'1',	0x40},
	{'2',	0x01},
	{'3',	0x02},
	{'4',	0x43},
	{'5',	0x04},
	{'6',	0x45},
	{'7',	0x46},
	{'8',	0x07},
	{'9',	0x08},
	{' ',	0x7F},
	{'.',	0x6E},
	{'(',	0x57},
	{'+',	0x6D},
	{'&',	0x15},
	{'$',	0x62},
	{'*',	0x23},
	{')',	0x2F},
	{'-',	0x61},
	{'/',	0x4C},
	{',',	0x16},
	{'\'',	0x0B},
	{'=',	0x4A},
};

#define WHEELCHARS_1403 (sizeof(codewheel1403)/sizeof(codewheel1403[0]))


