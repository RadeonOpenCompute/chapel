#include <typeinfo>
#include "astutil.h"
#include "analysis.h"
#include "build.h"
#include "files.h"
#include "misc.h"
#include "runtime.h"
#include "stmt.h"
#include "stringutil.h"
#include "symbol.h"
#include "symtab.h"
#include "astutil.h"
#include "passes.h"
#include "../traversals/createConfigVarTable.h"

FnSymbol* chpl_main = NULL;

Symbol *gNil = 0;
Symbol *gUnknown = 0;
Symbol *gUnspecified = 0;
Symbol *gVoid = 0;
VarSymbol *gTrue = 0;
VarSymbol *gFalse = 0;

Vec<Symbol *> builtinSymbols;

Vec<FnSymbol*> new_ast_functions;
Vec<TypeSymbol*> new_ast_types;

/*** Instantiation Cache vvv ***/
class Inst : public gc {
 public:
  Inst(FnSymbol* iOldFn, FnSymbol* iNewFn, ASTMap* iSubs) :
    oldFn(iOldFn), newFn(iNewFn), subs(new ASTMap(*iSubs)) { }
  FnSymbol* oldFn;
  FnSymbol* newFn;
  ASTMap* subs;
};
static Vec<Inst*> icache;

static bool 
subs_match(ASTMap* s1, ASTMap* s2) {
  form_Map(ASTMapElem, e, *s1)
    if (s2->get(e->key) != e->value)
      return false;
  form_Map(ASTMapElem, e, *s2)
    if (s1->get(e->key) != e->value)
      return false;
  return true;
}

static FnSymbol*
check_icache(FnSymbol* fn, ASTMap* substitutions) {
  forv_Vec(Inst, inst, icache)
    if (inst->oldFn == fn && subs_match(substitutions, inst->subs))
      return inst->newFn;
  return NULL;
}

static void
add_icache(FnSymbol* newFn) {
  icache.add(new Inst(newFn->instantiatedFrom, newFn, &newFn->substitutions));
}
/*** Instantiation Cache ^^^ ***/


/*** Default Wrapper Cache vvv ***/
class DWCacheItem : public gc {
 public:
  DWCacheItem(FnSymbol* iOldFn, FnSymbol* iNewFn, Vec<Symbol*>* iDefaults) :
    oldFn(iOldFn), newFn(iNewFn), defaults(new Vec<Symbol*>(*iDefaults)) { }
  FnSymbol* oldFn;
  FnSymbol* newFn;
  Vec<Symbol*>* defaults;
};
static Vec<DWCacheItem*> dwcache;

static bool 
dw_match(Vec<Symbol*>* d1, Vec<Symbol*>* d2) {
  forv_Vec(Symbol, d, *d1)
    if (!d2->in(d))
      return false;
  forv_Vec(Symbol, d, *d2)
    if (!d1->in(d))
      return false;
  return true;
}

static FnSymbol*
check_dwcache(FnSymbol* fn, Vec<Symbol*>* defaults) {
  forv_Vec(DWCacheItem, item, dwcache)
    if (item->oldFn == fn && dw_match(defaults, item->defaults))
      return item->newFn;
  return NULL;
}

static void
add_dwcache(FnSymbol* newFn, FnSymbol* oldFn, Vec<Symbol*>* defaults) {
  dwcache.add(new DWCacheItem(oldFn, newFn, defaults));
}
/*** Default Wrapper Cache ^^^ ***/


Symbol::Symbol(astType_t astType, char* init_name, Type* init_type) :
  BaseAST(astType),
  name(init_name),
  cname(name),
  type(init_type),
  defPoint(NULL),
  variableExpr(NULL),
  uses(NULL),
  asymbol(0),
  overload(NULL),
  isUnresolved(false)
{}


void Symbol::verify(void) {
  if (prev || next)
    INT_FATAL(this, "Symbol is in AList");
  if (defPoint && !defPoint->parentSymbol)
    INT_FATAL(this, "Symbol::defPoint is not in AST");
}


void Symbol::setParentScope(SymScope* init_parentScope) {
  parentScope = init_parentScope;
}


Symbol*
Symbol::copyInner(ASTMap* map) {
  INT_FATAL(this, "Illegal call to Symbol::copy");
  return NULL;
}


void Symbol::replaceChild(BaseAST* old_ast, BaseAST* new_ast) {
  INT_FATAL(this, "Unexpected call to Symbol::replaceChild");
}


void Symbol::traverse(Traversal* traversal, bool atTop) {
  SymScope* saveScope = NULL;
  if (atTop) {
    saveScope = Symboltable::setCurrentScope(parentScope);
  }
  if (traversal->processTop || !atTop) {
    currentLineno = lineno;
    currentFilename = filename;
    traversal->preProcessSymbol(this);
  }
  if (atTop || traversal->exploreChildSymbols) {
    if (atTop) {
      traverseDefSymbol(traversal);
    }
    else {
      traverseSymbol(traversal);
    }
  }
  if (traversal->processTop || !atTop) {
    currentLineno = lineno;
    currentFilename = filename;
    traversal->postProcessSymbol(this);
  }
  if (atTop) {
    Symboltable::setCurrentScope(saveScope);
  }
}


void Symbol::traverseDef(Traversal* traversal, bool atTop) {
  SymScope* saveScope = NULL;
  if (atTop) {
    saveScope = Symboltable::setCurrentScope(parentScope);
  }
  if (traversal->processTop || !atTop) {
    currentLineno = lineno;
    currentFilename = filename;
    traversal->preProcessSymbol(this);
  }
  traverseDefSymbol(traversal);
  if (traversal->processTop || !atTop) {
    currentLineno = lineno;
    currentFilename = filename;
    traversal->postProcessSymbol(this);
  }
  if (atTop) {
    Symboltable::setCurrentScope(saveScope);
  }
}


void Symbol::traverseSymbol(Traversal* traversal) {
  
}


void Symbol::traverseDefSymbol(Traversal* traversal) {
}


bool Symbol::isConst(void) {
  return false;
}


bool Symbol::isParam(){
  return false;
}


bool Symbol::isThis(void) {
  FnSymbol *f = dynamic_cast<FnSymbol*>(defPoint->parentSymbol);
  if (!f || !f->_this)
    return 0;
  else
    return f->_this == this;
}


void Symbol::print(FILE* outfile) {
  fprintf(outfile, "%s", name);
}


void Symbol::codegen(FILE* outfile) {
//   if (hasPragma("data class")) {
//     TypeSymbol* typeSymbol = dynamic_cast<TypeSymbol*>(this);
//     ClassType* dataType = dynamic_cast<ClassType*>(typeSymbol->definition);
//     dataType->fields.v[1]->type->codegen(outfile);
//     fprintf(outfile, "*");
//   } else {
    fprintf(outfile, "%s", cname);
//   }
}


void Symbol::codegenDef(FILE* outfile) {
  INT_FATAL(this, "Unanticipated call to Symbol::codegenDef");
}


void Symbol::codegenPrototype(FILE* outfile) { }


void Symbol::printDef(FILE* outfile) {
  print(outfile);
}


FnSymbol* Symbol::getFnSymbol(void) {
  return NULL;
}


Symbol* Symbol::getSymbol(void) {
  return this;
}


