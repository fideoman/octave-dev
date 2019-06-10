/*

Copyright (C) 1993-2019 John W. Eaton

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

#if defined (HAVE_CONFIG_H)
#  include "config.h"
#endif

#include <cstdio>

#include <string>
#include <iostream>

#include "cmd-edit.h"
#include "cmd-hist.h"
#include "file-stat.h"
#include "fpucw-wrappers.h"
#include "lo-blas-proto.h"
#include "lo-error.h"
#include "oct-env.h"
#include "str-vec.h"
#include "signal-wrappers.h"
#include "unistd-wrappers.h"

#include "builtin-defun-decls.h"
#include "defaults.h"
#include "Cell.h"
#include "call-stack.h"
#include "defun.h"
#include "display.h"
#include "error.h"
#include "file-io.h"
#include "graphics.h"
#include "help.h"
#include "input.h"
#include "interpreter-private.h"
#include "interpreter.h"
#include "load-path.h"
#include "load-save.h"
#include "octave-link.h"
#include "octave.h"
#include "oct-hist.h"
#include "oct-map.h"
#include "oct-mutex.h"
#include "ovl.h"
#include "ov.h"
#include "ov-classdef.h"
#include "parse.h"
#include "pt-classdef.h"
#include "pt-eval.h"
#include "pt-jump.h"
#include "pt-stmt.h"
#include "settings.h"
#include "sighandlers.h"
#include "sysdep.h"
#include "unwind-prot.h"
#include "utils.h"
#include "variables.h"
#include "version.h"

// TRUE means the quit() call is allowed.
bool quit_allowed = true;

// TRUE means we are ready to interpret commands, but not everything
// is ready for interactive use.
bool octave_interpreter_ready = false;

// TRUE means we've processed all the init code and we are good to go.
bool octave_initialized = false;

DEFUN (__version_info__, args, ,
       doc: /* -*- texinfo -*-
@deftypefn {} {retval =} __version_info__ (@var{name}, @var{version}, @var{release}, @var{date})
Undocumented internal function.
@end deftypefn */)
{
  static octave_map vinfo;

  int nargin = args.length ();

  if (nargin != 0 && nargin != 4)
    print_usage ();

  octave_value retval;

  if (nargin == 0)
    retval = vinfo;
  else if (nargin == 4)
    {
      if (vinfo.nfields () == 0)
        {
          vinfo.assign ("Name", args(0));
          vinfo.assign ("Version", args(1));
          vinfo.assign ("Release", args(2));
          vinfo.assign ("Date", args(3));
        }
      else
        {
          octave_idx_type n = vinfo.numel () + 1;

          vinfo.resize (dim_vector (n, 1));

          octave_value idx (n);

          vinfo.assign (idx, "Name", Cell (octave_value (args(0))));
          vinfo.assign (idx, "Version", Cell (octave_value (args(1))));
          vinfo.assign (idx, "Release", Cell (octave_value (args(2))));
          vinfo.assign (idx, "Date", Cell (octave_value (args(3))));
        }
    }

  return retval;
}

DEFUN (quit, args, ,
       doc: /* -*- texinfo -*-
@deftypefn  {} {} exit
@deftypefnx {} {} exit (@var{status})
@deftypefnx {} {} quit
@deftypefnx {} {} quit (@var{status})
Exit the current Octave session.

If the optional integer value @var{status} is supplied, pass that value to
the operating system as Octave's exit status.  The default value is zero.

When exiting, Octave will attempt to run the m-file @file{finish.m} if it
exists.  User commands to save the workspace or clean up temporary files
may be placed in that file.  Alternatively, another m-file may be scheduled
to run using @code{atexit}.
@seealso{atexit}
@end deftypefn */)
{
  // Confirm OK to shutdown.  Note: A dynamic function installation similar
  // to overriding polymorphism for which the GUI can install its own "quit"
  // yet call this base "quit" could be nice.  No link would be needed here.
  if (! octave_link::confirm_shutdown ())
    return ovl ();

  if (! quit_allowed)
    error ("quit: not supported in embedded mode");

  int exit_status = 0;

  if (args.length () > 0)
    exit_status = args(0).nint_value ();

  // Instead of simply calling exit, we thrown an exception so that no
  // matter where the call to quit occurs, we will run the
  // unwind_protect stack, clear the OCTAVE_LOCAL_BUFFER allocations,
  // etc. before exiting.

  throw octave::exit_exception (exit_status);

  return ovl ();
}

DEFALIAS (exit, quit);

DEFUN (atexit, args, nargout,
       doc: /* -*- texinfo -*-
@deftypefn  {} {} atexit (@var{fcn})
@deftypefnx {} {} atexit (@var{fcn}, @var{flag})
Register a function to be called when Octave exits.

For example,

@example
@group
function last_words ()
  disp ("Bye bye");
endfunction
atexit ("last_words");
@end group
@end example

@noindent
will print the message @qcode{"Bye bye"} when Octave exits.

The additional argument @var{flag} will register or unregister @var{fcn}
from the list of functions to be called when Octave exits.  If @var{flag} is
true, the function is registered, and if @var{flag} is false, it is
unregistered.  For example, after registering the function @code{last_words}
above,

@example
atexit ("last_words", false);
@end example

@noindent
will remove the function from the list and Octave will not call
@code{last_words} when it exits.

Note that @code{atexit} only removes the first occurrence of a function
from the list, so if a function was placed in the list multiple times with
@code{atexit}, it must also be removed from the list multiple times.
@seealso{quit}
@end deftypefn */)
{
  int nargin = args.length ();

  if (nargin < 1 || nargin > 2)
    print_usage ();

  std::string arg = args(0).xstring_value ("atexit: FCN argument must be a string");

  bool add_mode = (nargin == 2)
    ? args(1).xbool_value ("atexit: FLAG argument must be a logical value")
    : true;

  octave_value_list retval;

  if (add_mode)
    octave::interpreter::add_atexit_function (arg);
  else
    {
      bool found = octave::interpreter::remove_atexit_function (arg);

      if (nargout > 0)
        retval = ovl (found);
    }

  return retval;
}

