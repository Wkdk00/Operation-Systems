import sys
import ctypes

if len(sys.argv) != 5:
    print("Usage: python3 test.py <lib_path> <key> <input_file> <output_file>")
    sys.exit(1)

lib_path = sys.argv[1]
key = int(sys.argv[2]) & 0xFF
input_file = sys.argv[3]
output_file = sys.argv[4]

lib = ctypes.CDLL(lib_path)

lib.set_key.argtypes = [ctypes.c_char]
lib.set_key.restype = None

lib.caesar.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
lib.caesar.restype = None

with open(input_file, 'rb') as f:
    data = f.read()

buffer = ctypes.create_string_buffer(data, len(data))

lib.set_key(ctypes.c_char(key))

lib.caesar(buffer, buffer, len(data))

with open(output_file, 'wb') as f:
    f.write(buffer.raw)