Type* Symbol::typeInfo(void) {
  return type;
}


UnresolvedSymbol::UnresolvedSymbol(char* init_name, char* init_cname) :
  Symbol(SYMBOL_UNRESOLVED, init_name)
{
  if (init_cname) {
    cname = init_cname;
  }
  isUnresolved = true;
}


void UnresolvedSymbol::verify(void) {
  Symbol::verify();
  if (astType != SYMBOL_UNRESOLVED) {
    INT_FATAL(this, "Bad UnresolvedSymbol::astType");
  }
}


void UnresolvedSymbol::codegen(FILE* outfile) {
  INT_FATAL(this, "ERROR:  Cannot codegen an unresolved symbol.");
  fprintf(outfile, "%s /* unresolved */ ", name);
}


UnresolvedSymbol*
UnresolvedSymbol::copyInner(ASTMap* map) {
  return new UnresolvedSymbol(stringcpy(name));
}


void UnresolvedSymbol::traverseDefSymbol(Traversal* traversal) {
  TRAVERSE(this, traversal, false);
}


VarSymbol::VarSymbol(char* init_name,
                     Type* init_type,
                     varType init_varClass, 
                     consType init_consClass) :
  Symbol(SYMBOL_VAR, init_name, init_type),
  varClass(init_varClass),
  consClass(init_consClass),
  immediate(NULL),
  literalType(NULL),
  noDefaultInit(false)
{ }


void VarSymbol::verify(void) {
  Symbol::verify();
  if (astType != SYMBOL_VAR) {
    INT_FATAL(this, "Bad VarSymbol::astType");
  }
}


VarSymbol*
VarSymbol::copyInner(ASTMap* map) {
  VarSymbol* newVarSymbol = 
    new VarSymbol(stringcpy(name), type, varClass, consClass);
  newVarSymbol->variableExpr = COPY_INT(variableExpr);
  newVarSymbol->cname = stringcpy(cname);
  assert(!newVarSymbol->immediate);
  assert(!newVarSymbol->literalType);
  newVarSymbol->noDefaultInit = noDefaultInit;
  return newVarSymbol;
}


void VarSymbol::replaceChild(BaseAST* old_ast, BaseAST* new_ast) {
  if (old_ast == variableExpr)
    variableExpr = dynamic_cast<Expr*>(new_ast);
  else
    type->replaceChild(old_ast, new_ast);
}


void VarSymbol::traverseDefSymbol(Traversal* traversal) {
  TRAVERSE(variableExpr, traversal, false);
  TRAVERSE(type, traversal, false);
  TRAVERSE(literalType, traversal, false);
}


bool VarSymbol::isConst(void) {
  return (consClass == VAR_CONST);
}


bool VarSymbol::isParam(void){
  return (consClass == VAR_PARAM) || immediate;
}


void VarSymbol::print(FILE* outfile) {
  if (immediate) {
    bool isString = (immediate->const_kind == IF1_CONST_KIND_STRING);
    if (isString) {
      if (CreateConfigVarTable::running) {
        fprintf(outfile, "\\\"");
      } else {
        fprintf(outfile, "\"");
      }
    }
    fprint_imm(outfile, *immediate);
    if (isString) {
      if (CreateConfigVarTable::running) {
        fprintf(outfile, "\\\"");
      } else {
        fprintf(outfile, "\"");
      }
    }
  } else
    Symbol::print(outfile);
}


void VarSymbol::printDef(FILE* outfile) {
  if (varClass == VAR_CONFIG) {
    fprintf(outfile, "config ");
  }
  if (varClass == VAR_STATE) {
    fprintf(outfile, "state ");
  }
  //Roxana -- introduced various types of constness: const, param, nothing (var)
  if (consClass == VAR_CONST) {
    fprintf(outfile, "const ");
  } else if (consClass == VAR_PARAM){
        fprintf(outfile, "param");
  }
  else {
    fprintf(outfile, "var ");
  }
  print(outfile);
  fprintf(outfile, ": ");
  type->print(outfile);
}


void VarSymbol::codegenDef(FILE* outfile) {
  if (type == dtVoid)
    return;
  // need to ensure that this can be realized in C as a const, and
  // move its initializer here if it can be
  if (0 && (consClass == VAR_CONST)) {
    fprintf(outfile, "const ");
  }
  type->codegen(outfile);
  fprintf(outfile, " ");
  this->codegen(outfile);
  fprintf(outfile, ";\n");
}


static char* intentTagNames[NUM_INTENT_TYPES] = {
  "",
  "in",
  "inout",
  "out",
  "const",
  "ref",
  "param",
  "type"
};


ArgSymbol::ArgSymbol(intentTag iIntent, char* iName, 
                     Type* iType, Expr* iDefaultExpr,
                     Expr* iVariableExpr) :
  Symbol(SYMBOL_ARG, iName, iType),
  intent(iIntent),
  defaultExpr(iDefaultExpr),
  genericSymbol(NULL),
  isGeneric(false),
  isExactMatch(false)
{
  if (intent == INTENT_PARAM || intent == INTENT_TYPE)
    isGeneric = true;
  variableExpr = iVariableExpr;
}


void ArgSymbol::verify(void) {
  Symbol::verify();
  if (astType != SYMBOL_ARG) {
    INT_FATAL(this, "Bad ArgSymbol::astType");
  }
}


ArgSymbol*
ArgSymbol::copyInner(ASTMap* map) {
  ArgSymbol *ps = new ArgSymbol(intent, stringcpy(name),
                                type, COPY_INT(defaultExpr));
  ps->variableExpr = COPY_INT(variableExpr);
  if (genericSymbol)
    ps->genericSymbol = genericSymbol;
  ps->isGeneric = isGeneric;
  ps->isExactMatch = isExactMatch;
  ps->cname = stringcpy(cname);
  return ps;
}


void ArgSymbol::replaceChild(BaseAST* old_ast, BaseAST* new_ast) {
  if (old_ast == defaultExpr)
    defaultExpr = dynamic_cast<Expr*>(new_ast);
  else if (old_ast == variableExpr)
    variableExpr = dynamic_cast<Expr*>(new_ast);
  else
    type->replaceChild(old_ast, new_ast);
}


void ArgSymbol::traverseDefSymbol(Traversal* traversal) {
  TRAVERSE(defaultExpr, traversal, false);
  TRAVERSE(variableExpr, traversal, false);
  TRAVERSE(type, traversal, false);
  TRAVERSE(genericSymbol, traversal, false);
}


void ArgSymbol::printDef(FILE* outfile) {
  fprintf(outfile, "%s ", intentTagNames[intent]);
  Symbol::print(outfile);
  fprintf(outfile, ": ");
  type->print(outfile);
}


bool ArgSymbol::requiresCPtr(void) {
  return intent == INTENT_OUT || intent == INTENT_INOUT || intent == INTENT_REF;
}


bool ArgSymbol::requiresCopyBack(void) {
  return intent == INTENT_OUT || intent == INTENT_INOUT;
}


bool ArgSymbol::requiresCTmp(void) {
  return type->requiresCParamTmp(intent);
}


bool ArgSymbol::isConst(void) {
  return intent == INTENT_BLANK || intent == INTENT_CONST;
}


