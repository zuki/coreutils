/* Save and restore the working directory, possibly using a child process.

   Copyright (C) 2006-2007, 2009-2025 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* Written by Paul Eggert.  */

#include <config.h>

#define SAVEWD_INLINE _GL_EXTERN_INLINE

#include "savewd.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "assure.h"
#include "attribute.h"
#include "filename.h"

#if GNULIB_FCNTL_SAFER
# include "fcntl--.h"
#endif

#if defined _WIN32 && !defined __CYGWIN__
# define fork() (assure (false), -1)
#endif

/* Save the working directory into *WD, if it hasn't been saved
   already.  Return true if a child has been forked to do the real
   work.  */
static bool
savewd_save (struct savewd *wd)
{
  switch (wd->state)
    {
    case INITIAL_STATE:
      /* Save the working directory, or prepare to fall back if possible.  */
      {
        int fd = open (".", O_SEARCH);
        if (0 <= fd)
          {
            wd->state = FD_STATE;
            wd->val.fd = fd;
            break;
          }

        /* There is no point to forking on native MS-Windows which lacks 'fork'.
           Although there is also no point on systems that support O_SEARCH,
           macOS 12.6 APFS open (".", O_SEARCH) incorrectly fails if "." is
           mode 311, so do not attempt to optimize for O_SEARCH != O_RDONLY.  */
# if defined _WIN32 && !defined __CYGWIN__
        bool try_fork = false;
# else
        bool try_fork = errno == EACCES || errno == ESTALE;
# endif
        if (!try_fork)
          {
            wd->state = ERROR_STATE;
            wd->val.errnum = errno;
            break;
          }
      }
      wd->state = FORKING_STATE;
      wd->val.child = -1;
      FALLTHROUGH;
    case FORKING_STATE:
      if (wd->val.child < 0)
        {
          /* "Save" the initial working directory by forking a new
             subprocess that will attempt all the work from the chdir
             until the next savewd_restore.  */
          wd->val.child = fork ();
          if (wd->val.child != 0)
            {
              if (0 < wd->val.child)
                return true;
              wd->state = ERROR_STATE;
              wd->val.errnum = errno;
            }
        }
      break;

    case FD_STATE:
    case FD_POST_CHDIR_STATE:
    case ERROR_STATE:
    case FINAL_STATE:
      break;

    default:
      assure (false);
    }

  return false;
}

int
savewd_chdir (struct savewd *wd, char const *dir, int options,
              int open_result[2])
{
  int fd = -1;
  int result = 0;

  /* Open the directory if requested, or if avoiding a race condition
     is requested and possible.  */
  if (open_result
      || (options & (HAVE_WORKING_O_NOFOLLOW ? SAVEWD_CHDIR_NOFOLLOW : 0)))
    {
      fd = open (dir,
                 (O_SEARCH | O_DIRECTORY | O_NOCTTY | O_NONBLOCK
                  | (options & SAVEWD_CHDIR_NOFOLLOW ? O_NOFOLLOW : 0)));

      if (open_result)
        {
          open_result[0] = fd;
          open_result[1] = errno;
        }

      if (fd < 0 && errno != EACCES)
        result = -1;
    }

  if (result == 0 && ! (0 <= fd && options & SAVEWD_CHDIR_SKIP_READABLE))
    {
      if (savewd_save (wd))
        {
          open_result = NULL;
          result = -2;
        }
      else
        {
          result = (fd < 0 ? chdir (dir) : fchdir (fd));

          if (result == 0)
            switch (wd->state)
              {
              case FD_STATE:
                wd->state = FD_POST_CHDIR_STATE;
                break;

              case ERROR_STATE:
              case FD_POST_CHDIR_STATE:
              case FINAL_STATE:
                break;

              case FORKING_STATE:
                assure (wd->val.child == 0);
                break;

              default:
                assure (false);
              }
        }
    }

  if (0 <= fd && ! open_result)
    {
      int e = errno;
      close (fd);
      errno = e;
    }

  return result;
}

int
savewd_restore (struct savewd *wd, int status)
{
  switch (wd->state)
    {
    case INITIAL_STATE:
    case FD_STATE:
      /* The working directory is the desired directory, so there's no
         work to do.  */
      break;

    case FD_POST_CHDIR_STATE:
      /* Restore the working directory using fchdir.  */
      if (fchdir (wd->val.fd) == 0)
        {
          wd->state = FD_STATE;
          break;
        }
      else
        {
          int chdir_errno = errno;
          close (wd->val.fd);
          wd->state = ERROR_STATE;
          wd->val.errnum = chdir_errno;
        }
      FALLTHROUGH;
    case ERROR_STATE:
      /* Report an error if asked to restore the working directory.  */
      errno = wd->val.errnum;
      return -1;

    case FORKING_STATE:
      /* "Restore" the working directory by waiting for the subprocess
         to finish.  */
      {
        pid_t child = wd->val.child;
        if (child == 0)
          _exit (status);
        if (0 < child)
          {
            int child_status;
            while (waitpid (child, &child_status, 0) < 0)
              assure (errno == EINTR);
            wd->val.child = -1;
            if (! WIFEXITED (child_status))
              raise (WTERMSIG (child_status));
            return WEXITSTATUS (child_status);
          }
      }
      break;

    default:
      assure (false);
    }

  return 0;
}

void
savewd_finish (struct savewd *wd)
{
  switch (wd->state)
    {
    case ERROR_STATE:
      break;

    case FD_STATE:
    case FD_POST_CHDIR_STATE:
      close (wd->val.fd);
      FALLTHROUGH;
    case INITIAL_STATE:
      wd->val.errnum = 0;
      break;

    case FORKING_STATE:
      assure (wd->val.child < 0);
      wd->val.errnum = 0;
      break;

    default:
      assure (false);
    }

  wd->state = FINAL_STATE;
}

/* Return true if the actual work is currently being done by a
   subprocess.

   A true return means that the caller and the subprocess should
   resynchronize later with savewd_restore, using only their own
   memory to decide when to resynchronize; they should not consult the
   file system to decide, because that might lead to race conditions.
   This is why savewd_chdir is broken out into another function;
   savewd_chdir's callers _can_ inspect the file system to decide
   whether to call savewd_chdir.  */
static bool
savewd_delegating (struct savewd const *wd)
{
  return wd->state == FORKING_STATE && 0 < wd->val.child;
}

int
savewd_process_files (int n_files, char **file,
                      int (*act) (char *, struct savewd *, void *),
                      void *options)
{
  int i = 0;
  int last_relative;
  int exit_status = EXIT_SUCCESS;
  struct savewd wd;
  savewd_init (&wd);

  for (last_relative = n_files - 1; 0 <= last_relative; last_relative--)
    if (! IS_ABSOLUTE_FILE_NAME (file[last_relative]))
      break;

  for (; i < last_relative; i++)
    {
      if (! savewd_delegating (&wd))
        {
          int s = act (file[i], &wd, options);
          if (exit_status < s)
            exit_status = s;
        }

      if (! IS_ABSOLUTE_FILE_NAME (file[i + 1]))
        {
          int r = savewd_restore (&wd, exit_status);
          if (exit_status < r)
            exit_status = r;
        }
    }

  savewd_finish (&wd);

  for (; i < n_files; i++)
    {
      int s = act (file[i], &wd, options);
      if (exit_status < s)
        exit_status = s;
    }

  return exit_status;
}
