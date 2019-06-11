#include <Rcpp.h>
using namespace Rcpp;
#include <Rversion.h>

struct Expand {
  bool alrep;
  bool charsxp;
  bool env;
};

SEXP obj_children_(SEXP x, std::map<SEXP, int>& seen, Expand expand);
bool is_namespace(Environment env);

bool is_altrep(SEXP x) {
#if defined(R_VERSION) && R_VERSION >= R_Version(3, 5, 0)
  return ALTREP(x);
#else
  return false;
#endif
}

SEXP obj_inspect_(SEXP x,
                 std::map<SEXP, int>& seen,
                 Expand& expand) {

  int nprotect = 1;
  int id;
  SEXP children;
  bool has_seen;
  if (seen.count(x)) {
    has_seen = true;
    id = seen[x];
    children = PROTECT(Rf_allocVector(VECSXP, 0));
  } else {
    has_seen = false;
    id = seen[x] = seen.size() + 1;
    children = PROTECT(obj_children_(x, seen, expand));
  }

  // don't store object directly to avoid increasing refcount
  Rf_setAttrib(children, Rf_install("addr"),   Rf_mkString(tfm::format("%p", x).c_str()));
  Rf_setAttrib(children, Rf_install("has_seen"), Rf_ScalarLogical(has_seen));
  Rf_setAttrib(children, Rf_install("id"),     Rf_ScalarInteger(id));
  Rf_setAttrib(children, Rf_install("type"),   Rf_ScalarInteger(TYPEOF(x)));
  Rf_setAttrib(children, Rf_install("length"), Rf_ScalarReal(Rf_length(x)));
  if (Rf_isVector(x)) {
    if (TRUELENGTH(x) > 0) {
      Rf_setAttrib(children, Rf_install("truelength"), Rf_ScalarReal(TRUELENGTH(x)));
    }
  }
  Rf_setAttrib(children, Rf_install("altrep"),  Rf_ScalarLogical(is_altrep(x)));
  Rf_setAttrib(children, Rf_install("named"),  Rf_ScalarInteger(NAMED(x)));
  Rf_setAttrib(children, Rf_install("object"), Rf_ScalarInteger(OBJECT(x)));

  // TODO: protect
  if (TYPEOF(x) == SYMSXP) {
    Rf_setAttrib(children, Rf_install("value"), Rf_ScalarString(PRINTNAME(x)));
  } else if (TYPEOF(x) == ENVSXP) {
    if (x == R_GlobalEnv) {
      Rf_setAttrib(children, Rf_install("value"), Rf_mkString("global"));
    } else if (x == R_EmptyEnv) {
      Rf_setAttrib(children, Rf_install("value"), Rf_mkString("empty"));
    } else if (x == R_BaseEnv) {
      Rf_setAttrib(children, Rf_install("value"), Rf_mkString("base"));
    } else {
      Rf_setAttrib(children, Rf_install("value"), R_PackageEnvName(x));
    }
  }
  Rf_setAttrib(children, Rf_install("x"), R_MakeWeakRef(R_NilValue, x, R_NilValue, FALSE));
  Rf_setAttrib(children, Rf_install("class"),  Rf_mkString("lobstr_inspector"));


  UNPROTECT(1);
  return children;
}

inline void recurse(
                    std::vector< std::pair<std::string, SEXP> >& children,
                    std::map<SEXP, int>& seen,
                    const char* name,
                    SEXP child,
                    Expand& expand) {

  children.push_back(
    std::make_pair(std::string(name), obj_inspect_(child, seen, expand))
  );
}

