# c++ notes

pointer <br>
- use to pass variable by reference.
- allow function to use a variable thats outside its scop.
- when pass into function, it allows function to edit original data itself instead of copying data over.

constructor <br>
- a special method, excuted everytime when a new object of the class is created.
- same name as class, no return value.
- can be used to set initial value for class atributes.
- can have parameters, used to set initial value for class atribute.

destructor <br>
- same as constructor, excuted when object is deleted.

abstract class<br>
- when the implementation of the function cannot be provided in a base class because we don’t know the implementation. Such a class is called abstract class.
- that function is called a pure virtual function
- the purpose of having a abstract class is to organise the things together, to provide a public root for the subclasses.

virtual method <br>
- polymorphism is achieved using virtual method.
- adding virtual method in base class allows each subclass to redefine the moethod, so they all have th same method name but do different things.
- it allows the use of a base class pointer or reference to access the method in base class and subclasses.

pure virtual method <br>
- A pure virtual function (or abstract function) is a virtual function for which we can have implementation, But we must override that function in the derived class, otherwise the derived class will also become abstract class 
- A pure virtual function is declared by assigning 0 in virtual function declaration.
- it allows a method to be decleared in the base class and each subclass to have their own implementation

name space <br>
-

static <br>
-

std::move()<br>
- it cannabalise a variable's existing resource in the memory and give it to another one
- avoid copying data and therefore boost performance

example:
```
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}

  Function *codegen();
};
```
move() is used to move the value in argument variable to class attribute. : is used to initialise class attribute.



example on return tpye of a function:
```
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number
  return std::move(Result);
}
```
In above code, static followed by the return tpye of the function, the function does not take in arguments but since NumVal in a global variable, the Result is generated using NumVal and returned.




