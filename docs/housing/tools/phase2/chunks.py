"""Walk IFF chunks of retail WDT/ADT/WMO files and print key fields. Usage: chunks.py file..."""
import sys, struct
def chunks(d):
    o = 0
    while o + 8 <= len(d):
        tag = d[o:o+4][::-1].decode('latin1'); size = struct.unpack_from('<I', d, o+4)[0]
        yield tag, d[o+8:o+8+size]; o += 8 + size
for f in sys.argv[1:]:
    d = open(f, 'rb').read(); print('==', f.rsplit('/',1)[-1], len(d))
    for tag, b in chunks(d):
        extra = ''
        if tag == 'MVER': extra = struct.unpack_from('<I', b)[0]
        elif tag == 'MPHD': extra = 'flags=%#x fdids=%s' % (struct.unpack_from('<I', b)[0], struct.unpack_from('<7I', b, 4))
        elif tag == 'MAIN':
            tiles = [i for i in range(4096) if struct.unpack_from('<I', b, i*8)[0] & 1]
            extra = '%d tiles, x %d-%d y %d-%d' % (len(tiles), min(t%64 for t in tiles), max(t%64 for t in tiles), min(t//64 for t in tiles), max(t//64 for t in tiles)) if tiles else '0 tiles'
        elif tag == 'MAID':
            ent = [struct.unpack_from('<8I', b, i*32) for i in range(len(b)//32)]
            nz = [(i, e) for i, e in enumerate(ent) if any(e)]
            extra = '%d non-zero; first %s' % (len(nz), nz[:2])
        elif tag == 'MOHD':
            n = struct.unpack_from('<9I', b); extra = 'mat=%d grp=%d portals=%d lights=%d doodadNames=%d doodadDefs=%d sets=%d flags=%#x' % (n[0], n[1], n[2], n[3], n[4], n[5], n[6], struct.unpack_from('<H', b, 60)[0])
        elif tag == 'GFID': extra = list(struct.unpack_from('<%dI' % (len(b)//4), b))[:8]
        elif tag == 'MOMT': extra = '%d mats; tex fdids %s' % (len(b)//64, [struct.unpack_from('<3I', b, i*64+12)[0::1] for i in range(min(2, len(b)//64))])
        elif tag == 'MODI': extra = list(struct.unpack_from('<%dI' % (len(b)//4), b))[:6]
        elif tag == 'MODF': extra = '%d defs' % (len(b)//64)
        elif tag == 'MDDF': extra = '%d defs' % (len(b)//36)
        elif tag == 'MOGP': extra = 'flags=%#x subchunks=%s' % (struct.unpack_from('<I', b, 8)[0], [t for t, _ in chunks(b[68:])])
        print(' %s %7d %s' % (tag, len(b), extra))
