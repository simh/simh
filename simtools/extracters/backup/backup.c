
/* Dump contents of Tops-10 BACKUP tapes, which have been read
   into a disk file. The "known good" way to do this is to use the
   unix utility "dd", and a command line something like this:

   dd if=/dev/rmt0 of=data ibs=2720 obs=2720 conv=block

   the key thing is that this program expects a fixed block size of
   2720 bytes.  If the tape actually has some other format, this 
   program probably won't succeed.  You can use the unix utility "tcopy"
   to inspect the contents of the tape.

   Here's the tcopy output from a good tape:

   tcopy /dev/rmt0 
   file 0: block size 2720: 9917 records
   file 0: eof after 9917 records: 26974240 bytes
   eot
   total length: 26974240 bytes

*/

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include "backup.h"

#define bool long
#define false 0
#define true 1

#define RAWSIZE (5*(32+512))

#define endof(s) (strchr(s, (char) 0))

FILE* source;			/* Source "tape". */

bool eightbit = false;		/* Status of -8 (eight-bit) flag. */
bool copytape = false;		/* Status of -c (copytape fmt) flag. */
bool buildtree = false;		/* Status of -d (build trees) flag. */
bool interchange = false;	/* Status of -i (interchange) flag. */
bool binary = false;		/* Status of -b (binary) flag. */
bool timfmt = false;		/* Status of -m (mts format) flag. */
long verbose = 0;		/* Status of -v (verbose) flag. */

char** argfiles;		/* File spec's to extract. */
long argcount;			/* Number of them. */

unsigned char rawdata[RAWSIZE];	/* Raw data for a tape block. */

long headlh[32], headrh[32];	/* Header block from tape. */
long datalh[512], datarh[512];	/* Data block from tape. */

long prevSEQ;			/* SEQ number of previous block. */
long currentfilenumber;

char deferbyte;			/* Defered byte for output. */
long defercount;			/* Count of defered output bytes. */

bool extracting;
FILE* destination;

/* Tape information: */

char systemname[100];
char savesetname[100];

/* File information: */

long a_bsiz;				/* For now. */
long a_alls;
long a_mode;
long a_leng;

char filedev[100];		/* Device: */
char filedir[100];		/* [ufd] */
char filename[100];		/* file name. */
char fileext[100];		/* extension. */

char filespec[7][100];		/* [0]: device:ufd. */
				/* [1-5]: sfd's, stored directly here. */
				/* [6]: file.ext */

char cname[100];		/* Canonical name. */

/* unpackheader unpacks the header block from the raw stream. */

void unpackheader() {
  unsigned char* rawptr;
  long i, left, right;
  unsigned char c;

  rawptr = &rawdata[0];

  for (i = 0; i < 32; i++) {
    left = *(rawptr++) << 10;
    left |= *(rawptr++) << 2;
    left |= (c = *(rawptr++)) >> 6;
    right = (c & 077) << 12;
    right |= *(rawptr++) << 4;
    right |= *(rawptr++) & 017;
    headlh[i] = left;
    headrh[i] = right;
	if(verbose>1) {printf("\n%i l=%d, r=%d",i,left,right);}
  }
}

/* unpackdata unpacks the data block from the raw stream. */

void unpackdata() {
  unsigned char* rawptr;
  long i, left, right;
  unsigned char c;

  rawptr = &rawdata[32*5];

  for (i = 0; i < 512; i++) {
    left = *(rawptr++) << 10;
    left |= *(rawptr++) << 2;
    left |= (c = *(rawptr++)) >> 6;
    right = (c & 077) << 12;
    right |= *(rawptr++) << 4;
    right |= *(rawptr++) & 017;
    datalh[i] = left;
    datarh[i] = right;
  }
}

/* pars_36bits reads 36 bits from a machine word. */

void pars_36bits(index, store)
long index;
char *store;
{
  long l, r;

  l = datalh[index];
  r = datarh[index];

  store[0] = r & 0377;
  store[1] = (r >> 8) & 0377;
  store[2] = ((r >> 16) & 03) | ((l << 2) & 0374);
  store[3] = (l >> 6) & 0377;
  store[4] = (l >> 14) & 017;
  store[5] = store[6] = store[7] = 0;
}

/* pars_5chars reads five ASCII chars from a machine word. */

