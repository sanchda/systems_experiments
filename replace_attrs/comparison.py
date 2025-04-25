import attr
import typing as T

# Here's a standard decorator using attr.s to add slots to a class
def attrs_class(cls):
    return attr.s(slots=True)(cls)

# And then here's a decorator that does the same thing without attr.s
# by using class attributes
def classic_class(cls):
    annotations = cls.__annotations__
    defaults = {k: v for k, v in cls.__dict__.items() if k in annotations}  # avoid methods and non-annotated attributes
    cls.__slots__ = tuple(annotations.keys())

    # For initialization
    def __init__(self, *args, **kwargs):
        super(cls, self).__init__() # init superclass
        for key, value in defaults.items(): # defaults are already in annotations
            setattr(self, key, value)
        for key, value in kwargs.items(): # handle keyword arguments
            if key in annotations:
                setattr(self, key, value)
        for key, value in zip(annotations.keys(), args): # handle positional arguments
            setattr(self, key, value)

    cls.__init__ = __init__

    # For printing
    def __repr__(self):
        return f"{cls.__name__}({', '.join(f'{k}={getattr(self, k)}' for k in annotations.keys())})"
    cls.__repr__ = __repr__
    return cls

# Here's a class that uses the attrs_class decorator
@attrs_class
class A:
    x = attr.ib(default=None)
    y = attr.ib(default=0, type=int)


# And here's a class that uses the classic_class decorator
@classic_class
class B:
    x: T.Optional[T.Any] = None
    y: int = 0


print("=====")
print(A())
print(B())
print("=====")
print(A(1, 2))
print(B(1, 2))
print("=====")
print(A(x=1, y=2))
print(B(x=1, y=2))
