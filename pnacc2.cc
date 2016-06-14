/********************************************
    Please, Not Another Compiler Compiler 2 
      ____  _   _____   ____________   ___ 
     / __ \/ | / /   | / ____/ ____/  |__ \
    / /_/ /  |/ / /| |/ /   / /       __/ /
   / ____/ /|  / ___ / /___/ /___    / __/ 
  /_/   /_/ |_/_/  |_\____/\____/   /____/ 
********************************************/
#include <iostream>
#include <fstream>
#include <string>
#include <list>
#include <map>
#include <regex>
#include <exception>
#include <memory>
#include <algorithm>
#include <set>

#include <boost/noncopyable.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/container/stable_vector.hpp>

#include <tbb/tbb.h>

// Thrown when we don't like the input.
class InputError : public std::exception {
};

class Rule;
class Symbol;

// Bitset of terminals (each of which have a numerical ID).
typedef boost::dynamic_bitset<> TerminalSet;

// Do something for each bit (terminal) set.
template <class F>
void foreachInTerminalSet(const TerminalSet &fs, F f)
{
  TerminalSet::size_type pos = fs.find_first();
  while (pos != TerminalSet::npos) {
    f(pos);
    pos = fs.find_next(pos);
  }
}

// This class combines the "FIRST" set and the "nullable" state
// of a symbol or a sequence of symbols.
class FirstNullable
{
  public:
    FirstNullable(size_t sz=0) : m_first(sz), m_nullable(false) {}

    FirstNullable(const FirstNullable &first, const FirstNullable &second)
    : m_first(first.nullable() ? first.first() | second.first() : first.first()),
      m_nullable(first.nullable() && second.nullable())
    {}

    const TerminalSet &first() const { return m_first; }
    bool nullable() const { return m_nullable; }
    void setNullable() { m_nullable = true; }

    void addFirst(unsigned index) { m_first.set(index); }

    bool merge(const FirstNullable &other)
    {
      if (!nullable() && other.nullable()) {
        m_nullable = true;
        m_first |= other.first();
        return true;
      } else if (!(other.first().is_subset_of(first()))) {
        m_first |= other.first();
        return true;
      } else {
        return false;
      }
    }

  private:
    boost::dynamic_bitset<> m_first;
    bool m_nullable;
};


// Either a terminal or a non-terminal.
class Symbol : boost::noncopyable
{
  public:
    friend class Rule;

    Symbol(const std::string &name) : m_name(name), m_reserved(false), m_isEOF(false), m_terminalId(0) {}

    const std::string &name() const { return m_name; }
    const std::vector<Rule*> rules() const { return m_rules; }

    bool reserved() const { return m_reserved; }
    void makeReserved() { m_reserved = true; }

    bool isTerminal() const { return m_rules.empty(); }
    unsigned terminalId() const { return m_terminalId; }
    void setTerminalId(unsigned id) { m_terminalId = id; }

    bool isEOF() const { return m_isEOF; }
    void makeEOF() { m_isEOF = true; }

    const FirstNullable &firstNullable() const { return m_firstNullable; }

    void initFirstNullable(size_t sz) 
    { 
      m_firstNullable = FirstNullable(sz); 
      if (isTerminal()) {
        m_firstNullable.addFirst(terminalId());
      }
    }

    void updateFirstNullable(const FirstNullable &firstNullable);

  private:
    std::vector<Rule*> m_rules;

    typedef std::pair<Rule*,unsigned> RuleUse;
    std::vector<RuleUse> m_usedRules;

    std::string m_name;
    bool m_reserved;
    bool m_isEOF;
    unsigned m_terminalId;

    FirstNullable m_firstNullable;
};

class Rule : boost::noncopyable
{
  public:
    Rule(Symbol *lhs, const std::vector<Symbol*> &rhs, 
        unsigned lineNumber, unsigned id)
      : m_lhs(lhs), m_rhs(rhs), m_lineNumber(lineNumber), m_id(id), m_used(false)
    {
      lhs->m_rules.push_back(this);
      unsigned pos = 0;
      for (Symbol *sym : m_rhs) {
        sym->m_usedRules.emplace_back(this, pos);
        pos++;
      }
    }