void pars_5chars(index, store)
long index;
char* store;
{
  long l, r;

  l = datalh[index];
  r = datarh[index];

  store[0] = (0177 & (l >> 11));
  store[1] = (0177 & (l >> 4));
  store[2] = (0177 & ((l << 3) | ((r >> 15) & 017)));
  store[3] = (0177 & (r >> 8));
  store[4] = (0177 & (r >> 1));
}

/* pars_asciz stores asciz text from data */

void pars_asciz(index, store)
long index;
char* store;
{
  long words;

  words = datarh[index++];
  while ((words--) > 0) {
    pars_5chars(index++, store);
    store += 5;
  }
  *store = (char) 0;
}

/* pars_o_name parses an o$name block from data. */

void pars_o_name(index)
long index;
{
  long lastw;

  lastw = index + datarh[index];
  ++index;
  while (index < lastw) {
    switch (datalh[index]) {
    case 0:  index = lastw; break;
    case 1:  pars_asciz(index, filedev);  break;
    case 2:  pars_asciz(index, filename); break;
    case 3:  pars_asciz(index, fileext);  break;
    case 32: pars_asciz(index, filedir);  break;
    case 33: pars_asciz(index, filespec[1]); break;
    case 34: pars_asciz(index, filespec[2]); break;
    case 35: pars_asciz(index, filespec[3]); break;
    case 36: pars_asciz(index, filespec[4]); break;
    case 37: pars_asciz(index, filespec[5]); break;
    }
    index += datarh[index];
  }
}

void pars_o_attr(index)
long index;
{
  /* parse off file attribute block */
  ++index;
  a_bsiz = datarh[index + A_BSIZ];	/* for now... */
  a_alls = datarh[index + A_ALLS];	/* for now... */
  a_mode = datarh[index + A_MODE];	/* for now... */
  a_leng = datarh[index + A_LENG];	/* for now... */
}

void pars_o_dirt(index)
long index;
{
  /* parse off directory attribute block */
}

void pars_o_sysn(index)
long index;
{
  pars_asciz(index, systemname);
}

void pars_o_ssnm(index)
long index;
{
  pars_asciz(index, savesetname);
}

void zerotapeinfo() {
  systemname[0] = (char) 0;
  savesetname[0] = (char) 0;
}

void zerofileinfo() {

  filedev[0]  = (char) 0;
  filedir[0]  = (char) 0;
  filename[0] = (char) 0;
  fileext[0]  = (char) 0;

  filespec[0][0] = (char) 0;
  filespec[1][0] = (char) 0;
  filespec[2][0] = (char) 0;
  filespec[3][0] = (char) 0;
  filespec[4][0] = (char) 0;
  filespec[5][0] = (char) 0;
  filespec[6][0] = (char) 0;

  cname[0] = (char) 0;
}

/* unpackinfo picks non-data information from data block. */

void unpackinfo() {
  long index;

  unpackdata();

  index = 0;
  while (index < headrh[G_LND]) {
    switch (datalh[index]) {
    case 1: pars_o_name(index); break;
    case 2: pars_o_attr(index); break;
    case 3: pars_o_dirt(index); break;
    case 4: pars_o_sysn(index); break;
    case 5: pars_o_ssnm(index); break;
    }
    index += datarh[index];
  }
}

void printtapeinfo() {
  if (verbose) {
    if (*savesetname != (char) 0) printf("Saveset name: %s\n", savesetname);
    if (*systemname != (char) 0) printf("Written on: %s\n", systemname);
  }
}

void downcase(s)
char* s;
{
  while (*s != (char) 0) {
    if (isupper(*s)) *s = tolower(*s);
    s++;
  }
}

void buildfilenames() {
  long i;

  if (*filedev != (char) 0)
    sprintf(filespec[0], "%s:%s", filedev, filedir);
  else
    sprintf(filespec[0], "%s", filedir);

  sprintf(filespec[6], "%s.%s", filename, fileext);

  for(i = 0; i < 7; i++)
    downcase(filespec[i]);

  sprintf(cname, "%s", filespec[0]);
  for(i = 1; i < 6; i++) {
    if (*filespec[i] != (char) 0) sprintf(endof(cname), ".%s", filespec[i]);
  }
  if (*cname != (char) 0)
    sprintf(endof(cname), "..%s", filespec[6]);
  else
    sprintf(cname, "%s", filespec[6]);

}