SEXP obj_children_(
                  SEXP x,
                  std::map<SEXP, int>& seen,
                  Expand expand) {

  std::vector< std::pair<std::string, SEXP> > children;

  // Handle ALTREP objects
  if (expand.alrep && is_altrep(x)) {
#if defined(R_VERSION) && R_VERSION >= R_Version(3, 5, 0)
    SEXP klass = ALTREP_CLASS(x);
    SEXP classname = CAR(ATTRIB(klass));

    recurse(children, seen, "_class", klass, expand);
    if (classname == Rf_install("deferred_string")) {
      // Deferred string ALTREP uses an pairlist, but stores data in the CDR
      SEXP data1 = R_altrep_data1(x);
      recurse(children, seen, "_data1_car", CAR(data1), expand);
      recurse(children, seen, "_data1_cdr", CDR(data1), expand);
    } else {
      recurse(children, seen, "_data1", R_altrep_data1(x), expand);
    }
    recurse(children, seen, "_data2", R_altrep_data2(x), expand);
#endif
  } else {
    switch (TYPEOF(x)) {
    // Non-recursive types
    case NILSXP:
    case SPECIALSXP:
    case BUILTINSXP:
    case LGLSXP:
    case INTSXP:
    case REALSXP:
    case CPLXSXP:
    case RAWSXP:
    case CHARSXP:
    case SYMSXP:
      break;

    // Strings
    case STRSXP:
      if (expand.charsxp) {
        for (R_xlen_t i = 0; i < XLENGTH(x); i++) {
          recurse(children, seen, "", STRING_ELT(x, i), expand);
        }
      }
      break;

    // Recursive vectors
    case VECSXP:
    case EXPRSXP:
    case WEAKREFSXP: {
      SEXP names = Rf_getAttrib(x, R_NamesSymbol);
      if (TYPEOF(names) == STRSXP) {
        for (R_xlen_t i = 0; i < XLENGTH(x); ++i) {
          recurse(children, seen, CHAR(STRING_ELT(names, i)), VECTOR_ELT(x, i), expand);
        }
      } else {
        for (R_xlen_t i = 0; i < XLENGTH(x); ++i) {
          recurse(children, seen, "", VECTOR_ELT(x, i), expand);
        }
      }
      break;
    }

    // Linked lists
    case DOTSXP:
    case LISTSXP:
    case LANGSXP:
      if (x == R_MissingArg) // Needed for DOTSXP
        break;

      for(SEXP cons = x; cons != R_NilValue; cons = CDR(cons)) {
        SEXP tag = TAG(cons);
        if (TYPEOF(tag) == NILSXP) {
          recurse(children, seen, "", CAR(cons), expand);
        } else if (TYPEOF(tag) == SYMSXP) {
          recurse(children, seen, CHAR(PRINTNAME(tag)), CAR(cons), expand);
        } else {
          // TODO: add index? needs to be a list?
          recurse(children, seen, "_tag", tag, expand);
          recurse(children, seen, "_car", CAR(cons), expand);
        }
      }
      break;

    case BCODESXP:
      recurse(children, seen, "_tag", TAG(x), expand);
      recurse(children, seen, "_car", CAR(x), expand);
      recurse(children, seen, "_cdr", CDR(x), expand);
      break;

    // Environments
    case ENVSXP:
      if (x == R_BaseEnv || x == R_GlobalEnv || x == R_EmptyEnv || is_namespace(x))
        break;

      if (expand.env) {
        recurse(children, seen, "_frame", FRAME(x), expand);
        recurse(children, seen, "_hashtab", HASHTAB(x), expand);
      } else {
        SEXP names = PROTECT(R_lsInternal(x, TRUE));
        for (R_xlen_t i = 0; i < XLENGTH(names); ++i) {
          const char* name = CHAR(STRING_ELT(names, i));
          SEXP obj = Rf_findVarInFrame(x, Rf_install(name));
          recurse(children, seen, name, obj, expand);
        }
        UNPROTECT(1);
      }

      recurse(children, seen, "_enclos", ENCLOS(x), expand);
      break;

    // Functions
    case CLOSXP:
      recurse(children, seen, "_formals", FORMALS(x), expand);
      recurse(children, seen, "_body", BODY(x), expand);
      recurse(children, seen, "_env", CLOENV(x), expand);
      break;

    case PROMSXP:
      recurse(children, seen, "_value", PRVALUE(x), expand);
      recurse(children, seen, "_code", PRCODE(x), expand);
      recurse(children, seen, "_env", PRENV(x), expand);
      break;

    case EXTPTRSXP:
      recurse(children, seen, "_prot", EXTPTR_PROT(x), expand);
      recurse(children, seen, "_tag", EXTPTR_TAG(x), expand);
      break;

    case S4SXP:
      recurse(children, seen, "_tag", TAG(x), expand);
      break;

    default:
      stop("Don't know how to handle type %s", Rf_type2char(TYPEOF(x)));
    }
  }

  // CHARSXPs have fake attriibutes
  if (TYPEOF(x) != CHARSXP && !Rf_isNull(ATTRIB(x))) {
    recurse(children, seen, "_attrib", ATTRIB(x), expand);
  }


  // Convert std::vector to named list
  int n = children.size();
  SEXP out = PROTECT(Rf_allocVector(VECSXP, n));
  SEXP names = PROTECT(Rf_allocVector(STRSXP, n));

  for (int i = 0; i < n; ++i) {
    std::pair<std::string, SEXP> pair = children[i];
    SET_STRING_ELT(names, i, pair.first == "" ? NA_STRING : Rf_mkChar(pair.first.c_str()));
    SET_VECTOR_ELT(out, i, pair.second);
  }
  Rf_setAttrib(out, R_NamesSymbol, names);
  UNPROTECT(2);

  return out;
}


// [[Rcpp::export]]
Rcpp::List obj_inspect_(SEXP x,
                        bool expand_char = false,
                        bool expand_altrep = false,
                        bool expand_env = false) {
  std::map<SEXP, int> seen;
  Expand expand = {expand_altrep, expand_char, expand_env};

  return obj_inspect_(x, seen, expand);
}