    void initFirstNullable(size_t sz) { 
      m_itemFirstNullable = std::vector<FirstNullable>(rhs().size() + 1, FirstNullable(sz));
    }

    FirstNullable computeFirstNullable(size_t item);

    void updateFirstNullable(size_t item) {
      while (true) {
        FirstNullable newFirstNullable = computeFirstNullable(item);
        if (!m_itemFirstNullable[item].merge(newFirstNullable)) {
          return;
        }
        if (item == 0) {
          lhs()->updateFirstNullable(m_itemFirstNullable[0]);
          return;
        }
        item--;
      }
    }

    Symbol *lhs() const { return m_lhs; }
    const std::vector<Symbol*> &rhs() const { return m_rhs; }

    Symbol *rhs(unsigned pos) const { return (pos < m_rhs.size()) ? m_rhs[pos] : nullptr; }

    unsigned lineNumber() const { return m_lineNumber; }
    unsigned id() const { return m_id; }
    const FirstNullable &firstNullable(size_t item) const { return m_itemFirstNullable[item]; }

    void printRule(std::ostream &out) const
    {
      out << lhs()->name() << " ->";
      for (Symbol *sym : rhs()) {
        out << " " << sym->name();
      }
      out << "  (id: " << id() << ", line: " << lineNumber() << ")";
    }

    bool used() const { return m_used; }
    void markUsed() { m_used = true; }

  private:
    Symbol *m_lhs;
    std::vector<Symbol*> m_rhs;
    unsigned m_lineNumber;
    unsigned m_id;
    bool m_used;
    std::vector<FirstNullable> m_itemFirstNullable;
};


class LR0Item
{
  public:
    LR0Item() : m_rule(nullptr), m_position(0) {}
    LR0Item(Rule *rule, unsigned pos)
      : m_rule(rule), m_position(pos) {}

    Rule *rule() const { return m_rule; }
    unsigned position() const { return m_position; }

    bool operator==(const LR0Item &other) const { return (rule() == other.rule()) && (position() == other.position()); }
    bool operator<(const LR0Item &other) const 
    { return (rule() < other.rule()) || ((rule() == other.rule()) && (position() < other.position())); }

    Symbol *currentSymbol() const { return rule()->rhs(position()); }

    LR0Item next() const { return LR0Item(rule(), position() + 1); }

    bool atEnd() const { return position() == rule()->rhs().size(); }

  private:
    Rule *m_rule;
    unsigned m_position;
};

typedef std::map<LR0Item, TerminalSet> LR1StateMap;

class LR1State {
  public:
    typedef LR1StateMap::iterator iterator;
    typedef LR1StateMap::const_iterator const_iterator;
    typedef LR1StateMap::value_type value_type;

    LR1State() : m_map(std::make_shared<LR1StateMap>()) {}

    LR1StateMap &map() { return *m_map; }
    const LR1StateMap &map() const { return *m_map; }

    bool operator==(const LR1State &other) const { return map() == other.map(); }
    bool operator<(const LR1State &other) const { return map() < other.map(); }

    iterator begin() { return m_map->begin(); }
    iterator end() { return m_map->end(); }

    const_iterator begin() const { return m_map->begin(); }
    const_iterator end() const { return m_map->end(); }

    iterator find(const LR0Item &item) { return m_map->find(item); }

    TerminalSet &operator[](const LR0Item &item) { return (*m_map)[item]; }

   private:
    std::shared_ptr<LR1StateMap> m_map;
};

bool updateLR1State(LR1State &state, const LR0Item &item, const TerminalSet &lookahead)
{
  LR1State::iterator it = state.find(item);
  if (it == state.end()) {
    state[item] = lookahead;
    return true;
  } else {
    if (lookahead.is_subset_of(it->second)) {
      return false;
    } else {
      it->second |= lookahead;
      return true;
    }
  }
}

void updateLR1StateRecursively(LR1State &state, const LR0Item &item, const TerminalSet &lookahead)
{
  auto updateForRules = [&state](Symbol *symbol, const TerminalSet &lookahead) {
    for (Rule *rule : symbol->rules()) {
      updateLR1StateRecursively(state, LR0Item(rule, 0), lookahead);
    }
  };

  if (updateLR1State(state, item, lookahead)) {
      Symbol *symbol = item.currentSymbol();
      if (symbol) {
        const FirstNullable &firstNullable = item.rule()->firstNullable(item.position() + 1);
        const TerminalSet &nextLookahead = firstNullable.first();
        if (firstNullable.nullable()) {
          updateForRules(symbol, nextLookahead | lookahead);
        } else if (!firstNullable.first().none()) {
          updateForRules(symbol, nextLookahead);
        }
      }
  }
}