void printfileinfo() {

  buildfilenames();
  printf("%3d  %s", currentfilenumber, cname);
  if (verbose) {
     printf(" (%d) alloc:%d, mode:%o, len:%d", a_bsiz, a_alls, a_mode, a_leng);
  }
  printf("\n");
}

/* readblock reads one logical block from the input stream. */
/* The header is unpacked into head{l,r}; the data is not. */

long blockn=0;

void readblock() {
  long i, bytes;
  unsigned char bc[4];

  i = fread(bc, sizeof(char), 4, source);
  if (i == 0) return;
  bytes = ((long) bc[1] << 8) | (bc[0]);
  if (bytes == 0) return;
  if (bytes != RAWSIZE)
	  fprintf(stderr, "backup: incorrect block size = %d\n", bytes);
  i = fread(rawdata, sizeof(char), RAWSIZE, source);
  blockn++;
  while (i++ < RAWSIZE) rawdata[i] = (char) 0;
  fread(bc, sizeof(char), 4, source);
  unpackheader();
}

/* Disk file output routines: */

void WriteBlock() {
  char buffer[5*512];
  char binbuf[8*512];
  long bufpos, index;

  for (index = 0; index < 5*512; index++) buffer[index] = 0;
  for (index = 0; index < 8*512; index++) binbuf[index] = 0;

  unpackdata();
  if (binary) {
    for (index = headrh[G_LND], bufpos = 0;
       index < (headrh[G_LND] + headrh[G_SIZE]); index++) {
	  pars_36bits(index, &binbuf[bufpos]);
	  bufpos += 8;
	}
    (void) fwrite(binbuf, sizeof(char), bufpos, destination);
  }
  else {
    for (index = headrh[G_LND], bufpos = 0;
       index < (headrh[G_LND] + headrh[G_SIZE]); index++) {
      pars_5chars(index, &buffer[bufpos]);
      bufpos += 5;
    }

    if (headlh[G_FLAGS] & GF_EOF) {
      for (index = 1; (index < (eightbit ? 4 : 5)) && (bufpos > 0); index++) {
        if (buffer[bufpos - 1] == (char) 0) bufpos--;
      }
    }
    (void) fwrite(buffer, sizeof(char), bufpos, destination);
  }
}

/* OpenOutput opens the output file, according to -d and -i flags. */

bool OpenOutput() {

  struct stat statbuf;
  char oname[100];
  long i;

  defercount = 0;

  if (interchange) {
	destination = fopen(filespec[6], (binary? "wb": "w"));
  } else if (!buildtree) {
	for (i = 0; (i < sizeof (cname)) && cname[i]; i++)
		if (cname[i] == ':') cname[i] = '.';
    destination = fopen(cname, (binary? "wb": "w"));
  } else {
/*    for(i = 0, oname[0] = (char) 0; i < 6; i++) {
      if (*filespec[i] == (char) 0) break;
      sprintf(endof(oname), "%s", filespec[i]);
      if (stat(oname, &statbuf) != 0) {
	if (mkdir(oname, 0777) != 0) {
	  fprintf(stderr, "backup: cannot create %s/\n", oname);
	  return(false);
	}
      }
      sprintf(endof(oname), "/");
    }
    sprintf(endof(oname), "%s", filespec[6]);
    destination = fopen(oname, (binary? "wb": "w")); */
	  fprintf(stderr, "backup: tree mode not supported\n");
	  return(false);
  }

  return(destination != NULL);
}

void CloseOutput() {
  /* Close output file after us. */
}

/* Argmatch checks if the current file matches the given argument: */

bool argmatch(arg)
char* arg;
{
  long target;
  char* f;
  char* p;
  char* s;

  if (*arg == '#') {
    (void) sscanf(arg, "#%d", &target);
    return(target == currentfilenumber);
  }

  if (*arg == '*') return(1);

  for (f = cname; *f != (char) 0; f++) {
    for (p = f, s = arg; (*s != (char) 0) && (*p == *s); p++, s++);
    if (*s == (char) 0) return (true);
  }
  return (false);
}

/* doextract performs the job of "backup -x ..." */

