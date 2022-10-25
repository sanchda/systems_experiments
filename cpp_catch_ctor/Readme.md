static_overrun
===

`std::string` is a strange beast.  You might be able to tame it, but not as a `constexpr`.  It may fail, even in a global (static).

Sometimes you can't stringview.  What do you do?
