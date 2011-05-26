#! /usr/bin/python2.6
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

"""
menu.lst parser implementation for pybootmgmt
"""

import re


class MenuLstError(Exception):
    def __init__(self, msg):
        super(LegacyGRUBMenuError, self).__init__()
        self.msg = msg

    def __str__(self):
        return self.msg


class MenuLstCommand(object):
    """A menu.lst command and its arguments from the menu.lst file"""

    def __init__(self, command, args=None):
        self._command = command
        self._args = list(args) if not args is None else []

    def get_command(self):
        return self._command

    def get_args(self):
        return self._args

    def __str__(self):
        if not self._command is None and not self._args is None:
            return self._command + ' ' + ' '.join(self._args)
        elif not self._command is None:
            return self._command
        else:
            return ''

    def __repr__(self):
        ostr = ('(' + repr(self._command) + ',' +
               repr(self._args) + ')')
        return ostr


class MenuLstMenuEntry(object):
    """Representation of a menu.lst menu entry, which consists of a list of
    MenuLstCommand objects (the first of which must be the 'title' command).

    <instance>._cmdlist = [MenuLstCommand #1, MenuLstCommand #2, ...]
    """

    def __init__(self, args=None):
        if args is None:
            self._cmdlist = []
        else:
            self._cmdlist = list(args) # make a copy

    def add_command(self, mlcmd):
        self._cmdlist.append(mlcmd)

    def add_non_command(self, noncmd):
        "Add a string (blank line or comment) to the command list"
        self._cmdlist.append(noncmd)

    def find_command(self, name):
        for cmd in self._cmdlist:
            if isinstance(cmd, MenuLstCommand) and name == cmd.get_command():
                return True
        return False

    def commands(self):
        return self._cmdlist

    def __str__(self):
        ostr = 'MenuLstMenuEntry {\n'
        for cmd in self._cmdlist:
            ostr += '\t' + str(cmd).rstrip('\n') + '\n'
        ostr += '}'
        return ostr

    def __repr__(self):
        ostr = '['
        i = 0
        if len(self._cmdlist) >= 1:
            for i in range(len(self._cmdlist) - 1):
                ostr += repr(self._cmdlist[i]) + ', '
        if len(self._cmdlist) >= 2:
            ostr += repr(self._cmdlist[i + 1])
        ostr += ']'
        return ostr


class MenuDotLst(object):

    def __init__(self, filename):
        self.target = self
        self._line = 0
        self._last = ''
        self._entitylist = []        # per-instance list of entities
        self._filename = filename
        self._parse()

    def entities(self):
        "Return a list of entities encapsulated by this MenuDotLst"
        return self._entitylist

    def add_command(self, cmd):
        "Add a MenuLstCommand to the entitylist"
        self._entitylist.append(cmd)

    def __str__(self):
        ostr = ''
        for entity in self._entitylist:
            ostr += str(entity) + '\n'
        return ostr

    def __repr__(self):
        return (repr(self._entitylist))

    def _parse(self):
        "Parse the menu.lst file"
        fileobj = open(self._filename)
        try:
            for line in fileobj:
                self._parse_line(line)
            self._parse_line(None)    # end of file reached
        finally:
            fileobj.close()

        self._analyze_syntax()

    def _analyze_syntax(self):
        "This can be overridden in child classes, if needed"
        pass

    @staticmethod
    def _process_escapes(istr):
        res = ''
        newline_escaped = False
        for idx in range(len(istr)):
            #
            # Legacy GRUB allows escaping the newline
            #
            # We're guaranteed to always have a character
            # after the backslash, so no try is needed here.
            if istr[idx] == '\\' and istr[idx + 1] == '\n':
                newline_escaped = True
                res += ' '
            elif istr[idx] != '\n':
                res += istr[idx]
        return res, newline_escaped

    def _parse_line(self, nextline):
        """Parses a line of a menu.lst configuration file.
        The grammar of the menu.lst configuration file is simple:
        Comments are lines that begin with '#' (with or without
        preceeding whitespace), and commands are non-comments that
        include one or more non-whitespace character sequences,
        separated by whitespace.  The commands are not checked
        for semantic correctness.  They're just stored for later
        analysis.

        The parser works as follows:

        If the line is None, parsing is complete, so save the last
        entry processed to the statement list.

        If the line (stripped of any leading whitespace) starts with
        '#', then it's a comment, so save it and return.

        Split the line into a command portion and an optional
        argument(s) portion, taking care to process escape sequences
        (including the special escape of the newline)

        If the line begins with the 'title' keyword, then a new entry
        is created and saved so that future commands can be added to
        it.

        If an entry is active (if we're parsing after a title command),
        add the current command and arguments to the current entry.

        If an entry is not active, add the command and arguments to
        the statement list.

        The configuration file entity list contains commands and
        entries::

        [(MenuLstCommand|CommentString)*, (MenuLstMenuEntry|CommentString)*]

        Comments and blank space is stored as a plain string.
        """

        if nextline is None:
            # If there's still text in the last-line buffer,
            # we must have had a dangling backslash
            if self._last != '':
                raise MenuLstError('Dangling backslash detected')
            return

        self._line += 1

        # Remove the comment portion of the line
        stripped = nextline.strip()
        if stripped == '' or stripped[0] == '#':
            self.target.add_non_command(nextline)
            return

        # Remove escape sequences from the line:
        try:
            nextline, newline_escaped = (
                self._process_escapes(nextline))
        except IndexError:
            # The error must have been a dangling backslash
            raise MenuLstError('Dangling backslash detected')

        # If the newline was escaped, save the string for later
        if newline_escaped:
            self._last += nextline
            return        # Wait for the next line
        else:
            nextline = self._last + nextline
            self._last = ''

        pattern = (r'([^ \t"]+"([^\\"]|\\.)*"[^ \t]*|'
              r'([^ \t\\"]|\\.)*"[^"]*(?![^"]*")|'
              # r'"([^ \t\\"]|\\.)*(?![^"]*[^\\]")|'
               r'[^ "\t]+)')
        argv = [v[0] for v in re.compile(pattern).findall(nextline)]

        if (len(argv) > 0):
            if argv[0] == 'title':
                self.target = MenuLstMenuEntry()
                self._entitylist.append(self.target)
            # Check argv[0] (if it exists) for an equals sign, since 
            # that's a valid way to set certain variables
            argv = argv[0].split('=', 1) + argv[1:]

            self.target.add_command(MenuLstCommand(argv[0], argv[1:]))