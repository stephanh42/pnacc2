# Please, Not Another Compiler Compiler 2 

```
      ____  _   _____   ____________   ___ 
     / __ \/ | / /   | / ____/ ____/  |__ \
    / /_/ /  |/ / /| |/ /   / /       __/ /
   / ____/ /|  / ___ / /___/ /___    / __/ 
  /_/   /_/ |_/_/  |_\____/\____/   /____/ 
```

## What it is

Relax, PNACC 2 is not another compiler compiler.

That said, it *is* a parser generator tool, somewhat similar to `yacc` or `bison`.
But also quite different. Mostly much simpler.

It takes a simple grammar description like so:

```
   expr -> expr + term
   expr -> term

   term -> term * atom
   term -> atom

   atom -> - atom
   atom -> ID
   atom -> ( expr )
```

Put is in a file `example.grammar` and then run:

```
   pnacc2 example.grammar -o example.json
```

This will create JSON file `example.json`.
