import struct, sys, os, collections
for path in sys.argv[1:]:
    data = open(path, 'rb').read()
    n = struct.unpack('<I', data[80:84])[0]
    vols = 0.0; mn = [1e9]*3; mx = [-1e9]*3
    edges = collections.Counter()
    key = lambda v: (round(v[0], 4), round(v[1], 4), round(v[2], 4))
    for i in range(n):
        off = 84 + i*50
        f = struct.unpack('<12f', data[off:off+48])
        a, b, c = f[3:6], f[6:9], f[9:12]
        for v in (a, b, c):
            for k in range(3):
                mn[k] = min(mn[k], v[k]); mx[k] = max(mx[k], v[k])
        vols += (a[0]*(b[1]*c[2]-b[2]*c[1]) - a[1]*(b[0]*c[2]-b[2]*c[0]) + a[2]*(b[0]*c[1]-b[1]*c[0])) / 6.0
        ka, kb, kc = key(a), key(b), key(c)
        for e in ((ka, kb), (kb, kc), (kc, ka)):
            edges[tuple(sorted(e))] += 1
    bad = sum(1 for v in edges.values() if v != 2)
    print('%-28s tri %6d  dims %.1f x %.1f x %.1f mm  vol %.0f mm3  aristas no-manifold %d' % (
        os.path.basename(path), n, mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2], abs(vols), bad))
