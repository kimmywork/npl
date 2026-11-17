import sys, re, pathlib
def tokenize(s):
    s = re.sub(r'#\|.*?\|#', ' ', s, flags=re.S)
    toks=[]; i=0; n=len(s)
    while i<n:
        c=s[i]
        if c in ' \t\r\n': i+=1
        elif c==';':
            while i<n and s[i]!='\n': i+=1
        elif c=='"':
            j=i+1
            while j<n and s[j]!='"': j+= 2 if s[j]=='\\' else 1
            toks.append('"str"'); i=j+1
        elif c in '()[]': toks.append('(' if c in '([' else ')'); i+=1
        elif s.startswith('#(',i): toks.append('('); i+=2
        elif c in "'`": i+=1
        elif s.startswith('#\\',i):
            j=i+3
            while j<n and s[j] not in ' \t\n()': j+=1
            toks.append('char'); i=j
        else:
            j=i
            while j<n and s[j] not in ' \t\r\n()[]";': j+=1
            toks.append(s[i:j]); i=j
    return toks
def parse(toks):
    stack=[[]]
    for t in toks:
        if t=='(': stack.append([])
        elif t==')':
            if len(stack)>1: x=stack.pop(); stack[-1].append(x)
        else: stack[-1].append(t)
    return stack[0]
def walk(x, f):
    if isinstance(x,list):
        f(x)
        for y in x: walk(y,f)
def check_pattern(p, hits, path):
    if not isinstance(p,list): return
    if p.count('...')>1: hits.append((path,p))
    for y in p: check_pattern(y,hits,path)
for path in sys.argv[1:]:
    try: tree=parse(tokenize(open(path,encoding='utf-8',errors='replace').read()))
    except Exception as e: continue
    hits=[]
    def f(x):
        if x and x[0]=='syntax-rules':
            rules = x[2:] if len(x)>1 and isinstance(x[1],list) else x[3:]
            for r in rules:
                if isinstance(r,list) and r: check_pattern(r[0],hits,path)
    walk(tree,f)
    for h in hits: print(h[0], ' '.join(str(e) if not isinstance(e,list) else '(..)' for e in h[1])[:120])
