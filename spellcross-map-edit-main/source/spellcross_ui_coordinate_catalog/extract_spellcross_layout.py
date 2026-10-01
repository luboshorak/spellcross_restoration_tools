#!/usr/bin/env python3
"""Minimal Spellcross FS/QH/LST inventory extractor.
Reads FS index and exports QH x,y,w,h records and LST references. LZ decompression is intentionally not embedded; use the project's LZ decoder for decoded sizes.
"""
from pathlib import Path
import struct,re,csv,sys

def fs_entries(path):
 b=Path(path).read_bytes(); n=struct.unpack_from("<I",b,0)[0]; p=4
 for i in range(n):
  name=b[p:p+13].split(b"\0",1)[0].decode("latin1"); p+=13
  off,size=struct.unpack_from("<II",b,p);p+=8
  yield name,off,size,b[off:off+size]

def qh_records(data):
 lines=data.decode("latin1").replace("\r\n","\n").replace("\r","\n").split("\n")
 rx=re.compile(r"^\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*$")
 i=0
 while i<len(lines):
  m=rx.match(lines[i]); i+=1
  if not m: continue
  x,y,w,h=map(int,m.groups()); desc=[]
  while i<len(lines) and not rx.match(lines[i]) and not lines[i].lstrip().startswith(";"):
   if lines[i].strip(): desc.append(lines[i].strip())
   i+=1
  yield x,y,w,h," ".join(desc)

if __name__=="__main__":
 if len(sys.argv)<2: raise SystemExit("usage: extract_spellcross_layout.py COMMON.FS [INFO.FS]")
 for fs in sys.argv[1:]:
  for name,off,size,data in fs_entries(fs):
   if name.upper().endswith(".QH"):
    for n,(x,y,w,h,d) in enumerate(qh_records(data),1): print(f"QH,{Path(fs).name},{name},{n},{x},{y},{w},{h},{d}")
   elif name.upper().endswith(".LST"):
    for n,s in enumerate(data.decode("latin1").splitlines(),1):
     if s.strip(): print(f"LST,{Path(fs).name},{name},{n},{s.strip()}")
