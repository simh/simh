#include "osd.h"

/* OS-dependant code to get a character from the user.
 * This function must not block, and must either return an ASCII code or -1.
 */
#include <conio.h>
int osd_get_char(void)
{
	int ch = -1;
	if(kbhit())
	{
		while(kbhit())
			ch = getch();
	}
	return ch;
}
