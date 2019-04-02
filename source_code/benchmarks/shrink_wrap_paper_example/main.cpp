
#include <iostream>
/*Diamond shape class hierarchy
  A1     A2   A3
   \      \  /
    \      \/
     \     B
      \   /
       \ /
        C  
        |
        |
        /D 
*/
using namespace std;

class A1{
    public:
        int basedata;

        virtual void set_a1(int data){
            basedata = data;
        }

        virtual int get_a1(){
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

/*
class D : virtual public C
{
    public:
       int basedata;
       int get_d(){
           return 3 * basedata;           // multiply by 3
       }
       void set_d(int data){
           basedata = data;
       }
};
*/

int main(int argc, char * argv[]){
   cout << "Hello World"<< endl;

   /*
   A1 * ptr_a1 = new A1();
   ptr_a1->set_a1(10);
   int get_a1_value = ptr_a1->get_a1();
   cout << "The value in A1 class is " << get_a1_value << endl;

   A2 * ptr_a2 = new A2();
   ptr_a2->set_a2(20);
   int get_a2_value = ptr_a2 -> get_a2();
   cout << "The value in A2 class is " << get_a2_value << endl;
   */
   
   A3 * ptr_a3 = new A3();//this is A3* a3; a3->f_A3(); from the SW paper
   ptr_a3->set_a3(30);
   int get_a3_value = ptr_a3 -> get_a3();
   cout << "The value in A3 class is " << get_a3_value << endl;

   B * ptr_b = new B();//this is B* b; b->f_A3(); from the SW paper
   ptr_b->set_a3(40);
   int get_b_value = ptr_b -> get_a3();
   cout << "The value in B class is " << get_b_value << endl;
   
   
   C * ptr_c = new C();
   ptr_c->set_c(50);
   int get_c_value = ptr_c -> get_c();
   cout << "The value in C class is " << get_c_value << endl;

   /*
   D * ptr_d = new D();
   ptr_d->set_d(60);
   int get_d_value = ptr_d -> get_c();
   cout << "The value in D class is " << get_d_value << endl;
   */
   
}
