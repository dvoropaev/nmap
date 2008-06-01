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

import errno
import gtk
import gobject
import os
import re

from higwidgets.hignotebooks import HIGNotebook, HIGAnimatedTabLabel
from higwidgets.higboxes import HIGVBox
from higwidgets.higdialogs import HIGAlertDialog, HIGDialog
from higwidgets.higscrollers import HIGScrolledWindow

from zenmapGUI.NmapOutputViewer import NmapOutputViewer
from zenmapGUI.ScanHostDetailsPage import ScanHostDetailsPage
from zenmapGUI.ScanToolbar import ScanCommandToolbar, ScanToolbar
from zenmapGUI.ScanHostsView import ScanHostsView, SCANNING
from zenmapGUI.ScanOpenPortsPage import ScanOpenPortsPage
from zenmapGUI.ScanRunDetailsPage import ScanRunDetailsPage
from zenmapGUI.ScanNmapOutputPage import ScanNmapOutputPage
from zenmapGUI.Icons import get_os_icon, get_os_logo, get_vulnerability_logo

from zenmapCore.NmapCommand import NmapCommand
from zenmapCore.NmapCommand import CommandConstructor
from zenmapCore.UmitConf import CommandProfile, ProfileNotFound, is_maemo
from zenmapCore.NmapParser import NmapParser
from zenmapCore.Paths import Path, get_extra_executable_search_paths
from zenmapCore.UmitLogging import log
from zenmapCore.I18N import _

icon_dir = Path.pixmaps_dir

class PageStatus(object):
    """
    Pages status:
    The page status can be one of the following:
       * saved: there is nothing to be saved in the current scan tab
       * unsaved_unchanged: for recently scanned results that were parsed and is unchanged.
       * unsaved_changed: for recently scanned results that were parsed and got some changes
         on its contents (like a comment)
       * loaded_unchanged: for scan results that were loaded from a file and got no
         modifications
       * loaded_changed: for scan results that were loaded from a file and got some
         modifications (like comments)
       * parsing_result: the result is been parsed to be displayed at the tab
       * scanning: there is no parsed result related to this tab, but there is a related scan
         running to show results on that tab
       * empty: there is nothing related to this tab. His widgets are disabled and this is
         the initial state of a new tab
       * unknown: the page status is unknown
       * scan_failed: the scan has failed
       * search_loaded: the scan was loaded from a search result
    """
    def __init__(self, status=False):
        if status:
            self.status = status
        else:
            self.set_unknown()

    def set_empty(self):
        self._status = "empty"

    def set_saved(self):
        self._status = "saved"

    def set_unsaved_unchanged(self):
        self._status = "unsaved_unchanged"

    def set_unsaved_changed(self):
        self._status = "unsaved_changed"

    def set_loaded_unchanged(self):
        self._status = "loaded_unchanged"

    def set_loaded_changed(self):
        self._status = "loaded_changed"

    def set_parsing_result(self):
        self._status = "parsing_result"

    def set_scanning(self):
        self._status = "scanning"

    def set_unknown(self):
        self._status = "unknown"

    def set_scan_failed(self):
        self._status = "scan_failed"

    def set_search_loaded(self):
        self._status = "search_loaded"

    def get_status(self):
        return self._status

    def set_status(self, status):
        if status in self.available_status:
            self._status = status
        else:
            raise Exception("Unknown status!")

    def _verify_status(self, status):
        if self._status == status:
            return True
        return False

    def get_unsaved_unchanged(self):
        return self._verify_status("unsaved_unchanged")

    def get_unsaved_changed(self):
        return self._verify_status("unsaved_changed")

    def get_loaded_unchanged(self):
        return self._verify_status("loaded_unchanged")

    def get_loaded_changed(self):
        return self._verify_status("loaded_changed")

    def get_parsing_result(self):
        return self._verify_status("parsing_result")

    def get_scanning(self):
        return self._verify_status("scanning")

    def get_empty(self):
        return self._verify_status("empty")

    def get_unknown(self):
        return self._verify_status("unknown")

    def get_saved(self):
        return self._verify_status("saved")

    def get_scan_failed(self):
        return self._verify_status("scan_failed")

    def get_search_loaded(self):
        return self._verify_status("search_loaded")


    status = property(get_status, set_status)
    saved = property(get_saved)
    unsaved_unchanged = property(get_unsaved_unchanged)
    unsaved_changed = property(get_unsaved_changed)
    loaded_unchanged = property(get_loaded_unchanged)
    loaded_changed = property(get_loaded_changed)
    parsing_result = property(get_parsing_result)
    scanning = property(get_scanning)
    empty = property(get_empty)
    unknown = property(get_unknown)
    scan_failed = property(get_scan_failed)
    search_loaded = property(get_search_loaded)
    
    _status = "unknown"
    available_status = ["saved",
                        "unsaved_unchanged",
                        "unsaved_changed",
                        "loaded_unchanged",
                        "loaded_changed",
                        "parsing_result",
                        "scanning",
                        "empty",
                        "unknown",
                        "search_loaded"]

