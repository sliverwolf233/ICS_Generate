"""Generates res/app.ico (16x16 + 32x32, 32bpp BGRA, uncompressed BMP entries).

Pure stdlib: BMP payloads keep the build dependency-free, and two small sizes keep
the resource under 6 KB so it cannot dominate the executable.
"""
import os
import struct

BLUE = (0xD2, 0x78, 0x00, 0xFF)      # BGRA strong blue
BLUE_DARK = (0x9A, 0x4E, 0x00, 0xFF)
WHITE = (0xFC, 0xFC, 0xFD, 0xFF)
GREY = (0xB0, 0xB4, 0xBA, 0xFF)
RED = (0x3C, 0x3C, 0xE8, 0xFF)
CLEAR = (0, 0, 0, 0)


def inside_round_rect(x, y, l, t, r, b, rad):
    if x < l or x > r or y < t or y > b:
        return False
    if x < l + rad or x > r - rad:
        if y < t + rad or y > b - rad:
            for cx, cy in ((l + rad, t + rad), (r - rad, t + rad),
                           (l + rad, b - rad), (r - rad, b - rad)):
                if (x < l + rad and cx == l + rad) or (x > r - rad and cx == r - rad):
                    if (y < t + rad and cy == t + rad) or (y > b - rad and cy == b - rad):
                        if (x - cx) ** 2 + (y - cy) ** 2 > rad * rad:
                            return False
    return True


def make_canvas(size):
    """Calendar page: white body, blue header, two rings, day grid."""
    s = size
    px = [[CLEAR for _ in range(s)] for _ in range(s)]
    margin = max(1, s // 16)
    left, right = margin, s - margin
    top = max(2, s // 8)
    bottom = s - max(1, s // 12)
    radius = max(1, s // 10)
    header_h = max(2, (bottom - top) // 3 + 1)
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            if not inside_round_rect(x, y, left, top, right, bottom, radius):
                continue
            px[y][x] = BLUE if y - top < header_h else WHITE
    for x in range(left, right + 1):
        for y in range(max(top, bottom - 1), bottom + 1):
            if px[y][x][3]:
                px[y][x] = GREY
    ring_w = max(1, s // 16)
    ring_h = max(1, s // 10)
    for x in (left + max(1, (right - left) // 4),
              right - max(1, (right - left) // 4) - ring_w + 1):
        for y in range(max(0, top - ring_h), top + ring_h):
            for dx in range(ring_w):
                if 0 <= x + dx < s and 0 <= y < s:
                    px[y][x + dx] = RED
    grid_top = top + header_h + max(1, s // 12)
    grid_bottom = bottom - max(1, s // 10)
    cols = 3
    rows = 2 if s < 24 else 3
    cell = max(1, (right - left) // (cols + 1))
    dot = max(1, s // 14)
    for r in range(rows):
        for c in range(cols):
            if s < 24 and (r + c) % 2 == 1:
                continue
            cx = left + cell // 2 + c * cell + cell // 4
            span = max(1, grid_bottom - grid_top)
            cy = grid_top + (r * span) // max(1, rows)
            for dy in range(dot):
                for dx in range(dot):
                    x, y = cx + dx, cy + dy
                    if left < x < right and top + header_h < y < bottom:
                        px[y][x] = BLUE_DARK
    return px


def bmp_payload(px, size):
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         size * size * 4, 0, 0, 0, 0)
    body = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            body += bytes(px[y][x])
    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray()
    for y in range(size - 1, -1, -1):
        row = bytearray(mask_stride)
        for x in range(size):
            if px[y][x][3] < 128:
                row[x // 8] |= 0x80 >> (x % 8)
        mask += row
    return bytes(header) + bytes(body) + bytes(mask)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, os.pardir, "res", "app.ico")
    sizes = (16, 32)
    images = [bmp_payload(make_canvas(s), s) for s in sizes]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries, blobs = b"", b""
    for size, data in zip(sizes, images):
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        blobs += data
        offset += len(data)
    with open(out, "wb") as fh:
        fh.write(header + entries + blobs)
    print("wrote %s (%d bytes)" % (os.path.normpath(out), offset))


if __name__ == "__main__":
    main()
