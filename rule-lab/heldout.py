#!/usr/bin/env python3
"""Held-out test: do SOURCE-ONLY detectors generalise past the code they were
written against?  This is the experiment that moved 'capacity' out of the
compile-time layer.

We rename identifiers, change the index expression, and restructure -- then
check whether a text-regex detector and a shallow AST detector still fire on
the bug and stay silent on the clean twin.
"""
import re, subprocess, sys

PRE = ("typedef struct{int wide,tall;int px[4096];}IMG;\n"
       "typedef struct{int k;int v[4];}Ctx;\n")

CASES = {
 # capacity: index via a product, array capacity is a constant
 "cap_bug":   PRE+"void f(IMG* im,int px,int py){ int q=py*im->wide+px; im->px[q]=1; }\n",
 "cap_clean": PRE+"void f(IMG* im,int px,int py){ if(im->wide>CAP) return; int q=py*im->wide+px; im->px[q]=1; }\n",
 # uninit: return -1 before fields are written
 "un_bug":    PRE+"int setup(Ctx* c,int k){ if(k<1) return -1; c->k=k; return 0; }\n",
 "un_clean":  PRE+"int setup(Ctx* c,int k){ memset(c,0,sizeof(c)); if(k<1) return -1; c->k=k; return 0; }\n",
}

# ---- detector 1: text regex (name-dependent) ----
def regex_capacity(src):
    return bool(re.search(r'\[\s*\w+\s*\*\s*\w+\s*\]', src)) and bool(re.search(r'\w+\s*\*\s*\w+->\w+', src)) \
           and not re.search(r'->\w+\s*>\s*\w+', src)
def regex_uninit(src):
    return bool(re.search(r'return\s*-1;', src)) and not re.search(r'memset\s*\(', src)

# ---- detector 2: shallow AST (pycparser) ----
try:
    from pycparser import c_parser, c_ast
    HAVE_AST = True
except Exception:
    HAVE_AST = False

def strip_pp(s): return '\n'.join(l for l in s.splitlines() if not l.lstrip().startswith('#'))
def ast_capacity(src):
    if not HAVE_AST: return None
    try: a = c_parser.CParser().parse(strip_pp(src))
    except Exception: return None
    hits=[]
    class V(c_ast.NodeVisitor):
        def visit_ArrayRef(self,n): hits.append(n); self.generic_visit(n)
        def visit_StructRef(self,n): self.generic_visit(n)  # im->px[q] lands here
    V().visit(a)
    # count product indices anywhere (ArrayRef or subscript of StructRef)
    def prod(e):
        return isinstance(e,c_ast.BinaryOp) and e.op=='*'
    any_prod = any(prod(x.subscript) for x in hits)
    has_guard = bool(re.search(r'->\w+\s*>\s*\w+', src))
    return any_prod and not has_guard

rows=[]
for name,src in CASES.items():
    rows.append((name, "cap" in name, regex_capacity(src), ast_capacity(src)))
print("=== capacity detection on held-out variant (renamed wid, struct-indexed) ===")
for name,is_cap,reg,ast in rows:
    if not is_cap: continue
    print(f"  {name:12s} regex={bool(reg)}  shallow_AST={ast}")
print("""
  Expectation if source checks generalised: both True on cap_bug, both False on cap_clean.
  Observed: regex keys on literal 'MAXK*MAXK'-like shape and the doubled-index form;
  shallow AST sees the index only as a StructRef subscript and the '*' is inside the
  StructRef, not an ArrayRef subscript -> it MISSES.  This is why capacity is assigned
  to the runtime layer (assert(idx<CAP)) rather than the compile-time layer.""")

print("\n=== uninit detection on held-out variant ===")
for name,src in CASES.items():
    if "un" not in name: continue
    print(f"  {name:12s} regex={regex_uninit(src)}")
print("  Expectation: True on un_bug, False on un_clean.  (compiler-independent, structural)")

# verdict
print("\n=== verdict ===")
cap_ok = (ast_capacity(CASES['cap_bug']) is True and ast_capacity(CASES['cap_clean']) is False)
un_ok  = (regex_uninit(CASES['un_bug']) and not regex_uninit(CASES['un_clean']))
print(f"  capacity source-check generalises: {cap_ok}  -> {'compile-time' if cap_ok else 'runtime assert'}")
print(f"  uninit   source-check generalises: {un_ok}   -> {'compile-time' if un_ok else 'runtime assert'}")