class ScanNotebook(HIGNotebook):
    """
    ScanNotebook
    Manages the Notebook tabs within Zenmap.
    Gives functions that allow you to remove and add pages.
    Provides similar functionality for the tab titles, as well as a
    "sanitized" title formatting functionality.
    """
    def __init__(self):
        HIGNotebook.__init__(self)
        self.set_scrollable(True)
        self.tab_titles = []
        self.scan_num = 1
        self.close_scan_cb = None

    def remove_page(self, page_num):
        page = self.get_nth_page(page_num)
        self.remove_tab_title(self.get_tab_title(page))
        
        HIGNotebook.remove_page(self, page_num)

    def add_scan_page(self, title):
        """Adds a new scan page using append_page function with the given page and tab title,
        selecting the first profile and setting the focus at the target combo box."""

        page = ScanNotebookPage()
        page.select_first_profile()

        self.append_page(page, self.close_scan_cb, tab_title=title)
        page.show_all()

        self.set_current_page(-1)

        # Put focus at the target combo.
        page.target_focus()

        return page

    def append_page(self, page, close_cb, tab_label=None, tab_title=None):
        """Appending a new page involves either getting a tab's label, title,
        or else the newest tab's title, and setting the given tab's label to the value.
        Also, connects a tab-closing listener."""

        log.debug(">>> Appending Scan Tab.")
        if tab_label:
            tab_label.set_text(self.sanitize_tab_title(tab_label.get_text()))
        elif tab_title:
            tab_label = HIGAnimatedTabLabel(self.sanitize_tab_title(tab_title))
        else:
            tab_label = HIGAnimatedTabLabel(self.get_new_tab_title())

        tab_label.connect("close-clicked", close_cb, page)
        HIGNotebook.append_page(self, page, tab_label)

    def sanitize_tab_title(self, title):
        """Sanitizes the tab title, which means that it concatenates the tab title
        with its incremental scan_id (based on the number of tab titles) and adds this
        new tab title value."""

        #log.debug(">>> Sanitize this title: %s" % title)
        scan_id = 1
        title2 = title
        while title2 in self.tab_titles:
            title2 = "%s (%s)" % (title, scan_id)
            scan_id += 1
        
        self.add_tab_title(title2)
        #log.debug(">>> Title sanitized: %s" % title2)
        return title2

    def remove_tab_title(self, title):
        log.debug(">>> Remove tab title: %s" % title)
        try: self.tab_titles.remove(title)
        except: pass

    def get_new_tab_title(self, parsed_result=None):
        """Retrieves and sanitizes the newest tab title. Checks to see if the result
        already has a scan_name. If it does, it returns this value as the title.
        Otherwise, finds the value in nmap_xml_file"""

        log.debug(">>> Get new tab title")
        if parsed_result:
            if parsed_result.scan_name:
                return self.sanitize_tab_title(parsed_result.scan_name)
            try:
                filename = parsed_result.nmap_xml_file
                if filename and type(filename) in StringTypes:
                    return self.sanitize_tab_title(filename)
            except:
                pass
    
        index = self.scan_num
        self.scan_num += 1
        return self.sanitize_tab_title(_('untitled_scan%s') % index)

    def add_tab_title(self, title):
        log.debug(">>> Add tab title: %s" % title)
        self.tab_titles.append(title)

    def get_tab_title(self, page):
        log.debug(">>> Get tab title")
        return self.get_tab_label(page).get_text()

    def set_tab_title(self, page, title):
        log.debug(">>> Set tab title: %s" % title)
        
        old_title = self.get_tab_title(page)
        if old_title:
            self.remove_tab_title(old_title)
            
        if not title:
            title = self.get_new_tab_title(page.parsed)
        else:
            title = self.sanitize_tab_title(title)
        
        self.get_tab_label(page).set_text(title)


