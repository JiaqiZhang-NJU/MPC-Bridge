"""Synthetic sample-table and sidx fixtures; no internet or decoder required."""
import pathlib
import struct
import subprocess
from cache_integration import ROOT

def u32(*values): return struct.pack('>' + 'I'*len(values), *values)
def box(name, content): return u32(len(content)+8) + name.encode() + content
def full(name, content): return box(name, u32(0) + content)
def moov(offset, fragmented=False):
    mvhd = full('mvhd', u32(0,0,1000,6000) + bytes(80))
    tkhd = full('tkhd', u32(0,0,1,0,6000) + bytes(60))
    mdhd = full('mdhd', u32(0,0,1000,6000) + bytes(4))
    hdlr = full('hdlr', u32(0) + b'vide' + bytes(13))
    count = 0 if fragmented else 6
    tables = full('stsd',u32(0)) + full('stts',u32(1,count,1000)) + full('stsc',u32(1,1,max(1,count),1))
    tables += full('stsz',u32(16,count)) + full('stco',u32(1,offset)) + full('stss',u32(2,1,4))
    trak = box('trak', tkhd + box('mdia', mdhd + hdlr + box('minf',box('stbl',tables))))
    extra = box('mvex', full('trex',u32(1,1,1000,16,0))) if fragmented else b''
    return box('moov',mvhd + trak + extra)

def probe(path):
    out = subprocess.check_output([str(ROOT / '_bin/bridge/index-probe.exe'), str(path)], text=True)
    lines = out.splitlines(); return int(lines[0]), [line.split() for line in lines[1:]]

def main():
    folder = ROOT / '_bin/bridge/index-qa'; folder.mkdir(parents=True,exist_ok=True)
    ftyp = box('ftyp',b'isom'+u32(0)+b'isom')
    media = box('mdat',bytes(2*1024*1024))
    first = len(ftyp)+8
    path = folder / 'moov-tail.mp4'; path.write_bytes(ftyp+media+moov(first))
    count, tracks = probe(path)
    assert count==1 and int(tracks[0][0])==6 and int(tracks[0][1])==first and float(tracks[0][2])==6, (count,tracks)
    first = len(ftyp)+len(moov(0))+8
    path = folder / 'moov-front.mp4'; path.write_bytes(ftyp+moov(first)+media)
    count, tracks = probe(path); assert count==1 and int(tracks[0][1])==first
    header = ftyp+moov(0,True)
    # 3 independent SAP type 1 segments; fixture tests metadata, not encoded frame data.
    refs = b''.join(u32(1024,2000,0x90000000) for _ in range(3))
    sidx = full('sidx',u32(1,1000,0,0)+struct.pack('>HH',0,3)+refs)
    path = folder / 'indexed-fragments.mp4'; path.write_bytes(header+sidx+box('mdat',bytes(3072)))
    count, tracks = probe(path)
    assert count==1 and int(tracks[0][0])==3 and int(tracks[0][1])==len(header)+len(sidx) and float(tracks[0][2])==6, (count,tracks)
    bad = bytearray(sidx); bad[-4:]=u32(0) # last segment lacks independent SAP
    path = folder / 'unsafe-fragments.mp4'; path.write_bytes(header+bad+box('mdat',bytes(3072)))
    assert probe(path)[0]==0
    path=folder/'empty-sidx.mp4'; path.write_bytes(header+box('sidx',b''))
    assert probe(path)[0]==0
    excessive=moov(first).replace(full('stsz',u32(16,6)),full('stsz',u32(16,0xffffffff)))
    path=folder/'excessive-samples.mp4'; path.write_bytes(ftyp+excessive+media)
    assert probe(path)[0]==0
    path = folder / 'unindexed-fragments.mp4'; path.write_bytes(header+media)
    assert probe(path)[0]==0
    print('PASS: ordinary MP4 with moov at front/tail, exact sample offsets/time, complete SAP-indexed fragments, unsupported fragmented layouts')

if __name__=='__main__': main()
