/* scp_help.h: hierarchical help definitions

   Copyright (c) 2013, Timothe Litt

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.
*/

#ifndef SCP_HELP_H_
#define SCP_HELP_H_  0

/* The SCP structured help uses help text that defines a hierarchy of information
 * organized into topics and subtopics.  This is modelled on the VMS help command.
 *
 * This arrangement allows the long help messages being used in many devices to be
 * organized to be easier to approach and navigate.
 *
 * The helpx utility (a Perl script) will read a device source file
 * that conforms to the previous conventions, and produce a template and
 * initial translation into the format described here.
 *
 * The structure of the help text is:
 *
 * Lines beginning with whitespace are displayed as part of the current topic, except:
 *   * The leading white space is replaced by a standard indentation of 4 spaces.
 *     Additional indentation, where appropriate, can be obtained with '+', 4 spaces each.
 *
 *   * The following % escapes are recognized:
 *     * %D    - Inserts the name of the device     (e.g. "DTA").
 *     * %U    - Inserts the name of the unit       (e.g. "DTA0").
 *     * %S    - Inserts the current simulator name (e.g. "PDP-10")
 *     * %#s   - Inserts the string suppled in the "#"th optional argument to the help
 *               routine.  # starts with 1.  Any embedded newlines will cause following
 *               text to be indented.
 *     * %#H   - Appends the #th optional argument to the help text.  Use to add common
 *               help to device specific help.  The text is placed AFTER the current help
 *               string, and after any previous %H inclusions.  Parameter numbers restart
 *               with the new string, following the last parameter used by the previous tree.
 *     * %%    - Inserts a literal %.
 *     * %+    - Inserts a literal +.
 *      - Any other escape is reserved and will cause an exception.  However, the goal
 *        is to provide help, not a general formatting facility.  Use sprintf to a
 *        local buffer, and pass that as a string if more general formatting is required.
 *
 * Lines beginning with a number introduce a subtopic of the device.  The number indicates
 * the subtopic's place in the help hierarchy.  Topics offered as Additional Information
 * under the device's main topic are at level 1.  Their sub-topics are at level 2, and
 * so on.  Following the number is a string that names the sub-topic.  This is displayed,
 * and what the user types to access the information.  Whitespace in the topic name is
 * typed as an underscore (_).  Topic names beginning with '$' invoke other kinds of help,
 * These are:
 *    $Registers     - Displays the device register help
 *    $Set commands  - Displays the standard SET command help.
 *    $Show commands - Displays the standard SHOW command help.
 *
 * For these special topics, any text that you provide will be added after
 * the output from the system routines.  This allows you to add more information, or
 * an introduction to subtopics with more detail.
 *
 * Topic names that begin with '?' are conditional topics.
 * Some devices adopt radically different personalities at runtime,
 * e.g. when attached to a processor with different bus.
 * In rare cases, it's better not to include help that doesn't apply.
 * For these cases, ?#, where # is a 1-based parameter number, can be used
 * to selectively include a topic.  If the specified parameter is TRUE
 * (a string with the value "T", "t" or '1'), the topic will be visible.
 * If the parameter is FALSE (NULL, or a string with any other value), 
 * the topic will not be visible.
 *
 * If it can be determined at compile time whether the topic in question
 * is needed, #ifdef around those lines of the help is a better choice.
 *
 * If both $ and ? are used, ? comes first.
 *
 * Guidelines:
 *   Help should be concise and easy to understand.
 *
 *   The main topic should be short - less than a sceenful when presented with the
 *   subtopic list.
 *
 *   Keep line lengths to 76 columns or less.
 *
 *   Follow the subtopic naming conventions (under development) for a consistent style:
 *
 *   At the top level, the device should be summarized in a few sentences.
 *   The subtopics for detail should be:
 *     Hardware Description - The details of the hardware.  Feeds & speeds are OK here.
 *          Models          -   If the device was offered in distinct models, a subtopic for each.
 *          Registers       -   Register descriptions
 *            
 *     Configuration         - How to configure the device under SimH.  SET commands.
 *          Operating System -   If the device needs special configuration for a particular
 *                               OS, a subtopic for each such OS goes here.
 *          Files            - If the device uses external files (tapes, cards, disks, configuration)
 *                             A subtopic for each here.
 *          Examples         - Provide usable examples for configuring complex devices.
 *
 *     Operation             - How to operate the device under SimH.  Attach, runtime events
 *                             (e.g. how to load cards or mount a tape)
 *
 *     Monitoring            - How to obtain status (SHOW commands)
 *
 *     Restrictions          - If some aspects of the device aren't emulated, list them here.
 *
 *     Debugging             - Debugging information
 *
 *     Related Devices       - If devices are configured or used together, list the other devices here.
 *                             E.G. The DEC KMC/DUP are two hardware devices that are closely related;
 *                             The KMC controlls the DUP on behalf of the OS.
 *
 * This text can be created by any convenient means.  It can be mechanically extracted from the device
 * source, read from a file, or simply entered as a string in the help routine.  To facilitate the latter,
 * this file defines two convenience macros:
 *
 *   L(text)     - provides a string with a leading space and a trailing \n.  Enter a line of topic text.
 *   T(n, NAME)  - provides a string with the topic level n and the topic name NAME, and a trailing \n.
 *
 * These are concatenated normally, e.g.
   const char *const help =
    L (The %D device is interesting)
    L (It has lots of help options)
    T (1, TOPIC 1)
    L (And this is topic 1)
    ;
 *
 * API:
 *  To make use of this type of help in your device, create (or replace) a help routine with one that
 *   calls scp_help.  Most of the arguments are the same as those of the device help routine.
 *
 *  t_stat scp_help (FILE *st, DEVICE *dptr,
 *                   UNIT *uptr, int flag, const char *help, char *cptr, ...)
 *
 *  If you need to pass the variable argument list from another routine, use:
 * 
 *  t_stat scp_vhelp (FILE *st, DEVICE *dptr,
 *                    UNIT *uptr, int flag, const char *help, char *cptr, va_list ap)
 *
 *  To obtain the help from an external file (Note this reads the entire file into memory):
 *  t_stat scp_helpFromFile (FILE *st, DEVICE *dptr,
 *                            UNIT *uptr, int flag, const char *helpfile, char *cptr, ...)
 *  and for va_list:
 *  t_stat scp_vhelpFromFile (FILE *st, DEVICE *dptr,
 *                            UNIT *uptr, int flag, const char *helpfile, char *cptr, va_list ap) {
 *
 * dptr and uptr are only used if the %D and/or %U escapes are encountered.
 * help is the help text; helpfile is the help file name.
 *
 * flag is usually the flag from the help command dispatch.  SCP_HELP_FLAT is set in non-interactive
 * environments.  When this flag, or DEV_FLATHELP in DEVICE.flags is set, the entire help text
 * will be flattened and displayed in outline form.
 *
 * Help files are easier to edit, but can become separated from the SimH executable.  Finding them
 * at runtime can also be a challenge.  SimH tries...but the project standard is to embed help
 * as strings in the device.  (It may be easier to develop help as a file before converting it
 * to a string.)
 *
 * Lines beginning with ';' will be ignored.
 * 
 * Here is a worked-out example:
 *
;****************************************************************************
 The Whizbang 100 is a DMA line printer controller used on the Whizbang 1000
 and Gurgle 1200 processor familes of the Obsolete Hardware Corporation.
1 Hardware Description
 The Whizbang 100 is specified to operate "any printer you and a friend can
 lift", and speeds up to 0.5 C.

 The controller requires a refrigerator-sized box, consumes 5.5KW, and is
 liquid cooled.  It uses GBL (Granite Baked Logic).

 Painted a cool blue, it consistently won industrial design awards, even
 as mechanically, it was less than perfect.  Plumbers had full employment.
2 Models
 The Whizbang 100 model G was commissioned by a government agency, which
 insisted on dull gray paint, and speeds limited to 11 MPH.

 The Whizbang 100 Model X is powered by the improbability drive, and is
 rarely seen once installed.
2 $Registers
 The two main registers are the Print Control register and the Print Data
 register.  The Print Maintenance register is usually broken.
3 Print Control register
  Bit 0 turns the attached printer on when set, off when clear.
  Bit 1 ejects the current page
  Bit 2 ejects the operator
  Bit 3 enables interrupts
3 Print data register
  The print data register is thiry-seven bits wide, and accepts data in
  elephantcode, the precursor to Unicode.  Paper advance is accomplished
  with the Rocket Return and Page Trampoline characters.
1 Configuration
  The Whizbang 100 requires 4 address slots on the LooneyBus.
+  SET WHIZBANG LUNA 11
  will assign the controller to its default bus address.
2 $Set commands
  The output codeset can be ASCII or elephantcode
+ SET WHIZBANG CODESET ASCII
+   SET WHIZBANG CODESET ELEPHANTCODE

  The VFU (carriage control tape) is specifed with
+ SET WHIZBANG TAPE vfufile
2 WOS
  Under WOS, the device will only work at LooneyBus slot 9
2 RTG
  The RTG driver has been lost.  It is not known if the
  Whizbang will operate correctly.
2 Files
  The VFU is programmed with an ASCII text file.  Each line of the
  file corresponds to a line of the form.  Enter the channel numbers
  as base 33 roman numerals.
2 Examples
  TBS
1 Operation
  Specify the host file to receive output using the 
+ATTACH WHIZBANG filespec
 command.
1 Monitoring
  The Whizbang has no lights or switches.  The model X may be located
  with the
+SHOW WHIZBANG LOCATION
 simulator command.
2 $Show commands
1 Restrictions
 The emulator is limited to a single Whizbang controller.
1 Debugging
 The only implemented debugging command is
+ SET WHIZBANG DEBUG=PRAY
 To stop:
+ SET WHIZBANG NODEBUG=PRAY
1 Related Devices
  See also the Whizbang paper shredder (SHRED).
 *
 */

#define T(level, text) #level " " #text "\n"
#define L(text) " " #text "\n"

#endif
