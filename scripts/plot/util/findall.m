## Copyright (C) 2008-2019 Bill Denney
##
## This file is part of Octave.
##
## Octave is free software: you can redistribute it and/or modify it
## under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## Octave is distributed in the hope that it will be useful, but
## WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with Octave; see the file COPYING.  If not, see
## <https://www.gnu.org/licenses/>.

## -*- texinfo -*-
## @deftypefn  {} {@var{h} =} findall ()
## @deftypefnx {} {@var{h} =} findall (@var{prop_name}, @var{prop_value}, @dots{})
## @deftypefnx {} {@var{h} =} findall (@var{prop_name}, @var{prop_value}, "-@var{logical_op}", @var{prop_name}, @var{prop_value})
## @deftypefnx {} {@var{h} =} findall ("-property", @var{prop_name})
## @deftypefnx {} {@var{h} =} findall ("-regexp", @var{prop_name}, @var{pattern})
## @deftypefnx {} {@var{h} =} findall (@var{hlist}, @dots{})
## @deftypefnx {} {@var{h} =} findall (@var{hlist}, "flat", @dots{})
## @deftypefnx {} {@var{h} =} findall (@var{hlist}, "-depth", @var{d}, @dots{})
## Find graphics object, including hidden ones, with specified properties.
##
## The return value @var{h} is a list of handles to the found graphic objects.
##
## @code{findall} performs the same search as @code{findobj}, but it
## includes hidden objects (HandleVisibility = @qcode{"off"}).  For full
## documentation, @pxref{XREFfindobj,,findobj}.
## @seealso{findobj, allchild, get, set}
## @end deftypefn

## Author: Bill Denney <bill@denney.ws>

function h = findall (varargin)

  unwind_protect
    shh = get (0, "showhiddenhandles");
    set (0, "showhiddenhandles", "on");
    h = findobj (varargin{:});
  unwind_protect_cleanup
    set (0, "showhiddenhandles", shh);
  end_unwind_protect

endfunction


%!testif HAVE_OPENGL, HAVE_QT; have_window_system () && any (strcmp ("qt", available_graphics_toolkits ()))
%! toolkit = graphics_toolkit ("qt");
%! hf = figure ("visible", "off");
%! unwind_protect
%!   h = findall (hf);
%!   all_handles(1) = {"figure"};
%!   all_handles(2,1) = {"uitoolbar"};
%!   all_handles(3:5) = {"uimenu"};
%!   all_handles([6:7, 9]) = {"uipushtool"};
%!   all_handles([8, 10:13]) = {"uitoggletool"};
%!   all_handles(14:33) = {"uimenu"};
%!   assert (get (h, "type"), all_handles);
%! unwind_protect_cleanup
%!   close (hf);
%!   graphics_toolkit (toolkit);
%! end_unwind_protect
