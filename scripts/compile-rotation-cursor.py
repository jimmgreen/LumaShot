"""Build a multi-resolution Windows cursor resource (stdlib only).
32-DIP canvas matches standard cursors; artwork is 24 DIP, with a centered hotspot.
"""
import math
import pathlib
import struct
import sys


def distance(p, a, b):
    dx, dy = b[0]-a[0], b[1]-a[1]
    t = max(0, min(1, ((p[0]-a[0])*dx+(p[1]-a[1])*dy)/(dx*dx+dy*dy)))
    return math.hypot(p[0]-a[0]-t*dx, p[1]-a[1]-t*dy)


def cross(p, a, b):
    return (p[0]-a[0])*(b[1]-a[1])-(p[1]-a[1])*(b[0]-a[0])


def raster(size):
    first, last = math.radians(50), math.radians(310)
    a = (16+9*math.cos(first), 16+9*math.sin(first))
    b = (16+9*math.cos(last), 16+9*math.sin(last))
    t = (-math.sin(last), math.cos(last))
    n = (-t[1], t[0])
    tip = tuple(b[i]+3.4*t[i] for i in range(2))
    left = tuple(b[i]-2.4*t[i]+3.6*n[i] for i in range(2))
    right = tuple(b[i]-2.4*t[i]-3.6*n[i] for i in range(2))
    stride = ((size+31)//32)*4
    mask = bytearray([255])*(stride*size)
    pixels = bytearray()
    for y in reversed(range(size)):
        for x in range(size):
            coverage = white = 0
            for sy in range(4):
                for sx in range(4):
                    p = (16+((x+(sx+.5)/4)*32/size-16)/.75,
                         16+((y+(sy+.5)/4)*32/size-16)/.75)
                    radius = math.hypot(p[0]-16, p[1]-16)
                    angle = math.atan2(p[1]-16, p[0]-16) % (2*math.pi)
                    arc = abs(radius-9) if first <= angle <= last else min(math.dist(p,a), math.dist(p,b))
                    c = (cross(p,tip,left), cross(p,left,right), cross(p,right,tip))
                    inside = min(c)>=0 or max(c)<=0
                    triangle = 0 if inside else min(distance(p,tip,left),distance(p,left,right),distance(p,right,tip))
                    if arc<=2.0 or triangle<=.95 or radius<=1.15:
                        coverage += 1
                        white += int(arc<=.95 or inside or radius<=.4)
            alpha = (coverage*255+8)//16
            # CUR DIB stores straight alpha, not premultiplied color.
            ink = round(white*255/coverage) if coverage else 0
            pixels.extend((ink,ink,ink,alpha))
            if alpha:
                mask[(size-1-y)*stride+x//8] &= ~(128>>(x%8))
    header = struct.pack('<IiiHHIIiiII',40,size,size*2,1,32,0,len(pixels)+len(mask),0,0,0,0)
    return header+pixels+mask


def main():
    dest = pathlib.Path(sys.argv[1])
    dest.mkdir(parents=True,exist_ok=True)
    sizes = (32,40,48,64,96,128)
    blobs = [raster(s) for s in sizes]
    offset = 6+16*len(sizes)
    data = bytearray(struct.pack('<HHH',0,2,len(sizes)))
    for size, blob in zip(sizes,blobs):
        data.extend(struct.pack('<BBBBHHII',size,size,0,0,size//2,size//2,len(blob),offset))
        offset += len(blob)
    path = dest/'rotation.cur'
    path.write_bytes(data+b''.join(blobs))
    (dest/'rotation_cursor.rc').write_text('4201 CURSOR "'+path.resolve().as_posix()+'"\n',encoding='utf-8')


if __name__ == '__main__':
    main()