class ScanNotebookPage(HIGVBox):
    """
    ScanNotebookPage
    Manages all the information associated with a Notebook page.
    """
    def __init__(self):
        HIGVBox.__init__(self)

        # The borders are consuming too much space on Maemo. Setting it to
        # 0 pixels while on Maemo
        if is_maemo():
            self.set_border_width(0)
        
        self.set_spacing(0)
        self.status = PageStatus()
        self.status.set_empty()
        
        self.changes = False
        self.comments = {}

        self.parsed = NmapParser()
        self.top_box = HIGVBox()
        
        self.__create_toolbar()
        self.__create_command_toolbar()
        self.__create_scan_result()
        self.disable_widgets()
        
        self.saved = False
        self.saved_filename = ''

        self.top_box.set_border_width(6)
        self.top_box.set_spacing(5)
        
        self.top_box._pack_noexpand_nofill(self.toolbar)
        self.top_box._pack_noexpand_nofill(self.command_toolbar)
        
        self._pack_noexpand_nofill(self.top_box)
        self._pack_expand_fill(self.scan_result)

    def target_focus(self):
        self.toolbar.target_entry.grab_focus()

    def select_first_profile(self):
        self.toolbar.profile_entry.child.set_text(self.toolbar.profile_entry.get_model()[0][0])

    def verify_changes(self):
        return self.__verify_comments_changes()

    def go_to_host(self, host):
        """Go to host line on nmap output result"""
        self.scan_result.scan_result_notebook.nmap_output.nmap_output.go_to_host(host)

    def __create_scan_result(self):
        self.scan_result = ScanResult()

    def __create_toolbar(self):
        """Create Scan Toolbar with a set empty_target. Handles changed commands by performing
        a refresh. A clicked scan button runs the scan."""

        self.toolbar = ScanToolbar()
        self.empty_target = _("<target>")
        
        self.toolbar.target_entry.connect('changed', self.refresh_command_target)
        self.toolbar.profile_entry.connect('changed', self.refresh_command)

        self.toolbar.scan_button.connect('clicked', self.start_scan_cb)

    
    def __create_command_toolbar(self):
        """Assigns several events to the command toolbar, including activate
        on scan button being clicked, retaining command when a user clicks
        inside a command entry before edition, and check command changes when a
        user clicks outside the command entry. Sets command_edited variable if
        command entry was edited."""

        self.command_toolbar = ScanCommandToolbar()
        self.command_toolbar.command_entry.connect('activate',
                                    lambda x: self.toolbar.scan_button.clicked())
        self.command_edited = False
        self.command_toolbar.command_entry.connect("focus-in-event", self.remember_command)
        self.command_toolbar.command_entry.connect("focus-in-event", self.check_command)

        # If you modify the command field entry at all, it will clear the profile, target fields
	# so as to not use a profile when you change the command manually, but instead just run
	# the command given in the field.
        self.command_toolbar.command_entry.connect('key-press-event', self.clear_profile_target)

    def clear_profile_target(self, extra1=None, extra2=None):
        """Used to clear the profile & target fields, so that no profile is used upon command field
        modification."""

        self.toolbar.profile_entry.child.set_text("")
        self.toolbar.target_entry.child.set_text("")

    def remember_command(self, widget, extra=None):
        """Remembers the current command entered before the user is editing the command field."""
        
        self.old_target = self.toolbar.target_entry.selected_target # Target may be empty

        if not self.old_target:
            self.old_target = self.empty_target

        self.old_full_command = self.command_toolbar.command_entry.get_text()
        self.old_command = self.old_full_command.split(self.old_target)[0]
        

    def check_command(self, widget, extra=None):
        """Checking the command involves seeing if anything has been changed
        in the field by the user."""

        new_command = self.command_toolbar.command

        # The command entry has been altered, so clear the profile and target.
        #if new_command != self.old_full_command:
        #    self.toolbar.profile_entry.child.set_text("")
        #    self.toolbar.target_entry.child.set_text("")

        #print "New:", new_command
        #print "Old:", self.old_full_command

        #if new_command != self.old_full_command:
        #    print "Gosh! Is different!! Now, what?"


        #target = self.toolbar.target_entry.selected_target
        #self.toolbar.target_entry.child.set_text(self.command_toolbar.command_entry.get_text().split(("<target>"))[0])

        #if self.saved_target:
        #    self.saved_command = self.command_toolbar.command_entry.get_text().split(
        #                                                                   self.saved_target)[0]
        #else:
        #    self.saved_command = self.command_toolbar.command_entry.get_text().split(
        #                                                                      _("<target>"))[0]
        
        #print self.saved_target
        #print self.saved_command
    
    def disable_widgets(self):
        self.scan_result.set_sensitive(False)
    
    def enable_widgets(self):
                self.scan_result.set_sensitive(True)
    
    def refresh_command_target(self, widget):
        """Refreshes the command target only if the selected profile
        is not empty."""

        #log.debug(">>> Refresh Command Target")
        
        profile = self.toolbar.selected_profile
        #log.debug(">>> Profile: %s" % profile)
        
        if profile != '':
            target = self.toolbar.selected_target
            #log.debug(">>> Target: %s" % target)
            try:
                cmd_profile = CommandProfile()
                command = cmd_profile.get_command(profile) % target
                del(cmd_profile)
                
                self.command_toolbar.command = command
            except ProfileNotFound:
                pass # Go without a profile
            except TypeError:
                pass # The target is empty...
                #self.profile_not_found_dialog()
    
    def refresh_command(self, widget):
        """Try to set a new CommandProfile and delete the current profile, unless
    find some kind of problem (i.e. Profile not found or not able to be deleted."""

        #log.debug(">>> Refresh Command")
        profile = self.toolbar.selected_profile
        target = self.toolbar.selected_target

        #log.debug(">>> Profile: %s" % profile)
        #log.debug(">>> Target: %s" % target)
        
        if target == '':
            target = self.empty_target
        
        try:
            cmd_profile = CommandProfile()
            command = cmd_profile.get_command(profile) % target
            del(cmd_profile)
            
            self.command_toolbar.command = command
        except ProfileNotFound:
            pass
            #self.profile_not_found_dialog()
        except TypeError:
            pass # That means that the command string convertion "%" didn't work

    def profile_not_found_dialog(self):
        warn_dialog = HIGAlertDialog(message_format=_("Profile not found!"),
                                     secondary_text=_("The profile name you \
selected/typed couldn't be found, and probably doesn't exist. Please, check the profile \
name and try again."),
                                     type=gtk.MESSAGE_QUESTION)
        warn_dialog.run()
        warn_dialog.destroy()

    def get_tab_label(selscf):
        return self.get_parent().get_tab_title(self)

    def set_tab_label(self, label):
        self.get_parent().set_tab_title(self, label)
    
    def start_scan_cb(self, widget=None):
        target = self.toolbar.selected_target
        command = self.command_toolbar.command
        profile = self.toolbar.selected_profile

        log.debug(">>> Start Scan:")
        log.debug(">>> Target: '%s'" % target)
        log.debug(">>> Profile: '%s'" % profile)
        log.debug(">>> Command: '%s'" % command)

        if target and profile:
            self.set_tab_label("%s on %s" %(profile, target))
        elif target:
            self.set_tab_label("Scan on %s" % target)
        elif profile:
            self.set_tab_label(profile)
        #####
        else:
            log.debug("not target OR profile")
        #####

        ##### If target empty, we are not changing the command
        if target != '':    
            self.toolbar.add_new_target(target)



        if (command.find("-iR") == -1 and command.find("-iL") == -1):
            if command.find("<target>") > 0:
                warn_dialog = HIGAlertDialog(message_format=_("No Target Host!"), 
                                             secondary_text=_("Target specification \
is mandatory. Either by an address in the target input box or through the '-iR' and \
'-iL' nmap options. Aborting scan."),
                                             type=gtk.MESSAGE_ERROR)
                warn_dialog.run()
                warn_dialog.destroy()
                return

        if command != '':
            # Setting status to scanning
            self.status.set_scanning()
            self.execute_command(command)
        else:
            warn_dialog = HIGAlertDialog(message_format=_("Empty Nmap Command!"),
                                         secondary_text=_("There is no command to  \
execute! Maybe the selected/typed profile doesn't exist. Please, check the profile name \
or type the nmap command you would like to execute."),
                                         type=gtk.MESSAGE_ERROR)
            warn_dialog.run()
            warn_dialog.destroy()

    def close_tab(self):
        try:
            gobject.source_remove(self.verify_thread_timeout_id)
        except:
            pass

    def collect_umit_info(self):
        profile = CommandProfile()
        profile_name = self.toolbar.selected_profile
        
        self.parsed.target = self.toolbar.get_target()
        self.parsed.profile_name = profile_name
        self.parsed.nmap_command = self.command_toolbar.get_command()
        self.parsed.profile = profile.get_command(profile_name)
        self.parsed.profile_hint = profile.get_hint(profile_name)
        self.parsed.profile_description = profile.get_description(profile_name)
        self.parsed.profile_annotation = profile.get_annotation(profile_name)
        self.parsed.profile_options = profile.get_options(profile_name)

        del(profile)

        try:
            # First try to get the contents of the output file, if we just ran a
            # scan.
            self.parsed.nmap_output = self.command_execution.get_raw_output()
        except:
            # Otherwise get the contents of the output display, if we loaded
            # results from a file and don't have access to the original output
            # file.
            self.parsed.nmap_output = self.scan_result.get_nmap_output()

    def kill_scan(self):
        """Tries to successfully kill scan, and then clear nmap output, host view, and disable
        widgets, set status set empty."""

        try:
            self.command_execution.kill()
        except AttributeError:
            pass

        self.scan_result.clear_nmap_output()
        self.scan_result.clear_host_view()
        self.status.set_empty()
        self.disable_widgets()
    
    def execute_command(self, command):
        """If scan state is alive and user responds OK, stop the currently active scan
        and allow creation of another, and if user responds Cancel, wait the current scan to finish.
        Invokes NmapCommand for execution. Verifies if a valid nmap executable exists in PATH, if not,
        displays error with the offending PATH. Refreshes and changes to nmap output view from given file."""

        log.critical("execute_command %s" % command)
        try:
            alive = self.command_execution.scan_state()
            if alive:
                warn_dialog = HIGAlertDialog(message_format=_("Scan has not finished yet"),
                                             secondary_text=_("Another scan is running in \
the background. To start another scan and kill the old one, click Ok. To wait for the \
conclusion of the old scan, choose Cancel."),
                                             type=gtk.MESSAGE_QUESTION,
                                             buttons=gtk.BUTTONS_OK_CANCEL)
                response = warn_dialog.run()
                warn_dialog.destroy()

                if response == gtk.RESPONSE_OK:
                    # Kill current scan, and let the another one to be created
                    self.kill_scan()
                else:
                    return
        except:
            pass

        self.command_execution = NmapCommand(command)
        
        try:
            self.command_execution.run_scan()
        except Exception, e:
            text = str(e)
            if type(e) == OSError:
                # Handle ENOENT specially.
                if e.errno == errno.ENOENT:
                    # nmap_command_path comes from zenmapCore.NmapCommand.
                    text += "\n\n" + _("This means that the nmap executable was not found in your system PATH, which is") + "\n\n" + os.getenv("PATH", _("<undefined>"))
                    path_env = os.getenv("PATH")
                    if path_env is None:
                        default_paths = []
                    else:
                        default_paths = path_env.split(os.pathsep)
                    extra_paths = get_extra_executable_search_paths()
                    extra_paths = [p for p in extra_paths if p not in default_paths]
                    if len(extra_paths) > 0:
                        if len(extra_paths) == 1:
                            text += "\n\n" + _("plus the extra directory")
                        else:
                            text += "\n\n" + _("plus the extra directories")
                        text += "\n\n" + os.pathsep.join(extra_paths)
            warn_dialog = HIGAlertDialog(message_format=_("Error executing command"),
                secondary_text=text, type=gtk.MESSAGE_ERROR)
            warn_dialog.run()
            warn_dialog.destroy()

        # Ask NmapOutputViewer to show/refresh nmap output from given file
        self.scan_result.show_nmap_output(self.command_execution.get_output_file())

        # Set a "EXECUTING" icon to host list
        self.scan_result.set_hosts({SCANNING:{'stock':gtk.STOCK_EXECUTE,'action':None}})
        self.scan_result.set_services({SCANNING:{'action':None}})

        # Clear port list, to remove old information
        self.scan_result.clear_port_list()

        # When scan starts, change to nmap output view tab and refresh output
        self.scan_result.change_to_nmap_output_tab()
        self.scan_result.refresh_nmap_output()
        
        self.enable_widgets()

        # Add a timeout function
        self.verify_thread_timeout_id = gobject.timeout_add(2000, self.verify_execution)

    def verify_execution(self):
        """To verify execution, get the scan_state of the command execution. If it errors out,
        disable the widgets and set the scan as failed. Automatically refreshes the nmap output. Returns
        True if execution verified, False otherwise"""

        # Using new subprocess style
        try:
            alive = self.command_execution.scan_state()
        except:
            self.disable_widgets()
            self.status.set_scan_failed()
            self.scan_result.set_nmap_output(self.command_execution.get_error())
            return False

        #log.debug(">>> Process alive? %s" % alive)
        #log.debug(">>> Nmap output:\n %s" % self.command_execution.get_output())

        # Maybe this automatic refresh should be eliminated to avoid processor burning
        self.scan_result.refresh_nmap_output()
        
        if alive:
            return True
        else:
            self.parse_result(self.command_execution.get_xml_output_file())
            return False

    def load_result(self, file_to_parse):
        """Sets status to parsing_result and then to loaded_unchanged"""

        self.status.set_parsing_result()
        self._parse(file_to_parse=file_to_parse)

        self.status.set_loaded_unchanged()

    def parse_result(self, file_to_parse):
        """Sets status to parsing_result and then to unsaved_unchanged"""

        self.status.set_parsing_result()
        self._parse(file_to_parse=file_to_parse)

        self.status.set_unsaved_unchanged()

    def load_from_parsed_result(self, parsed_result):
        """Sets status to parsing_result and then to unsaved_unchanged.
        Statuses appear to be identical to parse_result."""

        self.status.set_parsing_result()
        self._parse(parsed_result=parsed_result)

        self.status.set_unsaved_unchanged()

    def _parse(self, file_to_parse=None, parsed_result=None):
        """Called when scan is done. Parses out the results of scan. Verifies if any hosts were found.
        Attempts to parse unless it hits an error. For all hosts, it gets the host details and states as well
        as the corresponding host services and matched OS signature. It displays first host it can find, and
        shows it in nmap output. If target and profile name are non-NULL, set text for target and add new profile,
        respectively. Special note: removes and recreates self.parsed to avoid duplicate entry in same Scan tab."""

        log.debug(">>> XML output file that is going to be parsed: %s" % file_to_parse)
        
        self.host_view_selection = self.scan_result.get_host_selection()
        self.service_view_selection = self.scan_result.get_service_selection()
        
        # All hosts details pages
        self.host_pages = []
        self.changes = True
        
        self.host_view_selection.connect('changed', self.update_host_info)
        self.service_view_selection.connect('changed', self.update_service_info)
        self.scan_result.scan_host_view.clear_host_list()
        self.hosts = {}
        self.services = {}

        # Removed and created again to avoid host duplication problems when making
        # multiple scans inside the same scan tab
        try: del(self.parsed)
        except: pass

        if file_to_parse:
            self.parsed = NmapParser()
            self.parsed.set_xml_file(file_to_parse)
            try:
                log.debug(">>> Start parsing...")
                self.parsed.parse()
                log.debug(">>> Successfully parsed!")
            except:
                log.debug(">>> An exception occurred during xml output parsing")
                try:
                    error = self.command_execution.get_error()
                except:
                    error = _('Unknown error!')

                log.debug(">>> Error: '%s'" % error)

                # Treat root exceptions more carefully!
                if re.findall('[rR][oO0]{2}[tT]', error):
                    need_root = HIGAlertDialog(\
                            message_format=_('Root privileges are needed!'),\
                            secondary_text=error)
                    need_root.run()
                    need_root.destroy()
                else:
                    unknown_problem = HIGAlertDialog(\
                        message_format=_('An unexpected error occurred!'),\
                        secondary_text=error)
                    unknown_problem.run()
                    unknown_problem.destroy()
                return
        elif parsed_result:
            self.parsed = parsed_result

        if int(self.parsed.get_hosts_up()):
            for host in self.parsed.get_hosts():
                hostname = host.get_hostname()
                host_page = self.set_host_details(host)
                list_states = ["open", "filtered", "open|filtered"]

                for service in host.services:
                    name = service["service_name"]
                    state = service["port_state"]

                    if state not in list_states:
                        continue
                    
                    if name not in self.services.keys():
                        self.services[name] = {"hosts":[]}

                    hs = {"host":host, "page":host_page, "hostname":hostname}
                    hs.update(service)
                        
                    self.services[name]["hosts"].append(hs)
                    
                self.hosts[hostname] = {'host':host, 'page':host_page}
                    
                host_details = self.hosts[hostname]['page'].host_details
                host_info = self.hosts[hostname]['host']
                    
                try:
                    host_details.set_os_image(get_os_logo(host.get_osmatch()['name']))
                except:
                    host_details.set_os_image(get_os_logo(''))
                    
                host_details.set_vulnerability_image(get_vulnerability_logo\
                                                     (host_info.get_open_ports()))
                    
                icon = None
                try:icon = get_os_icon(host.get_osmatch()['name'])
                except:icon = get_os_icon('')
                    
                self.scan_result.scan_host_view.add_host({hostname:{'stock':icon,
                                                                    'action':None}})
                
            # Select the first host found
            self.host_view_selection.select_iter(self.scan_result.scan_host_view.\
                                                 host_list.get_iter_root())

        self.scan_result.scan_host_view.set_services(self.services.keys())
            
        try:
            # And them, we update the nmap output! ;)
            self.scan_result.scan_result_notebook.nmap_output.nmap_output.refresh_output()
        except:
            # Put saved nmap output
            self.scan_result.scan_result_notebook.nmap_output.\
                        nmap_output.text_buffer.\
                        set_text('\n'.join(self.parsed.get_nmap_output().split('\\n')))
            
        target = self.parsed.get_target()
            
        if target != '':
            self.toolbar.target_entry.child.set_text(target)
            
        profile_name = self.parsed.profile_name
            
        if profile_name != '':
            profile = CommandProfile()
            profile.add_profile(self.parsed.profile_name,
                                command=self.parsed.profile,
                                hint=self.parsed.profile_hint,
                                options=self.parsed.profile_options,
                                description=self.parsed.profile_description,
                                annotation=self.parsed.profile_annotation)
            del(profile)
                
            self.toolbar.profile_entry.update()
                
            self.toolbar.selected_profile = profile_name
        else:
            self.command_toolbar.command = self.parsed.get_nmap_command()
            self.clear_profile_target()

        self.collect_umit_info()
        self.switch_scan_details(self.__set_scan_info())
        self.check_fingerprints()

    def check_fingerprints(self):
        """To check if a fingerprint exists, check the lines in the nmap output
        and search for a matching entry for host/os/service. If no matching entries are found
        then user is prompted to submit these new fingerprint(s)."""

        re_host = re.compile(r"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})")
        re_os_fp = re.compile(r"(SInfo.*\);)")
        re_service_fp = re.compile(r"(SF-Port.*\);)")
        re_service_number = re.compile(r"SF-Port(\d+)")
        
        nmap_output = self.parsed.nmap_output.split("\\n\\n")
        fingerprints = {}
        current_ip = None

        for line in nmap_output:
            match_host = re_host.search(line)
            match_os = re_os_fp.search(line)
            match_service = re_service_fp.search(line)
            
            if match_host:
                current_ip = match_host.groups()[0]
            if match_os:
                if current_ip not in fingerprints.keys():
                    fingerprints[current_ip] = {}
                
                fingerprints[current_ip]["os"] = match_os.groups()[0]
            if match_service:
                if current_ip not in fingerprints.keys():
                    fingerprints[current_ip] = {}

                fp = match_service.groups()[0]
                
                fingerprints[current_ip]["service"] = fp

                #port = re_service_port.search(fp).groups()[0]
                #fingerprints[current_ip]["service_port"] = port
                #fingerprints[current_ip]["service_name"]
                

        """
        for fp in fingerprints:
            # We've found a new fp! Please contribute dialog
            # If ok, show the form, sending the ip and fingerprint.
        """

        key_num = len(fingerprints.keys())
        dialog_text = "%s. The submission and registration of \
fingerprints are very important for you and the Nmap project! If you would like to contribute \
to see your favorite network mapper recognizing those fingerprints in the future, choose the \
Ok button, and a submission page will be open in your default web browser with instructions \
about how to proceed on this registration."
        
        if key_num == 1:
            msg = _("Your network scan discovered an unknown fingerprint sent by the \
host %s") % fingerprints.keys()[0]
            
            self.show_contribute_dialog(dialog_text % msg)

        elif key_num > 1:
            msg = _("Your network scan discovered several unknown fingerprints sent by the \
following hosts: ")
            for i in fingerprints:
                msg += "%s, " % i
            msg = msg[:-2]

            self.show_contribute_dialog(dialog_text % msg)


    def show_contribute_dialog(self, dialog_text):
        """If the Services/OS Fingerprint that was found does not match any that is known, 
        open browser to the submission site if user gives positive response to the contribution
        dialog that displays."""

        contribute_dialog = HIGAlertDialog(message_format=_("Unrecognized Services/OS \
Fingerprints Found!"),
                                           secondary_text=dialog_text,
                                           type=gtk.MESSAGE_QUESTION,
                                           buttons=gtk.BUTTONS_OK_CANCEL)
        response = contribute_dialog.run()
        contribute_dialog.destroy()

        if response == gtk.RESPONSE_OK:
            import webbrowser
            webbrowser.open("http://nmap.org/submit/")
        

    def __verify_comments_changes(self):
        """Making sure that the actual comments changes go through."""

        try:
            for hostname in self.hosts:
                if self.hosts[hostname]['page'].host_details.\
                       get_comment() != self.comments[hostname]:
                    
                    log.debug("Changes on comments")
                    self.changes = True
                    return True
        except:
            return False
    
    def __set_scan_info(self):
        self.clean_scan_details()
        run_details = ScanRunDetailsPage()
        
        run_details.set_command_info(\
            {'command':self.parsed.get_nmap_command(),\
             'version':self.parsed.get_scanner_version(),\
             'verbose':self.parsed.get_verbose_level(),
             'debug':self.parsed.get_debugging_level()})
        
        run_details.set_general_info(\
            {'start':self.parsed.get_formated_date(),\
             'finish':self.parsed.get_formated_finish_date(),\
             'hosts_up':str(self.parsed.get_hosts_up()),\
             'hosts_down':str(self.parsed.get_hosts_down()),\
             'hosts_scanned':str(self.parsed.get_hosts_scanned()),\
             'open_ports':str(self.parsed.get_open_ports()),\
             'filtered_ports':str(self.parsed.get_filtered_ports()),\
             'closed_ports':str(self.parsed.get_closed_ports())})
             
        run_details.set_scan_infos(self.parsed.get_scaninfo())
        
        return run_details
    
    def update_host_info(self, widget):
        """"""

        self.scan_result.scan_result_notebook.port_mode()

        model_host_list, selection = widget.get_selected_rows()
        #host_objs = [self.hosts[model_host_list[i[0]][1]] for i in selection]
        host_objs = []
        for i in selection:
            key = model_host_list[i[0]][1]
            if self.hosts.has_key(key):
                host_objs.append(self.hosts[key])

        self.clean_host_details()
        
        if len(host_objs) == 1:
            self.set_single_host_port(host_objs[0]['host'])
            self.switch_host_details(host_objs[0]['page'])
        else:
            self.set_multiple_host_port(host_objs)
            self.switch_host_details(self.set_multiple_host_details(host_objs))

        # Switch nmap output to show first host occourrence
        try:
            self.go_to_host(host_objs[0]['host'].get_hostname())
        except IndexError:
            pass

    def update_service_info(self, widget):
        """If a service information in a particular widget has changed, it will update it."""

        self.scan_result.scan_result_notebook.host_mode()
        
        model_service_list, selection = widget.get_selected_rows()
        #serv_objs = [self.services[model_service_list[i[0]][0]] for i in selection]
        serv_objs = []
        for i in selection:
            key = model_service_list[i[0]][0]
            if self.services.has_key(key):
                serv_objs.append(self.services[key])

        # Removing current widgets from the host details page
        self.clean_host_details()

        if len(serv_objs) == 1:
            self.set_single_service_host(serv_objs[0]['hosts'])
            self.switch_host_details([page["page"] for page in serv_objs[0]['hosts']])
        else:
            servs = []
            for s in serv_objs:
                servs.append({"service_name":s["hosts"][0]["service_name"],
                     "hosts":s["hosts"]})

            self.set_multiple_service_host(servs)
            
            pages = []
            for serv in [serv["hosts"] for serv in serv_objs]:
                for h in serv:
                    # Prevent from adding a host more then once
                    if h["page"] not in pages:
                        pages.append(h["page"])
            
            self.switch_host_details(pages)

        # Change scan tab to "Ports/Hosts"
        self.scan_result.scan_result_notebook.set_current_page(0)
    
    def clean_host_details(self):
        parent = self.scan_result.scan_result_notebook.host_details_vbox
        children = parent.get_children()
        
        for child in children:
            parent.remove(child)
            
    def clean_scan_details(self):
        parent = self.scan_result.scan_result_notebook.scan_details_vbox
        children = parent.get_children()
        
        for child in children:
            parent.remove(child)

    def switch_host_details(self, page):
        """To switch host details, check the length of the page. If there is multiple pages,
        set them hidden and not expanded. If there is single page, set that page to the first page."""

        if type(page) == type([]):
            if len(page) > 1:
                for p in page:
                    p.hide()
                    p.set_expanded(False)
                    self.scan_result.scan_result_notebook.host_details_vbox._pack_noexpand_nofill(p)
                
                self.scan_result.scan_result_notebook.host_details_vbox.show_all()
                
                return
            elif len(page) == 1:
                page = page[0]
        
        try:
            page.hide()
        except:
            pass
        else:
            self.scan_result.scan_result_notebook.host_details_vbox._pack_noexpand_nofill(page)
            page.set_expanded(True)
            page.show_all()
    
    def switch_scan_details(self, page):
        """Removes current widget from the host details page and shows the whole of the page."""

        self.scan_result.scan_result_notebook.scan_details_vbox._pack_noexpand_nofill(page)
        page.show_all()
    
    def set_multiple_host_details(self, host_list):
        """Set details for multiple hosts in the list"""

        hosts = []
        for h in host_list:
            hosts.append(h['page'])
        
        return hosts

    def _save_comment(self, widget, extra, host_id):
        """Saves comment to the end, changes unsaved/unchanged states accordingly."""

        if self.status.unsaved_unchanged:
            self.status.set_unsaved_changed()
        elif self.status.loaded_unchanged or self.status.saved:
            self.status.set_loaded_changed()
        
        # Catch a comment and record it to be saved at the end.
        log.debug(">>> Catching edited comment to be saved posteriorly.")
        buff = widget.get_buffer()
        self.parsed.set_host_comment(host_id, buff.get_text(buff.get_start_iter(),
                                                            buff.get_end_iter()))
    
    def set_host_details(self, host):
        """Sets up all the host details and updates the host page with this information.
        Connects event to automatically update comments, target, and profile information. Then it
        sets the events to record comments to be saved automatically. It gathers the comments, uptime,
        status variables (incl. state, uptime, and lastboot entries) and attempts to get ipv4, ipv6, mac
        address info and show it. It gets hostname, OS info (if available), as well as TCP/IP info and displays it."""

        # Start connecting event to automatically update comments, target and profile information.
        host_page = ScanHostDetailsPage(host.get_hostname())
        host_details = host_page.host_details

        log.debug(">>> Setting host details")
        log.debug(">>> Hostname: %s" % host.get_hostname())
        log.debug(">>> Comment: %s" % self.parsed.get_host_comment(host.id))
        host_details.set_comment(self.parsed.get_host_comment(host.id))
        

        # Setting events to automatically record the commentary to be maintained
        host_page.host_details.comment_txt_vw.connect("insert-at-cursor", self._save_comment,
                                                      host.id)
        host_page.host_details.comment_txt_vw.connect("focus-out-event", self._save_comment,
                                                      host.id)

        
        self.comments[host.get_hostname()] = host.get_comment()
        
        uptime = host.get_uptime()

        host_details.set_host_status({'state':host.get_state(),
                                      'open':str(host.get_open_ports()),
                                      'filtered':str(host.get_filtered_ports()),
                                      'closed':str(host.get_closed_ports()),
                                      'scanned':str(host.get_scanned_ports()),
                                      'uptime':uptime['seconds'],
                                      'lastboot':uptime['lastboot']})

        ipv4 = ''
        try:ipv4 = host.get_ip()['addr']
        except KeyError: pass
        
        ipv6 = ''
        try:ipv6 = host.get_ipv6()['addr']
        except KeyError: pass
        
        mac = ''
        try:mac = host.get_mac()['addr']
        except KeyError: pass
        
        host_details.set_addresses({'ipv4':ipv4,'ipv6':ipv6,'mac':mac})
        
        host_details.set_hostnames(host.get_hostnames())
        
        os = host.get_osmatch()
        if os:
            os['portsused'] = host.get_ports_used()
            os['osclass'] = host.get_osclasses()
        
        host_details.set_os(os)
        host_details.set_tcpseq(host.get_tcpsequence())
        host_details.set_ipseq(host.get_ipidsequence())
        host_details.set_tcptsseq(host.get_tcptssequence())
        
        return host_page
    
    def set_single_host_port(self, host):
        """For a single host, each of the host's services including 
        hostname, portid, protocol, port_state, service_product, and service_version."""

        host_page = self.scan_result.scan_result_notebook.open_ports.host
        host_page.switch_port_to_list_store()
        
        p = host.get_ports()
        ports = []
        for port in p:
            ports += port['port']
        
        host_page.clear_port_list()
        for p in ports:
            host_page.add_port([self.findout_service_icon(p),
                                int(p.get('portid', '0')),
                                p.get('protocol', ''),
                                p.get('port_state', ''),
                                p.get('service_name', ''),
                                p.get('service_product', '')])

    def set_single_service_host(self, service):
        """For a single host, add each of the host's services including hostname,
        portid, protocol, port_state, service_product, and service_version."""

        host_page = self.scan_result.scan_result_notebook.open_ports.host
        host_page.switch_host_to_list_store()
        host_page.clear_host_list()

        for h in service:
            host_page.add_host([self.findout_service_icon(h),
                                h.get('hostname', ''),
                                int(h.get('portid', '0')),
                                h.get('protocol', ''),
                                h.get('port_state', ''),
                                h.get('service_product', ''),
                                h.get('service_version', '')])
        
    
    def set_multiple_host_port(self, host_list):
        """For multiple hosts in the host list, append a new entry to the tree, and
        get all of its ports, and append them to the respective host tree. Give for each port
                the portid, protocol, port_state, service_name and service_product."""

        host_page = self.scan_result.scan_result_notebook.open_ports.host
        host_page.switch_port_to_tree_store()
        host_page.clear_port_tree()
        
        for host in host_list:
            parent = host_page.port_tree.append(None, [host['host'].\
                                            get_hostname(),0,'','','','', ''])
            for port in host['host'].get_ports():
                for p in port.get('port', []):
                    host_page.port_tree.append(parent, \
                                ['',
                                 self.findout_service_icon(p),
                                 int(p.get('portid', "0")),
                                 p.get('protocol', ''),
                                 p.get('port_state', ""),
                                 p.get('service_name', _("Unknown")),
                                 p.get('service_product', "")])

    def set_multiple_service_host(self, service_list):
        """For multiple hosts that have services in the service list, append its entry to the host tree,
                and for each of these hosts append its properties (hostname, portid, protocol, port_state,
        service_product, service_version)."""

        host_page = self.scan_result.scan_result_notebook.open_ports.host
        host_page.switch_host_to_tree_store()
        host_page.clear_host_tree()
        
        for host in service_list:
            parent = host_page.host_tree.append(None, [host['service_name'],
                                                       '','',0,'','', '', ''])
            for h in host['hosts']:
                host_page.host_tree.append(parent, \
                                           ['',
                                            self.findout_service_icon(h),
                                            h["hostname"],
                                            int(h.get('portid', "0")),
                                            h.get('protocol', ""),
                                            h.get('port_state', _("Unknown")),
                                            h.get('service_product', ''),
                                            h.get('service_version', _("Unknown"))])
    
    def findout_service_icon(self, port_info):
        return gtk.STOCK_YES

