#!/usr/bin/env python3

# Change some string in all files recursively in directories. Use with caution.

import re
import argparse
import os
from pprint import pprint

class Config:
    def __init__(self):
        self.extensions = []
        self.is_dry_run = True
        self.regex = None
        self.replace_with = None
        self.changed_files = []

config = Config()

def walk_directory(d):
    for dirpath, _, filenames in os.walk(d):
        for f in filenames:
            ext_ok = False
            for ext in config.extensions:
                if f.endswith(ext):
                    ext_ok = True
                    break

            if not ext_ok:
                continue

            filepath = os.path.join(dirpath, f)
            replace_in_file(filepath)


def replace_in_file(filepath):
    print('Replacing in file', filepath)
    with open(filepath, 'r') as stream:
        source = stream.read()
        new_string = re.sub(config.regex, config.replace_with, source)
        if new_string != source:
            write_to_file(filepath, new_string)


def write_to_file(filepath, new_string):
    config.changed_files.append(filepath)
    if config.is_dry_run:
        return
    print(new_string)
    with open(filepath, 'w') as stream:
        stream.write(new_string)

def arg_list(strlist: str):
    s = strlist.strip()
    assert(len(s) >= 2)
    assert(s[0] == '[')
    assert(s[-1] == ']')
    s = s[1:-1]
    libnames = [name.strip() for name in s.split(',')]
    return libnames

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description='Change string in all text files recursively')
    ap.add_argument('directories', nargs='*', default=[], help='list of directories to traverse')
    ap.add_argument('-e', '--extensions', default='', help='list of extensions')
    ap.add_argument('-r', '--regex', default=None, help='regex to match string against')
    ap.add_argument('-n', '--newstring', default=None, help='new string')
    ap.add_argument('-y', '--yes', action='store_true', help='Yes, do actually modify the files')

    args = ap.parse_args()
    print('Traversing directories - ' + str(args.directories))

    if args.extensions == '':
        print('Will test ALL files')
    else:
        config.extensions = arg_list(args.extensions)
        print('Replacing in files with extensions: ' + str(config.extensions))

    config.is_dry_run = not args.yes
    if config.is_dry_run:
        print('Doing a dry run')
    else:
        print('NOT a dry run')

    if args.regex is None:
        print('Error - Need regex')
        exit()

    if args.newstring is None:
        print('Error - Need string to replace with')
        exit()


    config.regex = re.compile(args.regex)
    config.replace_with = args.newstring

    for d in args.directories:
        walk_directory(d)


    print('Files changed - \n{}'.format('\n'.join(config.changed_files)))