namespace octave
{
  // Execute commands from a file and catch potential exceptions in a consistent
  // way.  This function should be called anywhere we might parse and execute
  // commands from a file before we have entered the main loop in
  // toplev.cc.

  static int safe_source_file (const std::string& file_name,
                               const std::string& context = "",
                               bool verbose = false,
                               bool require_file = true,
                               const std::string& warn_for = "")
  {
    try
      {
        source_file (file_name, context, verbose, require_file, warn_for);
      }
    catch (const interrupt_exception&)
      {
        interpreter::recover_from_exception ();

        return 1;
      }
    catch (const execution_exception& e)
      {
        std::string stack_trace = e.info ();

        if (! stack_trace.empty ())
          std::cerr << stack_trace;

        interpreter::recover_from_exception ();

        return 1;
      }

    return 0;
  }

  static void initialize_version_info (void)
  {
    octave_value_list args;

    args(3) = OCTAVE_RELEASE_DATE;
    args(2) = config::release ();
    args(1) = OCTAVE_VERSION;
    args(0) = "GNU Octave";

    F__version_info__ (args, 0);
  }

  static void xerbla_abort (void)
  {
    error ("Fortran procedure terminated by call to XERBLA");
  }

  static void initialize_xerbla_error_handler (void)
  {
    // The idea here is to force xerbla to be referenced so that we will
    // link to our own version instead of the one provided by the BLAS
    // library.  But numeric_limits<double>::NaN () should never be -1, so
    // we should never actually call xerbla.  FIXME (again!): If this
    // becomes a constant expression the test might be optimized away and
    // then the reference to the function might also disappear.

    if (numeric_limits<double>::NaN () == -1)
      F77_FUNC (xerbla, XERBLA) ("octave", 13 F77_CHAR_ARG_LEN (6));

    typedef void (*xerbla_handler_ptr) (void);

    typedef void (*octave_set_xerbla_handler_ptr) (xerbla_handler_ptr);

    dynamic_library libs ("");

    if (libs)
      {
        octave_set_xerbla_handler_ptr octave_set_xerbla_handler
          = reinterpret_cast<octave_set_xerbla_handler_ptr>
          (libs.search ("octave_set_xerbla_handler"));

        if (octave_set_xerbla_handler)
          octave_set_xerbla_handler (xerbla_abort);
      }
  }

  OCTAVE_NORETURN static void
  lo_error_handler (const char *fmt, ...)
  {
    va_list args;
    va_start (args, fmt);
    verror_with_cfn (fmt, args);
    va_end (args);

    octave_throw_execution_exception ();
  }

  OCTAVE_NORETURN static void
  lo_error_with_id_handler (const char *id, const char *fmt, ...)
  {
    va_list args;
    va_start (args, fmt);
    verror_with_id_cfn (id, fmt, args);
    va_end (args);

    octave_throw_execution_exception ();
  }

  static void initialize_error_handlers (void)
  {
    set_liboctave_error_handler (lo_error_handler);
    set_liboctave_error_with_id_handler (lo_error_with_id_handler);
    set_liboctave_warning_handler (warning);
    set_liboctave_warning_with_id_handler (warning_with_id);
  }

  // Create an interpreter object and perform initialization up to the
  // point of setting reading command history and setting the load
  // path.

