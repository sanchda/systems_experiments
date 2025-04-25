extern void weak_symbol(void* arg);
int foo() { return !!weak_symbol; }
int main() { return foo(); }
