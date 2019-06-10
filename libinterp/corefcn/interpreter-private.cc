/*

Copyright (C) 2017-2019 John W. Eaton

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

#include <list>
#include <string>

#include "bp-table.h"
#include "call-stack.h"
#include "cdef-manager.h"
#include "child-list.h"
#include "error.h"
#include "gtk-manager.h"
#include "help.h"
#include "input.h"
#include "interpreter-private.h"
#include "interpreter.h"
#include "load-path.h"
#include "load-save.h"
#include "oct-hist.h"
#include "ov.h"
#include "ov-fcn-inline.h"
#include "pager.h"
#include "symtab.h"

namespace octave
{
  interpreter& __get_interpreter__ (const std::string& who)
  {
    interpreter *interp = interpreter::the_interpreter ();

    if (! interp)
      {
        abort ();
        error ("%s: interpreter context missing", who.c_str ());
      }

    return *interp;
  }

  dynamic_loader& __get_dynamic_loader__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_dynamic_loader ();
  }

  error_system& __get_error_system__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_error_system ();
  }

  help_system& __get_help_system__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_help_system ();
  }

  history_system& __get_history_system__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_history_system ();
  }

  input_system& __get_input_system__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_input_system ();
  }

  output_system& __get_output_system__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_output_system ();
  }

  load_path& __get_load_path__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_load_path ();
  }

  load_save_system& __get_load_save_system__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_load_save_system ();
  }

  type_info& __get_type_info__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_type_info ();
  }

  symbol_table& __get_symbol_table__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_symbol_table ();
  }

  symbol_scope __get_current_scope__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_current_scope ();
  }

  symbol_scope __require_current_scope__ (const std::string& who)
  {
    symbol_scope scope = __get_current_scope__ (who);

    if (! scope)
      error ("%s: symbol table scope missing", who.c_str ());

    return scope;
  }

  tree_evaluator& __get_evaluator__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_evaluator ();
  }

  bp_table& __get_bp_table__ (const std::string& who)
  {
    tree_evaluator& tw = __get_evaluator__ (who);

    return tw.get_bp_table ();
  }

  call_stack& __get_call_stack__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_call_stack ();
  }

  child_list& __get_child_list__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_child_list ();
  }

  cdef_manager& __get_cdef_manager__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_cdef_manager ();
  }

  gtk_manager& __get_gtk_manager__ (const std::string& who)
  {
    interpreter& interp = __get_interpreter__ (who);

    return interp.get_gtk_manager ();
  }

  octave_value
  get_function_handle (interpreter& interp, const octave_value& arg,
                       const std::string& parameter_name)
  {
    std::list<std::string> parameter_names;
    parameter_names.push_back (parameter_name);
    return get_function_handle (interp, arg, parameter_names);
  }

  octave_value
  get_function_handle (interpreter& interp, const octave_value& arg,
                       const std::list<std::string>& parameter_names)
  {
    if (arg.is_function_handle () || arg.is_inline_function ())
      return arg;
    else if (arg.is_string ())
      {
        std::string fstr = arg.string_value ();

        if (fstr.empty ())
          return octave_value ();

        symbol_table& symtab = interp.get_symbol_table ();

        octave_value fcn = symtab.find_function (fstr);

        if (fcn.is_defined ())
          return fcn;

        fcn = octave_value (new octave_fcn_inline (fstr, parameter_names));

        // Possibly warn here that passing the function body in a
        // character string is discouraged.

        return fcn;
      }

    return octave_value ();
  }
}
