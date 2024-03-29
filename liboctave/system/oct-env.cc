/*

Copyright (C) 1996-2019 John W. Eaton

This file is part of Octave.

Octave is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Octave is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Octave; see the file COPYING.  If not, see
<https://www.gnu.org/licenses/>.

*/

/*

The functions listed below were adapted from a similar functions
from GNU Bash, the Bourne Again SHell, copyright (C) 1987, 1989, 1991
Free Software Foundation, Inc.

  octave::sys::env::do_absolute_pathname
  octave::sys:env::do_base_pathname
  octave::sys:env::do_chdir
  octave::sys:env::do_getcwd
  octave::sys:env::do_make_absolute
  octave::sys:env::do_polite_directory_format
  octave::sys:env::pathname_backup

*/

#if defined (HAVE_CONFIG_H)
#  include "config.h"
#endif

#include <cctype>
#include <cstdlib>
#include <cstring>

#include <string>

#include "file-ops.h"
#include "lo-error.h"
#include "lo-sysdep.h"
#include "lo-utils.h"
#include "oct-env.h"
#include "oct-passwd.h"
#include "oct-syscalls.h"
#include "set-program-name-wrapper.h"
#include "singleton-cleanup.h"
#include "unistd-wrappers.h"

namespace octave
{
  namespace sys
  {
    env::env (void)
      : follow_symbolic_links (true), verbatim_pwd (true),
        current_directory (), prog_name (), prog_invocation_name (),
        user_name (), host_name ()
    {
      // Get a real value for the current directory.
      do_getcwd ();

      // Etc.
      do_get_user_name ();

      do_get_host_name ();
    }

    env *env::instance = nullptr;

    bool
    env::instance_ok (void)
    {
      bool retval = true;

      if (! instance)
        {
          instance = new env ();
          singleton_cleanup_list::add (cleanup_instance);
        }

      return retval;
    }

    std::string
    env::polite_directory_format (const std::string& name)
    {
      return (instance_ok ())
        ? instance->do_polite_directory_format (name) : "";
    }

    bool
    env::absolute_pathname (const std::string& s)
    {
      return (instance_ok ())
        ? instance->do_absolute_pathname (s) : false;
    }

    bool
    env::rooted_relative_pathname (const std::string& s)
    {
      return (instance_ok ())
        ? instance->do_rooted_relative_pathname (s) : false;
    }

    std::string
    env::base_pathname (const std::string& s)
    {
      return (instance_ok ())
        ? instance->do_base_pathname (s) : "";
    }

    std::string
    env::make_absolute (const std::string& s, const std::string& dot_path)
    {
      return (instance_ok ())
        ? instance->do_make_absolute (s, dot_path) : "";
    }

    std::string
    env::get_current_directory ()
    {
      return (instance_ok ())
        ? instance->do_getcwd () : "";
    }

    std::string
    env::get_home_directory ()
    {
      return (instance_ok ())
        ? instance->do_get_home_directory () : "";
    }

    std::string
    env::get_temp_directory ()
    {
      return (instance_ok ())
        ? instance->do_get_temp_directory () : "";
    }

    std::string
    env::get_program_name (void)
    {
      return (instance_ok ())
        ? instance->prog_name : "";
    }

    std::string
    env::get_program_invocation_name (void)
    {
      return (instance_ok ())
        ? instance->prog_invocation_name : "";
    }

    void
    env::set_program_name (const std::string& s)
    {
      if (instance_ok ())
        instance->do_set_program_name (s);
    }

    std::string
    env::get_user_name (void)
    {
      return (instance_ok ())
        ? instance->do_get_user_name () : "";
    }

    std::string
    env::get_host_name (void)
    {
      return (instance_ok ())
        ? instance->do_get_host_name () : "";
    }

    std::string
    env::do_get_temp_directory (void) const
    {
      std::string tempd;

#if defined (__MINGW32__) || defined (_MSC_VER)

      tempd = do_getenv ("TEMP");

      if (tempd.empty ())
        tempd = do_getenv ("TMP");

#if defined (P_tmpdir)
      if (tempd.empty ())
        tempd = P_tmpdir;
#endif

      // Some versions of MinGW and MSVC either don't define P_tmpdir, or
      // define it to a single backslash.  In such cases just use C:\temp.
      if (tempd.empty () || tempd == R"(\)")
        tempd = R"(c:\temp)";

#else

      tempd = do_getenv ("TMP");

#if defined (P_tmpdir)
      if (tempd.empty ())
        tempd = P_tmpdir;
#else
      if (tempd.empty ())
        tempd = "/tmp";
#endif

#endif

      return tempd;
    }