  interpreter::interpreter (application *app_context)
    : m_app_context (app_context),
      m_environment (),
      m_settings (),
      m_error_system (*this),
      m_help_system (*this),
      m_input_system (*this),
      m_output_system (*this),
      m_history_system (*this),
      m_dynamic_loader (*this),
      m_load_path (),
      m_load_save_system (*this),
      m_type_info (),
      m_symbol_table (*this),
      m_evaluator (*this),
      m_stream_list (*this),
      m_child_list (),
      m_url_handle_manager (),
      m_cdef_manager (*this),
      m_gtk_manager (),
      m_interactive (false),
      m_read_site_files (true),
      m_read_init_files (m_app_context != nullptr),
      m_verbose (false),
      m_inhibit_startup_message (false),
      m_load_path_initialized (false),
      m_history_initialized (false),
      m_initialized (false)
  {
    // FIXME: When thread_local storage is used by default, this message
    // should change to say something like
    //
    //   only one Octave interpreter may be active in any given thread

    if (instance)
      throw std::runtime_error
        ("only one Octave interpreter may be active");

    instance = this;

    // Matlab uses "C" locale for LC_NUMERIC class regardless of local setting
    setlocale (LC_NUMERIC, "C");
    setlocale (LC_TIME, "C");
    sys::env::putenv ("LC_NUMERIC", "C");
    sys::env::putenv ("LC_TIME", "C");

    // Initialize the default floating point unit control state.
    octave_set_default_fpucw ();

    thread::init ();

    octave_ieee_init ();

    initialize_xerbla_error_handler ();

    initialize_error_handlers ();

    if (m_app_context)
      {
        install_signal_handlers ();
        octave_unblock_signal_by_name ("SIGTSTP");
      }
    else
      quit_allowed = false;

    bool line_editing = false;
    bool traditional = false;

    if (m_app_context)
      {
        // Embedded interpeters don't execute command line options.
        const cmdline_options& options = m_app_context->options ();

        // Make all command-line arguments available to startup files,
        // including PKG_ADD files.

        string_vector args = options.all_args ();

        m_app_context->intern_argv (args);
        intern_nargin (args.numel () - 1);

        bool is_octave_program = m_app_context->is_octave_program ();

        std::list<std::string> command_line_path = options.command_line_path ();

        for (const auto& pth : command_line_path)
          m_load_path.set_command_line_path (pth);

        std::string exec_path = options.exec_path ();
        if (! exec_path.empty ())
          m_environment.exec_path (exec_path);

        std::string image_path = options.image_path ();
        if (! image_path.empty ())
          m_environment.image_path (image_path);

        if (options.no_window_system ())
          display_info::no_window_system ();

        // Is input coming from a terminal?  If so, we are probably
        // interactive.

        // If stdin is not a tty, then we are reading commands from a
        // pipe or a redirected file.
        bool stdin_is_tty = octave_isatty_wrapper (fileno (stdin));

        m_interactive = (! is_octave_program && stdin_is_tty
                         && octave_isatty_wrapper (fileno (stdout)));

        // Check if the user forced an interactive session.
        if (options.forced_interactive ())
          m_interactive = true;

        line_editing = options.line_editing ();
        if ((! m_interactive || options.forced_interactive ())
            && ! options.forced_line_editing ())
          line_editing = false;

        traditional = options.traditional ();

        // FIXME: if possible, perform the following actions directly
        // instead of using the interpreter-level functions.

        if (options.echo_commands ())
          m_evaluator.echo
            (tree_evaluator::ECHO_SCRIPTS | tree_evaluator::ECHO_FUNCTIONS
             | tree_evaluator::ECHO_ALL);

        std::string docstrings_file = options.docstrings_file ();
        if (! docstrings_file.empty ())
          Fbuilt_in_docstrings_file (*this, octave_value (docstrings_file));

        std::string doc_cache_file = options.doc_cache_file ();
        if (! doc_cache_file.empty ())
          Fdoc_cache_file (*this, octave_value (doc_cache_file));

        std::string info_file = options.info_file ();
        if (! info_file.empty ())
          Finfo_file (*this, octave_value (info_file));

        std::string info_program = options.info_program ();
        if (! info_program.empty ())
          Finfo_program (*this, octave_value (info_program));

        if (options.debug_jit ())
          Fdebug_jit (octave_value (true));

        if (options.jit_compiler ())
          Fjit_enable (octave_value (true));

        std::string texi_macros_file = options.texi_macros_file ();
        if (! texi_macros_file.empty ())
          Ftexi_macros_file (*this, octave_value (texi_macros_file));
      }

    m_input_system.initialize (line_editing);

    // These can come after command line args since none of them set any
    // defaults that might be changed by command line options.

    initialize_version_info ();

    // This should be done before initializing the load path because
    // some PKG_ADD files might need --traditional behavior.

    if (traditional)
      maximum_braindamage ();

    octave_interpreter_ready = true;
  }

  OCTAVE_THREAD_LOCAL interpreter *interpreter::instance = nullptr;

  interpreter::~interpreter (void)
  {
    cleanup ();
  }

  void interpreter::intern_nargin (octave_idx_type nargs)
  {
    m_evaluator.set_auto_fcn_var (stack_frame::NARGIN, nargs);
  }

  // Read the history file unless a command-line option inhibits that.

  void interpreter::initialize_history (bool read_history_file)
  {
    if (! m_history_initialized)
      {
        // Allow command-line option to override.

        if (m_app_context)
          {
            const cmdline_options& options = m_app_context->options ();

            read_history_file = options.read_history_file ();

            if (! read_history_file)
              command_history::ignore_entries ();
          }

        m_history_system.initialize (read_history_file);

        if (! m_app_context)
          command_history::ignore_entries ();

        m_history_initialized = true;
      }
  }

  // Set the initial path to the system default unless command-line
  // option says to leave it empty.

  void interpreter::initialize_load_path (bool set_initial_path)
  {
    if (! m_load_path_initialized)
      {
        // Allow command-line option to override.

        if (m_app_context)
          {
            const cmdline_options& options = m_app_context->options ();

            set_initial_path = options.set_initial_path ();
          }

        // Temporarily set the execute_pkg_add function to one that
        // catches exceptions.  This is better than wrapping
        // load_path::initialize in a try-catch block because it will
        // not stop executing PKG_ADD files at the first exception.
        // It's also better than changing the default execute_pkg_add
        // function to use safe_source file because that will normally
        // be evaluated from the normal intepreter loop where exceptions
        // are already handled.

        unwind_protect frame;

        frame.add_method (m_load_path, &load_path::set_add_hook,
                          m_load_path.get_add_hook ());

        m_load_path.set_add_hook ([this] (const std::string& dir)
                                  { this->execute_pkg_add (dir); });

        m_load_path.initialize (set_initial_path);

        m_load_path_initialized = true;
      }
  }

  // This may be called separately from execute

  void interpreter::initialize (void)
  {
    if (m_initialized)
      return;

    display_startup_message ();

    // Wait to read the history file until the interpreter reads input
    // files and begins evaluating commands.

    initialize_history ();

    // Initializing the load path may execute PKG_ADD files, so can't be
    // done until the interpreter is ready to execute commands.

    // Deferring it to the execute step also allows the path to be
    // initialized between creating and execute the interpreter, for
    // example, to set a custom path for an embedded interpreter.

    initialize_load_path ();

    m_initialized = true;
  }