bool ArgSymbol::isRef(void) {
  if (intent == INTENT_REF)
    return true;
  if (is_Reference_Type(type))
    return true;
  return false;
}


void ArgSymbol::codegen(FILE* outfile) {
  bool requiresDeref = requiresCPtr();
 
  if (requiresDeref) {
    fprintf(outfile, "(*");
  }
  Symbol::codegen(outfile);
  if (requiresDeref) {
    fprintf(outfile, ")");
  }
}


void ArgSymbol::codegenDef(FILE* outfile) {
  type->codegen(outfile);
  if (requiresCPtr()) {
    fprintf(outfile, "* const");
  }
  fprintf(outfile, " ");
  Symbol::codegen(outfile);
}

TypeSymbol::TypeSymbol(char* init_name, Type* init_definition) :
  Symbol(SYMBOL_TYPE, init_name, init_definition ? init_definition->metaType : NULL), 
  definition(init_definition)
{
  if (!definition)
    isUnresolved = true;
  else
    definition->addSymbol(this);
}


void TypeSymbol::verify(void) {
  Symbol::verify();
  if (astType != SYMBOL_TYPE) {
    INT_FATAL(this, "Bad TypeSymbol::astType");
  }
  definition->verify();
}


TypeSymbol*
TypeSymbol::copyInner(ASTMap* map) {
  Type* new_definition = COPY_INT(definition);
  TypeSymbol* new_definition_symbol = new TypeSymbol(stringcpy(name), new_definition);
  new_definition->addSymbol(new_definition_symbol);
  new_definition_symbol->cname = stringcpy(cname);
  return new_definition_symbol;
}


TypeSymbol* TypeSymbol::clone(ASTMap* map) {
  ClassType* originalClass = dynamic_cast<ClassType*>(definition);
  if (!originalClass) {
    INT_FATAL(this, "Attempt to clone non-class type");
  }
  TypeSymbol* clone = copy(map);
  ClassType* newClass = dynamic_cast<ClassType*>(clone->definition);
  if (!newClass) {
    INT_FATAL(this, "Class cloning went horribly wrong");
  }
  clone->cname = stringcat("_clone_", clone->cname);
  defPoint->parentStmt->insertBefore(new DefExpr(clone));
  clone->addPragmas(&pragmas);
  newClass->typeParents.add(originalClass);
  if (no_infer)
    newClass->dispatchParents.copy(originalClass->dispatchParents);
  return clone;
}


void TypeSymbol::replaceChild(BaseAST* old_ast, BaseAST* new_ast) {
  if (old_ast == type)
    type = dynamic_cast<Type*>(new_ast);
  else
    definition->replaceChild(old_ast, new_ast);
}


void TypeSymbol::traverseDefSymbol(Traversal* traversal) {
  TRAVERSE(type, traversal, false);
  TRAVERSE_DEF(definition, traversal, false);
}


void TypeSymbol::codegenPrototype(FILE* outfile) {
  definition->codegenPrototype(outfile);
}


void TypeSymbol::codegenDef(FILE* outfile) {
  definition->codegenDef(outfile);
}


FnSymbol::FnSymbol(char* initName,
                   TypeSymbol* initTypeBinding,
                   AList<DefExpr>* initFormals,
                   Type* initRetType,
                   Expr* initWhereExpr,
                   BlockStmt* initBody,
                   fnType initFnClass,
                   bool initNoParens,
                   bool initRetRef) :
  Symbol(SYMBOL_FN, initName, new FnType()),
  typeBinding(initTypeBinding),
  formals(initFormals),
  retType(initRetType),
  whereExpr(initWhereExpr),
  retExpr(NULL),
  body(initBody),
  fnClass(initFnClass),
  noParens(initNoParens),
  retRef(initRetRef),
  argScope(NULL),
  isSetter(false),
  isGeneric(false),
  hasVarArgs(false),
  _this(NULL),
  _setter(NULL),
  _getter(NULL),
  isMethod(false),
  instantiatedFrom(NULL),
  instantiatedTo(NULL),
  visible(true),
  basicBlocks(NULL),
  calledBy(NULL),
  calls(NULL)
{
  if (!body)
    body = new BlockStmt();
}


void FnSymbol::verify(void) {
  Symbol::verify();
  if (astType != SYMBOL_FN) {
    INT_FATAL(this, "Bad FnSymbol::astType");
  }
  if (normalized) {
    if (!dynamic_cast<ReturnStmt*>(body->body->last()))
      INT_FATAL(this, "Last statement in normalized function is not a return");
  }
}


FnSymbol* FnSymbol::getFnSymbol(void) {
  return this;
}


FnSymbol*
FnSymbol::copyInner(ASTMap* map) {
  FnSymbol* copy = new FnSymbol(name,
                                typeBinding,
                                COPY_INT(formals),
                                retType,
                                COPY_INT(whereExpr),
                                COPY_INT(body),
                                fnClass,
                                noParens,
                                retRef);
  copy->retExpr = COPY_INT(retExpr);
  copy->cname = cname;
  copy->isSetter = isSetter;
  copy->isGeneric = false;  // set in normalize()
  copy->_this = _this;
  copy->_setter = _setter;
  copy->_getter = _getter; // If it is a cloned class we probably want
                           // this to point to the new member, but how
                           // do we do that
  copy->isMethod = isMethod;
  return copy;
}


void FnSymbol::replaceChild(BaseAST* old_ast, BaseAST* new_ast) {
  if (old_ast == body) {
    body = dynamic_cast<BlockStmt*>(new_ast);
  } else if (old_ast == formals) {
    formals = dynamic_cast<AList<DefExpr>*>(new_ast);
  } else if (old_ast == whereExpr) {
    whereExpr = dynamic_cast<Expr*>(new_ast);
  } else if (old_ast == retExpr) {
    retExpr = dynamic_cast<Expr*>(new_ast);
  } else {
    INT_FATAL(this, "Unexpected case in FnSymbol::replaceChild");
  }
}


void FnSymbol::traverseDefSymbol(Traversal* traversal) {
  SymScope* saveScope = NULL;

  if (argScope) {
    saveScope = Symboltable::setCurrentScope(argScope);
  }
  TRAVERSE(formals, traversal, false);
  TRAVERSE(type, traversal, false);
  TRAVERSE(body, traversal, false);
  TRAVERSE(retType, traversal, false);
  TRAVERSE(whereExpr, traversal, false);
  TRAVERSE(retExpr, traversal, false);
  if (argScope) {
    Symboltable::setCurrentScope(saveScope);
  }
}


static bool function_returns_void(FnSymbol* fn) {
  if (fn->retType == dtVoid)
    return true;

  if (fn->retType != dtUnknown || fn->defPoint->exprType)
    return false;

  Vec<BaseAST*> asts;
  collect_asts(&asts, fn);
  forv_Vec(BaseAST, ast, asts) {
    if (ReturnStmt* returnStmt = dynamic_cast<ReturnStmt*>(ast)) {
      return returnStmt->expr->typeInfo() == dtVoid;
    }
  }

  return true;
}


