# sigaction_deadlock

* Typically, you can't interrupt segfaults with segfaults
* But when you specify the handler with SA_NODEFER, you can
* Static variables to the rescue?