void doextract() {
  long i;

  currentfilenumber = 0;
  extracting = false;
  while (!feof(source)) {
    readblock();
    if (headrh[G_SEQ] == prevSEQ) continue;

    if (headrh[G_TYPE] == T_FILE) {
      if (headlh[G_FLAGS] & GF_SOF) {
	currentfilenumber++;
	zerofileinfo();
	unpackinfo();
	buildfilenames();
	for (i = 0; i < argcount; i++) {
	  if (argmatch(argfiles[i])) {
	    if (*argfiles[i] == '#') {
	      /* Maybe do a pure shift here? */
	      argfiles[i] = argfiles[--argcount];
	    }
	    extracting = true;
	    break;
	  }
	}
	if (extracting) {
	  if (OpenOutput()) {
	    if (verbose) {
	      printf("Extracting %s", cname);
	      fflush(stdout);
	    }
	  } else {
	    fprintf(stderr, "backup: can't open %s for output\n", cname);
	    extracting = false;
	  }
	}
      }
      if (extracting) {
	WriteBlock();
	if (headlh[G_FLAGS] & GF_EOF) {
	  (void) fclose(destination);
	  extracting = false;
	  if (verbose) printf("\n");
	  if (argcount == 0)
	    break;
	}
      }
    }
    prevSEQ = headrh[G_SEQ];
  }
}

/* dodirectory performs the job of "backup -t ..." */

void dodirectory() {

  currentfilenumber = 0;

  while (!feof(source)) {
    readblock();
    if (headrh[G_SEQ] == prevSEQ) continue;

    if (headrh[G_TYPE] == T_BEGIN) {
      zerotapeinfo();
      unpackinfo();
      printtapeinfo();
    }
    if (headrh[G_TYPE] == T_FILE) {
      if (headlh[G_FLAGS] & GF_SOF) {
	++currentfilenumber;
	zerofileinfo();
	unpackinfo();
	printfileinfo();
      }
    }
    prevSEQ = headrh[G_SEQ];
  }
}

/* command decoder and dispatcher */

bool checkarg(arg)
char* arg;
{
  long i;
  char c;

  if (*arg == '#') {
    if (sscanf(arg, "#%d%c", &i, &c) != 1) {
      fprintf(stderr, "backup: bad argument: %s\n", arg);
      return(true);
    }
  }
  return(false);
}


int main(argc, argv)
long argc;
char* argv[];
{
  long i;
  char* s, tapetype[4];
  bool namenext = false;
  bool actgiven = false;
  char action;
  char* inputname = NULL;

  if (--argc > 0) {
    for (s = *(++argv); *s != (char) 0; s++)
      switch(*s) {
      case '-':
	break;
      case '8':
	eightbit = true;  break;
	  case 'b':
	binary = true; break;
      case 'c':
	copytape = true; break;
      case 'd':
	buildtree = true;  break;
      case 'f':
	namenext = true;  break;
      case 'i':
	interchange = true;  break;
      case 'm':
	timfmt = true; break;
      case 't':
      case 'x':
	action = *s;  actgiven = true;  break;
      case 'v':
	  verbose++;  break;
      default:
	fprintf(stderr, "backup: bad option %c\n", *s);
	return 0;
      }
  }
  if (namenext) {
    if (--argc > 0)
      inputname = *(++argv);
    else {
      fprintf(stderr, "backup: input file name missing\n");
      return 0;
    }
  }

  argfiles = ++argv;		/* Keep rest of arguments. */
  argcount = --argc;		/* ... and count 'em. */

  for (i = 0; i < argcount; i++) {
    if (checkarg(argfiles[i])) {
		fprintf(stderr, "backup: error in argument %d = %s\n", i, argfiles[i]);
		return 0;  }  }

  if (inputname == NULL) {
    /* Use environment var. TAPE here? */
    fprintf(stderr, "backup: no input file given\n");
    return 0;
  }

  if (strcmp(inputname, "-") != 0) {
    if ((source = fopen(inputname, "rb")) == NULL) {
      fprintf(stderr, "backup: can't open %s for input\n", inputname);
      return 0;
    }
	fprintf (stderr, "backup: opening %s for input\n", inputname);
	if (timfmt) fread (tapetype, sizeof(char), 4, source);
  } else {
    source = stdin;
  }

  switch (action) {
  case 't': dodirectory(); break;
  case 'x': doextract(); break;
  default:
    fprintf(stderr, "backup: internal error in program\n");
    return 0;
  }

}