    // FIXME: this leaves no way to distinguish between a
    // variable that is not set and one that is set to the empty string.
    // Is this a problem?

    std::string
    env::getenv (const std::string& name)
    {
      return (instance_ok ())
        ? instance->do_getenv (name) : "";
    }

    void
    env::putenv (const std::string& name, const std::string& value)
    {
      putenv_wrapper (name, value);
    }

    bool
    env::have_x11_display (void)
    {
      std::string display = getenv ("DISPLAY");

      return ! display.empty ();
    }

    bool
    env::chdir (const std::string& newdir)
    {
      return (instance_ok ())
        ? instance->do_chdir (newdir) : false;
    }

    void
    env::do_set_program_name (const std::string& s) const
    {
      static bool initialized = false;

      if (! initialized)
        {
          // octave_set_program_name_wrapper returns a cleaned up
          // version of the program name (stripping libtool's "lt-"
          // prefix, for example).

          // The string passed to gnulib's ::set_program_name function must
          // exist for the duration of the program so allocate a copy here
          // instead of passing S.c_str () which only exists as long as the
          // string object S.

          prog_invocation_name
            = octave_set_program_name_wrapper (strsave (s.c_str ()));

          size_t pos
            = prog_invocation_name.find_last_of (sys::file_ops::dir_sep_chars ());

          // Also keep a shortened version of the program name.
          prog_name = (pos == std::string::npos
                       ? prog_invocation_name
                       : prog_invocation_name.substr (pos+1));

          initialized = true;
        }
    }

    // Return a pretty pathname.  If the first part of the pathname is the
    // same as $HOME, then replace that with '~'.

    std::string
    env::do_polite_directory_format (const std::string& name) const
    {
      std::string retval;

      std::string home_dir = do_get_home_directory ();

      size_t len = home_dir.length ();

      if (len > 1 && home_dir == name.substr (0, len)
          && (name.length () == len || sys::file_ops::is_dir_sep (name[len])))
        {
          retval = "~";
          retval.append (name.substr (len));
        }
      else
        retval = name;

      return retval;
    }

    bool
    env::do_absolute_pathname (const std::string& s) const
    {
      size_t len = s.length ();

      if (len == 0)
        return false;

      if (sys::file_ops::is_dir_sep (s[0]))
        return true;

#if defined (OCTAVE_HAVE_WINDOWS_FILESYSTEM)
      if ((len == 2 && isalpha (s[0]) && s[1] == ':')
          || (len > 2 && isalpha (s[0]) && s[1] == ':'
              && sys::file_ops::is_dir_sep (s[2])))
        return true;
#endif

      return false;
    }

    bool
    env::do_rooted_relative_pathname (const std::string& s) const
    {
      size_t len = s.length ();

      if (len == 0)
        return false;

      if (len == 1 && s[0] == '.')
        return true;

      if (len > 1 && s[0] == '.' && sys::file_ops::is_dir_sep (s[1]))
        return true;

      if (len == 2 && s[0] == '.' && s[1] == '.')
        return true;

      if (len > 2 && s[0] == '.' && s[1] == '.'
          && sys::file_ops::is_dir_sep (s[2]))
        return true;

      return false;
    }

    // Return the 'basename' of the pathname in STRING (the stuff after
    // the last directory separator).  If STRING is not a full pathname,
    // simply return it.

    std::string
    env::do_base_pathname (const std::string& s) const
    {
      if (! (do_absolute_pathname (s) || do_rooted_relative_pathname (s)))
        return s;

      size_t pos = s.find_last_of (sys::file_ops::dir_sep_chars ());

      if (pos == std::string::npos)
        return s;
      else
        return s.substr (pos+1);
    }

    // Turn STRING (a pathname) into an absolute pathname, assuming that
    // DOT_PATH contains the symbolic location of the current directory.

