/*
 * qsort for the freestanding userspace runtime.
 *
 * lwext4's directory index calls qsort when it splits an htree node, so the
 * runtime owes it a real implementation. This is heapsort, not quicksort: it
 * is in place, needs no recursion and therefore no unbounded stack, and its
 * worst case is O(n log n) rather than O(n^2).
 *
 * That choice is about the input, not about elegance. The array being sorted
 * is directory entries read from a volume Astra did not create, so its
 * ordering is attacker-influenced. Quicksort's worst case is reachable by
 * choosing the input; heapsort's is not. A 4 KiB block holds a few hundred
 * sort entries, and O(n^2) on a 16 MHz MC68030 is a visible stall on every
 * directory split.
 */

#include <stddef.h>

static void
swap_bytes(unsigned char *left, unsigned char *right, size_t size)
{
    while (size-- != 0u) {
        unsigned char held = *left;

        *left++ = *right;
        *right++ = held;
    }
}

/*
 * Restores the heap property for the subtree rooted at `root`, over the first
 * `count` elements. Iterative so the stack cost is constant.
 */
static void
sift_down(unsigned char *base, size_t size, size_t root, size_t count,
          int (*compare)(const void *, const void *))
{
    for (;;) {
        size_t child = (2u * root) + 1u;
        size_t largest;

        if (child >= count) {
            return;
        }
        largest = root;
        if (compare(base + (child * size), base + (largest * size)) > 0) {
            largest = child;
        }
        if (child + 1u < count &&
            compare(base + ((child + 1u) * size), base + (largest * size)) >
                0) {
            largest = child + 1u;
        }
        if (largest == root) {
            return;
        }
        swap_bytes(base + (root * size), base + (largest * size), size);
        root = largest;
    }
}

void
qsort(void *array, size_t count, size_t size,
      int (*compare)(const void *, const void *))
{
    unsigned char *base = array;
    size_t index;

    if (base == NULL || compare == NULL || size == 0u || count < 2u) {
        return;
    }

    /* Build the max-heap from the last parent downwards. */
    index = count / 2u;
    while (index-- != 0u) {
        sift_down(base, size, index, count, compare);
    }

    /* Repeatedly move the largest element to the end of the live region. */
    index = count;
    while (index-- > 1u) {
        swap_bytes(base, base + (index * size), size);
        sift_down(base, size, 0u, index, compare);
    }
}
