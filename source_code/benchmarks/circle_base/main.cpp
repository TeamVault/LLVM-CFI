
#include <iostream>
/*Diamond shape class hierarchy
      Parent
       /\
      /  \
 child1  child2
      \   /
       \ /
    MixedChild    
*/
using namespace std;

class Parent{
    public:
        int basedata;

        virtual void setData(int data){
            basedata = data;
        }

        virtual int getData(){
            return basedata;
        }
};

class Child1 : virtual  public Parent // shares copy of Parent
{};

class Child2 : virtual  public Parent // shares copy of Parent
{};

class Grandchild :virtual public Child1, virtual public Child2
{
};

class MixedChild : virtual public Child1, virtual public Child2, virtual public Grandchild,  virtual public Parent
{
    public:
       int basedata;
       int getData(){
           return 3*basedata;           // okay
       }
       void setData(int data){
           basedata = data;
       }
};

int main(int argc, char * argv[]){
   cout << "Hello World"<< endl;

   Parent * ptr_parent = new Parent();
   ptr_parent->setData(20);
   int get_parent_value = ptr_parent->getData();
   cout << "The value in Parent class is " << get_parent_value << endl;

   Grandchild * ptr_Grandchild = new Grandchild();
   ptr_Grandchild->setData(20);
   int get_grandchild_value = ptr_Grandchild -> getData();
   cout << "The value in Grandchild class is " << get_grandchild_value << endl;

   MixedChild * ptr_Mixedchild = new MixedChild();
   ptr_Mixedchild->setData(20);
   int get_mixedchild_value = ptr_Mixedchild -> getData();
   cout << "The value in Mixedchild class is " << get_mixedchild_value << endl;
}