enum class ActionType {
  Shift,
  Reduce,
  Accept
};

class Action {
  public:
    Action() {}
    Action(ActionType type, unsigned parameter=0)
    : m_type(type), m_parameter(parameter)
    {}

    ActionType type() const { return m_type; }
    unsigned parameter() const { return m_parameter; }

    const char *typeName() const
    {
      switch (type()) {
        case ActionType::Shift:
        return "S";

        case ActionType::Reduce:
        return "R";

        default:
        return "A";
      }
    }

    void writeJson(std::ostream &out) const
    {
      out << "[\"" << typeName() << "\"";
      if (type() != ActionType::Accept) {
        out << ", " << m_parameter;
      }
      out << "]";
    }

  private:
    ActionType m_type;
    unsigned m_parameter;
};

typedef std::map<Symbol*,LR1State> TransitionMap;

typedef std::vector<Action> ActionVec;
typedef std::map<Symbol*,ActionVec> ActionMap;

class StateInfo {
  public:
    StateInfo() : m_index(0) {}

    StateInfo(const LR1State &state, unsigned index,
        const StateInfo *predecessor, Symbol *predecessorSymbol)
      : m_state(state), m_index(index),
        m_predecessor(predecessor), m_predecessorSymbol(predecessorSymbol)
    {}

    void makeTransitionMap()
    {
      for (const LR1State::value_type &p : m_state) {
        const LR0Item &item = p.first;
        const TerminalSet &lookahead = p.second;
        Symbol *nextSymbol = item.currentSymbol();
        if (nextSymbol == nullptr) {
          continue;
        }
        if (nextSymbol->isEOF()) {
          m_accepts.insert(nextSymbol);
          continue;
        }
        LR1State &nextState = m_transitionMap[nextSymbol];
        updateLR1StateRecursively(nextState, item.next(), lookahead);
      }
    }

    ActionMap getActions() const;

    const LR1State &state() const { return m_state; }
    unsigned index() const { return m_index; }
    const TransitionMap &transitionMap() const { return m_transitionMap; }
    const std::set<Symbol*> accepts() const { return m_accepts; }

    void printState(std::ostream &out) const
    {
      if (m_predecessor) {
        m_predecessor->printState(out);
        out << m_predecessorSymbol->name() << " ";
      }
    }

  private:
    LR1State m_state;
    TransitionMap m_transitionMap;
    unsigned m_index;
    std::set<Symbol*> m_accepts;

    const StateInfo *m_predecessor;
    Symbol *m_predecessorSymbol;
};

typedef std::map<LR1State, std::unique_ptr<StateInfo> > StateTable;

void computeStateTable(StateTable &stateTable, const LR1State &startingState)
{
  unsigned index = 0;
  std::vector<StateInfo*> todoStack;

  auto addToStateTable = [&index, &todoStack, &stateTable]
    (const LR1State &state, const StateInfo *predecessor, Symbol *predecessorSymbol) {
    std::unique_ptr<StateInfo> &stateInfoPtr = stateTable[state];
    if (!stateInfoPtr) {
      std::unique_ptr<StateInfo> ptr(new StateInfo(state, index++, predecessor, predecessorSymbol));
      stateInfoPtr = std::move(ptr);
      todoStack.push_back(stateInfoPtr.get());
    }
  };


  addToStateTable(startingState, nullptr, nullptr);

  while (!todoStack.empty()) {

    std::vector<StateInfo*> oldTodoStack(std::move(todoStack));
    todoStack.clear();

    tbb::parallel_for_each(oldTodoStack.begin(), oldTodoStack.end(),
    [](StateInfo *info) { info->makeTransitionMap(); });

    for (StateInfo *info : oldTodoStack) {
      for (const TransitionMap::value_type &p: info->transitionMap()) {
        addToStateTable(p.second, info, p.first);
      }
    }
  }
}


