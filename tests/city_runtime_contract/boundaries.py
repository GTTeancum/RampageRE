"""Small source-location parser used to place standard-C statement boundaries.

It does not interpret expressions or change their evaluation order. Every edited
statement and enclosing function is recorded for independent review.
"""
import re

def mask(s):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|^[ \t]*#(?:[^\n]*\\\n)*[^\n]*',
                  lambda m: ''.join('\n' if c=='\n' else ' ' for c in m[0]),s,flags=re.S|re.M)

def functions(s):
    text=mask(s)
    pattern=re.compile(r'(?m)^(?:static\s+)?([\w *]+?)\b(\w+)\s*\(([^;{}]*)\)\s*\{')
    for m in pattern.finditer(text):
        if m[2] in ['if','for','while','switch'] or '->' in m[3] or '?' in m[3]: continue
        j=m.end();depth=1
        while depth and j<len(text): depth+=(text[j]=='{')-(text[j]=='}');j+=1
        yield {'name':m[2], 'ret':m[1].strip(),'args':m[3], 'begin':m.start(), 'body':m.end()-1, 'end':j,
               'calls':set(re.findall(r'\b(\w+)\s*\(',text[m.end():j]))}

def instrument(s,f,hazards,guard):
    """Guard after hazardous full expressions, including branch/loop predicates."""
    txt=mask(s)
    toks=[(m[0],m.start(),m.end()) for m in re.finditer(r'\w+|[^\s]',txt[f['body']:f['end']])]
    toks=[(x,a+f['body'],b+f['body']) for x,a,b in toks]
    edits=[]; records=[]
    def danger(a,b):
        return bool(set(re.findall(r'\b(\w+)\s*\(',txt[a:b])) & hazards)
    def paired(i,l='(',r=')'):
        assert toks[i][0]==l,(f['name'],toks[i],l)
        depth=1;j=i+1
        while depth:
            depth+=(toks[j][0]==l)-(toks[j][0]==r);j+=1
        return j
    def check_after(a,b,parent_block):
        if parent_block:
            edits.append((b,'\n    '+guard))
        else:
            edits.append((a,'{ '));edits.append((b,' '+guard+' }'))
        records.append((a,b))
    def at_entry(a,b):
        if s[a]=='{': edits.append((a+1,'\n    '+guard))
        else: edits.append((a,'{ '+guard+' '));edits.append((b,' }'))
    def statement(i,parent_block=True):
        start=i;word=toks[i][0]
        if word=='{':
            i+=1
            while toks[i][0]!='}': i=statement(i)
            return i+1
        if word in ('case','default'):
            while toks[i][0]!=':': i+=1
            return i+1
        if i+1<len(toks) and toks[i+1][0]==':':return i+2
        if word in ('if','for','while','switch'):
            endcond=paired(i+1);bad=danger(toks[i+1][1],toks[endcond-1][2])
            a=toks[endcond][1];end=statement(endcond,False)
            if bad and word!='switch':at_entry(a,toks[end-1][2])
            if word=='if' and end<len(toks) and toks[end][0]=='else':
                a=toks[end+1][1];end=statement(end+1,False)
                if bad:at_entry(a,toks[end-1][2])
            if bad:check_after(toks[start][1],toks[end-1][2],parent_block)
            return end
        if word=='do':
            endbody=statement(i+1,False)
            assert toks[endbody][0]=='while'
            end=paired(endbody+1)+1
            if danger(toks[endbody][1],toks[end-1][2]):
                at_entry(toks[i+1][1],toks[endbody-1][2]);check_after(toks[start][1],toks[end-1][2],parent_block)
            return end
        # A declaration or expression, including initializer compound literals.
        while toks[i][0]!=';':
            if toks[i][0]=='(':i=paired(i)
            elif toks[i][0]=='{':i=paired(i,'{','}')
            elif toks[i][0]=='[':i=paired(i,'[',']')
            else:i+=1
        end=i+1
        if word!='return' and danger(toks[start][1],toks[i][2]):
            check_after(toks[start][1],toks[i][2],parent_block)
        return end
    statement(0)
    return edits,records
