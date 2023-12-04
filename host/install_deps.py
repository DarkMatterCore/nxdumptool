# Copyright (c) 2019-2021 Ian Burgwin

# This is meant to be double-clicked from File Explorer.
# This doesn't import pip as a module in case the way it's executed changes, which it has in the past.
# Instead we call it like we would in the command line.

from subprocess import run
from os.path import dirname, join
from sys import executable
from platform import system

root_dir = dirname(__file__)

requirements_file = ('requirements-win32.txt' if system() == 'Windows' else 'requirements.txt')

run([executable, '-m', 'pip', 'install', '-r', join(root_dir, requirements_file)])
input('Press enter to close')
