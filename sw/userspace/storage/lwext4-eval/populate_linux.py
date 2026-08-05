import os, sys

root = sys.argv[1]

def pattern(index, n):
    return bytes(((index * 31) + (o * 7) + (o >> 8)) & 0xff for o in range(n))

os.makedirs(os.path.join(root, 'dir/nested'), exist_ok=True)
os.makedirs(os.path.join(root, 'many'), exist_ok=True)

def write(rel, index, size):
    with open(os.path.join(root, rel), 'wb') as f:
        f.write(pattern(index, size))

write('dir/renamed.txt', 1, 37)
write('dir/nested/big.bin', 2, 192 * 1024)
for i in range(200):
    if i == 7:
        continue
    write('many/entry_%03d.dat' % i, 100 + i, 64 + (i % 97))
print('linux populate ok')
