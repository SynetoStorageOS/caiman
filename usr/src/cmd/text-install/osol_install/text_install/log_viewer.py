#!/usr/bin/python
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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

'''
Read in and display the install log to the user
'''

import curses

from osol_install.profile.install_profile import INSTALL_PROF_LABEL
from osol_install.text_install import _
from solaris_install.engine import InstallEngine
from terminalui.base_screen import BaseScreen
from terminalui.i18n import convert_paragraph
from terminalui.scroll_window import ScrollWindow
from terminalui.window_area import WindowArea

class LogViewer(BaseScreen):
    '''Screen for reading and displaying the install log'''
    
    HEADER_TEXT = _("Installation Log")
    
    def __init__(self, main_win):
        super(LogViewer, self).__init__(main_win)
        self.log_data = None
        self.scroll_area = None
        self.install_profile = None
    
    def set_actions(self):
        '''Remove all actions except F3_Back'''
        self.main_win.actions.pop(curses.KEY_F2)
        self.main_win.actions.pop(curses.KEY_F6)
        self.main_win.actions.pop(curses.KEY_F9)
    
    def _show(self):
        '''Create a scrollable region and fill it with the install log'''
        doc = InstallEngine.get_instance().doc
        self.install_profile = doc.get_descendants(name=INSTALL_PROF_LABEL,
                                                   not_found_is_err=True)[0]
        
        self.center_win.border_size = (0, 0)
        self.scroll_area = WindowArea(self.win_size_y,
                                      self.win_size_x,
                                      0, 0, len(self.get_log_data()))
        log = ScrollWindow(self.scroll_area, window=self.center_win)
        log.add_paragraph(self.get_log_data(), 0, 2)
        self.center_win.activate_object(log)
    
    def get_log_data(self):
        '''Attempt to read in the install log file. If an error occurs,
        the log_data is set to a string explaining the cause, if possible.
        
        '''
        if self.log_data is None:
            log_file = None
            try:
                try:
                    log_file = open(self.install_profile.log_location)
                    log_data = log_file.read()
                except (OSError, IOError), error:
                    self.log_data = _("Could not read log file:\n\t%s") % \
                                    error.strerror
            finally:
                if log_file is not None:
                    log_file.close()
            max_chars = self.win_size_x - 4
            self.log_data = convert_paragraph(log_data, max_chars)
        return self.log_data
