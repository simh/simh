/* sim_iso_names.h
 *
 * Provide renames for deprecated POSIX function names, providing a shim if/when other platforms need similar functionality.
 * This seems to only affect MSVC/Win32/Win64 platforms.
 * .
 *
 * Author: B. Scott Michel
 * "scooter me fecit"
 */
#if !defined(SIM_ISO_NAMES_H)

#if !defined(USE_ISO_NAMES)
#if (defined(_WIN32_WINNT) && (_WIN32_WINNT >= _WIN32_WINNT_WIN7)) || (defined(_MSC_VER) && (_MSC_VER >= 1900))
#define USE_ISO_NAMES
#endif
#endif /* USE_ISO_NAMES */

#if defined(USE_ISO_NAMES)
#define sim_chdir _chdir
#define sim_fileno _fileno
#define sim_getpid _getpid
#define sim_mkdir _mkdir
#define sim_mktemp _mktemp
#define sim_rmdir _rmdir
#define sim_strcmpi _stricmp
#define sim_strdup _strdup
#define sim_strnicmp _strnicmp
#define sim_unlink _unlink
#else
#define sim_chdir chdir
#define sim_fileno fileno
#define sim_getpid getpid
#define sim_mkdir mkdir
#define sim_mktemp mktemp
#define sim_rmdir rmdir
#define sim_strcmpi strcmpi
#define sim_strdup strdup
#define sim_strnicmp strnicmp
#define sim_unlink unlink
#endif

#define SIM_ISO_NAMES_H
#endif /* SIM_ISO_NAMES_H */