class ScanResult(gtk.HPaned):
    """
        ScanResult
        Controls showing the results of a scan.
        Includes controls to change/clear output text, clear the hosts/services, return the
        selected host/services,        obtain and display the nmap output, let the services and hosts
        lists be selected, and refresh output.
        """
    def __init__(self):
        gtk.HPaned.__init__(self)
        
        self.scan_host_view = ScanHostsView()
        self.scan_result_notebook = ScanResultNotebook()

        self.pack1(self.scan_host_view, True, False)
        self.pack2(self.scan_result_notebook, True, False)

    def set_nmap_output(self, msg):
        self.scan_result_notebook.nmap_output.nmap_output.text_view.get_buffer().set_text(msg)

    def clear_nmap_output(self):
        self.scan_result_notebook.nmap_output.nmap_output.text_view.get_buffer().set_text("")

    def clear_host_view(self):
        self.set_hosts({})

    def clear_service_view(self):
        self.set_services({})

    def get_host_selection(self):
        return self.scan_host_view.host_view.get_selection()

    def get_service_selection(self):
        return self.scan_host_view.service_view.get_selection()

    def get_nmap_output(self):
        return self.scan_result_notebook.nmap_output.get_nmap_output()

    def show_nmap_output(self, file):
        """Ask NmapOutputViewer to show/refresh nmap output from the given file"""
        self.scan_result_notebook.nmap_output.nmap_output.show_nmap_output(file)

    def set_hosts(self, hosts_dic):
        """Set hosts to those in host list"""
        self.scan_host_view.set_hosts(hosts_dic)

    def set_services(self, services_dic):
        """Set services to those in services list"""
        self.scan_host_view.set_services(services_dic)

    def clear_port_list(self):
        """Clear the scan result ports list"""
        self.scan_result_notebook.open_ports.host.clear_port_list()

    def change_to_nmap_output_tab(self):
        """Show the nmap output tab"""
        self.scan_result_notebook.set_current_page(1)

    def refresh_nmap_output(self):
        """Refresh Nmap output in nmap output tab"""
        self.scan_result_notebook.nmap_output.nmap_output.refresh_output()
        

