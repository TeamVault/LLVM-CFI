// Compile with Clang 3.8
// clang++ -fsanitize=cfi -flto -Wall -Werror -std=c++0x -O3
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

//Traditional RefCounted Interface
struct RefCounted {
        // Empty method bodies, because they are never used in POC
        virtual void addRef() {}
        virtual void delRef() {}
};

//Traditional Logged Interface
struct Logged {
        // Implementation of log to be used by most classes
        virtual void log() { 
            printf("Logging succeded\n");
        }
};

// Custom class inheriting both RefCounted and Logged interface
struct MyClass : RefCounted, Logged {};

// Custom implementation of Logged interface
// Sibling of MyClass in hierarchy
struct CustomLogged : RefCounted, Logged {
        // Implementation of log that should not be targetable from MyClass
        virtual void log() {
            printf("PWNED\n");
        }
};

int main(int argc, char ** argv) {
    // --Original Object Pointer--
    MyClass *ptr = new MyClass();
    // --Memory Corruption--
    unsigned long *addr = (unsigned long *)(&ptr);
    *addr = (unsigned long)(void*)(new CustomLogged());
    // --Hijacked Call-Site--
    ptr->log();
}
