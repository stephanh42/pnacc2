import json
import sys
from collections import namedtuple

Token = namedtuple("Token", "type linenumber tokennumber line")
StackEntry = namedtuple("StackEntry", "state value")

class AST(namedtuple("AST", "lhs args")):
    def __str__(self):
        lhs, args = self
        return "%s(%s)" % (lhs, ", ".join(str(arg) for arg in args))

class Parser(object):
    def __init__(self, automaton):
        self.terminals = set(automaton["terminals"])
        self.rules  = automaton["rules"]
        self.automaton = automaton["automaton"]

        self.stack = [StackEntry(0, None)]

    def current_state(self):
        return self.automaton[self.stack[-1].state]

    def tokenize_file(self, f):
        linenumber = 0
        for line in f:
            linenumber += 1
            for tokennumber, item in enumerate(line.split()):
                if item not in self.terminals:
                    sys.stderr.write("Line %d: Illegal token: %s\n" % (linenumber, item))
                    sys.exit(1)
                yield Token(item, linenumber, tokennumber+1, line)
        yield Token("EOF", linenumber, 1, "")

    def parse_file(self, f):
        for token in self.tokenize_file(f):
            self.feed(token)
        print(self.result)

    def feed(self, token):
        stack = self.stack
        while True:
            state = self.current_state()
            try:
                action = state["actions"][token.type]
            except KeyError:
                sys.stderr.write("Parse error in line %d, token %d at: %s\n" % (token.linenumber, token.tokennumber, token.type))
                sys.stderr.write("%s\n" % token.line)
                sys.exit(1)
            action_type = action[0]
            if action_type == "S":
                stack.append(StackEntry(action[1], token.type))
                return True
            elif action_type == "R":
                rule = self.rules[action[1]]
                arity = len(rule["rhs"])
                lhs = rule["lhs"]
                if arity > 0:
                    args = [item[1] for item in stack[-arity:]]
                    del stack[-arity:]
                else:
                    args = ()
                if arity == 1:
                    value = args[0]
                else:
                    value = AST(lhs, args)
                new_state = self.current_state()["goto"][lhs]
                stack.append(StackEntry(new_state, value))
            else:
                self.result = stack[-1].value
                return True

def main():
    if len(sys.argv) <= 1:
        sys.stderr.write("Usage: %s grammar.json [file ...]\n" % sys.argv[0])
        sys.exit(1)
    with open(sys.argv[1], "r") as f:
        automaton = Parser(json.load(f))

    if len(sys.argv) <= 2:
        automaton.parse_file(sys.stdin)
    else:
        for filename in sys.argv[2:]:
            with open(filename, "r") as f:
                automaton.parse_file(f)

if __name__ == "__main__":
    main()