  // FIXME: this function is intended to be executed only once.  Should
  // we enforce that restriction?

  int interpreter::execute (void)
  {
    try
      {
        initialize ();

        // We ignore errors in startup files.

        execute_startup_files ();

        int exit_status = 0;

        if (m_app_context)
          {
            const cmdline_options& options = m_app_context->options ();

            if (m_app_context->have_eval_option_code ())
              {
                int status = execute_eval_option_code ();

                if (status )
                  exit_status = status;

                if (! options.persist ())
                  return exit_status;
              }

            // If there is an extra argument, see if it names a file to
            // read.  Additional arguments are taken as command line options
            // for the script.

            if (m_app_context->have_script_file ())
              {
                int status = execute_command_line_file ();

                if (status)
                  exit_status = status;

                if (! options.persist ())
                  return exit_status;
              }

            if (options.forced_interactive ())
              command_editor::blink_matching_paren (false);
          }

        // Avoid counting commands executed from startup or script files.

        command_editor::reset_current_command_number (1);

        return main_loop ();
      }
    catch (const exit_exception& ex)
      {
        return ex.exit_status ();
      }
  }

  void interpreter::display_startup_message (void) const
  {
    bool inhibit_startup_message = false;

    if (m_app_context)
      {
        const cmdline_options& options = m_app_context->options ();

        inhibit_startup_message = options.inhibit_startup_message ();
      }

    if (m_interactive && ! inhibit_startup_message)
      std::cout << octave_startup_message () << "\n" << std::endl;
  }

  // Initialize by reading startup files.  Return non-zero if an exception
  // occurs when reading any of them, but don't exit early because of an
  // exception.

  int interpreter::execute_startup_files (void)
  {
    bool read_site_files = m_read_site_files;
    bool read_init_files = m_read_init_files;
    bool verbose = m_verbose;
    bool inhibit_startup_message = m_inhibit_startup_message;

    if (m_app_context)
      {
        const cmdline_options& options = m_app_context->options ();

        read_site_files = options.read_site_files ();
        read_init_files = options.read_init_files ();
        verbose = options.verbose_flag ();
        inhibit_startup_message = options.inhibit_startup_message ();
      }

    verbose = (verbose && ! inhibit_startup_message);

    bool require_file = false;

    std::string context;

    int exit_status = 0;

    if (read_site_files)
      {
        // Execute commands from the site-wide configuration file.
        // First from the file $(prefix)/lib/octave/site/m/octaverc
        // (if it exists), then from the file
        // $(prefix)/share/octave/$(version)/m/octaverc (if it exists).

        int status = safe_source_file (config::local_site_defaults_file (),
                                       context, verbose, require_file);

        if (status)
          exit_status = status;

        status = safe_source_file (config::site_defaults_file (),
                                   context, verbose, require_file);

        if (status)
          exit_status = status;
      }

    if (read_init_files)
      {
        // Try to execute commands from the Matlab compatible startup.m file
        // if it exists anywhere in the load path when starting Octave.
        std::string ff_startup_m = file_in_path ("startup.m", "");

        if (! ff_startup_m.empty ())
          {
            int parse_status = 0;

            try
              {
                eval_string (std::string ("startup"), false, parse_status, 0);
              }
            catch (const interrupt_exception&)
              {
                recover_from_exception ();
              }
            catch (const execution_exception& e)
              {
                std::string stack_trace = e.info ();

                if (! stack_trace.empty ())
                  std::cerr << stack_trace;

                recover_from_exception ();
              }
          }

        // Schedule the Matlab compatible finish.m file to run if it exists
        // anywhere in the load path when exiting Octave.
        add_atexit_function ("__finish__");

        // Try to execute commands from $HOME/$OCTAVE_INITFILE and
        // $OCTAVE_INITFILE.  If $OCTAVE_INITFILE is not set,
        // .octaverc is assumed.

        bool home_rc_already_executed = false;

        std::string initfile = sys::env::getenv ("OCTAVE_INITFILE");

        if (initfile.empty ())
          initfile = ".octaverc";

        std::string home_dir = sys::env::get_home_directory ();

        std::string home_rc = sys::env::make_absolute (initfile, home_dir);

        std::string local_rc;

        if (! home_rc.empty ())
          {
            int status = safe_source_file (home_rc, context, verbose,
                                           require_file);

            if (status)
              exit_status = status;

            // Names alone are not enough.

            sys::file_stat fs_home_rc (home_rc);

            if (fs_home_rc)
              {
                // We want to check for curr_dir after executing home_rc
                // because doing that may change the working directory.

                local_rc = sys::env::make_absolute (initfile);

                home_rc_already_executed = same_file (home_rc, local_rc);
              }
          }

        if (! home_rc_already_executed)
          {
            if (local_rc.empty ())
              local_rc = sys::env::make_absolute (initfile);

            int status = safe_source_file (local_rc, context, verbose,
                                           require_file);

            if (status)
              exit_status = status;
          }
      }

    if (m_interactive && verbose)
      std::cout << std::endl;

    return exit_status;
  }

  // Execute any code specified with --eval 'CODE'

