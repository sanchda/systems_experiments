import os
import sys

def file_launched_with_python(filepath):
    # The first two bytes of an executable are special, #! indicates it's meant to be "interpreted"
    # by the specified program; if that program is a Python, then mark that file.
    try:
        with open(filepath, 'rb') as file:
            if file.read(2) == b'#!':
                return b"python" in file.readline().strip().lower()
    except Exception:
        pass
    return False

def search_directory(directory):
    for root, _, files in os.walk(directory):
        for file in files:
            filepath = os.path.join(root, file)
            if file_launched_with_python(filepath):
                print(filepath)

if __name__ == "__main__":
    directories_to_search = sys.argv[1:] if len(sys.argv) > 1 else os.environ['PATH'].split(':')
    for directory in directories_to_search:
        if os.path.isdir(directory):
            search_directory(directory)