FnSymbol* FnSymbol::clone(ASTMap* map) {
  Stmt* copyStmt = defPoint->parentStmt->copy(map, NULL);
  ExprStmt* exprStmt = dynamic_cast<ExprStmt*>(copyStmt);
  DefExpr* defExpr = dynamic_cast<DefExpr*>(exprStmt->expr);
  // BLC: we don't want to rename any clones that we build in the
  // prelude, because they likely refer to external functions for
  // which clones will not be built
  if (defPoint->getModule() == prelude) {
    defExpr->sym->cname = stringcat("_clone_", cname);
  }
  defPoint->parentStmt->insertAfter(copyStmt);
  clear_type_info(copyStmt);
  clear_type_info(defPoint);
  defExpr->sym->addPragmas(&pragmas);
  return dynamic_cast<FnSymbol*>(defExpr->sym);
}


FnSymbol* FnSymbol::coercion_wrapper(Map<Symbol*,Symbol*>* coercion_substitutions) {
  AList<DefExpr>* wrapper_formals = new AList<DefExpr>();
  AList<Expr>* wrapper_actuals = new AList<Expr>();
  BlockStmt* wrapper_body = new BlockStmt();
  Symbol *new_this = 0;
  for_alist(DefExpr, formal, formals) {
    Symbol* newFormal = formal->sym->copy();
    if (_this == formal->sym)
      new_this = newFormal;
    wrapper_formals->insertAtTail(new DefExpr(newFormal));
    Symbol* coercionSubstitution = coercion_substitutions->get(formal->sym);
    if (TypeSymbol *ts = dynamic_cast<TypeSymbol*>(coercionSubstitution)) {
      newFormal->type = ts->definition;
      char* tempName = stringcat("_coercion_temp_", formal->sym->name);
      VarSymbol* temp = new VarSymbol(tempName, formal->sym->type);
      wrapper_body->insertAtTail(new DefExpr(temp));
      wrapper_body->insertAtTail(new CallExpr("=", temp, new CastExpr(new SymExpr(newFormal), NULL, formal->sym->type)));
      wrapper_actuals->insertAtTail(new SymExpr(temp));
    } else {
      wrapper_actuals->insertAtTail(new SymExpr(newFormal));
    }
  }

  Expr* fn_call = new CallExpr(this, wrapper_actuals);
  if (function_returns_void(this)) {
    wrapper_body->insertAtTail(fn_call);
  } else {
    wrapper_body->insertAtTail(new ReturnStmt(fn_call));
  }

  FnSymbol* wrapper_fn = new FnSymbol(name, typeBinding, wrapper_formals,
                                      retType, NULL, wrapper_body,
                                      fnClass, noParens, retRef);
  wrapper_fn->visible = false;
  wrapper_fn->isMethod = isMethod;
  wrapper_fn->cname = stringcat("_coerce_wrap_", cname);
  wrapper_fn->addPragma("inline");
  wrapper_fn->_this = new_this;
  defPoint->parentStmt->insertAfter(new DefExpr(wrapper_fn));
  wrapper_fn->addPragmas(&pragmas);
  reset_file_info(wrapper_fn->defPoint->parentStmt, lineno, filename);
  normalize(wrapper_fn);
  return wrapper_fn;
}


FnSymbol* FnSymbol::default_wrapper(Vec<Symbol*>* defaults) {
  if (FnSymbol* cached = check_dwcache(this, defaults))
    return cached;

  FnSymbol* wrapper = new FnSymbol(name);
  wrapper->visible = false;
  wrapper->formals = new AList<DefExpr>();

  AList<Expr>* method_actuals = new AList<Expr>();
  AList<Expr>* wrapper_actuals = new AList<Expr>();
  ASTMap map;
  int formal_num = 0;
  for_alist(DefExpr, formalDef, formals) {
    formal_num++;
    ArgSymbol* formal = dynamic_cast<ArgSymbol*>(formalDef->sym);
    if (!defaults->in(formal)) {
      Symbol* newFormal = formal->copy();
      map.put(formal, newFormal);
      wrapper->formals->insertAtTail(new DefExpr(newFormal));
      if (formal_num <= 2 && isMethod)
        method_actuals->insertAtTail(new SymExpr(newFormal));
      else
        wrapper_actuals->insertAtTail(new SymExpr(newFormal));
      if (_this == formal)
        wrapper->_this = newFormal;
    } else {
      if (formal->intent == INTENT_TYPE) {
        wrapper_actuals->insertAtTail(formal->defaultExpr->copy());
      } else {
        char* temp_name = stringcat("_default_temp_", formal->name);
        VarSymbol* temp = new VarSymbol(temp_name, formal->type);
        map.put(formal, temp);
        Expr* temp_init = NULL;
        Expr* temp_type = NULL;
        if (formal->intent != INTENT_OUT)
          temp_init = formal->defaultExpr->copy();
        if (SymExpr* symExpr = dynamic_cast<SymExpr*>(temp_init)) {
          if (symExpr->var == gNil) {
            temp_init = NULL;
          }
        }
        if (formalDef->exprType)
          temp_type = formalDef->exprType->copy();
        wrapper->insertAtTail(new DefExpr(temp, temp_init, temp_type));
        if (formal->type != dtUnknown &&
            formal->intent != INTENT_REF &&
            formal->intent != INTENT_OUT &&
            formal->intent != INTENT_INOUT)
          wrapper_actuals->insertAtTail(new CastExpr(new SymExpr(temp), NULL, formal->type));
        else
          wrapper_actuals->insertAtTail(new SymExpr(temp));
      }
    }
  }
  update_symbols(wrapper->body, &map);
  CallExpr* call;
  if (isMethod) {
    call = new CallExpr(this, method_actuals);
    call->partialTag = PARTIAL_OK;
    call = new CallExpr(call, wrapper_actuals);
  } else
    call = new CallExpr(this, wrapper_actuals);
  wrapper->insertAtTail(
    function_returns_void(this) ? new ExprStmt(call) : new ReturnStmt(call));
  wrapper->typeBinding = typeBinding;
  wrapper->retType = retType;
  wrapper->fnClass = fnClass;
  wrapper->noParens = noParens;
  wrapper->retRef = retRef;
  wrapper->isMethod = isMethod;
  wrapper->cname = stringcat("_default_wrap_", cname);
  wrapper->addPragma("inline");
  defPoint->parentStmt->insertAfter(new DefExpr(wrapper));
  wrapper->addPragmas(&pragmas);
  reset_file_info(wrapper->defPoint->parentStmt, lineno, filename);
  add_dwcache(wrapper, this, defaults);
  normalize(wrapper);
  return wrapper;
}


static CallExpr*
fix_method_wrapper_call(FnSymbol* fn, CallExpr* call) {
  if (fn->isMethod) {
    call->partialTag = PARTIAL_OK;
    CallExpr* outer = new CallExpr(call);
    while (call->argList->length() > 2) {
      Expr* arg = call->get(3);
      arg->remove();
      outer->argList->insertAtTail(arg);
    }
    return outer;
  }
  return call;
}


