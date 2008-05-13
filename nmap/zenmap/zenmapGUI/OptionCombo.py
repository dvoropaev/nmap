#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (C) 2005 Insecure.Com LLC.
#
# Author: Adriano Monteiro Marques <py.adriano@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

import gtk

from zenmapCore.I18N import _
from zenmapCore.OptionsConf import options_file
from zenmapCore.NmapOptions import NmapOptions

class OptionCombo(gtk.ComboBoxEntry):
    def __init__(self):
        gtk.ComboBoxEntry.__init__(self, gtk.ListStore(str), 0)

        self.completion = gtk.EntryCompletion()
        self.child.set_completion(self.completion)
        self.completion.set_model(self.get_model())
        self.completion.set_text_column(0)

        self.update()

    def update(self):
        try:
            self.nmap_options = NmapOptions(options_file)
            self.options = self.nmap_options.get_options_list()
            self.options.sort()
        
            self.clean_model()
            model = self.get_model()
            for o in self.options:
                model.append([o])
        except:
            pass
        
    def clean_model(self):
        model = self.get_model()
        for i in range(len(model)):
            iter = model.get_iter_root()
            del(model[iter])

    def get_selected_option(self):
        return self.child.get_text()

    def set_selected_option(self, option):
        self.child.set_text(option)

    selected_option = property(get_selected_option, set_selected_option)
