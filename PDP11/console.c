/*
* console sub processor program.
*
* Attach to the shared memory segment and start reading register data and
* writing switch and knob settings.
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sim_defs.h>
#include <opcon.h>

int end_prog = 0;
int sighan() { end_prog = 1; }

/*
** Read data from the console processor.
*/
int oc_read(int fd, struct termios *tty, char *p, int c, int m)
{
  extern int errno;
  int x;
  fd_set s;
  struct timeval t;

  if (m == 0) {
    tty->c_cc[VMIN] = 0;		/* if no char, return		*/
    tcsetattr(fd, TCSANOW, tty);
    if ((x = read(fd, p, c)) != c && errno != EAGAIN && errno != EWOULDBLOCK)
      x = 0;
    }
  else {
    tty->c_cc[VMIN] = c;		/* at least 'c' chars		*/
    tcsetattr(fd, TCSANOW, tty);
    t.tv_sec = 0;
    t.tv_usec = 100;
    FD_ZERO (&s);
    FD_SET (fd, &s);
    select (FD_SETSIZE, &s, NULL, NULL, &t);
    if (FD_ISSET (fd, &s)) {
      if ((x = read(fd, p, c)) != c && errno != EAGAIN && errno != EWOULDBLOCK)
        x = 0;
      }
    else 
      x = 0;
    }

  /* reset to previous state */
  tty->c_cc[VMIN] = 1;
  tcsetattr(fd, TCSANOW, tty);

  return(x);
}

/*
** Send Address, Data and Port info to console processor.
*/
int oc_send_ADP(int oc_fd, oc_st *ocp)
{
  uint8 mask = 0, cmd[8];
  uint32 A;
  uint16 D;

  if (ocp->cpu_model == MOD_1145) {
    switch ((ocp->S[INP3] >> 4) & DSPA_MASK) {
      case DSPA_PROGPHY : A = ocp->A[ADDR_PRGPA]&0x3FFFF;break;
      case DSPA_CONSPHY : A = ocp->A[ADDR_CONPA]&0x3FFFF;break;
      case DSPA_KERNEL_D: A = ocp->A[ADDR_KERND]&0xFFFF; break;
      case DSPA_KERNEL_I: A = ocp->A[ADDR_KERNI]&0xFFFF; break;
      case DSPA_SUPER_D : A = ocp->A[ADDR_SUPRD]&0xFFFF; break;
      case DSPA_SUPER_I : A = ocp->A[ADDR_SUPRI]&0xFFFF; break;
      case DSPA_USER_D  : A = ocp->A[ADDR_USERD]&0xFFFF; break;
      case DSPA_USER_I  : A = ocp->A[ADDR_USERI]&0xFFFF; break;
      }
    switch ((ocp->S[INP3] >> 2) & DSPD_MASK) {
      case DSPD_DATA_PATHS : D = ocp->D[DISP_SHFR]; break;
      case DSPD_BUS_REG    : D = ocp->D[DISP_BR];   break;
      case DSPD_MU_ADRS    : D = ocp->D[DISP_FPP];  break;
      case DSPD_DISP_REG   : D = ocp->D[DISP_DR];   break;
      }
    }
  else {
    switch (ocp->S[INP5] & DSPA_MASK) {
      case DSPA_PROGPHY : A = ocp->A[ADDR_PRGPA]&0x3FFFFF;break;
      case DSPA_CONSPHY : A = ocp->A[ADDR_CONPA]&0x3FFFFF;break;
      case DSPA_KERNEL_D: A = ocp->A[ADDR_KERND]&0xFFFF; break;
      case DSPA_KERNEL_I: A = ocp->A[ADDR_KERNI]&0xFFFF; break;
      case DSPA_SUPER_D : A = ocp->A[ADDR_SUPRD]&0xFFFF; break;
      case DSPA_SUPER_I : A = ocp->A[ADDR_SUPRI]&0xFFFF; break;
      case DSPA_USER_D  : A = ocp->A[ADDR_USERD]&0xFFFF; break;
      case DSPA_USER_I  : A = ocp->A[ADDR_USERI]&0xFFFF; break;
      }
    switch ((ocp->S[INP5] >> 3) & DSPD_MASK) {
      case DSPD_DATA_PATHS : D = ocp->D[DISP_SHFR]; break;
      case DSPD_BUS_REG    : D = ocp->D[DISP_BR];   break;
      case DSPD_MU_ADRS    : D = ocp->D[DISP_FPP];  break;
      case DSPD_DISP_REG   : D = ocp->D[DISP_DR];   break;
      }
    }

  if (ocp->MMR0 & MMR0_MME) {
    mask = 0x03;
    if (ocp->MMR3 & MMR3_M22E)
        mask = 0x3F;
    }

  cmd[0] = 'U';
  cmd[1] = (uint8)((A >> 16) & 0xFF); /* & mask */
  cmd[2] = (uint8)((A >>  8) & 0xFF);
  cmd[3] = (uint8) (A & 0xFF);
  cmd[4] = (uint8)((D >> 8) & 0xFF);
  cmd[5] = (uint8) (D & 0xFF);
  cmd[6] = ocp->PORT1;
  cmd[7] = ocp->PORT2;
  write(oc_fd, cmd, 8);
}

