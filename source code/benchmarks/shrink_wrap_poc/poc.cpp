// Compile with GCC 4.9.2 VTV enable
// g++ -fvtable-verify=std -mpreferred-stack-boundary=2 -m32
#include <unistd.h>
#include <stdlib.h>
struct RefCounted {
        virtual void addRef() {}
        virtual void delRef() {} };
struct Logged {
        virtual void log() {} };
struct ProcessWrapper : virtual Logged, virtual RefCounted {
        virtual void run(char *path) {
            execlp(path, path, NULL);
        } };
int main(int argc, char ** argv) {
    // --Original Object Pointer--
    RefCounted *ptr = new RefCounted();
    
    // --Memory Corruption--
    ptr = (RefCounted*)(void*)(new ProcessWrapper());
    __asm__("push %0\n"::"r"("ls"));
    
    // --Hijacked Call-Site--
    ptr->delRef(); }

//    ProcessWrapper *tmp = ; // Object to get VTable from
//    size_t *vtableAddr = *reinterpret_cast<size_t**>(tmp);
//    size_t **vptrAddr = reinterpret_cast<size_t**>(ptr);
//    *vptrAddr = vtableAddr; // Swap VTable pointer
