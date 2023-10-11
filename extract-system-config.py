#!/usr/bin/env python

import sys
import re
import getopt
import os
import subprocess


class Data:
    def __init__(self, data):
        self.data = data

class DebugData:
    class Item:
        def __init__(self):
            self.type = ''
            self.ref = 0
            self.level = 0
            self.attributes = {}
    def __init__(self, debug_filename):
        file = subprocess.check_output(['readelf', '-wi', debug_filename], \
                   env=dict(os.environ, LC_ALL="C"))
        self.__lines = list(filter(None, file.decode().split('\n')))
        self.__current = 0
        self.__re1 = re.compile ('<([^>]+)><([^>]+)>:[^A]*Abbrev Number:.*\d+.*\((\w+)\)')
        self.__re2 = re.compile ('<[^>]+>[^D]*(DW_AT_\w+)([^:]*:)+ <?0?x?([^ \t><\)]+)[ >\t\)]*$')
        return
    def rewind (self):
        self.__current = 0
        return
    def read_line (self):
        if self.__current == len (self.__lines):
            return ''
        line = self.__lines[self.__current]
        self.__current = self.__current + 1
        return line
    def write_back_line (self):
        self.__current = self.__current - 1
        return
    def write_back_one (self, item):
        self.__current = self.__current - 1 - len (item.attributes.keys ())
        return
    def read_one (self):
        item = DebugData.Item ()
        while 1:
            line = self.read_line ()
            if line == '':
                if item.type == '':
                    return None
                else:
                    return item
            result = self.__re1.search (line)
            if result is None:
                continue
            item.level = result.group (1)
            item.ref = result.group (2)
            item.type = result.group (3)
            while 1:
                line = self.read_line ()
                result = self.__re1.search (line)
                if result is not None:
                    self.write_back_line ()
                    return item
                result = self.__re2.search (line)
                if result is None:
                    self.write_back_line ()
                    return item
                item.attributes[result.group (1)] = result.group (3)
        return item
    def find_struct (self, struct_type_name):
        return self.find_by_name ('DW_TAG_structure_type', struct_type_name)
    def find_by_name (self, type, name):
        item = self.read_one ()
        while item is not None:
            if item.type.strip() == type and \
                    'DW_AT_name' in item.attributes and \
                    item.attributes['DW_AT_name'].strip() == name:
                return item
            item = self.read_one ()
        return item
    def find_by_ref (self, ref):
        item = self.read_one ()
        while item is not None:
            if item.ref == ref:
                return item
            item = self.read_one ()
        return item
    def find_member (self, member_name, parent):
        sub_item = self.read_one ()
        while sub_item is not None:
            if sub_item.level == parent.level:
                self.write_back_one (sub_item)
                return None
            if sub_item.type.strip() == 'DW_TAG_member' and \
                    'DW_AT_name' in sub_item.attributes and \
                    sub_item.attributes['DW_AT_name'].strip() == member_name:
                return Data (sub_item.attributes['DW_AT_data_member_location'].strip())
            sub_item = self.read_one ()
        return None
    # public methods below
    def get_struct_member_offset (self, struct_type_name, member_name):
        self.rewind ()
        item = self.find_struct (struct_type_name)
        if item is None:
            return None
        return self.find_member (member_name, item)
    def get_struct_size (self, struct_type_name):
        self.rewind ()
        item = self.find_struct (struct_type_name)
        if item is None:
            return None
        if 'DW_AT_byte_size' not in item.attributes:
            return None
        return Data (item.attributes['DW_AT_byte_size'].strip())
    def get_typedef_member_offset (self, typename, member):
        self.rewind ()
        item = self.find_by_name ('DW_TAG_typedef', typename)
        if item is None:
            return None
        if 'DW_AT_type' not in item.attributes:
            return None
        ref = item.attributes['DW_AT_type'].strip()
        self.rewind ()
        item = self.find_by_ref (ref)
        if item is None:
            return None
        return self.find_member (member, item)

class CouldNotFindFile(BaseException):
    pass