/*
** Send status info to console processor.
*/
int oc_send_PORT(int oc_fd, oc_st *ocp)
{
  uint8 cmd[4];

  cmd[0] = 'F';
  cmd[1] = ocp->PORT1;
  cmd[2] = ocp->PORT2;
  write(oc_fd, cmd, 3);
}

/*
** Send single Addres & Data to processor.
*/
int oc_send_AD(int oc_fd, oc_st *ocp)
{
  uint8 mask = 0, cmd[6];

if (ocp->MMR0 & MMR0_MME) {
    mask = 0x03;
    if (ocp->MMR3 & MMR3_M22E)
	mask = 0x3F;
    }

  cmd[0] = 'B';
  cmd[1] = (uint8)((ocp->act_addr >> 16) & mask) ;
  cmd[2] = (uint8)((ocp->act_addr >> 8) & 0xFF) ;
  cmd[3] = (uint8) (ocp->act_addr & 0xFF);
  cmd[4] = (uint8)((ocp->D[0] >> 8) & 0xFF) ;
  cmd[5] = (uint8) (ocp->D[0] & 0xFF);
  write(oc_fd, cmd, 6);
}

/*
** Send single Address display to processor.
*/
int oc_send_A(int oc_fd, oc_st *ocp)
{
  uint8 mask = 0, cmd[4];

if (ocp->MMR0 & MMR0_MME) {
    mask = 0x03;
    if (ocp->MMR3 & MMR3_M22E)
	mask = 0x3F;
    }

  cmd[0] = 'A';
  cmd[1] = (uint8)((ocp->act_addr >> 16) & mask) ;
  cmd[2] = (uint8)((ocp->act_addr >>  8) & 0xFF) ;
  cmd[3] = (uint8) (ocp->act_addr & 0xFF);
  write(oc_fd, cmd, 4);
}

/*
** Request setting of the switches.
*/
int oc_read_SWR(int oc_fd, oc_st *ocp)
{
  uint8 c = 'Q';

  write(oc_fd, &c, 1);
  read(oc_fd, ocp->S, 5);
}

/*
** Acknowledge all toggle commands.
*/
int oc_ack_all(int oc_fd)
{
  uint8 c = 'i';	/* clear all */

  write(oc_fd, &c, 1);
}

/*
** Acknowledge on etoggle command using the mask.
*/
int oc_ack_one(int oc_fd, oc_st *ocp)
{
  write(oc_fd, ocp->ACK, 3);
}