void 
Symbol::updateFirstNullable(const FirstNullable &firstNullable)
{
  if (m_firstNullable.merge(firstNullable)) {
    for (const RuleUse &ruleUse : m_usedRules) {
      ruleUse.first->updateFirstNullable(ruleUse.second);
    }
  }
}


std::list<Symbol> allSymbols;
boost::container::stable_vector<Rule> allRules;
std::vector<Symbol*> terminals;

Symbol *eofSymbol = nullptr;
Symbol *startSymbol = nullptr;
Rule *startRule = nullptr;

LR1State startingState;
StateTable stateTable;

FirstNullable 
Rule::computeFirstNullable(size_t item)
{
  if (item < rhs().size()) {
    return FirstNullable(rhs(item)->firstNullable(), m_itemFirstNullable[item+1]);
  } else {
    FirstNullable result(terminals.size());
    result.setNullable();
    return result;
  }
}

ActionMap
StateInfo::getActions() const
{
  ActionMap result;

  // get SHIFT actions
  for (const TransitionMap::value_type &p: transitionMap()) {
    Symbol *sym = p.first;
    if (!sym->isTerminal()) {
      continue;
    }
    const LR1State &nextState = p.second;
    result[sym].push_back(Action(ActionType::Shift, stateTable[nextState]->index()));
  }

  // get REDUCE actions
  for (const LR1State::value_type &p : state()) {
    const LR0Item &item = p.first;
    if (item.atEnd()) {
      foreachInTerminalSet(p.second, [&result, &item](unsigned term) { 
          result[terminals[term]].push_back(Action(ActionType::Reduce, item.rule()->id()));
          item.rule()->markUsed();
          });
    }
  }

  // get ACCEPT actions
  for (Symbol *sym : accepts()) {
    result[sym].push_back(Action(ActionType::Accept));
  }

  return result;
}

void readGrammar(std::istream &input) 
{
  typedef std::map<std::string,Symbol*> SymbolMap;
  SymbolMap symbolMap;

  auto getSymbol = [&symbolMap](const std::string &str) {
    SymbolMap::iterator it = symbolMap.find(str);
    if (it != symbolMap.end()) {
      Symbol *result = it->second;
      if (result->reserved()) {
        std::cerr << "Reserved symbol name: " << str << std::endl;
        throw InputError();
      }
      return result;
    } else {
      allSymbols.emplace_back(str);
      Symbol *result = &allSymbols.back();
      symbolMap[str] = result;
      return result;
    }
  };

  std::regex emptyLine(R"(^\s*(#.*)?$)",
                 std::regex_constants::ECMAScript);
  std::regex ruleLine(R"(^\s*(\w+)\s*->(.*)$)",
                 std::regex_constants::ECMAScript);
  std::regex rhsRe(R"(\S+)", std::regex_constants::ECMAScript);

  std::string line;
  std::smatch mo;
  unsigned lineNumber = 0;

  eofSymbol = getSymbol("EOF");
  eofSymbol->makeReserved();
  eofSymbol->makeEOF();

  while (std::getline(input, line)) {
    lineNumber++;

    if (std::regex_match(line.cbegin(), line.cend(), mo, emptyLine)) {
      continue;
    }
    if (std::regex_match(line.cbegin(), line.cend(), mo, ruleLine)) {
      bool addStartRule = false;
      if (!startSymbol) {
        startSymbol = getSymbol(mo.str(1) + "'");
        startSymbol->makeReserved();
        addStartRule = true;
      }
      Symbol *lhs = getSymbol(mo.str(1));
      if (addStartRule) {
        allRules.emplace_back(startSymbol, std::vector<Symbol*>{lhs, eofSymbol}, lineNumber, allRules.size());
        startRule = &allRules.back();
      }
      std::vector<Symbol*> rhs;
      for (std::sregex_iterator it = std::sregex_iterator(mo[2].first, mo[2].second, rhsRe);
           it != std::sregex_iterator(); ++it) {
        rhs.push_back(getSymbol(it->str(0)));
      }

      allRules.emplace_back(lhs, rhs, lineNumber, allRules.size());

      continue;
    }

    std::cerr << "Invalid syntax at line " << lineNumber << ":" << std::endl;
    std::cerr << line << std::endl;
    throw InputError();
  }
  if (!startRule) {
    std::cerr << "You should specify at least one grammar rule." << std::endl;
    throw InputError();
  }
}