FnSymbol* FnSymbol::order_wrapper(Map<Symbol*,Symbol*>* formals_to_formals) {
  Map<Symbol*,Symbol*> old_to_new;
  for_alist(DefExpr, formal, formals) {
    old_to_new.put(formal->sym, formal->sym->copy());
  }

  AList<DefExpr>* wrapper_formals = new AList<DefExpr>();
  AList<Expr>* wrapper_actuals = new AList<Expr>();
  for_alist(DefExpr, formal, formals) {
    wrapper_formals->insertAtTail(new DefExpr(old_to_new.get(formals_to_formals->get(formal->sym))));
    wrapper_actuals->insertAtTail(new SymExpr(old_to_new.get(formal->sym)));
  }

  CallExpr* call = new CallExpr(this, wrapper_actuals);
  call = fix_method_wrapper_call(this, call);
  Stmt* stmt = (function_returns_void(this))
    ? new ExprStmt(call)
    : new ReturnStmt(call);

  FnSymbol* wrapper_fn = new FnSymbol(name, typeBinding, wrapper_formals,
                                      retType, NULL, new BlockStmt(stmt),
                                      fnClass, noParens, retRef);
  wrapper_fn->visible = false;
  wrapper_fn->isMethod = isMethod;
  wrapper_fn->cname = stringcat("_order_wrap_", cname);
  wrapper_fn->addPragma("inline");
  defPoint->parentStmt->insertBefore(new DefExpr(wrapper_fn));
  reset_file_info(wrapper_fn->defPoint->parentStmt, lineno, filename);
  normalize(wrapper_fn);
  return wrapper_fn;
}


FnSymbol* FnSymbol::promotion_wrapper(Map<Symbol*,Symbol*>* promotion_subs) {
  AList<DefExpr>* wrapper_formals = new AList<DefExpr>();
  AList<DefExpr>* indices = new AList<DefExpr>();
  AList<Expr>* iterators = new AList<Expr>();
  AList<Expr>* wrapper_actuals = new AList<Expr>();
  for_alist(DefExpr, formal, formals) {
    if (Symbol* ts = promotion_subs->get(formal->sym)) {
      Type* new_type = dynamic_cast<TypeSymbol*>(ts)->definition;
      Symbol* new_formal = formal->sym->copy();
      new_formal->type = new_type;
      Symbol* new_index = new VarSymbol("_index", formal->sym->type);
      wrapper_formals->insertAtTail(new DefExpr(new_formal));
      iterators->insertAtTail(new SymExpr(new_formal));
      indices->insertAtTail(new DefExpr(new_index));
      wrapper_actuals->insertAtTail(new SymExpr(new_index));
    } else {
      Symbol* new_formal = formal->sym->copy();
      wrapper_formals->insertAtTail(new DefExpr(new_formal));
      wrapper_actuals->insertAtTail(new SymExpr(new_formal));
    }
  }
  FnSymbol* wrapper;
  BlockStmt* body;
  if (function_returns_void(this)) {
    body = new BlockStmt(build_for_block(BLOCK_FORALL,
                                         indices,
                                         iterators,
                                         new BlockStmt(
                                           new ExprStmt(
                                             new CallExpr(this, wrapper_actuals)))));
    wrapper = new FnSymbol(name, typeBinding, wrapper_formals, dtUnknown, NULL,
                           body, FN_FUNCTION, false, false);
  } else {
    body = new BlockStmt(build_for_block(BLOCK_FORALL,
                                         indices,
                                         iterators,
                                         new BlockStmt(
                                           new ReturnStmt(
                                             new CallExpr(this, wrapper_actuals), true))));
    wrapper = new FnSymbol(name, typeBinding, wrapper_formals, dtUnknown, NULL,
                           body, FN_ITERATOR, false, false);
  }
  wrapper->visible = false;
  wrapper->isMethod = isMethod;
  wrapper->cname = stringcat("_promotion_wrap_", cname);
  defPoint->parentStmt->insertBefore(new DefExpr(wrapper));
  reset_file_info(wrapper->defPoint->parentStmt, lineno, filename);
  normalize(wrapper);
  return wrapper;
}

static void
instantiate_update_expr(ASTMap* substitutions, Expr* expr) {
  ASTMap map;
  map.copy(*substitutions);
  // for type variables, add TypeSymbols into the map as well
  for (int i = 0; i < substitutions->n; i++)
    if (Type *t = dynamic_cast<Type*>(substitutions->v[i].key))
      if (Type *tt = dynamic_cast<Type*>(substitutions->v[i].value))
        map.put(t->symbol, tt->symbol);
  update_symbols(expr, &map);
}


static void
instantiate_add_subs(ASTMap* substitutions, ASTMap* map) {
  ASTMap tmp;
  tmp.copy(*substitutions);
  for (int i = 0; i < tmp.n; i++) {
    if (tmp.v[i].key) {
      if (BaseAST *b = map->get(tmp.v[i].key))
        substitutions->put(b, tmp.v[i].value);
    }
  }
}


static FnSymbol*
instantiate_function(FnSymbol *fn, ASTMap *all_subs, ASTMap *generic_subs, ASTMap *map) {
  Stmt* fnStmt = fn->defPoint->parentStmt->copy(map);
  ExprStmt* exprStmt = dynamic_cast<ExprStmt*>(fnStmt);
  DefExpr* defExpr = dynamic_cast<DefExpr*>(exprStmt->expr);
  FnSymbol* fnClone = dynamic_cast<FnSymbol*>(defExpr->sym);
  fnClone->visible = false;
  fn->defPoint->parentStmt->insertBefore(fnStmt);
  fnClone->cname = stringcat("_inst_", defExpr->sym->cname);
  instantiate_add_subs(all_subs, map);
  instantiate_update_expr(all_subs, defExpr);
  fnClone->instantiatedFrom = fn;
  fnClone->substitutions.copy(*generic_subs);
  add_icache(fnClone);
  for_alist(DefExpr, formal, fnClone->formals) {
    if (ArgSymbol *ps = dynamic_cast<ArgSymbol *>(formal->sym)) {
      if (TypeSymbol *ts = dynamic_cast<TypeSymbol *>(ps->genericSymbol)) {
        if (ts->definition->astType != TYPE_VARIABLE) {
          ps->type = ts->type;
          ps->isExactMatch = true;
        }
      }
      if (all_subs->get(ps) && ps->intent == INTENT_PARAM) {
        ps->intent = INTENT_BLANK;
        ps->isGeneric = false;
        if (!no_infer) {
          if (VarSymbol *vs = dynamic_cast<VarSymbol*>(all_subs->get(ps))) {
            if (vs->literalType)
              ps->type = vs->literalType;
          }
        }
      }
    }
  }
  return fnClone;
}


FnSymbol*
FnSymbol::clone_generic(ASTMap* formal_types) {
  // use same cache as instantiate generic since they won't collide
  if (FnSymbol* cached = check_icache(this, formal_types))
    return cached;
  Stmt* fnStmt = defPoint->parentStmt->copy();
  ExprStmt* exprStmt = dynamic_cast<ExprStmt*>(fnStmt);
  DefExpr* defExpr = dynamic_cast<DefExpr*>(exprStmt->expr);
  FnSymbol* fnClone = dynamic_cast<FnSymbol*>(defExpr->sym);
  fnClone->visible = false;
  fnClone->cname = stringcat("_clone_", fnClone->cname);
  defPoint->parentStmt->insertBefore(fnStmt);
  DefExpr* oldFormalDef = formals->first();
  for_alist(DefExpr, formalDef, fnClone->formals) {
    ArgSymbol* oldFormal = dynamic_cast<ArgSymbol*>(oldFormalDef->sym);
    ArgSymbol* formal = dynamic_cast<ArgSymbol*>(formalDef->sym);
    if (Type* type = dynamic_cast<Type*>(formal_types->get(oldFormal))) {
      formal->type = type;
      formalDef->exprType = NULL;
    }
    oldFormalDef = formals->next();
  }
  fnClone->instantiatedFrom = this;
  fnClone->substitutions.copy(*formal_types);
  add_icache(fnClone);
  return fnClone;
}


