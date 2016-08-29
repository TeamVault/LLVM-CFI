
#include <iostream>
/*Diamond shape class hierarchy
  A1     A2   A3
   \      \  /
    \      \/
     \     B
      \   /
       \ /
        C    
*/
using namespace std;

class A1{
    public:
        int basedata;

        virtual void set_a1(int data){
            basedata = data;
        }

        virtual int get_a2(){
            return basedata;
        }
};

class A2{
    public:
        int basedata;

        virtual void set_a2(int data){
            basedata = data;
        }

        virtual int get_a2(){
            return basedata;
        }
};

class A3{
    public:
        int basedata;

        virtual void set_a3(int data){
            basedata = data;
        }

        virtual int get_a3(){
            return basedata;
        }
};


class B :virtual public A2, virtual public A3
{
 public:
        int basedata;

        virtual void set_b(int data){
            basedata = data;
        }

        virtual int get_b(){
            return basedata;
        }
};

class C : virtual public A1, virtual public B
{
    public:
       int basedata;
       int get_c(){
           return 3 * basedata;           // multiply by 3
       }
       void set_c(int data){
           basedata = data;
       }
};

int main(int argc, char * argv[]){
   cout << "Hello World"<< endl;

   
   /*A1 * ptr_a1 = new A1();
   ptr_a1->set_a1(10);
   int get_a1_value = ptr_a1->get_a1();
   cout << "The value in A1 class is " << get_a1_value << endl;

   A2 * ptr_a2 = new A2();
   ptr_a2->set_a2(20);
   int get_a2_value = ptr_a2 -> get_a2();
   cout << "The value in A2 class is " << get_a2_value << endl;*/

   A3 * ptr_a3 = new A3();
   ptr_a3->set_a3(30);
   int get_a3_value = ptr_a3 -> get_a3();
   cout << "The value in A3 class is " << get_a3_value << endl;

   B * ptr_b = new B();
   ptr_b->set_a3(40);
   int get_b_value = ptr_b -> get_a3();
   cout << "The value in B class is " << get_b_value << endl;
   
   /*C * ptr_c = new C();
   ptr_c->setData(50);
   int get_c_value = ptr_c -> getData();
   cout << "The value in C class is " << get_c_value << endl;*/
   
}
