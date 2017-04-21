#include <stdio.h>

static unsigned char sixlut[64] = {
'@','A','B','C','D','E','F','G', 
'H','I','J','K','L','M','N','O',
'P','Q','R','S','T','U','V','W',
'X','Y','Z','[','/',']','^','_',
' ','!','"','#','$','%','&','\'',
'(',')','*','+',',','-','.','/',
'0','1','2','3','4','5','6','7',
'8','9',':',';','<','=','>','?'
};

char sixbit(char c)
{
 return(sixlut[c & 0x3f]);
}

void printb(unsigned char c)
{
	int i;
	for(i=0; i<8; i++){
	 if((c & 0x80) == 0)
	  printf("%c",'0');
	 else
	  printf("%c",'1');
	 c = c << 1;
	}
	printf("\n");
}

/*
 * read an 18 bit paper tape frame
 *
 * bits 6,7 packed into high part of int so we can look at them
 */
unsigned int readframe()
{
	unsigned int i;
	unsigned char c;
	do{
	 c = getchar(); 
	 if(feof(stdin)) 
	  exit();
	  if((c & 0x80) == 0) printf("{nul}\n",c);
	} while((c & 0x80) == 0);

	i =    ((c & 0xc0)>>6)<<27;
	i = i | (c & 0x3f)<<12;
	do{
	 c = getchar(); 
	 if(feof(stdin)) 
	  exit();
	  if((c & 0x80) == 0) printf("{nul}\n",c);
	} while((c & 0x80) == 0);

	i = i |((c & 0xc0)>>6)<<24;
	i = i | (c & 0x3f)<<6;

	do{
	 c = getchar(); 
	 if(feof(stdin)) 
	  exit();
	  if((c & 0x80) == 0) printf("{nul}\n",c);
	} while((c & 0x80) == 0);

	i = i |((c & 0xc0)>>6)<<21;
	i = i | (c & 0x3f);
	return i;
}