bool
FnSymbol::isPartialInstantiation(ASTMap* generic_substitutions) {
  for_alist(DefExpr, formalDef, this->formals) {
    ArgSymbol* formal = dynamic_cast<ArgSymbol*>(formalDef->sym);
    if (formal->isGeneric) {
      bool found = false;
      for (int i = 0; i < generic_substitutions->n; i++) {
        if (TypeSymbol* ts = dynamic_cast<TypeSymbol*>(formal->genericSymbol)) {
          if (ts->definition == generic_substitutions->v[i].key) {
            found = true;
          }
        } else {
          if (formal == generic_substitutions->v[i].key) {
            found = true;
          }
        }
      }
      if (!found) {
        return true;
      }
    }
  }
  return false;
}

static void
check_promoter(ClassType *at) {
  if (!scalar_promotion)
    return;
  ClassType *t = dynamic_cast<ClassType*>(at->instantiatedFrom);
  forv_Vec(TypeSymbol, tt, t->types) {
    if (tt->hasPragma("promoter")) {
      Type *seqElementType = dynamic_cast<Type*>(at->substitutions.get(tt->definition));
      if (dynamic_cast<PrimitiveType*>(seqElementType)) {
        at->scalarPromotionType = seqElementType;
        if (!run_interpreter)
          at->dispatchParents.add(seqElementType);
      }
    }
  }
}

static void
add_new_ast_functions(FnSymbol* fn) {
  new_ast_functions.add(fn);
  Vec<BaseAST*> asts;
  collect_asts(&asts, fn->body);
  forv_Vec(BaseAST, ast, asts) {
    if (FnSymbol* fn = dynamic_cast<FnSymbol*>(ast))
      new_ast_functions.add(fn);
  }
}


FnSymbol*
FnSymbol::instantiate_generic(ASTMap* generic_substitutions) {
  // check to make sure this fully instantiates
  if (isPartialInstantiation(generic_substitutions))
    INT_FATAL(this, "partial instantiation detected");

  // change meta types to types
  for (int i = 0; i < generic_substitutions->n; i++)
    if (BaseAST* value = generic_substitutions->v[i].value)
      if (MetaType* metaType = dynamic_cast<MetaType*>(value))
        generic_substitutions->v[i].value = metaType->base;

  // return cached if we did this instantiation already
  if (FnSymbol* cached = check_icache(this, generic_substitutions))
    return cached;

  //  static int uid = 1;
  ASTMap substitutions(*generic_substitutions);
  FnSymbol* newfn = NULL;
  currentLineno = lineno;
  currentFilename = filename;
  ASTMap map;

  // check for infinite recursion by limiting the number of
  // instantiations of a particular type or function
  if (instantiatedTo == NULL)
    instantiatedTo = new Vec<FnSymbol*>();
  if (instantiatedTo->n >= instantiation_limit) {
    if (fnClass == FN_CONSTRUCTOR) {
      USR_FATAL_CONT(retType, "Type '%s' has been instantiated too many times",
                     retType->symbol->name);
    } else {
      USR_FATAL_CONT(this, "Function '%s' has been instantiated too many times",
                     name);
    }
    USR_PRINT("  If this is intentional, try increasing"
              " the instantiation limit from %d", instantiation_limit);
    USR_STOP();
  }

  if (fnClass == FN_CONSTRUCTOR) {

    //
    // Make what is visible at types to instantiate visible at
    // instantiated type
    //
    Vec<BaseAST*> values;
    generic_substitutions->get_values(values);
    forv_Vec(BaseAST, value, values)
      if (Type* type = dynamic_cast<Type*>(value))
        if (type->symbol->defPoint)
          defPoint->parentScope->uses.add(type->symbol->defPoint->getModule());

    TypeSymbol* clone = retType->symbol->clone(&map);
    new_ast_types.add(clone);
    instantiate_add_subs(&substitutions, &map);

    ClassType* cloneType = dynamic_cast<ClassType*>(clone->definition);
    cloneType->instantiatedFrom = retType;

    Vec<TypeSymbol *> types;
    types.move(cloneType->types);
    for (int i = 0; i < types.n; i++) {
      if (types.v[i] && substitutions.get(types.v[i]->definition)) {
        types.v[i]->defPoint->parentStmt->remove();
      } else
        cloneType->types.add(types.v[i]);
    }

    instantiate_update_expr(&substitutions, clone->defPoint);
    substitutions.put(retType, clone->definition);

    cloneType->substitutions.copy(*generic_substitutions);

    forv_Vec(Type*, parent, retType->typeParents)
      cloneType->typeParents.add(parent);

    forv_Vec(Type*, parent, retType->dispatchParents)
      cloneType->dispatchParents.add(parent);

    newfn = instantiate_function(this, &substitutions, generic_substitutions, &map);
    cloneType->defaultConstructor = newfn;
    newfn->typeBinding = clone;
    check_promoter(cloneType);

  } else {
    newfn = instantiate_function(this, &substitutions, generic_substitutions, &map);
  }
  normalize(newfn);
  instantiatedTo->add(newfn);
  add_new_ast_functions(newfn);

  //
  // set default expression for type intent arguments
  //
  for_alist(DefExpr, formal_def, newfn->formals) {
    ArgSymbol* formal = dynamic_cast<ArgSymbol*>(formal_def->sym);
    if (formal->intent == INTENT_TYPE) {
      if (!formal->defaultExpr) {
        BaseAST* prev = formal_def->prev;
        formal_def->remove();
        formal->defaultExpr = new SymExpr(formal->genericSymbol);
        prev->insertAfter(formal_def);
      }
    }
  }

  return newfn;
}


void FnSymbol::printDef(FILE* outfile) {
  fprintf(outfile, "function ");
  print(outfile);
  fprintf(outfile, "(");
  if (formals) {
    formals->print(outfile, ", ");
  }
  fprintf(outfile, ")");
  if (retType == dtVoid) {
    fprintf(outfile, " ");
  } else {
    fprintf(outfile, ": ");
    retType->print(outfile);
    fprintf(outfile, " ");
  }
  body->print(outfile);
  fprintf(outfile, "\n\n");
}


void FnSymbol::codegenHeader(FILE* outfile) {
  if (retType == dtUnknown) {
    retType = return_type_info(this);
    INT_WARNING(this, "return type unknown, calling analysis late");
  }
  retType->codegen(outfile);
  fprintf(outfile, " ");
  this->codegen(outfile);
  fprintf(outfile, "(");
  if (!formals) {
    fprintf(outfile, "void");
  } else {
    bool first = true;
    for_alist(DefExpr, formal, formals) {
      if (!first) {
        fprintf(outfile, ", ");
      }
      formal->sym->codegenDef(outfile);
      first = false;
    }
  }
  fprintf(outfile, ")");
}


