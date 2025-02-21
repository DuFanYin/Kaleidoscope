# LLVM

## Introduction

LLVM infrastructure is a library that provides useful complier technologies. It does not do anything by itself, but the user can use the tools provided in the library to build compiler parts according to their needs.

## Front end

Front end turns the source code into LLVM IR and pass it to optimizer.

### Lexer

used to read from the source code and tokenise it. Tokens are then passed into the parser one by one. Depends on the token type, the corresponding parser funciton will be called.

### Abstract syntax tree

In Kaleidoscope, there are four types of expression objects, one prototype object and one function object. One AST class for each type of construct. One object for each construct in the language.

ExprAST is the base class for all expression classes, which are NumberExprAST, VariableExprAST, BinaryExprAST and CallExprAST.

To support more tpyes, ExprAST class needs to have a type field.

A prototype for a function, captures its name, and its argument names (thus implicitly the number of arguments the function takes). It is an interface to a function.

A Function object is a way to talk about functions themselves.

### Parser

Parser turns the source code into an AST.

We first define the AST class, the parser the turn the tokens into instances, recursivly. The end product by the parser is a single AST that includes every token in the source code.

Helper Functions:  
(We use CurTok and getNextToken() to update the token. Allows us to look one token ahead at what the lexer is returning. Every function in our parser will assume that CurTok is the current token that needs to be parsed.

The LogError routines are simple helper routines that our parser will use to handle errors. The error recovery in our parser will not be the best and is not particular user-friendly, but it will be enough for our tutorial. These routines make it easier to handle errors in routines that have various return types: they always return null.)

For each production in our grammar, we’ll define a function which parses that production.

Recursion is uesd by calling ParseExpression() which can call ParseParenExpr()). Keep each product simple. Note that parentheses do not cause construction of AST nodes themselves. The most important role of parentheses are to guide the parser and provide grouping. Once the parser constructs the AST, parentheses are not needed.

A ParsePrimary() function is used to wrap all other expression parsing functions together, works as an entry point. It checks the type of the current token and calls corresponding function.

Binary expression parsing is handled using  Operator-Precedence Parsing. Where a map is used to store operator precedence and used to guide recursion. A GetTokPrecedence() is used to check operator precedence. Operator precedence parsing considers this as a stream of primary expressions separated by binary operators.

Function prototypes are used both for ‘extern’ function declarations as well as function body definitions.

### IR code generation

A virtual codegen() is added to each AST class to generate IR code. Returns an LLVM Value object. Value class is the base class of all values computed by a program that may be used as operands to other values. Value is the super class of other important classes such as Instruction and Function. All Values have a Type. Type is not a subclass of Value. Some values can have a name and they belong to some Module. Setting the name on the Value automatically updates the module's symbol table.

TheContext is an opaque object that owns a lot of core LLVM data structures, such as the type and constant value tables. we need a single instance to pass into APIs that require it.

The Builder object generates LLVM instructions. Instances of the IRBuilder class template keep track of the current place to insert instructions and has methods to create new instructions.

TheModule is an LLVM construct that contains functions and global variables. In many ways, it is the top-level structure that the LLVM IR uses to contain code. It will own the memory for all of the IR that we generate, which is why the codegen() method returns a raw Value*, rather than a unique_ptr<Value>.

The NamedValues map keeps track of which values are defined in the current scope and what their LLVM representation is. (a symbol table).As such, function parameters will be in this map when generating code for their function body.

In the LLVM IR, numeric constants are represented with the ConstantFP class, which holds the numeric value in an APFloat internally (APFloat has the capability of holding floating point constants of Arbitrary Precision).

IRBuilder knows where to insert the newly created instruction, all you have to do is specify what instruction to create (e.g. with CreateFAdd), which operands to use (L and R here) and optionally provide a name for the generated instruction.

### adding mutable variable

phi node selects the right version of the variable(ssa have versions) to use based on where control flow is coming from

llvm requires all register value to be in SSA form.

In LLVM, all memory accesses are explicit with load/store instructions, and it is carefully designed not to have (or need) an “address-of” operator. Stack variables work the same way, except that instead of being declared with global variable definitions, they are declared with the LLVM alloca instruction. using this technique we avoid the use phi node.

LLVM optimizer has a highly-tuned optimization pass named “mem2reg” that handles the high stack traffic, promoting allocas into SSA registers, inserting Phi nodes as appropriate. The mem2reg pass implements the standard “iterated dominance frontier” algorithm for constructing SSA form and has a number of optimizations that speed up (very common) degenerate cases.

The symbol table is managed at code generation time by the ‘NamedValues’ map. This map currently keeps track of the LLVM “Value*” that holds the double value for the named variable. In order to support mutation, we need to change it so it holds the memory location of the variable in question.

For each argument of the function, we make an alloca, store the input value to the function into the alloca, and register the alloca as the memory location for the argument. This method gets invoked by FunctionAST::codegen() right after it sets up the entry block for the function.

## Optimizer

Optimizer reveives the IR produced by front end.

Code optimiztion does not have to be inplemented in the AST. Because all calls to build LLVM IR go through the LLVM IR builder, and builder itself checkes optimization oppoturity.

LLVM provides wide range of optimizations in the form of <b>Passes</b>. Developer can chooes the passed based on their needs.

A <b>FunctionPassManager</b> is used to hold the passes we want to run. Passed are then added into the FunctionPassManager. A new FPM for each module we want to optimize. A function is used to initialise both the module and the FunctionPassManager.

The FPM is used by running it after our newly created function is constructed (in FunctionAST::codegen()), but before it is returned to the client. Optimizes and updates the LLVM Function* in place

