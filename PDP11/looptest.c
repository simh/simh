#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

extern void exit(int a);

int end_prog = 0;
int sighan() { end_prog = 1; }

int oc_read(int fd, struct termios *tty, char *p, int c)
{
  extern int errno;
  int x;

  tty->c_cc[VMIN] = 0;		/* if no char, return		*/
  tcsetattr(fd, TCSANOW, tty);

  if ((x = read(fd, p, c)) != c && errno != EAGAIN && errno != EWOULDBLOCK)
    x = 0;

  /* reset to previous state */
  tty->c_cc[VMIN] = 1;
  tcsetattr(fd, TCSANOW, tty);

  return(x);
}

int oc_ack_all(int oc_fd)
{
  unsigned char c = 'i';	/* clear all */

  write(oc_fd, &c, 1);
}

int oc_ack_one(int oc_fd, char c)
{
  char buf[4];
  unsigned char mask = 0;

  buf[0]='c';
  buf[1]=(unsigned char)(2 + 0x30);

  switch (c) {
    case 'c' : mask = 0x08;	break;
    case 'd' : mask = 0x40;	break;
    case 'l' : mask = 0x04;	break;
    case 's' : mask = 0x02;	break;
    case 'x' : mask = 0x01;	break;
    }
  buf[2] = mask;
  write(oc_fd, buf, 3);
}

int main(int ac, char **av)
{
  int x, oc_fd;
  char c, cmd_buf[4];
  struct termios tty, savetty;
  extern int errno, end_prog;

  end_prog = 0;
  if (ac < 2) {
    printf("Usage: $0 <serial line>\n");
    exit(1);
    }

  signal(SIGHUP, sighan);

  if ((oc_fd = open(av[1], O_RDWR|O_NOCTTY|O_NONBLOCK, 0666)) < 0) {
    printf("Open error (%d on %s).\n", errno, av[1]);
    exit(1);
    }

	/* set line dicipline (9600-8n1, raw) */

  if ((x = tcgetattr(oc_fd, &tty)) < 0) {
    printf("failed to get attr: %d, %s", x, strerror(errno));
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

  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0; /* no timeout */

  // Set the new attributes
  tcsetattr(oc_fd, TCSANOW, &tty);

  cmd_buf[0] = 'p';
  cmd_buf[1] = '5';
  write(oc_fd, cmd_buf, 2);

  while(end_prog == 0) {
    c = 0;
    if (oc_read(oc_fd, &tty, &c, 1) == 1) {/* look for halt 	*/
      printf(" Got byte '%c' (0x%02X)\n", c, c);
      switch (c) {
         case 'c' : printf("'continue' command received, ack it\n");
		    oc_ack_one(oc_fd, 'c');
		    break;
         case 'd' : printf("'deposit' command received, ack it\n");
		    oc_ack_one(oc_fd, 'd');
		    break;
         case 'l' : printf("'load' command received, ack it\n");
		    oc_ack_one(oc_fd, 'l');
		    break;
         case 's' : printf("'start' command received, ack it\n");
		    oc_ack_one(oc_fd, 's');
		    break;
	 case 'x' : printf("'examine' command received, ack it\n");
		    oc_ack_one(oc_fd, 'x');
		    break;
         default  : printf("Unknown command '%c', ack all to be sure\n");
		    oc_ack_all(oc_fd);
		    break;
	 }
      }
    }						/* end for loop		*/

  tcsetattr(oc_fd, TCSANOW, &savetty);		/* reset line		*/
  close(oc_fd);					/* close line		*/
  exit(0);
}