std::string jsonQuote(const std::string &str)
{
  std::string result("\"");
  for (char ch : str) {
    if ((ch == '"') || (ch == '\\')) {
      result.push_back('\\');
    }
    result.push_back(ch);
  }
  result.push_back('"');
  return result;
}

template <class Seq, class F>
void writeArray(std::ostream &out, const Seq &seq, F f, const char *separator = ", ")
{
  out << "[";
  bool isFirst = true;
  for (const auto &item : seq) {
    if (!isFirst) {
      out << separator;
    }
    isFirst = false;
    f(out, item);
  }
  out << "]";
}

void writeRules(std::ostream &out)
{
  writeArray(out, allRules,
  [](std::ostream &out, const Rule &rule) {
    out << "{\"id\": " << rule.id() << ", \"lhs\": " << jsonQuote(rule.lhs()->name()) << ", ";
    out << "\"rhs\": ";
    writeArray(out, rule.rhs(), [](std::ostream &out, const Symbol *sym) {
      out << jsonQuote(sym->name());
    });
    out << "}";
  }, ",\n");
}

void writeAutomaton(std::ostream &out, std::ostream &report)
{
  std::vector<const StateInfo*> stateInfoVec;
  bool isFirstConflict = true;

  for (const StateTable::value_type &p : stateTable) {
    stateInfoVec.push_back(p.second.get());
  }
  std::sort(stateInfoVec.begin(), stateInfoVec.end(), [](const StateInfo *i1, const StateInfo *i2) { return i1->index() < i2->index(); });

  writeArray(out, stateInfoVec,
  [&report, &isFirstConflict](std::ostream &out, const StateInfo *stateInfo) {
    out << "{\"id\": " << stateInfo->index() << ", ";

    out << "\"actions\": {";
    bool isFirst = true;
    for (const ActionMap::value_type &p: stateInfo->getActions()) {
      if (!isFirst) {
        out << ", ";
      }
      isFirst = false;
      out << jsonQuote(p.first->name()) << ": ";
      if (p.second.size() == 1) {
        p.second[0].writeJson(out);
      } else {
        out << "[\"C\"";
        for (const Action &action : p.second) {
          out << ", ";
          action.writeJson(out);
        }
        out << "]";

        if (!isFirstConflict) {
          report << "\n";
        }
        isFirstConflict = false;

        report << "Conflict for state " << stateInfo->index() << ":\n";
        stateInfo->printState(report);
        report << ". " << p.first->name() << "\n";
        for (const Action &action : p.second) {
          switch (action.type()) {
            case ActionType::Shift:
              report << "  SHIFT to state " << action.parameter();
              break;
            case ActionType::Reduce:
              report << "  REDUCE with rule: ";
              allRules[action.parameter()].printRule(report);
              break;
            case ActionType::Accept:
              report << "  ACCEPT";
              break;
          }
          report << "\n";
        }
      }
    }

    // get GOTO actions
    isFirst = true;
    for (const TransitionMap::value_type &p: stateInfo->transitionMap()) {
      Symbol *sym = p.first;
      if (sym->isTerminal()) {
        continue;
      }
      if (isFirst) {
        out << "}, \"goto\": {";
      } else {
        out << ", ";
      }
      isFirst = false;
      const LR1State &nextState = p.second;
      out << jsonQuote(sym->name()) << ": " << stateTable[nextState]->index();
    }
    out << "}}";
  }, ",\n");
}

void writeJson(std::ostream &out, std::ostream &report)
{
  out << "{";
  out << "\"terminals\": ";
  writeArray(out, terminals, [](std::ostream &out, Symbol *terminal) { out << jsonQuote(terminal->name()); });
  out << ",\n\"rules\": ";
  writeRules(out);
  out << ",\n\"automaton\": ",
  writeAutomaton(out, report);
  out << "}\n";

  for (const Rule &rule : allRules) {
    if (!rule.used() && (rule.id() != 0)) {
      report << "Unused rule: ";
      rule.printRule(report);
      report << "\n";
    }
  }
}

