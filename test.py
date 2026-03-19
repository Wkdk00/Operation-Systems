import sys
import ctypes
import os

if len(sys.argv) < 4:
    print("Usage: python3 test.py <file1> [file2 ...] <key> <output_dir>")
    sys.exit(1)

output_dir = sys.argv[-1]
key = int(sys.argv[-2]) & 0xFF
input_files = sys.argv[1:-2]

ArrayType = ctypes.c_char_p * len(input_files)
files_array = ArrayType(*[f.encode('utf-8') for f in input_files])

lib = ctypes.CDLL("./libcaesar.so")

lib.set_key.argtypes = [ctypes.c_char]
lib.set_key.restype = None

lib.process_files.argtypes = [ctypes.POINTER(ctypes.c_char_p), ctypes.c_int, ctypes.c_char_p]
lib.process_files.restype = ctypes.c_int

lib.set_key(ctypes.c_char(key))

os.makedirs(output_dir, exist_ok=True)

result = lib.process_files(files_array, len(input_files), output_dir.encode('utf-8'))

if result == 0:
    print("Done.")
else:
    print("Error processing files.")
    sys.exit(1)