    std::string
    env::do_make_absolute (const std::string& s,
                           const std::string& dot_path) const
    {
      if (dot_path.empty () || s.empty () || do_absolute_pathname (s))
        return s;

      // Optimization: every time Octave returns to the prompt it calls
      // make_absolute_filename with '.' as argument.
      if (s == ".")
        return dot_path;

      std::string current_dir = dot_path;

      if (! sys::file_ops::is_dir_sep (current_dir.back ()))
        current_dir.append (sys::file_ops::dir_sep_str ());

      size_t i = 0;
      size_t slen = s.length ();

      while (i < slen)
        {
          if (s[i] == '.')
            {
              if (i + 1 == slen)
                break;

              if (sys::file_ops::is_dir_sep (s[i+1]))
                {
                  i += 2;
                  continue;
                }

              if (s[i+1] == '.'
                  && (i + 2 == slen
                      || sys::file_ops::is_dir_sep (s[i+2])))
                {
                  i += 2;
                  if (i != slen)
                    i++;

                  pathname_backup (current_dir, 1);

                  continue;
                }
            }

          size_t sep_pos;
          sep_pos = s.find_first_of (sys::file_ops::dir_sep_chars (), i);

          if (sep_pos == std::string::npos)
            {
              current_dir.append (s, i, sep_pos-i);
              break;
            }
          else if (sep_pos == i)
            {
              /* Two separators in a row, skip adding 2nd separator */
              i++;
            }
          else
            {
              current_dir.append (s, i, sep_pos-i+1);
              i = sep_pos + 1;
            }
        }

      // Strip any trailing directory separator
      if (sys::file_ops::is_dir_sep (current_dir.back ()))
        current_dir.pop_back ();

      return current_dir;
    }

    // Return a string which is the current working directory.

    std::string
    env::do_getcwd () const
    {
      if (! follow_symbolic_links)
        current_directory = "";

      if (verbatim_pwd || current_directory.empty ())
        current_directory = sys::getcwd ();

      return current_directory;
    }

    // This value is not cached because it can change while Octave is
    // running.

    std::string
    env::do_get_home_directory (void) const
    {
      std::string hd = do_getenv ("HOME");

#if defined (__MINGW32__) || defined (_MSC_VER)
      // Maybe we are started directly from cmd.exe.
      if (hd.empty ())
        {
          std::string drv = do_getenv ("HOMEDRIVE");
          if (drv.empty ())
            hd = do_getenv ("HOMEPATH");
          else
            hd = drv + do_getenv ("HOMEPATH");
        }
#endif

      if (hd.empty ())
        {
          sys::password pw = sys::password::getpwuid (
                                       sys::getuid ());

          hd = (pw ? pw.dir () : std::string (sys::file_ops::dir_sep_str ()));
        }

      return hd;
    }

    std::string
    env::do_get_user_name (void) const
    {
      if (user_name.empty ())
        {
          sys::password pw = sys::password::getpwuid (
                                       sys::getuid ());

          user_name = (pw ? pw.name () : "unknown");
        }

      return user_name;
    }

    std::string
    env::do_get_host_name (void) const
    {
      if (host_name.empty ())
        {
          char hostname[1024];

          int status = octave_gethostname_wrapper (hostname, 1023);

          host_name = (status < 0) ? "unknown" : hostname;
        }

      return host_name;
    }

    std::string
    env::do_getenv (const std::string& name) const
    {
      return getenv_wrapper (name);
    }

    // Do the work of changing to the directory NEWDIR.
    // Handle symbolic link following, etc.

    bool
    env::do_chdir (const std::string& newdir)
    {
      bool retval = false;

      std::string tmp;

      if (follow_symbolic_links)
        {
          if (current_directory.empty ())
            do_getcwd ();

          if (current_directory.empty ())
            tmp = newdir;
          else
            tmp = do_make_absolute (newdir, current_directory);

          // Get rid of trailing directory separator.
          if (tmp.length () > 1 && sys::file_ops::is_dir_sep (tmp.back ()))
            tmp.pop_back ();

          if (! sys::chdir (tmp))
            {
              current_directory = tmp;
              retval = true;
            }
        }
      else
        retval = (! sys::chdir (newdir));

      return retval;
    }

    // Remove the last N directories from PATH.

    void
    env::pathname_backup (std::string& path, int n) const
    {
      if (path.empty ())
        return;

      size_t i = path.length () - 1;

      while (n--)
        {
          while (sys::file_ops::is_dir_sep (path[i]) && i > 0)
            i--;

#if defined (OCTAVE_HAVE_WINDOWS_FILESYSTEM)
          // Don't strip file letter part.
          if (i == 1 && path[i] == ':')
            {
              // Keep path separator if present.
              i = std::min (i+2, path.length ());
              break;
            }
#endif

          while (! sys::file_ops::is_dir_sep (path[i]) && i > 0)
            i--;

          i++;
        }

      path.resize (i);
    }

    void
    env::error (int err_num) const
    {
      (*current_liboctave_error_handler) ("%s", std::strerror (err_num));
    }

    void
    env::error (const std::string& s) const
    {
      (*current_liboctave_error_handler) ("%s", s.c_str ());
    }
  }
}
