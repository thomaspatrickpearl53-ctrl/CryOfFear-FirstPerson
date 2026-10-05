"""Decode the largest mip of a Valve texture (.vtf) to an RGBA numpy array."""
import struct
import numpy as np

# image formats
RGBA8888, ABGR8888, RGB888, BGR888, RGB565, I8, IA88, P8, A8, RGB888_BLUESCREEN, BGR888_BLUESCREEN, \
    ARGB8888, BGRA8888, DXT1, DXT3, DXT5, BGRX8888, BGR565, BGRX5551, BGRA4444, DXT1_ONEBITALPHA, \
    BGRA5551, UV88, UVWQ8888, RGBA16161616F, RGBA16161616, UVLX8888 = range(27)


def _rgb565(c):
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return np.stack([r, g, b], -1).astype(np.int32)


def _dxt(data, w, h, kind):
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    n = bw * bh
    bsize = 8 if kind == DXT1 or kind == DXT1_ONEBITALPHA else 16
    blocks = np.frombuffer(data[:n * bsize], np.uint8).reshape(n, bsize)
    cb = blocks[:, -8:]
    c0 = cb[:, 0].astype(np.int32) | (cb[:, 1].astype(np.int32) << 8)
    c1 = cb[:, 2].astype(np.int32) | (cb[:, 3].astype(np.int32) << 8)
    p0, p1 = _rgb565(c0), _rgb565(c1)
    four = (c0 > c1) | (kind != DXT1 and kind != DXT1_ONEBITALPHA)
    p2 = np.where(four[:, None], (2 * p0 + p1) // 3, (p0 + p1) // 2)
    p3 = np.where(four[:, None], (p0 + 2 * p1) // 3, 0)
    pal = np.stack([p0, p1, p2, p3], 1)                                    # n,4,3
    bits = cb[:, 4].astype(np.uint32) | (cb[:, 5].astype(np.uint32) << 8) | \
        (cb[:, 6].astype(np.uint32) << 16) | (cb[:, 7].astype(np.uint32) << 24)
    idx = (bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))) & 3      # n,16
    rgb = np.take_along_axis(pal, idx[:, :, None].astype(np.int64).repeat(3, 2), 1)
    if kind == DXT1 or kind == DXT1_ONEBITALPHA:
        alpha = np.where((~four[:, None]) & (idx == 3), 0, 255)
    elif kind == DXT3:
        ab = blocks[:, :8]
        nib = np.stack([ab & 15, ab >> 4], -1).reshape(n, 16)
        alpha = nib.astype(np.int32) * 17
    else:
        a0 = blocks[:, 0].astype(np.int32)
        a1 = blocks[:, 1].astype(np.int32)
        ab = blocks[:, 2:8].astype(np.uint64)
        abits = sum(ab[:, i] << np.uint64(8 * i) for i in range(6))
        aidx = ((abits[:, None] >> (np.uint64(3) * np.arange(16, dtype=np.uint64))) & np.uint64(7)).astype(np.int32)
        eight = (a0 > a1)[:, None]
        k = aidx
        val8 = np.where(k == 0, a0[:, None], np.where(k == 1, a1[:, None],
                        ((8 - k) * a0[:, None] + (k - 1) * a1[:, None]) // 7))
        val6 = np.where(k == 0, a0[:, None], np.where(k == 1, a1[:, None], np.where(k == 6, 0, np.where(k == 7, 255,
                        ((6 - k) * a0[:, None] + (k - 1) * a1[:, None]) // 5))))
        alpha = np.where(eight, val8, val6)
    px = np.concatenate([rgb, alpha[:, :, None]], 2).astype(np.uint8)    # n,16,4
    img = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return img[:h, :w]


def _size(fmt, w, h):
    if fmt in (DXT1, DXT1_ONEBITALPHA):
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 8
    if fmt in (DXT3, DXT5):
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 16
    bpp = {RGBA8888: 4, ABGR8888: 4, RGB888: 3, BGR888: 3, RGB565: 2, I8: 1, IA88: 2, P8: 1, A8: 1,
           RGB888_BLUESCREEN: 3, BGR888_BLUESCREEN: 3, ARGB8888: 4, BGRA8888: 4, BGRX8888: 4, BGR565: 2,
           BGRX5551: 2, BGRA4444: 2, BGRA5551: 2, UV88: 2, UVWQ8888: 4, RGBA16161616F: 8, RGBA16161616: 8,
           UVLX8888: 4}.get(fmt)
    if bpp is None:
        raise ValueError("unsupported VTF format %d" % fmt)
    return w * h * bpp


def _raw(data, w, h, fmt):
    a = np.frombuffer(data[:_size(fmt, w, h)], np.uint8)
    if fmt in (DXT1, DXT1_ONEBITALPHA, DXT3, DXT5):
        return _dxt(data, w, h, fmt)
    if fmt == RGBA8888:
        return a.reshape(h, w, 4).copy()
    if fmt == ABGR8888:
        return a.reshape(h, w, 4)[:, :, [3, 2, 1, 0]].copy()
    if fmt == ARGB8888:
        return a.reshape(h, w, 4)[:, :, [1, 2, 3, 0]].copy()
    if fmt == BGRA8888:
        return a.reshape(h, w, 4)[:, :, [2, 1, 0, 3]].copy()
    if fmt == BGRX8888:
        img = a.reshape(h, w, 4)[:, :, [2, 1, 0, 3]].copy()
        img[:, :, 3] = 255
        return img
    if fmt in (RGB888, RGB888_BLUESCREEN, BGR888, BGR888_BLUESCREEN):
        img = a.reshape(h, w, 3)
        if fmt in (BGR888, BGR888_BLUESCREEN):
            img = img[:, :, ::-1]
        alpha = np.full((h, w, 1), 255, np.uint8)
        if fmt in (RGB888_BLUESCREEN, BGR888_BLUESCREEN):
            alpha[(img[:, :, 0] == 0) & (img[:, :, 1] == 0) & (img[:, :, 2] == 255)] = 0
        return np.concatenate([img, alpha], 2)
    if fmt == I8:
        g = a.reshape(h, w, 1)
        return np.concatenate([g, g, g, np.full_like(g, 255)], 2)
    if fmt == IA88:
        ia = a.reshape(h, w, 2)
        return np.concatenate([ia[:, :, :1]] * 3 + [ia[:, :, 1:]], 2)
    if fmt == A8:
        al = a.reshape(h, w, 1)
        return np.concatenate([np.full_like(al, 255)] * 3 + [al], 2)
    if fmt in (RGB565, BGR565):
        c = a.view("<u2").reshape(h, w).astype(np.int32)
        rgb = _rgb565(c)
        if fmt == BGR565:
            rgb = rgb[:, :, ::-1]
        return np.concatenate([rgb.astype(np.uint8), np.full((h, w, 1), 255, np.uint8)], 2)
    if fmt in (BGRA4444,):
        c = a.view("<u2").reshape(h, w).astype(np.int32)
        b, g, r, al = (c & 15) * 17, ((c >> 4) & 15) * 17, ((c >> 8) & 15) * 17, ((c >> 12) & 15) * 17
        return np.stack([r, g, b, al], -1).astype(np.uint8)
    if fmt in (BGRX5551, BGRA5551):
        c = a.view("<u2").reshape(h, w).astype(np.int32)
        b, g, r = (c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31
        al = np.where((c >> 15) & 1, 255, 0) if fmt == BGRA5551 else np.full_like(c, 255)
        return np.stack([r, g, b, al], -1).astype(np.uint8)
    if fmt in (RGBA16161616F,):
        f = np.frombuffer(data[:w * h * 8], "<f2").reshape(h, w, 4).astype(np.float32)
        f[:, :, :3] = f[:, :, :3] / (1 + f[:, :, :3])                    # simple tone map
        return (np.clip(f, 0, 1) * 255).astype(np.uint8)
    if fmt in (RGBA16161616,):
        return (np.frombuffer(data[:w * h * 8], "<u2").reshape(h, w, 4) >> 8).astype(np.uint8)
    raise ValueError("unsupported VTF format %d" % fmt)


def decode(data, frame=0):
    """Returns (rgba HxWx4 uint8 of the largest mip, flags)."""
    if data[:4] != b"VTF\0":
        raise ValueError("not a VTF")
    major, minor, header_size = struct.unpack_from("<III", data, 4)
    w, h, flags, frames, first_frame = struct.unpack_from("<HHIHH", data, 16)
    fmt = struct.unpack_from("<i", data, 52)[0]
    mips = data[56]
    lr_fmt = struct.unpack_from("<i", data, 57)[0]
    lr_w, lr_h = data[61], data[62]
    depth = struct.unpack_from("<H", data, 63)[0] if minor >= 2 else 1
    faces = 6 if flags & 0x4000 else 1                                      # TEXTUREFLAGS_ENVMAP
    if faces == 6 and minor < 5 and first_frame == 0xFFFF:
        faces = 7                                                          # old envmaps carry a sphere map
    hi_offset = None
    if minor >= 3:
        num_res = struct.unpack_from("<I", data, 68)[0]
        for i in range(num_res):
            tag, rflags, off = struct.unpack_from("<3sBI", data, 80 + i * 8)
            if tag == b"\x30\0\0":
                hi_offset = off
    if hi_offset is None:
        hi_offset = header_size + (_size(lr_fmt, lr_w, lr_h) if lr_fmt >= 0 and lr_w and lr_h else 0)
    # mips are stored smallest first; skip to the largest
    p = hi_offset
    for m in range(mips - 1, 0, -1):
        mw, mh = max(1, w >> m), max(1, h >> m)
        p += _size(fmt, mw, mh) * frames * faces * max(1, depth >> m)
    frame = min(frame, frames - 1)
    p += _size(fmt, w, h) * faces * depth * frame
    return _raw(data[p:], w, h, fmt), flags
