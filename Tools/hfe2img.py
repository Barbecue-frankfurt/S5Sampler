import struct,sys
def load(fn):
    d=open(fn,'rb').read(); nt=d[9]; lut=struct.unpack('<H',d[18:20])[0]
    tr=[]
    for c in range(nt):
        off,ln=struct.unpack('<HH',d[lut*512+4*c:lut*512+4*c+4]); raw=d[off*512:off*512+ln]
        sides=[bytearray(),bytearray()]
        for i in range(0,len(raw),512):
            blk=raw[i:i+512]; sides[0]+=blk[:256]; sides[1]+=blk[256:512]
        tr.append(sides)
    return tr
def bits(b):
    out=[]
    for x in b:
        for k in range(8): out.append((x>>k)&1)
    return out
def decode(bs):
    # find sync a1a1a1 : 0100010010001001 x3
    sync=[0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,1]
    n=len(bs); res=[]; i=0
    s3=sync*3
    secs={}
    while i<n-60:
        if bs[i:i+48]==s3:
            j=i+48
            def byte(j):
                v=0
                for k in range(8): v=(v<<1)|bs[j+2*k+1]
                return v
            mark=byte(j); j+=16
            if mark==0xFE:
                c,h,r,nn=[byte(j+16*q) for q in range(4)]; cur=(c,h,r,nn)
                i=j+16*6; continue
            if mark in(0xFB,0xF8) and 'cur' in dir():
                data=bytes(byte(j+16*q) for q in range(512))
                secs[cur[:3]]=data; i=j+16*514; continue
            i=j
        else: i+=1
    return secs
def image(fn):
    tr=load(fn); img=bytearray(); bad=0
    for c in range(80):
        for h in range(2):
            s=decode(bits(tr[c][h]))
            for r in range(1,10):
                if (c,h,r) in s: img+=s[(c,h,r)]
                else: img+=bytes(512); bad+=1
    return bytes(img),bad
if __name__=='__main__':
    for n in ['h1','h2']:
        im,bad=image('/tmp/w/%s.hfe'%n); open('/tmp/w/%s.img'%n,'wb').write(im); print(n,len(im),'missing sectors',bad,im[4:8])
