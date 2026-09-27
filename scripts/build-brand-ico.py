"""Package native SVG renders into a Windows ICO without rescaling any frame.
Run build/lumashot_brand_icon_test.exe from the repo root first.
"""
from pathlib import Path
import struct
import sys
root=Path(__file__).resolve().parent.parent
frames=root/'build'/'brand-icon'
sizes=[16,20,24,28,32,40,48,64,96,128,256]
images=[(frames/f'{size}.png').read_bytes() for size in sizes]
header=struct.pack('<HHH',0,1,len(sizes));offset=6+16*len(sizes)
for size,data in zip(sizes,images):
    assert data[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',data[16:24])==(size,size)
    header+=struct.pack('<BBBBHHII',size%256,size%256,0,0,1,32,len(data),offset)
    offset+=len(data)
output=Path(sys.argv[1]) if len(sys.argv)>1 else root/'src'/'app'/'app.ico'
output.write_bytes(header+b''.join(images))
print(f'{output}: {output.stat().st_size} bytes, {len(sizes)} native-size SVG frames')
