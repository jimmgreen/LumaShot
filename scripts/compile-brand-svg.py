"""Compile the deliberately small SVG branding subset to Direct2D resources.
Unsupported SVG features fail the build rather than silently changing the logo.
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

source, output = map(Path, sys.argv[1:3])
root = ET.parse(source).getroot()
assert root.attrib['viewBox'] == '0 0 1254 1254'
def tag(e): return e.tag.rsplit('}', 1)[-1]
def f(v): return format(float(v), '.6g') + ('f' if '.' in format(float(v), '.6g') else '.f')
def point(x,y): return '{'+f(x)+','+f(y)+'}'
def color(v):
    assert re.fullmatch(r'#[0-9a-fA-F]{6}',v), v
    return '0x'+v[1:]
lines=['// Generated from assets/icon.svg. Do not edit.']
refs={e.attrib['id']:e for e in root.iter() if 'id' in e.attrib}
for e in root.find('{*}defs'):
    if tag(e)=='path': continue
    assert tag(e)=='linearGradient' and e.attrib['gradientUnits']=='userSpaceOnUse'
    stops=','.join('{'+f(s.attrib.get('offset',0))+',D2D1::ColorF('+color(s.attrib['stop-color'])+','+f(s.attrib.get('stop-opacity',1))+')}' for s in e)
    lines.append('auto '+e.attrib['id']+'=gradient('+point(e.attrib['x1'],e.attrib['y1'])+','+point(e.attrib['x2'],e.attrib['y2'])+',{'+stops+'});')
def path(d):
    tokens=re.findall(r'[MLHVQCZ]|[-+]?(?:\d*\.\d+|\d+)',d)
    assert re.sub(r'[MLHVQCZ\s,]|[-+]?(?:\d*\.\d+|\d+)','',d)==''
    out=[];i=0;x=y=0
    while i<len(tokens):
        op=tokens[i];i+=1
        n={'M':2,'L':2,'H':1,'V':1,'Q':4,'C':6,'Z':0}[op]
        a=list(map(float,tokens[i:i+n]));i+=n
        if op=='M':x,y=a;out.append('s->BeginFigure('+point(x,y)+',D2D1_FIGURE_BEGIN_FILLED);')
        elif op in ('L','H','V'):
            if op=='L':x,y=a
            elif op=='H':x=a[0]
            else:y=a[0]
            out.append('s->AddLine('+point(x,y)+');')
        elif op=='C':out.append('s->AddBezier({'+','.join(point(*a[j:j+2]) for j in (0,2,4))+'});');x,y=a[-2:]
        elif op=='Q':out.append('s->AddQuadraticBezier({'+point(*a[:2])+','+point(*a[2:])+'});');x,y=a[-2:]
        else:out.append('s->EndFigure(D2D1_FIGURE_END_CLOSED);')
    return 'path([](ID2D1GeometrySink* s){'+''.join(out)+'})'
for e in root:
    t=tag(e)
    if t in ('title','defs'):continue
    if t=='rect':
        a=e.attrib;x=float(a['x']);y=float(a['y']);w=float(a['width']);h=float(a['height']);r=a['rx']
        shape='rounded(D2D1::RoundedRect({'+','.join(map(f,[x,y,x+w,y+h]))+'},'+f(r)+','+f(r)+'))'
    elif t=='path':shape=path(e.attrib['d'])
    elif t=='use':shape=path(refs[e.attrib['href'][1:]].attrib['d'])
    else:raise ValueError(t)
    fill=e.attrib['fill'];fill=fill[5:-1] if fill.startswith('url(#') else 'solid('+color(fill)+','+f(e.attrib.get('fill-opacity',1))+')'
    stroke='solid('+color(e.attrib['stroke'])+','+f(e.attrib.get('stroke-opacity',1))+')' if 'stroke' in e.attrib else '{}'
    lines.append('add('+shape+','+fill+','+stroke+','+f(e.attrib.get('stroke-width',0))+');')
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text('\n'.join(lines)+'\n',encoding='utf-8')