class ScanResultNotebook(HIGNotebook):
    """
    ScanResultNotebook
    Creates a new Scan Results notebook page, which includes the sections Ports/Hosts,
    Nmap Output, Host Details, and Scan Details.
    Organizes the way the results for scan is displayed in its new tab.
    """
    def __init__(self):
        HIGNotebook.__init__(self)
        self.set_scrollable(True)
        self.set_border_width(5)
        
        self.__create_widgets()
        self.__nmap_output_refreshing()
        
        self.append_page(self.open_ports_page, gtk.Label(_('Ports / Hosts')))
        self.append_page(self.nmap_output_page, gtk.Label(_('Nmap Output')))
        self.append_page(self.host_details_page, gtk.Label(_('Host Details')))
        self.append_page(self.scan_details_page, gtk.Label(_('Scan Details')))

    def get_nmap_output(self):
        return self.nmap_output.get_map_output()
    
    def host_mode(self):
        self.open_ports.host.host_mode()

    def port_mode(self):
        self.open_ports.host.port_mode()
    
    def __create_widgets(self):
        self.open_ports_page = HIGVBox()
        self.nmap_output_page = HIGVBox()
        self.host_details_page = HIGScrolledWindow()
        self.scan_details_page = HIGScrolledWindow()
        self.scan_details_vbox = HIGVBox()
        self.host_details_vbox = HIGVBox()
        
        self.open_ports = ScanOpenPortsPage()
        self.nmap_output = ScanNmapOutputPage()
        
        self.no_selected = gtk.Label(_('No host selected!'))
        self.host_details = self.no_selected
        
        self.no_details = gtk.Label(_('Scan is not finished yet!'))
        self.scan_details = self.no_details
        
        self.open_ports_page.add(self.open_ports)
        self.nmap_output_page.add(self.nmap_output)
        
        self.host_details_page.add_with_viewport(self.host_details_vbox)
        self.host_details_vbox._pack_expand_fill(self.host_details)
        
        self.scan_details_page.add_with_viewport(self.scan_details_vbox)
        self.scan_details_vbox._pack_expand_fill(self.scan_details)
    
    def __nmap_output_refreshing(self):
        self.connect('switch-page', self.refresh_cb)
    
    def refresh_cb(self, widget, page=None, page_num=None):
        if self.nmap_output.nmap_output.thread.isAlive():
            if page_num == 2:
                self.nmap_output.nmap_output.refresh_output(None)


if __name__ == "__main__":
    status = PageStatus("empty")
    status.set_saved()
    status.set_unsaved_unchanged()
    status.set_unsaved_changed()
    status.set_loaded_unchanged()
    status.set_loaded_changed()
    status.set_empty()
    status.set_scanning()
    status.set_parsing_result()
    status.set_unknown()

    print "Saved:", status.saved
    print "Unsaved unchanged:", status.unsaved_unchanged
    print "Unsaved changed:", status.unsaved_changed
    print "Loaded unchanged:", status.loaded_unchanged
    print "Loaded changed:", status.loaded_changed
    print "Empty:", status.empty
    print "Scanning:", status.scanning
    print "Parsing result:", status.parsing_result
    print "Unknown:", status.unknown
