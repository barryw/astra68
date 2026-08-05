import os, sys

root = sys.argv[1]

def pattern(index, n):
    return bytes(((index * 31) + (o * 7) + (o >> 8)) & 0xff for o in range(n))

fails = []
def check(rel, index, size):
    p = os.path.join(root, rel)
    with open(p, 'rb') as f:
        data = f.read()
    if data != pattern(index, size):
        fails.append(rel)

check('dir/renamed.txt', 1, 37)
check('dir/nested/big.bin', 2, 192 * 1024)
for i in range(200):
    if i == 7:
        continue
    check('many/entry_%03d.dat' % i, 100 + i, 64 + (i % 97))

names = sorted(os.listdir(os.path.join(root, 'many')))
print('files_in_many=%d' % len(names))
print('mismatches=%d' % len(fails))
if fails:
    print('first:', fails[:5])
    sys.exit(1)
print('LINUX-MOUNT VERIFY OK')