  int interpreter::execute_eval_option_code (void)
  {
    const cmdline_options& options = m_app_context->options ();

    std::string code_to_eval = options.code_to_eval ();

    unwind_protect frame;

    octave_save_signal_mask ();

    can_interrupt = true;

    octave_signal_hook = respond_to_pending_signals;
    octave_interrupt_hook = nullptr;
    octave_bad_alloc_hook = nullptr;

    catch_interrupts ();

    octave_initialized = true;

    frame.add_method (this, &interpreter::interactive, m_interactive);

    m_interactive = false;

    int parse_status = 0;

    try
      {
        eval_string (code_to_eval, false, parse_status, 0);
      }
    catch (const interrupt_exception&)
      {
        recover_from_exception ();

        return 1;
      }
    catch (const execution_exception&)
      {
        recover_from_exception ();

        return 1;
      }

    return parse_status;
  }

  int interpreter::execute_command_line_file (void)
  {
    const cmdline_options& options = m_app_context->options ();

    unwind_protect frame;

    octave_save_signal_mask ();

    can_interrupt = true;

    octave_signal_hook = respond_to_pending_signals;
    octave_interrupt_hook = nullptr;
    octave_bad_alloc_hook = nullptr;

    catch_interrupts ();

    octave_initialized = true;

    frame.add_method (this, &interpreter::interactive, m_interactive);

    string_vector args = options.all_args ();

    frame.add_method (m_app_context, &application::intern_argv, args);

    frame.add_method (this, &interpreter::intern_nargin, args.numel () - 1);

    frame.add_method (m_app_context,
                      &application::program_invocation_name,
                      application::program_invocation_name ());

    frame.add_method (m_app_context,
                      &application::program_name,
                      application::program_name ());

    m_interactive = false;

    // If we are running an executable script (#! /bin/octave) then
    // we should only see the args passed to the script.

    string_vector script_args = options.remaining_args ();

    m_app_context->intern_argv (script_args);
    intern_nargin (script_args.numel () - 1);

    std::string fname = script_args[0];

    m_app_context->set_program_names (fname);

    std::string context;
    bool verbose = false;
    bool require_file = true;

    return safe_source_file (fname, context, verbose, require_file, "octave");
  }

  int interpreter::main_loop (void)
  {
    if (! m_app_context)
      return 0;

    octave_save_signal_mask ();

    can_interrupt = true;

    octave_signal_hook = respond_to_pending_signals;
    octave_interrupt_hook = nullptr;
    octave_bad_alloc_hook = nullptr;

    catch_interrupts ();

    octave_initialized = true;

    // The big loop.

    return m_evaluator.repl (application::interactive ());
  }

  // Call a function with exceptions handled to avoid problems with
  // errors while shutting down.

#define OCTAVE_IGNORE_EXCEPTION(E)                                      \
  catch (E)                                                             \
    {                                                                   \
      recover_from_exception ();                                        \
                                                                        \
      std::cerr << "error: ignoring " #E " while preparing to exit"     \
                << std::endl;                                           \
    }

#define OCTAVE_SAFE_CALL(F, ARGS)                                       \
  do                                                                    \
    {                                                                   \
      try                                                               \
        {                                                               \
          unwind_protect frame;                                         \
                                                                        \
          frame.add_method (m_error_system,                             \
                            &error_system::set_debug_on_error,          \
                            m_error_system.debug_on_error ());          \
          frame.add_method (m_error_system,                             \
                            &error_system::set_debug_on_warning,        \
                            m_error_system.debug_on_warning ());        \
                                                                        \
          m_error_system.debug_on_error (false);                        \
          m_error_system.debug_on_warning (false);                      \
                                                                        \
          F ARGS;                                                       \
        }                                                               \
      OCTAVE_IGNORE_EXCEPTION (const exit_exception&)                   \
      OCTAVE_IGNORE_EXCEPTION (const interrupt_exception&)              \
      OCTAVE_IGNORE_EXCEPTION (const execution_exception&)              \
      OCTAVE_IGNORE_EXCEPTION (const std::bad_alloc&)                   \
    }                                                                   \
  while (0)

  void interpreter::cleanup (void)
  {
    // If we are attached to a GUI, process pending events and
    // disconnect the link.

    octave_link::process_events (true);
    octave_link::disconnect_link ();

    OCTAVE_SAFE_CALL (m_input_system.clear_input_event_hooks, ());

    while (! atexit_functions.empty ())
      {
        std::string fcn = atexit_functions.front ();

        atexit_functions.pop_front ();

        OCTAVE_SAFE_CALL (m_error_system.reset, ());

        OCTAVE_SAFE_CALL (feval, (fcn, octave_value_list (), 0));

        OCTAVE_SAFE_CALL (flush_stdout, ());
      }

    // Do this explicitly so that destructors for mex file objects
    // are called, so that functions registered with mexAtExit are
    // called.
    OCTAVE_SAFE_CALL (m_symbol_table.clear_mex_functions, ());

    OCTAVE_SAFE_CALL (command_editor::restore_terminal_state, ());

    OCTAVE_SAFE_CALL (m_history_system.write_timestamp, ());

    if (! command_history::ignoring_entries ())
      OCTAVE_SAFE_CALL (command_history::clean_up_and_save, ());

    OCTAVE_SAFE_CALL (gh_manager::close_all_figures, ());

    m_gtk_manager.unload_all_toolkits ();

    OCTAVE_SAFE_CALL (cleanup_tmp_files, ());

    // FIXME:  May still need something like this to ensure that
    // destructors for class objects will run properly.  Should that be
    // done earlier?  Before or after atexit functions are executed?
    m_symbol_table.cleanup ();

    OCTAVE_SAFE_CALL (sysdep_cleanup, ());

    OCTAVE_SAFE_CALL (flush_stdout, ());

    // Don't call singleton_cleanup_list::cleanup until we have the
    // problems with registering/unregistering types worked out.  For
    // example, uncomment the following line, then use the make_int
    // function from the examples directory to create an integer
    // object and then exit Octave.  Octave should crash with a
    // segfault when cleaning up the typinfo singleton.  We need some
    // way to force new octave_value_X types that are created in
    // .oct files to be unregistered when the .oct file shared library
    // is unloaded.
    //
    // OCTAVE_SAFE_CALL (singleton_cleanup_list::cleanup, ());
  }