void FnSymbol::codegenPrototype(FILE* outfile) {
  if (hasPragma("no codegen"))
    return;
  codegenHeader(outfile);
  fprintf(outfile, ";\n");
}


void FnSymbol::codegenDef(FILE* outfile) {
  if (hasPragma("no codegen"))
    return;

  if (fnClass == FN_CONSTRUCTOR) {
    fprintf(outfile, "/* constructor */\n");
  }

  codegenHeader(outfile);
  fprintf(outfile, " ");

  // while these braces seem like they should be extraneous since
  // all function bodies are BlockStmts, it turns out that they are
  // not because in C the function's parameter scope is the same as
  // its local variable scope; so if we have a parameter and a local
  // variable of name x (as in trivial/bradc/vardecls1b.chpl), these
  // extra braces are required to make the generated code work out.
  // SJD: extra braces no longer necessary because of mangling
  // SJD: Why should we not make the same scope restriction??
  justStartedGeneratingFunction = true;
  body->codegen(outfile);
  fprintf(outfile, "\n\n");
}


void
FnSymbol::insertAtHead(BaseAST* ast) {
  body->insertAtHead(ast);
}


void
FnSymbol::insertAtTail(BaseAST* ast) {
  body->insertAtTail(ast);
}


int Symbol::nestingDepth() {
  if (!defPoint) // labels
    return 0;
  Symbol *s = defPoint->parentSymbol;
  int d = 0;
  while (s->astType == SYMBOL_FN) {
    d++;
    s = s->defPoint->parentSymbol;
  }
  return d;
}


FnSymbol *Symbol::nestingParent(int i) {
  if (!defPoint) // labels
    return 0;
  Symbol *s = defPoint->parentSymbol;
  while (s->astType == SYMBOL_FN) {
    i--;
    if (i >= 0)
      return dynamic_cast<FnSymbol*>(s);
    s = s->defPoint->parentSymbol;
  }
  return 0;
}


EnumSymbol::EnumSymbol(char* init_name) :
  Symbol(SYMBOL_ENUM, init_name)
{
  type = dtInt;
}


void EnumSymbol::verify(void) {
  Symbol::verify();
  if (astType != SYMBOL_ENUM) {
    INT_FATAL(this, "Bad EnumSymbol::astType");
  }
}


EnumSymbol*
EnumSymbol::copyInner(ASTMap* map) {
  return new EnumSymbol(stringcpy(name));
}


void EnumSymbol::traverseDefSymbol(Traversal* traversal) { }


void EnumSymbol::codegenDef(FILE* outfile) { }


ModuleSymbol::ModuleSymbol(char* init_name, modType init_modtype) :
  Symbol(SYMBOL_MODULE, init_name),
  modtype(init_modtype),
  stmts(new AList<Stmt>),
  initFn(NULL),
  modScope(NULL)
{ }


void ModuleSymbol::verify(void) {
  Symbol::verify();
  if (astType != SYMBOL_MODULE) {
    INT_FATAL(this, "Bad ModuleSymbol::astType");
  }
}


ModuleSymbol*
ModuleSymbol::copyInner(ASTMap* map) {
  INT_FATAL(this, "Illegal call to ModuleSymbol::copy");
  return NULL;
}


void ModuleSymbol::setModScope(SymScope* init_modScope) {
  modScope = init_modScope;
}


void ModuleSymbol::codegenDef(void) {
  fileinfo outfileinfo;

  inUserModule = (modtype == MOD_USER);
  openCFiles(name, &outfileinfo);

  fprintf(codefile, "#include \"stdchpl.h\"\n");
  fprintf(codefile, "#include \"_chpl_header.h\"\n");
  if (parallelPass) {
    // WAW: Should be done in codegen, but cannot add include's after codegen.
    fprintf (codefile, "#include \"chplthreads.h\"\n");
  }
  fprintf(codefile, "\n");

  modScope->codegenFunctions(codefile);

  closeCFiles(&outfileinfo);
}


void ModuleSymbol::startTraversal(Traversal* traversal) {
  SymScope* prevScope = NULL;

  if (modScope) {
    prevScope = Symboltable::setCurrentScope(modScope);
  }
  stmts->traverse(traversal, false);
  if (modScope) {
    Symboltable::setCurrentScope(prevScope);
  }
}


void ModuleSymbol::replaceChild(BaseAST* old_ast, BaseAST* new_ast) {
  if (old_ast == stmts) {
    stmts = dynamic_cast<AList<Stmt>*>(new_ast);
  } else {
    INT_FATAL(this, "Unexpected case in ModuleSymbol::replaceChild");
  }
}


/** SJD: Makes sense for this to take place of above startTraversal **/
void ModuleSymbol::traverseDefSymbol(Traversal* traversal) {
  startTraversal(traversal);
}


bool ModuleSymbol::isFileModule(void) {
  return (lineno == 0);
}


int ModuleSymbol::numUserModules(Vec<ModuleSymbol*>* moduleList) {
  int numUserModules = 0;
  forv_Vec(ModuleSymbol, mod, *moduleList) {
    if (mod->modtype == MOD_USER) {
      numUserModules++;
    }
  }

  return numUserModules;
}


LabelSymbol::LabelSymbol(char* init_name) :
  Symbol(SYMBOL_LABEL, init_name, NULL)
{ 
 
}


void LabelSymbol::verify(void) {
  Symbol::verify();
  if (astType != SYMBOL_LABEL) {
    INT_FATAL(this, "Bad LabelSymbol::astType");
  }
}

LabelSymbol* 
LabelSymbol::copyInner(ASTMap* map) {
  LabelSymbol* copy = new LabelSymbol(stringcpy(name));
  copy->cname = stringcpy(cname);
  return copy;
}

void LabelSymbol::codegenDef(FILE* outfile) { }

static int literal_id = 1;
HashMap<Immediate *, ImmHashFns, VarSymbol *> uniqueConstantsHash;
HashMap<char *, StringHashFns, VarSymbol *> uniqueSymbolHash;

void
initSymbol() {
  gNil = new VarSymbol("nil", dtNil, VAR_NORMAL, VAR_CONST);
  rootScope->define(gNil); // SJD: Should intrinsics have DefExprs?
  builtinSymbols.add(gNil);
  gUnknown = new VarSymbol("_unknown", dtUnknown, VAR_NORMAL, VAR_CONST);
  rootScope->define(gUnknown); // SJD: Should intrinsics have DefExprs?
  builtinSymbols.add(gUnknown);
  gUnspecified = new VarSymbol("_", dtUnspecified, VAR_NORMAL, VAR_CONST);
  rootScope->define(gUnspecified); // SJD: Should intrinsics have DefExprs?
  builtinSymbols.add(gUnspecified);
  gVoid = new VarSymbol("_void", dtVoid, VAR_NORMAL, VAR_CONST);
  rootScope->define(gVoid); // SJD: Should intrinsics have DefExprs?
  builtinSymbols.add(gVoid);
  gTrue = new VarSymbol("true", dtBool, VAR_NORMAL, VAR_CONST);
  rootScope->define(gTrue); // SJD: Should intrinsics have DefExprs?
  builtinSymbols.add(gTrue);
  gFalse = new VarSymbol("false", dtBool, VAR_NORMAL, VAR_CONST);
  rootScope->define(gFalse); // SJD: Should intrinsics have DefExprs?
  builtinSymbols.add(gFalse);
  gTrue->immediate = new Immediate;
  gTrue->immediate->v_bool = true;
  gTrue->immediate->const_kind = IF1_NUM_KIND_UINT;
  gTrue->immediate->num_index = IF1_INT_TYPE_1;
  gFalse->immediate = new Immediate;
  gFalse->immediate->v_bool = false;
  gFalse->immediate->const_kind = IF1_NUM_KIND_UINT;
  gFalse->immediate->num_index = IF1_INT_TYPE_1;
  dtBool->defaultValue = gFalse;
  uniqueConstantsHash.put(gTrue->immediate, gTrue);
  uniqueConstantsHash.put(gFalse->immediate, gFalse);
}