int main(int ac, char **av)
{
  int x, oc_fd, oc_shmid, oc_swr = 0;
  uint32 *shm_addr;
  key_t	oc_key;
  char c, cmd_buf[4];
  oc_st *ocp;
  struct termios tty, savetty;
  struct timespec ns;
  extern int errno, end_prog;

  struct shmid_ds shminfo;

  end_prog = 0;
  if (ac < 2) {
    printf("Usage: $0 <shm_address>\n");
    exit(1);
    }

  shm_addr = atoi(av[1]);
  signal(SIGHUP, sighan);

		/* attach to shm exchange area */

  oc_key = 201604;
  if ((oc_shmid = shmget(oc_key, sizeof(oc_st), 0)) == -1 ||
      (ocp = (oc_st *)shmat(oc_shmid, NULL, 0)) == (oc_st *)-1) {
    printf("OCC : shmget/shmctl/shmat error (errno = %d).\n", errno);
    exit(1);
    }

	/* open the serial line as passed in the control block */

  if ((oc_fd = open(ocp->line, O_RDWR|O_NOCTTY|O_NONBLOCK, 0666)) < 0) {
    printf("OCC : open error (%d on %s).\n", errno, ocp->line);
    shmdt((char *)ocp);			/* detach shared mem	*/
    exit(1);
    }

	/* set line dicipline (9600-8n1, raw) */

  if ((x = tcgetattr(oc_fd, &tty)) < 0) {
    printf("failed to get attr: %d, %s", x, strerror(errno));
    shmdt((char *)ocp);			/* detach shared mem	*/
    exit (1);
    }
  savetty = tty;    /* preserve original settings for restoration */
  fcntl(oc_fd, F_SETFL);            // Configure port reading
  tcgetattr(oc_fd, &tty);       // Get the current options for the port
  cfsetispeed(&tty, B9600);    // Set the baud rates to 230400
  cfsetospeed(&tty, B9600);

  tty.c_cflag |= (CLOCAL | CREAD); // Enable the receiver and set local mode
  tty.c_cflag &= ~PARENB;        // No parity bit
/*  tty.c_cflag &= ~CSTOPB; */       // 1 stop bit
  tty.c_cflag &= CSTOPB;	/* 2 stop bits */
  tty.c_cflag &= ~CSIZE;         // Mask data size
  tty.c_cflag |=  CS8;           // Select 8 data bits
  tty.c_cflag &= ~CRTSCTS;       // Disable hardware flow control  

  // Enable data to be processed as raw input
  tty.c_lflag &= ~(ICANON | ECHO | ISIG);

  tty.c_cc[VMIN] = 1;
  tty.c_cc[VTIME] = 0; /* no timeout */

  // Set the new attributes
  tcsetattr(oc_fd, TCSANOW, &tty);

/* init the console processor board */
  sprintf(cmd_buf, "p%d", ocp->cpu_model);
  write(oc_fd, cmd_buf, 2);

/* we are in business, let the other side know we are ready */
/* The A[0] field is set by SIMH for observation */
  ocp->A[0] = 0;

/*
 * Loop until a signal is received.
 * There are 2 modes, non interactive and interactive.
 * In the first mode, the following steps are executed :
 *   - check for a SWR get request (only during pre-boot of simulated cpu).
 *   - send current A, D & P to the CPB
 *   - check if HALT switch is used
 *     set halt mode, read SWR
 *     stray toggles are cleared.
 *     drop to interactive mode.
 *   - every 5th iteration, all switch settings are read.
 *
 * In the 2nd mode, the following steps are executed :
 *   - check if previous cmd was processed, loop until it is
 *   - wait for an input command
 *   - perform action based on input command.
 *
*/
  while(end_prog == 0) {
    if (ocp->HALT == 0) {		/* if 0, we are not interactive	*/
      if (ocp->OUT == 'Q') {			/* SWR requested?	*/
        oc_read_SWR(oc_fd, ocp);		/* Get switch data	*/
        ocp->OUT = 0;
        continue;
        }
      oc_send_ADP(oc_fd, ocp);
#define METHOD1 1
#ifdef METHOD1
      if (oc_read(oc_fd, &tty, &c, 1, 0) == 1) {/* look for halt 	*/
        if (c == 'H') {				/* HALT set?		*/
          ocp->HALT = 2;			/* Yes, set tmo mode 2	*/
          oc_read_SWR(oc_fd, ocp);		/* Get switch data	*/
          oc_ack_all(oc_fd);			/* Just ack it		*/
	  continue;
	  }
        else {
	  if (strchr ("cdlsx", c) != NULL)	/* Stray toggle?	*/
            oc_ack_all(oc_fd);			/* Just ack it		*/
          }
        }
      if (oc_swr++ > 6) {			/* counter max reached? */
        oc_swr = 0;				/* reset it		*/
        oc_read_SWR(oc_fd, ocp);		/* get switch data	*/
	if (ocp->S[1] & 0x4)			/* halt switch used?	*/
          ocp->HALT = 2;			/* Yes, set it		*/
	}
#endif
#ifdef METHOD2
      if (oc_swr++ > 6) {	/* counter max reached? */
        oc_swr = 0;
        oc_read_SWR(oc_fd, ocp);
	if (ocp->S[1] & 0x4)	/* halt switch used? */
          ocp->HALT = 2;
	}
      if (oc_read(oc_fd, &tty, &c, 1, 0) == 1) {
        if (c == 'H')
          ocp->HALT = 2;
        else {
	  if (strchr ("cdlsx", c) != NULL)
            oc_ack_all(oc_fd);
          }
        }
#endif
#ifdef METHOD3
      if (oc_swr++ > 6) {
        oc_swr = 0;
        if (oc_read(oc_fd, &tty, &c, 1, 0) == 1) {
          if (c == 'H') {
            ocp->HALT = 2;	/* set HALT & retrieve SWR */
	    oc_read_SWR(oc_fd, ocp);
	    }
          else
	    if (strchr ("cdlsx", c) != NULL)
              oc_ack_all(oc_fd);
          }
        }
#endif
      }
    else { /* in 'interactive' mode */
      while (ocp->IN != 0) {	/* previous cmd not processed yet	*/
	ns.tv_sec = 0;
	ns.tv_nsec = 10000;				/* 10 ms	*/
        nanosleep(&ns, (struct timespec *)0);
        }
      while ((c = oc_read(oc_fd, &tty, &ocp->IN, 1, 1)) == (char)0 ||
	     ocp->OUT == 0)
        ;

      switch (ocp->OUT) {
         case 'A' : oc_send_A(oc_fd, ocp);	break;
         case 'B' : oc_send_AD(oc_fd, ocp);	break;
         case 'F' : oc_send_PORT(oc_fd, ocp);	break;
         case 'Q' : oc_read_SWR(oc_fd, ocp);	break;
	 case 'a' : oc_ack_all(oc_fd);       	break;
	 case 'o' : oc_ack_one(oc_fd, ocp);	break;
         default  :				break;
	 }
      ocp->OUT = 0;				/* clear command	*/
      }
    }						/* end for loop		*/

  tcsetattr(oc_fd, TCSANOW, &savetty);		/* reset line		*/
  close(oc_fd);					/* close line		*/
  shmdt((char *)ocp);				/* detach shared mem	*/
  exit(0);
}

