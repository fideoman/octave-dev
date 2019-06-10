/*

Copyright (C) 2009-2019 John W. Eaton

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

#if ! defined (octave_pt_eval_h)
#define octave_pt_eval_h 1

#include "octave-config.h"

#include <list>
#include <set>
#include <stack>
#include <string>

#include "bp-table.h"
#include "call-stack.h"
#include "ov.h"
#include "ovl.h"
#include "profiler.h"
#include "pt-exp.h"
#include "pt-walk.h"

class octave_user_code;

namespace octave
{
  class symbol_scope;
  class tree_decl_elt;
  class tree_expression;

  class debugger;
  class interpreter;
  class unwind_protect;

  enum result_type
  {
    RT_UNDEFINED = 0,
    RT_VALUE = 1,
    RT_VALUE_LIST = 2
  };

  // How to evaluate the code that the parse trees represent.

  class OCTINTERP_API tree_evaluator : public tree_walker
  {
  public:

    enum echo_state
    {
      ECHO_OFF = 0,
      ECHO_SCRIPTS = 1,
      ECHO_FUNCTIONS = 2,
      ECHO_ALL = 4
    };

    template <typename T>
    class value_stack
    {
    public:

      value_stack (void) = default;

      value_stack (const value_stack&) = default;

      value_stack& operator = (const value_stack&) = default;

      ~value_stack (void) = default;

      void push (const T& val) { m_stack.push (val); }

      void pop (void)
      {
        m_stack.pop ();
      }

      T val_pop (void)
      {
        T retval = m_stack.top ();
        m_stack.pop ();
        return retval;
      }

      T top (void) const
      {
        return m_stack.top ();
      }

      size_t size (void) const
      {
        return m_stack.size ();
      }

      bool empty (void) const
      {
        return m_stack.empty ();
      }

      void clear (void)
      {
        while (! m_stack.empty ())
          m_stack.pop ();
      }

    private:

      std::stack<T> m_stack;
    };

    typedef void (*decl_elt_init_fcn) (tree_decl_elt&);

    tree_evaluator (interpreter& interp)
      : m_interpreter (interp), m_statement_context (SC_OTHER),
        m_result_type (RT_UNDEFINED), m_expr_result_value (),
        m_expr_result_value_list (), m_lvalue_list_stack (),
        m_nargout_stack (), m_autoload_map (), m_bp_table (*this),
        m_call_stack (*this), m_profiler (), m_debug_frame (0),
        m_debug_mode (false), m_quiet_breakpoint_flag (false),
        m_debugger_stack (), m_max_recursion_depth (256),
        m_whos_line_format ("  %a:4; %ln:6; %cs:16:6:1;  %rb:12;  %lc:-1;\n"),
        m_silent_functions (false), m_string_fill_char (' '),
        m_PS4 ("+ "), m_dbstep_flag (0), m_echo (ECHO_OFF),
        m_echo_state (false), m_echo_file_name (), m_echo_file_pos (1),
        m_echo_files (), m_in_loop_command (false),
        m_breaking (0), m_continuing (0), m_returning (0),
        m_indexed_object (nullptr), m_index_position (0),
        m_num_indices (0)
      { }

    // No copying!

    tree_evaluator (const tree_evaluator&) = delete;

    tree_evaluator& operator = (const tree_evaluator&) = delete;

    ~tree_evaluator (void) = default;

    bool at_top_level (void) const;

    void reset (void);

    int repl (bool interactive);

    std::string mfilename (const std::string& opt = "") const;

    octave_value_list eval_string (const std::string& eval_str, bool silent,
                                   int& parse_status, int nargout);

    octave_value eval_string (const std::string& eval_str, bool silent,
                              int& parse_status);

    octave_value_list eval_string (const octave_value& arg, bool silent,
                                   int& parse_status, int nargout);

    octave_value_list eval (const std::string& try_code, int nargout);

    octave_value_list eval (const std::string& try_code,
                            const std::string& catch_code, int nargout);

    octave_value_list evalin (const std::string& context,
                              const std::string& try_code, int nargout);

    octave_value_list evalin (const std::string& context,
                              const std::string& try_code,
                              const std::string& catch_code, int nargout);

    void visit_anon_fcn_handle (tree_anon_fcn_handle&);

    void visit_argument_list (tree_argument_list&);

    void visit_binary_expression (tree_binary_expression&);

    void visit_boolean_expression (tree_boolean_expression&);

    void visit_compound_binary_expression (tree_compound_binary_expression&);

    void visit_break_command (tree_break_command&);

    void visit_colon_expression (tree_colon_expression&);

    void visit_continue_command (tree_continue_command&);

    void visit_decl_command (tree_decl_command&);

    void visit_decl_init_list (tree_decl_init_list&);

    void visit_decl_elt (tree_decl_elt&);

    void visit_simple_for_command (tree_simple_for_command&);

    void visit_complex_for_command (tree_complex_for_command&);

    void visit_octave_user_script (octave_user_script&);

    octave_value_list
    execute_user_script (octave_user_script& user_script, int nargout,
                         const octave_value_list& args);

    void visit_octave_user_function (octave_user_function&);

    octave_value_list
    execute_user_function (octave_user_function& user_function,
                           int nargout, const octave_value_list& args,
                           stack_frame *closure_frames = nullptr);

    void visit_octave_user_function_header (octave_user_function&);

    void visit_octave_user_function_trailer (octave_user_function&);

    void visit_function_def (tree_function_def&);

    void visit_identifier (tree_identifier&);

    void visit_if_clause (tree_if_clause&);

    void visit_if_command (tree_if_command&);

    void visit_if_command_list (tree_if_command_list&);

    void visit_index_expression (tree_index_expression&);

    void visit_matrix (tree_matrix&);

    void visit_cell (tree_cell&);

    void visit_multi_assignment (tree_multi_assignment&);

    void visit_no_op_command (tree_no_op_command&);

    void visit_constant (tree_constant&);

    void visit_fcn_handle (tree_fcn_handle&);

    void visit_parameter_list (tree_parameter_list&);

    void visit_postfix_expression (tree_postfix_expression&);

    void visit_prefix_expression (tree_prefix_expression&);

    void visit_return_command (tree_return_command&);

    void visit_return_list (tree_return_list&);

    void visit_simple_assignment (tree_simple_assignment&);

    void visit_statement (tree_statement&);

    void visit_statement_list (tree_statement_list&);

    void visit_switch_case (tree_switch_case&);

    void visit_switch_case_list (tree_switch_case_list&);

    void visit_switch_command (tree_switch_command&);

    void visit_try_catch_command (tree_try_catch_command&);

    void do_unwind_protect_cleanup_code (tree_statement_list *list);

    void visit_unwind_protect_command (tree_unwind_protect_command&);

    void visit_while_command (tree_while_command&);
    void visit_do_until_command (tree_do_until_command&);

    void visit_superclass_ref (tree_superclass_ref&);
    void visit_metaclass_query (tree_metaclass_query&);

    void bind_ans (const octave_value& val, bool print);

    bool statement_printing_enabled (void);

    void reset_debug_state (void);

    void reset_debug_state (bool mode);

    void enter_debugger (const std::string& prompt = "debug> ");

    void keyboard (const std::string& prompt = "keyboard> ");

    void dbupdown (int n, bool verbose = false);

    // Possible types of evaluation contexts.
    enum stmt_list_type
    {
      SC_FUNCTION,  // function body
      SC_SCRIPT,    // script file
      SC_OTHER      // command-line input or eval string
    };

    Matrix ignored_fcn_outputs (void) const;

    bool isargout (int nargout, int iout) const;

    void isargout (int nargout, int nout, bool *isargout) const;

    const std::list<octave_lvalue> * lvalue_list (void) const
    {
      return (m_lvalue_list_stack.empty ()
              ? nullptr : m_lvalue_list_stack.top ());
    }

    void push_result (const octave_value& val)
    {
      m_result_type = RT_VALUE;
      m_expr_result_value = val;
    }

    void push_result (const octave_value_list& vals)
    {
      m_result_type = RT_VALUE_LIST;
      m_expr_result_value_list = vals;
    }

    octave_value evaluate (tree_expression *expr, int nargout = 1)
    {
      octave_value retval;

      m_nargout_stack.push (nargout);

      expr->accept (*this);

      m_nargout_stack.pop ();

      switch (m_result_type)
        {
        case RT_UNDEFINED:
          panic_impossible ();
          break;

        case RT_VALUE:
          retval = m_expr_result_value;
          m_expr_result_value = octave_value ();
          break;

        case RT_VALUE_LIST:
          retval = (m_expr_result_value_list.empty ()
                    ? octave_value () : m_expr_result_value_list(0));
          m_expr_result_value_list = octave_value_list ();
          break;
        }

      return retval;
    }

    octave_value_list evaluate_n (tree_expression *expr, int nargout = 1)
    {
      octave_value_list retval;

      m_nargout_stack.push (nargout);

      expr->accept (*this);

      m_nargout_stack.pop ();

      switch (m_result_type)
        {
        case RT_UNDEFINED:
          panic_impossible ();
          break;

        case RT_VALUE:
          retval = ovl (m_expr_result_value);
          m_expr_result_value = octave_value ();
          break;

        case RT_VALUE_LIST:
          retval = m_expr_result_value_list;
          m_expr_result_value_list = octave_value_list ();
          break;
        }

      return retval;
    }

    octave_value evaluate (tree_decl_elt *);

    void install_variable (const std::string& name,
                           const octave_value& value, bool global);

    octave_value global_varval (const std::string& name) const;

    void global_assign (const std::string& name,
                        const octave_value& val = octave_value ());

    octave_value top_level_varval (const std::string& name) const;

    void top_level_assign (const std::string& name,
                           const octave_value& val = octave_value ());

    bool is_variable (const std::string& name) const;

    bool is_local_variable (const std::string& name) const;

    bool is_variable (const tree_expression *expr) const;

    bool is_defined (const tree_expression *expr) const;

    bool is_variable (const symbol_record& sym) const;

    bool is_defined (const symbol_record& sym) const;

    bool is_global (const std::string& name) const;

    octave_value varval (const symbol_record& sym) const;

    octave_value varval (const std::string& name) const;

    void assign (const std::string& name,
                 const octave_value& val = octave_value ());

    void assignin (const std::string& context, const std::string& name,
                   const octave_value& val = octave_value ());

    void source_file (const std::string& file_name,
                      const std::string& context = "",
                      bool verbose = false, bool require_file = true,
                      const std::string& warn_for = "");

    void set_auto_fcn_var (stack_frame::auto_var_type avt,
                           const octave_value& val = octave_value ());

    octave_value get_auto_fcn_var (stack_frame::auto_var_type avt) const;

    void define_parameter_list_from_arg_vector
      (tree_parameter_list *param_list, const octave_value_list& args);

    void undefine_parameter_list (tree_parameter_list *param_list);

    octave_value_list
    convert_to_const_vector (tree_argument_list *arg_list,
                             const octave_value *object = nullptr);

    octave_value_list
    convert_return_list_to_const_vector
      (tree_parameter_list *ret_list, int nargout, const Cell& varargout);

    bool eval_decl_elt (tree_decl_elt *elt);

    bool switch_case_label_matches (tree_switch_case *expr,
                                    const octave_value& val);

    interpreter& get_interpreter (void) { return m_interpreter; }

    bp_table& get_bp_table (void) { return m_bp_table; }

    profiler& get_profiler (void) { return m_profiler; }

    call_stack& get_call_stack (void) { return m_call_stack; }

    const stack_frame& get_current_stack_frame (void) const
    {
      return m_call_stack.get_current_stack_frame ();
    }

    stack_frame& get_current_stack_frame (void)
    {
      return m_call_stack.get_current_stack_frame ();
    }

    void push_dummy_scope (const std::string& name);
    void pop_scope (void);

    symbol_scope get_top_scope (void) const;
    symbol_scope get_current_scope (void) const;

    octave_value find (const std::string& name);

    void clear_objects (void);

    void clear_variable (const std::string& name);

    void clear_variable_pattern (const std::string& pattern);

    void clear_variable_regexp (const std::string& pattern);

    void clear_variables (void);

    void clear_global_variable (const std::string& name);

    void clear_global_variable_pattern (const std::string& pattern);

    void clear_global_variable_regexp (const std::string& pattern);

    void clear_global_variables (void);

    void clear_all (bool force = false);

    void clear_symbol (const std::string& name);

    void clear_symbol_pattern (const std::string& pattern);

    void clear_symbol_regexp (const std::string& pattern);

    std::list<std::string> global_variable_names (void) const;

    std::list<std::string> variable_names (void) const;

    octave_user_code * get_user_code (const std::string& fname = "",
                                      const std::string& class_name = "");

    octave_map get_autoload_map (void) const;

    std::string lookup_autoload (const std::string& nm) const;

    std::list<std::string> autoloaded_functions (void) const;

    std::list<std::string> reverse_lookup_autoload (const std::string& nm) const;

    void add_autoload (const std::string& fcn, const std::string& nm);

    void remove_autoload (const std::string& fcn, const std::string& nm);

    int max_recursion_depth (void) const { return m_max_recursion_depth; }

    int max_recursion_depth (int n)
    {
      int val = m_max_recursion_depth;
      m_max_recursion_depth = n;
      return val;
    }

    octave_value
    max_recursion_depth (const octave_value_list& args, int nargout);

    bool silent_functions (void) const { return m_silent_functions; }

    bool silent_functions (bool b)
    {
      int val = m_silent_functions;
      m_silent_functions = b;
      return val;
    }

    octave_value whos_line_format (const octave_value_list& args, int nargout);

    std::string whos_line_format (void) const { return m_whos_line_format; }

    std::string whos_line_format (const std::string& s)
    {
      std::string val = m_whos_line_format;
      m_whos_line_format = s;
      return val;
    }

    octave_value
    silent_functions (const octave_value_list& args, int nargout);

    size_t debug_frame (void) const { return m_debug_frame; }

    size_t debug_frame (size_t n)
    {
      size_t val = m_debug_frame;
      m_debug_frame = n;
      return val;
    }

    bool debug_mode (void) const { return m_debug_mode; }

    bool debug_mode (bool flag)
    {
      bool val = m_debug_mode;
      m_debug_mode = flag;
      return val;
    }

    bool quiet_breakpoint_flag (void) const { return m_quiet_breakpoint_flag; }

    bool quiet_breakpoint_flag (bool flag)
    {
      bool val = m_quiet_breakpoint_flag;
      m_quiet_breakpoint_flag = flag;
      return val;
    }

    char string_fill_char (void) const { return m_string_fill_char; }

    char string_fill_char (char c)
    {
      int val = m_string_fill_char;
      m_string_fill_char = c;
      return val;
    }

    // The following functions are provided for convenience and forward
    // to the corresponding functions in the debugger class for the
    // current debugger (if any).
    bool in_debug_repl (void) const;
    bool in_debug_repl (bool flag);
    bool exit_debug_repl (void) const;
    bool exit_debug_repl (bool flag);
    bool abort_debug_repl (void) const;
    bool abort_debug_repl (bool flag);

    octave_value PS4 (const octave_value_list& args, int nargout);

    std::string PS4 (void) const { return m_PS4; }

    std::string PS4 (const std::string& s)
    {
      std::string val = m_PS4;
      m_PS4 = s;
      return val;
    }

    const octave_value * indexed_object (void) const
    {
      return m_indexed_object;
    }

    int index_position (void) const { return m_index_position; }

    int num_indices (void) const { return m_num_indices; }

    int breaking (void) const { return m_breaking; }

    int breaking (int n)
    {
      int val = m_breaking;
      m_breaking = n;
      return val;
    }

    int continuing (void) const { return m_continuing; }

    int continuing (int n)
    {
      int val = m_continuing;
      m_continuing = n;
      return val;
    }

    int returning (void) const { return m_returning; }

    int returning (int n)
    {
      int val = m_returning;
      m_returning = n;
      return val;
    }

    int dbstep_flag (void) const { return m_dbstep_flag; }

    int dbstep_flag (int val)
    {
      int old_val = m_dbstep_flag;
      m_dbstep_flag = val;
      return old_val;
    }

    void set_dbstep_flag (int step) { m_dbstep_flag = step; }

    octave_value echo (const octave_value_list& args, int nargout);

    int echo (void) const { return m_echo; }

    int echo (int val)
    {
      int old_val = m_echo;
      m_echo = val;
      return old_val;
    }

    octave_value
    string_fill_char (const octave_value_list& args, int nargout);

    void final_index_error (index_exception& e, const tree_expression *expr);

    octave_value do_who (int argc, const string_vector& argv,
                         bool return_list, bool verbose = false);

    void push_echo_state (unwind_protect& frame, int type,
                          const std::string& file_name, size_t pos = 1);

  private:

    void set_echo_state (int type, const std::string& file_name, size_t pos);

    void maybe_set_echo_state (void);

    void push_echo_state_cleanup (unwind_protect& frame);

    bool maybe_push_echo_state_cleanup (void);

    void do_breakpoint (tree_statement& stmt);

    void do_breakpoint (bool is_breakpoint,
                        bool is_end_of_fcn_or_script = false);

    bool is_logically_true (tree_expression *expr, const char *warn_for);

    octave_value_list
    make_value_list (tree_argument_list *args,
                     const string_vector& arg_nm,
                     const octave_value *object, bool rvalue = true);

    std::list<octave_lvalue> make_lvalue_list (tree_argument_list *);

    // For unwind-protect.
    void uwp_set_echo_state (bool state, const std::string& file_name,
                             size_t pos);

    bool echo_this_file (const std::string& file, int type) const;

    void echo_code (size_t line);

    bool quit_loop_now (void);

    void bind_auto_fcn_vars (const string_vector& arg_names, int nargin,
                             int nargout, bool takes_varargs,
                             const octave_value_list& va_args);

    void init_local_fcn_vars (octave_user_function& user_fcn);

    std::string check_autoload_file (const std::string& nm) const;

    interpreter& m_interpreter;

    // The context for the current evaluation.
    stmt_list_type m_statement_context;

    result_type m_result_type;
    octave_value m_expr_result_value;
    octave_value_list m_expr_result_value_list;

    value_stack<const std::list<octave_lvalue>*> m_lvalue_list_stack;

    value_stack<int> m_nargout_stack;

    // List of autoloads (function -> file mapping).
    std::map<std::string, std::string> m_autoload_map;

    bp_table m_bp_table;

    call_stack m_call_stack;

    profiler m_profiler;

    // The number of the stack frame we are currently debugging.
    size_t m_debug_frame;

    bool m_debug_mode;

    bool m_quiet_breakpoint_flag;

    // When entering the debugger we push it on this stack.  Managing
    // debugger invocations this way allows us to handle recursive
    // debugger calls.  When we exit a debugger the object is popped
    // from the stack and deleted and we resume working with the
    // previous debugger (if any) that is now at the top of the stack.
    std::stack<debugger *> m_debugger_stack;

    // Maximum nesting level for functions, scripts, or sourced files
    // called recursively.
    int m_max_recursion_depth;

    // Defines layout for the whos/who -long command
    std::string m_whos_line_format;

    // If TRUE, turn off printing of results in functions (as if a
    // semicolon has been appended to each statement).
    bool m_silent_functions;

    // The character to fill with when creating string arrays.
    char m_string_fill_char;

    // String printed before echoed commands (enabled by --echo-commands).
    std::string m_PS4;

    // If > 0, stop executing at the (N-1)th stopping point, counting
    //         from the the current execution point in the current frame.
    //
    // If < 0, stop executing at the next possible stopping point.
    int m_dbstep_flag;

    // Echo commands as they are executed?
    //
    //   1  ==>  echo commands read from script files
    //   2  ==>  echo commands from functions
    //
    // more than one state can be active at once.
    int m_echo;

    // Are we currently echoing commands?  This state is set by the
    // functions that execute fucntions and scripts.
    bool m_echo_state;

    std::string m_echo_file_name;

    // Next line to echo, counting from 1.
    size_t m_echo_file_pos;

    std::map<std::string, bool> m_echo_files;

    // TRUE means we are evaluating some kind of looping construct.
    bool m_in_loop_command;

    // Nonzero means we're breaking out of a loop or function body.
    int m_breaking;

    // Nonzero means we're jumping to the end of a loop.
    int m_continuing;

    // Nonzero means we're returning from a function.
    int m_returning;

    // Used by END function.
    const octave_value *m_indexed_object;
    int m_index_position;
    int m_num_indices;
  };
}

#endif
