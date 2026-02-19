import ctypes
import sys

try:
    lib = ctypes.CDLL("./libcaesar.so")
except OSError:
    print("Ошибка загрузки библиотеки")
    sys.exit(1)

lib.caesar_key.argtypes = [ctypes.c_ubyte]
lib.caesar_key.restype = None

lib.caesar.argtypes = [
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_int
]
lib.caesar.restype = None

data = b"hello world"
length = len(data)

src = ctypes.create_string_buffer(data)
dst = ctypes.create_string_buffer(length)

lib.caesar_key(3)
lib.caesar(src, dst, length)

print("Encrypted:", bytes(dst.raw))

decrypted = ctypes.create_string_buffer(length)
lib.caesar(dst, decrypted, length)

print("Decrypted:", bytes(decrypted.raw))