VarSymbol *new_StringSymbol(char *str) {
  Immediate imm;
  imm.const_kind = IF1_CONST_KIND_STRING;
  imm.v_string = cannonicalize_string(str);
  VarSymbol *s = uniqueConstantsHash.get(&imm);
  if (s)
    return s;
  s = new VarSymbol(stringcat("_literal_", intstring(literal_id++)), dtString);
  int l = strlen(str);
  char *n = (char*)MALLOC(l + 3);
  strcpy(n + 1, str);
  n[0]='\"';
  n[l+1]='\"';
  n[l+2]=0;
  s->cname = n;
  s->immediate = new Immediate;
  *s->immediate = imm;
  uniqueConstantsHash.put(s->immediate, s);
  return s;
}

VarSymbol *new_IntSymbol(long b) {
  Immediate imm;
  imm.v_int64 = b;
  imm.const_kind = IF1_NUM_KIND_INT;
  imm.num_index = IF1_INT_TYPE_64;
  VarSymbol *s = uniqueConstantsHash.get(&imm);
  if (s)
    return s;
  s = new VarSymbol(stringcat("_literal_", intstring(literal_id++)), dtInt);
  rootScope->define(s);
  char n[80];
  sprintf(n, "%ld", b);
  s->cname = dupstr(n);
  s->immediate = new Immediate;
  *s->immediate = imm;
  s->literalType = new_LiteralType(s);
  uniqueConstantsHash.put(s->immediate, s);
  if (!dtInt->defaultValue)
    dtInt->defaultValue = new_IntSymbol(0);
  return s;
}

VarSymbol *new_UIntSymbol(unsigned long b) {
  Immediate imm;
  imm.v_uint64 = b;
  imm.const_kind = IF1_NUM_KIND_UINT;
  imm.num_index = IF1_INT_TYPE_64;
  VarSymbol *s = uniqueConstantsHash.get(&imm);
  if (s)
    return s;
  s = new VarSymbol(stringcat("_literal_", intstring(literal_id++)), dtUInt);
  rootScope->define(s);
  char n[80];
  sprintf(n, "%lud", b);
  s->cname = dupstr(n);
  s->immediate = new Immediate;
  *s->immediate = imm;
  s->literalType = new_LiteralType(s);
  uniqueConstantsHash.put(s->immediate, s);
  if (!dtUInt->defaultValue)
    dtUInt->defaultValue = new_UIntSymbol(0);
  return s;
}

VarSymbol *new_FloatSymbol(char *n, double b) {
  (void)n;
  Immediate imm;
  imm.v_float64 = b;
  imm.const_kind = IF1_NUM_KIND_FLOAT;
  imm.num_index = IF1_FLOAT_TYPE_64;
  VarSymbol *s = uniqueConstantsHash.get(&imm);
  if (s)
    return s;
  s = new VarSymbol(stringcat("_literal_", intstring(literal_id++)), dtFloat);
  s->immediate = new Immediate;
  s->cname = dupstr(n);
  *s->immediate = imm;
  uniqueConstantsHash.put(s->immediate, s);
  return s;
}

VarSymbol *new_ComplexSymbol(char *n, double r, double i) {
  (void)n;
  Immediate imm;
  imm.v_complex64.r = r;
  imm.v_complex64.i = i;
  imm.const_kind = IF1_NUM_KIND_COMPLEX;
  imm.num_index = IF1_FLOAT_TYPE_64;
  VarSymbol *s = uniqueConstantsHash.get(&imm);
  if (s)
    return s;
  s = new VarSymbol(stringcat("_literal_", intstring(literal_id++)), dtComplex);
  s->immediate = new Immediate;
  s->cname = dupstr(n);
  *s->immediate = imm;
  uniqueConstantsHash.put(s->immediate, s);
  return s;
}

VarSymbol *new_ImmediateSymbol(Immediate *imm) {
  VarSymbol *s = uniqueConstantsHash.get(imm);
  if (s)
    return s;
  PrimitiveType *t = immediate_type(imm);
  s = new VarSymbol(stringcat("_literal_", intstring(literal_id++)), t);
  s->immediate = new Immediate;
  char str[512], *ss = str;
  if (imm->const_kind == IF1_CONST_KIND_STRING)
    ss = imm->v_string;
  else
    sprint_imm(str, *imm);
  s->cname = dupstr(ss);
  *s->immediate = *imm;
  uniqueConstantsHash.put(s->immediate, s);
  return s;
}

VarSymbol *new_SymbolSymbol(char *str) {
  str = cannonicalize_string(str);
  VarSymbol *s = uniqueSymbolHash.get(str);
  if (s)
    return s;
  s = new VarSymbol(str, dtSymbol);
  s->cname = intstring(s->id);
  uniqueSymbolHash.put(str, s);
  return s;
}

PrimitiveType *
immediate_type(Immediate *imm) {
  switch (imm->const_kind) {
    default: 
  Lerror:
      USR_FATAL("bad immediate type");
      break;
    case IF1_CONST_KIND_STRING: return dtString;
    case IF1_NUM_KIND_UINT:
      if (imm->num_index == IF1_INT_TYPE_1) return dtBool;
      goto Lerror;
    case IF1_NUM_KIND_INT:
      if (imm->num_index == IF1_INT_TYPE_64) return dtInt;
      goto Lerror;
    case IF1_NUM_KIND_FLOAT:
      if (imm->num_index == IF1_FLOAT_TYPE_64) return dtFloat;
      goto Lerror;
    case IF1_NUM_KIND_COMPLEX:
      if (imm->num_index == IF1_FLOAT_TYPE_64) return dtComplex;
      goto Lerror;
  }
  return NULL;
}

int
set_immediate_type(Immediate *imm, Type *t) {
  if (t == dtBool) {
    imm->const_kind = IF1_NUM_KIND_UINT;
    imm->num_index = IF1_INT_TYPE_1; 
  } else if (t == dtInt) {
    imm->const_kind = IF1_NUM_KIND_INT;
    imm->num_index = IF1_INT_TYPE_64; 
  } else if (t == dtFloat) {
    imm->const_kind = IF1_NUM_KIND_FLOAT;
    imm->num_index = IF1_INT_TYPE_64;
  } else if (t == dtComplex) {
    imm->const_kind = IF1_NUM_KIND_COMPLEX;
    imm->num_index = IF1_FLOAT_TYPE_64;
  } else if (t == dtString) {
    imm->const_kind = IF1_CONST_KIND_STRING;
  } else
    return -1;
  return 0;
}

