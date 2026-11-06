"""Create deterministic, non-clinical PDF upload fixtures; never read user files."""
import argparse,hashlib,json
from pathlib import Path

def pdf(mebibytes):
 width=1024;height=mebibytes*1024
 image=bytes(range(256))*(width*height//256)
 content=b'q 512 0 0 640 40 80 cm /TestImage Do Q\n'
 objects=[b'<< /Type /Catalog /Pages 2 0 R >>',b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /XObject << /TestImage 5 0 R >> >> /Contents 4 0 R >>',b'<< /Length '+str(len(content)).encode()+b' >>\nstream\n'+content+b'endstream',f'<< /Type /XObject /Subtype /Image /Width {width} /Height {height} /ColorSpace /DeviceGray /BitsPerComponent 8 /Length {len(image)} >>\nstream\n'.encode()+image+b'\nendstream']
 data=bytearray(b'%PDF-1.4\n% Cloud Stick synthetic QA upload fixture; no clinical data\n');offsets=[0]
 for i,obj in enumerate(objects,1):offsets.append(len(data));data+=f'{i} 0 obj\n'.encode()+obj+b'\nendobj\n'
 xref=len(data);data+=f'xref\n0 {len(offsets)}\n0000000000 65535 f \n'.encode()
 for offset in offsets[1:]:data+=f'{offset:010d} 00000 n \n'.encode()
 data+=f'trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
 return bytes(data)
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('output',type=Path);parser.add_argument('--mib',type=int,choices=[2,8,32,64],default=2);args=parser.parse_args()
 data=pdf(args.mib);args.output.write_bytes(data);print(json.dumps(dict(file=args.output.name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())))