def search_debug_file():
    files_to_try = ['/usr/lib64/debug/lib64/ld-2.11.2.so.debug',
                    '/usr/lib/debug/lib64/ld-linux-x86-64.so.2.debug',
                    '/usr/lib/debug/ld-linux-x86-64.so.2',
                    '/usr/lib/debug/lib/ld-linux.so.2.debug',
                    '/usr/lib/debug/ld-linux.so.2',
                    # ubuntu 1104/1110
                    '/usr/lib/debug/lib/i386-linux-gnu/ld-2.13.so',
                    '/usr/lib/debug/lib/x86_64-linux-gnu/ld-2.13.so',
                    # for ubuntu 0910. braindead
                    '/usr/lib/debug/lib/ld-2.10.1.so',
                    # for ubuntu 1004.
                    '/usr/lib/debug/lib/ld-2.11.1.so',
                    # for ubuntu 1010.
                    '/usr/lib/debug/lib/ld-2.12.1.so',
                    # ubuntu 1204
                    '/usr/lib/debug/lib/x86_64-linux-gnu/ld-2.15.so',
                    '/usr/lib/debug/lib/i386-linux-gnu/ld-2.15.so',
                    # ubuntu 1304
                    '/usr/lib/debug/lib/x86_64-linux-gnu/ld-2.17.so',
                    '/usr/lib/debug/lib/i386-linux-gnu/ld-2.17.so',
                    # ubuntu 1404
                    '/usr/lib/debug/lib/x86_64-linux-gnu/ld-2.19.so',
                    '/usr/lib/debug/lib/i386-linux-gnu/ld-2.19.so',
                    # ubuntu 1604
                    '/usr/lib/debug/lib/x86_64-linux-gnu/ld-2.23.so',
                    '/usr/lib/debug/lib/i386-linux-gnu/ld-2.23.so',
                    ]
    for file in files_to_try:
        if os.path.isfile (file):
            return file

    sfiles_to_try = ['/usr/lib64/ld-linux-x86-64.so.2',
                     '/usr/lib32/ld-linux-x86-64.so.2',
                     '/usr/lib/ld-linux-x86-64.so.2',
                    ]
    for file in sfiles_to_try:
        if os.path.isfile (file):
            notes = subprocess.check_output(['readelf', '-n', file], \
                        env=dict(os.environ, LC_ALL="C")).decode().split("\n")
            for line in notes:
                if re.search("Build ID", line):
                    hash = line.split()[2]
                    path = "/usr/lib/debug/.build-id/" + hash[:2] + "/" + \
                            hash[2:] + ".debug"
                    return path

    raise CouldNotFindFile ()

def list_lib_path():
    paths = []
    re_lib = re.compile ('(?<=^#)')
    for filename in os.listdir("/etc/ld.so.conf.d/"):
        try:
            for line in open ("/etc/ld.so.conf.d/" + filename, 'r'):
                if re_lib.search (line) is not None:
                    continue
                paths.append(line.rstrip ())
        except:
            continue
    return ':'.join(paths)

def usage():
    print('')

def main(argv):
    config_filename = ''
    debug_filename = ''
    try:
        opts, args = getopt.getopt(argv, 'hc:d:',
                                   ['help', 'config=', 'debug='])
    except getopt.GetoptError:
        usage()
        sys.exit(2)
    for opt, arg in opts:
        if opt in ('-h', '--help'):
            usage()
            sys.exit()
        elif opt in ('-c', '--config'):
            config_filename = arg
        elif opt in ('-d', '--debug'):
            debug_filename = arg

    if config_filename != '':
        config = open (config_filename, 'w')
    else:
        config = sys.stdout
    if debug_filename == '':
        debug_filename = search_debug_file ()
    debug = DebugData (debug_filename)
    data = debug.get_struct_size ('rtld_global')
    if data is None:
        sys.exit (1)
    config.write ('#define CONFIG_RTLD_GLOBAL_SIZE ' + str(data.data) + '\n')

    data = debug.get_struct_size ('rtld_global_ro')
    if data is None:
        sys.exit (1)
    config.write ('#define CONFIG_RTLD_GLOBAL_RO_SIZE ' + str(data.data) + '\n')

    data = debug.get_struct_member_offset ('rtld_global_ro', '_dl_pagesize')
    if data is None:
        sys.exit (1)
    config.write ('#define CONFIG_RTLD_DL_PAGESIZE_OFFSET ' + str(data.data) + '\n')

#    data = debug.get_struct_member_offset ('rtld_global', '_dl_error_catch_tsd')
#    if data is None:
#        sys.exit (1)
#    config.write ('#define CONFIG_DL_ERROR_CATCH_TSD_OFFSET ' + str(data.data) + '\n')

    data = debug.get_struct_size ('pthread')
    if data is None:
        sys.exit (1)
    config.write ('#define CONFIG_TCB_SIZE ' + str(data.data) + '\n')

    data = debug.get_typedef_member_offset ('tcbhead_t', 'tcb')
    if data is None:
        sys.exit (1)
    offset = str(data.data) if len(str(data.data)) > 0 else '0'
    config.write ('#define CONFIG_TCB_TCB_OFFSET ' + offset + '\n')

    data = debug.get_typedef_member_offset ('tcbhead_t', 'dtv')
    if data is None:
        sys.exit (1)
    config.write ('#define CONFIG_TCB_DTV_OFFSET ' + str(data.data) + '\n')

    data = debug.get_typedef_member_offset ('tcbhead_t', 'self')
    if data is None:
        sys.exit (1)
    config.write ('#define CONFIG_TCB_SELF_OFFSET ' + str(data.data) + '\n')

    data = debug.get_typedef_member_offset ('tcbhead_t', 'sysinfo')
    if data is None:
        sys.exit (1)
    config.write ('#define CONFIG_TCB_SYSINFO_OFFSET ' + str(data.data) + '\n')

    data = debug.get_typedef_member_offset ('tcbhead_t', 'stack_guard')
    if data is None:
        sys.exit (1)
    config.write ('#define CONFIG_TCB_STACK_GUARD ' + str(data.data) + '\n')

    config.write ('#define CONFIG_SYSTEM_LDSO_LIBRARY_PATH \"' + list_lib_path () + '\"\n')

if __name__ == "__main__":
    main(sys.argv[1:])






