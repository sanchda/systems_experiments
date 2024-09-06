import ctypes
import contextvars

libpython = ctypes.PyDLL(None)
libpython.Py_DecRef.argtypes = [ctypes.py_object]
libpython.Py_DecRef.restype = None

vars = [contextvars.ContextVar(f'var_{i}', default=None) for i in range(10)]

def populate():
    # Create several huge objects
    for var in vars:
        obj = 'x' * 64 * 1024 * 1024
        var.set(obj)


def depop():
    for var in vars:
        obj = var.get()
        libpython.Py_DecRef(obj)

def crash():
    var = contextvars.ContextVar('var', default=None)
    var.set("Oops")


if __name__ == '__main__':
    populate()
    print("populated")
    depop()
    crash()
    print("Done.  We might segfault now because this is a toy example, but that's fine.")