  tree_evaluator& interpreter::get_evaluator (void)
  {
    return m_evaluator;
  }

  stream_list& interpreter::get_stream_list (void)
  {
    return m_stream_list;
  }

  url_handle_manager& interpreter::get_url_handle_manager (void)
  {
    return m_url_handle_manager;
  }

  symbol_scope
  interpreter::get_top_scope (void) const
  {
    return m_evaluator.get_top_scope ();
  }

  symbol_scope
  interpreter::get_current_scope (void) const
  {
    return m_evaluator.get_current_scope ();
  }

  symbol_scope
  interpreter::require_current_scope (const std::string& who) const
  {
    symbol_scope scope = get_current_scope ();

    if (! scope)
      error ("%s: symbol table scope missing", who.c_str ());

    return scope;
  }

  call_stack& interpreter::get_call_stack (void)
  {
    return m_evaluator.get_call_stack ();
  }

  profiler& interpreter::get_profiler (void)
  {
    return m_evaluator.get_profiler ();
  }

  void interpreter::mlock (void)
  {
    call_stack& cs = get_call_stack ();

    octave_function *fcn = cs.current ();

    if (! fcn)
      error ("mlock: invalid use outside a function");

    fcn->lock ();
  }

  void interpreter::munlock (const std::string& nm)
  {
    octave_value val = m_symbol_table.find_function (nm);

    if (val.is_defined ())
      {
        octave_function *fcn = val.function_value ();

        if (fcn)
          fcn->unlock ();
      }
  }

  bool interpreter::mislocked (const std::string& nm)
  {
    bool retval = false;

    octave_value val = m_symbol_table.find_function (nm);

    if (val.is_defined ())
      {
        octave_function *fcn = val.function_value ();

        if (fcn)
          retval = fcn->islocked ();
      }

    return retval;
  }

  std::string interpreter::mfilename (const std::string& opt) const
  {
    return m_evaluator.mfilename (opt);
  }

  octave_value_list interpreter::eval_string (const std::string& eval_str,
                                              bool silent, int& parse_status,
                                              int nargout)
  {
    return m_evaluator.eval_string (eval_str, silent, parse_status, nargout);
  }

  octave_value interpreter::eval_string (const std::string& eval_str,
                                         bool silent, int& parse_status)
  {
    return m_evaluator.eval_string (eval_str, silent, parse_status);
  }

  octave_value_list interpreter::eval_string (const octave_value& arg,
                                              bool silent, int& parse_status,
                                              int nargout)
  {
    return m_evaluator.eval_string (arg, silent, parse_status, nargout);
  }

  octave_value_list interpreter::eval (const std::string& try_code,
                                       int nargout)
  {
    return m_evaluator.eval (try_code, nargout);
  }

  octave_value_list interpreter::eval (const std::string& try_code,
                                       const std::string& catch_code,
                                       int nargout)
  {
    return m_evaluator.eval (try_code, catch_code, nargout);
  }

  octave_value_list interpreter::evalin (const std::string& context,
                                         const std::string& try_code,
                                         int nargout)
  {
    return m_evaluator.evalin (context, try_code, nargout);
  }

  octave_value_list interpreter::evalin (const std::string& context,
                                         const std::string& try_code,
                                         const std::string& catch_code,
                                         int nargout)
  {
    return m_evaluator.evalin (context, try_code, catch_code, nargout);
  }

  //! Evaluate an Octave function (built-in or interpreted) and return
  //! the list of result values.
  //!
  //! @param name The name of the function to call.
  //! @param args The arguments to the function.
  //! @param nargout The number of output arguments expected.
  //! @return A list of output values.  The length of the list is not
  //!         necessarily the same as @c nargout.

  octave_value_list interpreter::feval (const char *name,
                                        const octave_value_list& args,
                                        int nargout)
  {
    return feval (std::string (name), args, nargout);
  }

  octave_value_list interpreter::feval (const std::string& name,
                                        const octave_value_list& args,
                                        int nargout)
  {
    octave_value fcn = m_symbol_table.find_function (name, args);

    if (fcn.is_undefined ())
      error ("feval: function '%s' not found", name.c_str ());

    octave_function *of = fcn.function_value ();

    return of->call (m_evaluator, nargout, args);
  }

  octave_value_list interpreter::feval (octave_function *fcn,
                                        const octave_value_list& args,
                                        int nargout)
  {
    if (fcn)
      return fcn->call (m_evaluator, nargout, args);

    return octave_value_list ();
  }

  octave_value_list interpreter::feval (const octave_value& val,
                                        const octave_value_list& args,
                                        int nargout)
  {
    // FIXME: do we really want to silently return an empty ovl if
    // the function object is undefined?  It's essentially what the
    // version above that accepts a pointer to an octave_function
    // object does and some code was apparently written to rely on it
    // (for example, __ode15__).

    if (val.is_undefined ())
      return ovl ();

    if (val.is_function ())
      {
        return feval (val.function_value (), args, nargout);
      }
    else if (val.is_function_handle ())
      {
        // This covers function handles, inline functions, and anonymous
        //  functions.

        std::list<octave_value_list> arg_list;
        arg_list.push_back (args);

        // FIXME: could we make octave_value::subsref a const method?
        // It would be difficult because there are instances of
        // incrementing the reference count inside subsref methods,
        // which means they can't be const with the current way of
        // handling reference counting.

        octave_value xval = val;
        return xval.subsref ("(", arg_list, nargout);
      }
    else if (val.is_string ())
      {
        return feval (val.string_value (), args, nargout);
      }
    else
      error ("feval: first argument must be a string, inline function, or a function handle");

    return ovl ();
  }