disasm(unsigned int instr)
{
	char *idx;
	if(instr & 0010000) idx = ",X"; else idx = "  ";

	if((instr & 0700000) != 0700000)
	switch(instr & 0740000){
	 case(0000000):
	  printf("CAL%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0040000):
	  printf("DAC%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0100000):
	  printf("JMS%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0140000):
	  printf("DZM%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0200000):
	  printf("LAC%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0240000):
	  printf("XOR%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0300000):
	  printf("ADD%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0340000):
	  printf("TAD%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0400000):
	  printf("XCT%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0440000):
	  printf("ISZ%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0500000):
	  printf("AND%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0540000):
	  printf("SAD%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 case(0600000):
	  printf("JMP%c %04o%s ", (instr & 0020000)?'*':' ', instr &07777, idx);
	  break;
	 default:
	  printf("????         ");
	  break;
	}
/*
 */
	if((instr & 0700000) == 0700000)
	 switch(instr){
	 case(0700000):
	  printf("IOT        ");
	  break;
	 case(0700002):
	  printf("IOF        ");
	  break;
	 case(0700004):
	  printf("CLOF       ");
	  break;
	 case(0700042):
	  printf("ION        ");
	  break;	

	 case(0700101):
	  printf("RSF (hsrdr)");
	  break;
	 case(0700102):
	  printf("RCF (hsrdr)");
	  break;
	 case(0700104):
	  printf("RSA (hsrdr)");
	  break;
	 case(0700112):
	  printf("RRB (hsrdr)");
	  break;
	 case(0700144):
	  printf("RSB (hsrdr)");
	  break;

	 case(0700201):
	  printf("PSF (hsptp)");
	  break;
	 case(0700202):
	  printf("PCF (hsptp)");
	  break;
	 case(0700204):
	  printf("PSA (hsptp)");
	  break;
	 case(0700244):
	  printf("PSB (hsptp)");
	  break;

	 case(0700301):
	  printf("KSF (ttykb)");
	  break;
	 case(0700312):
	  printf("KRB (ttyrd)");
	  break;
	 case(0700314):
	  printf("IORS       ");
	  break;
	 case(0700322):
	  printf("KRS (ttykb)");
	  break;

	 case(0700401):
	  printf("TSF (ttyout)");
	  break;
	 case(0700402):
	  printf("TCF (ttyout)");
	  break;
	 case(0700406):
	  printf("TLS (ttyout)");
	  break;

	
	 case(0703302):
	  printf("CAF         ");
	  break;

	 case(0707721):
	  printf("SBA         ");
	  break;
	 case(0707722):
	 case(0707762):
	  printf("DBA         ");
	  break;
	 case(0707724):
	 case(0707764):
	  printf("EBA         ");
	  break;

/*
 */
	 case(0720000):
	  printf("AAS         ");
	  break;
	 case(0723000):
	  printf("AAC         ");
	  break;
	 case(0725000):
	  printf("AXS         ");
	  break;
	 case(0736000):
	  printf("CLLR        ");
	  break;
	 case(0735000):
	  printf("CLX         ");
	  break;
	 case(0722000):
	  printf("PAL         ");
	  break;
	 case(0721000):
	  printf("PAX         ");
	  break;
	 case(0730000):
	  printf("PLA         ");
	  break;
	 case(0731000):
	  printf("PLX         ");
	  break;
	 case(0724000):
	  printf("PXA         ");
	  break;
	 case(0726000):
	  printf("PXL         ");
	  break;
	

/*
 * operate instructions
 */
	 case(0740000):
	  printf("NOP         ");
	  break;
	 case(0740001):		// CMA compliment AC
	  printf("CMA         ");
	  break;
	 case(0740002):
	  printf("CML         ");
	  break;
	 case(0740004):
	  printf("OAS         ");
	  break;
	 case(0740010):
	  printf("RAL         ");
	  break;
	 case(0740020):
	  printf("RAR         ");
	  break;
	 case(0740030):
	  printf("IAC         ");
	  break;
	 case(0740031):
	  printf("TCA         ");
	  break;
	 case(0740040):
	  printf("HLT         ");
	  break;
	 case(0740100):
	  printf("SMA         ");
	  break;
	 case(0740200):
	  printf("SZA         ");
	  break;
	 case(0740400):
	  printf("SNL         ");
	  break;
	 case(0741000):
	  printf("SKP         ");
	  break;
	 case(0741100):
	  printf("SPA         ");
	  break;
	 case(0741200):
	  printf("SNA         ");
	  break;
	 case(0741400):
	  printf("SZL         ");
	  break;
	 case(0742010):
	  printf("RTL         ");
	  break;
	 case(0742020):
	  printf("RTR         ");
	  break;
	 case(0742030):
	  printf("SWHA        ");
	  break;
	 case(0744000):
	  printf("CLL         ");
	  break;
	 case(0744002):
	  printf("STL         ");
	  break;	
	 case(0744010):
	  printf("CCL         ");
	  break;
	 case(0744020):
	  printf("RCL         ");
	  break;
	 case(0750000):
	  printf("CLA         ");
	  break;
	 case(0750001):
	  printf("LAS         ");
	  break;
	 case(0750004):
	  printf("LAT         ");
	  break;
	 case(0750010):
	  printf("GLK         ");
	  break;
	 case(0760000):
	  printf("LAW         ");
	  break;
	 default:
	  printf("???         ");
	  break;
	}
}

main()
{
	int totalblks = 0;
	int badblks = 0;
	unsigned int fullwd = 0;
	unsigned int currentwd = 0;
	int wds = 0;
	int bytecnt = 0;	
	int col = 0;
	int framecount;
	unsigned int cksum;
	unsigned int adr;
        unsigned char c;
	/*
	 *  read 3 chars and pack them in a word 
	 */
        do {
	     currentwd = readframe();
	     printf("%010o ", currentwd);
	     printf("%c %c %c ",sixbit((currentwd & 0770000) >>12),
                                 sixbit((currentwd & 007700) >>6),
			         sixbit(currentwd & 077) );
	     col++;
	     if(col == 4){
	      printf("\n");
	      col = 0;
	     }
	     if(currentwd & 00010000000) {
	      printf("\n loader end ----\n");
	     /*
	      * start looking for binary data frames
	      */
	      while(1){
	     /*
	      * frames start at the first 0x80 punched
	      * (check is in readframe..)
	      */

	       adr = readframe() & 0777777;		// staring adr
	       cksum = adr;
	       printf("ADR: %010o\n", adr);

	       framecount = readframe() & 0777777;	// word count
	       cksum += framecount;
	       framecount = -(0xfffe0000 | framecount); // sign extend
	       printf("CNT: %010o (%d)\n", framecount, framecount);

	       if((adr & 0700000) != 0){
	        printf("FRAMECOUNT == 0 START ADR == %06o\n", adr);
		printf("TOTAL BLKS %d TOTAL ERRS %d\n", totalblks, badblks);
	        exit();
	       }

	       currentwd = readframe() & 0777777;		// checksum
	       cksum += currentwd;
	       printf("CKS: %06o\n", currentwd);
	       /*
		* read all the data words
	        */
	       while(framecount > 0){
	 	currentwd = readframe();
	        cksum += currentwd & 0777777;
		printf("%05o: %06o  ",adr++, currentwd & 0777777);
		disasm(currentwd & 0777777);
	        printf("   ; %c%c%c ",sixbit((currentwd & 0770000) >>12),
                                 sixbit((currentwd & 007700) >>6),
                                 sixbit(currentwd & 077) );
 		printf("\n");
		framecount--;
	       }

	       if(cksum&0777777){
	        printf("****BAD CKSUM**** %06o\n",cksum&0777777);
	        badblks++;
	       }

	       totalblks++;
	      }
	     }
        } while(!feof(stdin));

	if(bytecnt > 0){
	 printf("%d chrs left\n", bytecnt);
	}
}

