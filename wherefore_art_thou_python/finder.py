# wherefore doesn't mean "where" lol
# eg python3 finder.py $(echo $PATH | tr ':' ' ') | xargs -n 1 basename | sort | uniq
import os
import sys
import subprocess

def is_python_shebang(line):
    return line.startswith("#!") and "python" in line.lower()

def is_text_file(filepath):
    try:
        result = subprocess.run(['file', '-L', '--mime-type', filepath], capture_output=True, text=True)
        mime_type = result.stdout.split(':')[1].strip()
        return mime_type.startswith('text')
    except Exception as e:
        return False

def check_file_for_shebang(filepath):
    if is_text_file(filepath):
        try:
            with open(filepath, 'r') as file:
                first_line = file.readline().strip()
                if is_python_shebang(first_line):
                    print(f"{filepath}")
        except (IOError, OSError, UnicodeDecodeError):
                pass

def search_directory(directory):
    for root, dirs, files in os.walk(directory):
        for file in files:
            filepath = os.path.join(root, file)
            check_file_for_shebang(filepath)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python script_name.py <dir1> <dir2> ... <dirN>")
        sys.exit(1)

    directories_to_search = sys.argv[1:]

    for directory in directories_to_search:
        if os.path.isdir(directory):
                search_directory(directory)