  //! Evaluate an Octave function (built-in or interpreted) and return
  //! the list of result values.
  //!
  //! @param args The first element of @c args is the function to call.
  //!             It may be the name of the function as a string, a function
  //!             handle, or an inline function.  The remaining arguments are
  //!             passed to the function.
  //! @param nargout The number of output arguments expected.
  //! @return A list of output values.  The length of the list is not
  //!         necessarily the same as @c nargout.

  octave_value_list interpreter::feval (const octave_value_list& args,
                                        int nargout)
  {
    if (args.length () == 0)
      error ("feval: first argument must be a string, inline function, or a function handle");

    octave_value f_arg = args(0);

    octave_value_list tmp_args = args.slice (1, args.length () - 1, true);

    return feval (f_arg, tmp_args, nargout);
  }

  void interpreter::install_variable (const std::string& name,
                                      const octave_value& value, bool global)
  {
    m_evaluator.install_variable (name, value, global);
  }

  octave_value interpreter::global_varval (const std::string& name) const
  {
    return m_evaluator.global_varval (name);
  }

  void interpreter::global_assign (const std::string& name,
                                   const octave_value& val)
  {
    m_evaluator.global_assign (name, val);
  }

  octave_value interpreter::top_level_varval (const std::string& name) const
  {
    return m_evaluator.top_level_varval (name);
  }

  void interpreter::top_level_assign (const std::string& name,
                                      const octave_value& val)
  {
    m_evaluator.top_level_assign (name, val);
  }

  bool interpreter::is_variable (const std::string& name) const
  {
    return m_evaluator.is_variable (name);
  }

  bool interpreter::is_local_variable (const std::string& name) const
  {
    return m_evaluator.is_local_variable (name);
  }

  octave_value interpreter::varval (const std::string& name) const
  {
    return m_evaluator.varval (name);
  }

  void interpreter::assign (const std::string& name,
                            const octave_value& val)
  {
    m_evaluator.assign (name, val);
  }

  void interpreter::assignin (const std::string& context,
                              const std::string& name,
                              const octave_value& val)
  {
    m_evaluator.assignin (context, name, val);
  }

  void interpreter::source_file (const std::string& file_name,
                                 const std::string& context, bool verbose,
                                 bool require_file,
                                 const std::string& warn_for)
  {
    m_evaluator.source_file (file_name, context, verbose, require_file,
                             warn_for);
  }

  static void
  safe_fclose (FILE *f)
  {
    if (f)
      fclose (static_cast<FILE *> (f));
  }

  octave_value
  interpreter::parse_fcn_file (const std::string& full_file,
                               const std::string& file,
                               const std::string& dir_name,
                               const std::string& dispatch_type,
                               const std::string& package_name,
                               bool require_file,
                               bool force_script, bool autoload,
                               bool relative_lookup,
                               const std::string& warn_for)
  {
    octave_value retval;

    unwind_protect frame;

    octave_function *fcn_ptr = nullptr;

    // Open function file and parse.

    FILE *in_stream = command_editor::get_input_stream ();

    frame.add_fcn (command_editor::set_input_stream, in_stream);

    frame.add_fcn (command_history::ignore_entries,
                   command_history::ignoring_entries ());

    command_history::ignore_entries ();

    FILE *ffile = nullptr;

    if (! full_file.empty ())
      ffile = sys::fopen (full_file, "rb");

    if (ffile)
      {
        frame.add_fcn (safe_fclose, ffile);

        parser parser (ffile, *this);

        parser.m_curr_class_name = dispatch_type;
        parser.m_curr_package_name = package_name;
        parser.m_autoloading = autoload;
        parser.m_fcn_file_from_relative_lookup = relative_lookup;

        parser.m_lexer.m_force_script = force_script;
        parser.m_lexer.prep_for_file ();
        parser.m_lexer.m_parsing_class_method = ! dispatch_type.empty ();

        parser.m_lexer.m_fcn_file_name = file;
        parser.m_lexer.m_fcn_file_full_name = full_file;
        parser.m_lexer.m_dir_name = dir_name;
        parser.m_lexer.m_package_name = package_name;

        int status = parser.run ();

        fcn_ptr = parser.m_primary_fcn_ptr;

        if (status == 0)
          {
            if (parser.m_lexer.m_reading_classdef_file
                && parser.m_classdef_object)
              {
                // Convert parse tree for classdef object to
                // meta.class info (and stash it in the symbol
                // table?).  Return pointer to constructor?

                if (fcn_ptr)
                  panic_impossible ();

                bool is_at_folder = ! dispatch_type.empty ();

                try
                  {
                    fcn_ptr = parser.m_classdef_object->make_meta_class (*this, is_at_folder);
                  }
                catch (const execution_exception&)
                  {
                    delete parser.m_classdef_object;
                    throw;
                  }

                if (fcn_ptr)
                  retval = octave_value (fcn_ptr);

                delete parser.m_classdef_object;

                parser.m_classdef_object = nullptr;
              }
            else if (fcn_ptr)
              {
                retval = octave_value (fcn_ptr);

                fcn_ptr->maybe_relocate_end ();

                if (parser.m_parsing_subfunctions)
                  {
                    if (! parser.m_endfunction_found)
                      parser.m_subfunction_names.reverse ();

                    fcn_ptr->stash_subfunction_names (parser.m_subfunction_names);
                  }
              }
          }
        else
          error ("parse error while reading file %s", full_file.c_str ());
      }
    else if (require_file)
      error ("no such file, '%s'", full_file.c_str ());
    else if (! warn_for.empty ())
      error ("%s: unable to open file '%s'", warn_for.c_str (),
             full_file.c_str ());

    return retval;
  }