void processGrammar()
{
  for (Symbol &sym : allSymbols) {
    if (sym.isTerminal()) {
      sym.setTerminalId(terminals.size());
      terminals.push_back(&sym);
    }
  }
  for (Symbol &sym : allSymbols) {
    sym.initFirstNullable(terminals.size());
  }
  for (Rule &rule : allRules) {
    rule.initFirstNullable(terminals.size());
  }
  for (Rule &rule : allRules) {
    rule.updateFirstNullable(rule.rhs().size());
  }

  TerminalSet eofSet(terminals.size());
  eofSet.set(eofSymbol->terminalId());

  updateLR1StateRecursively(startingState, LR0Item(startRule, 0), eofSet);

  computeStateTable(stateTable, startingState);
}

struct ProgramArgs
{
  std::string inputFile;
  std::string outputFile;
  std::string reportFile;

  enum class Options {
    Help,
    OutputFile,
    ReportFile,
    InputFile
  };

  static void showHelp(const char *argv0)
  {
    std::cerr << "Usage:\n";
    std::cerr << "  " << argv0 << "input.grammar [-o output.json] [-r report.txt]\n";
    std::cerr << "  " << argv0 << "[-h | --help]\n";
    std::cerr << 
    "  -o            Specify output .json file (defaults to standard output)\n"
    "  -r | --report Specify conflict report output file (defaults to standard error)\n"
    "  -h | --help   Show this help message\n";
    throw InputError();
  }

  void setString(std::string &setting, const std::string &arg, const char *argv0, const char *description)
  {
    if (!setting.empty()) {
      std::cerr << "Value for " << description << " already specified" << std::endl;
      showHelp(argv0);
    } else {
      setting = arg;
    }
  }

  std::shared_ptr<std::ostream> getOutputFile(const std::string &filename, std::ostream &defaultOut)
  {
    if (filename.empty()) {
      return std::shared_ptr<std::ostream>(&defaultOut, [](std::ostream *){});
    } else {
      return std::make_shared<std::ofstream>(filename.c_str());
    }
  }

  void parse(int argc, const char *argv[])
  {
    typedef std::map<std::string,Options> OptionsMap;
    static const OptionsMap optionsMap {
      {"-h", Options::Help},
      {"--help", Options::Help},
      {"-o", Options::OutputFile},
      {"-i", Options::InputFile},
      {"-r", Options::ReportFile},
      {"--report", Options::ReportFile}
    };

    if (argc <= 1) {
      showHelp(argv[0]);
    }

    int pos = 1;
    while (pos < argc) {
      std::string arg(argv[pos]);
      OptionsMap::const_iterator it = optionsMap.find(arg);
      Options options = Options::InputFile;
      if (it != optionsMap.end()) {
        options = it->second;
        if (options == Options::Help) {
          showHelp(argv[0]);
          return;
        }
        pos++;
        if (pos < argc) {
          arg = argv[pos];
        } else {
          std::cerr << "Expecting argument after " << arg << std::endl;
          showHelp(argv[0]);
        }
      }
      switch (options) {
        case Options::OutputFile:
          setString(outputFile, arg, argv[0], "output file");
          break;

        case Options::InputFile:
          setString(inputFile, arg, argv[0], "input file");
          break;

        case Options::ReportFile:
          setString(reportFile, arg, argv[0], "report file");
          break;

        default:
          assert(0);
      }
      pos++;
    }

    if (inputFile.empty()) {
      std::cerr << "No input grammar file specified" << std::endl;
      showHelp(argv[0]);
    }
  }
};

int main(int argc, const char *argv[])
{
  tbb::task_scheduler_init init;
  ProgramArgs programArgs;
   
  std::shared_ptr<std::ostream> outputFile, reportFile;

  try {
    programArgs.parse(argc, argv);
    outputFile = programArgs.getOutputFile(programArgs.outputFile, std::cout);
    reportFile = programArgs.getOutputFile(programArgs.reportFile, std::cerr);

    std::ifstream inputFile(programArgs.inputFile);
    if (!inputFile.is_open()) {
      std::cerr << "Cannot open input file: " << programArgs.inputFile << std::endl;
      programArgs.showHelp(argv[0]);
    }
    readGrammar(inputFile);
  } catch (InputError) {
    return 1;
  }
  processGrammar();
  writeJson(*outputFile, *reportFile);
  return 0;
}