  bool interpreter::at_top_level (void) const
  {
    return m_evaluator.at_top_level ();
  }

  bool interpreter::isglobal (const std::string& name) const
  {
    return m_evaluator.is_global (name);
  }

  octave_value interpreter::find (const std::string& name)
  {
    return m_evaluator.find (name);
  }

  void interpreter::clear_all (bool force)
  {
    m_evaluator.clear_all (force);
  }

  void interpreter::clear_objects (void)
  {
    m_evaluator.clear_objects ();
  }

  void interpreter::clear_variable (const std::string& name)
  {
    m_evaluator.clear_variable (name);
  }

  void interpreter::clear_variable_pattern (const std::string& pattern)
  {
    m_evaluator.clear_variable_pattern (pattern);
  }

  void interpreter::clear_variable_regexp (const std::string& pattern)
  {
    m_evaluator.clear_variable_regexp (pattern);
  }

  void interpreter::clear_variables (void)
  {
    m_evaluator.clear_variables ();
  }

  void interpreter::clear_global_variable (const std::string& name)
  {
    m_evaluator.clear_global_variable (name);
  }

  void interpreter::clear_global_variable_pattern (const std::string& pattern)
  {
    m_evaluator.clear_global_variable_pattern (pattern);
  }

  void interpreter::clear_global_variable_regexp (const std::string& pattern)
  {
    m_evaluator.clear_global_variable_regexp (pattern);
  }

  void interpreter::clear_global_variables (void)
  {
    m_evaluator.clear_global_variables ();
  }

  void interpreter::clear_functions (bool force)
  {
    m_symbol_table.clear_functions (force);
  }

  void interpreter::clear_function (const std::string& name)
  {
    m_symbol_table.clear_function (name);
  }

  void interpreter::clear_symbol (const std::string& name)
  {
    m_evaluator.clear_symbol (name);
  }

  void interpreter::clear_function_pattern (const std::string& pat)
  {
    m_symbol_table.clear_function_pattern (pat);
  }

  void interpreter::clear_function_regexp (const std::string& pat)
  {
    m_symbol_table.clear_function_regexp (pat);
  }

  void interpreter::clear_symbol_pattern (const std::string& pat)
  {
    return m_evaluator.clear_symbol_pattern (pat);
  }

  void interpreter::clear_symbol_regexp (const std::string& pat)
  {
    return m_evaluator.clear_symbol_regexp (pat);
  }

  std::list<std::string> interpreter::global_variable_names (void)
  {
    return m_evaluator.global_variable_names ();
  }

  std::list<std::string> interpreter::variable_names (void)
  {
    return m_evaluator.variable_names ();
  }

  std::list<std::string> interpreter::user_function_names (void)
  {
    return m_symbol_table.user_function_names ();
  }

  std::list<std::string> interpreter::autoloaded_functions (void) const
  {
    return m_evaluator.autoloaded_functions ();
  }

  void interpreter::recover_from_exception (void)
  {
    can_interrupt = true;
    octave_interrupt_state = 0;
    octave_signal_caught = 0;
    octave_exception_state = octave_no_exception;
    octave_restore_signal_mask ();
    catch_interrupts ();
  }

  // Functions to call when the interpreter exits.

  std::list<std::string> interpreter::atexit_functions;

  void interpreter::add_atexit_function (const std::string& fname)
  {
    atexit_functions.push_front (fname);
  }

  bool interpreter::remove_atexit_function (const std::string& fname)
  {
    bool found = false;

    for (auto it = atexit_functions.begin ();
         it != atexit_functions.end (); it++)
      {
        if (*it == fname)
          {
            atexit_functions.erase (it);
            found = true;
            break;
          }
      }

    return found;
  }

  // What internal options get configured by --traditional.

  void interpreter::maximum_braindamage (void)
  {
    m_input_system.PS1 (">> ");
    m_input_system.PS2 ("");

    m_evaluator.PS4 ("");

    m_load_save_system.crash_dumps_octave_core (false);
    m_load_save_system.save_default_options ("-mat-binary");

    m_history_system.timestamp_format_string ("%%-- %D %I:%M %p --%%");

    m_error_system.beep_on_error (true);
    Fconfirm_recursive_rmdir (octave_value (false));

    Fdisable_diagonal_matrix (octave_value (true));
    Fdisable_permutation_matrix (octave_value (true));
    Fdisable_range (octave_value (true));
    Ffixed_point_format (octave_value (true));
    Fprint_empty_dimensions (octave_value (false));
    Fstruct_levels_to_print (octave_value (0));

    disable_warning ("Octave:abbreviated-property-match");
    disable_warning ("Octave:data-file-in-path");
    disable_warning ("Octave:function-name-clash");
    disable_warning ("Octave:possible-matlab-short-circuit-operator");
  }

  void interpreter::execute_pkg_add (const std::string& dir)
  {
    try
      {
        m_load_path.execute_pkg_add (dir);
      }
    catch (const interrupt_exception&)
      {
        interpreter::recover_from_exception ();
      }
    catch (const execution_exception&)
      {
        interpreter::recover_from_exception ();
      }
  }
}
