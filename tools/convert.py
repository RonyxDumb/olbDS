#!/usr/bin/env python3
"""Winner Winner Chicken Dinner DS asset converter.

Usage:  python3 tools/convert.py <path to the mod folder (1WWCD)> <path to a CodenameEngine v1.0.1 checkout> [--no-audio]

Reads the original Codename Engine mod assets and writes:
  nitrofiles/...          binary data loaded at runtime from NitroFS
  maxmod_data/*.wav       sound effects (built into a maxmod soundbank)
  source/gen_assets.c     generated metadata tables
  include/gen_assets.h
"""
import json
import math
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw, ImageFont, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *  # noqa

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
MOD = ARGS[0] if len(ARGS) > 0 else '/home/user/mod/1WWCD'
CNE = ARGS[1] if len(ARGS) > 1 else '/home/user/cne'
IMG = os.path.join(MOD, 'images')
CA = os.path.join(CNE, 'assets')
OUT = os.path.join(ROOT, 'nitrofiles')

C_OUT = []
H_OUT = []


def outpath(*p):
    path = os.path.join(OUT, *p)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    return path


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def cfloat(v):
    return repr(float(v)) + 'f'


def cid(s):
    return re.sub(r'[^A-Za-z0-9]', '_', s).upper()


def img(path):
    return Image.open(os.path.join(IMG, path)).convert('RGBA')


def scaled(im, s):
    w = max(1, int(round(im.width * s)))
    h = max(1, int(round(im.height * s)))
    return im.resize((w, h), Image.LANCZOS)


def trim(im, thr=8):
    bb = im.getchannel('A').point(lambda a: 255 if a >= thr else 0).getbbox()
    if bb is None:
        return im.crop((0, 0, 1, 1)), (0, 0)
    return im.crop(bb), (bb[0], bb[1])


# ---------------------------------------------------------------------------
# Sheets: groups of textures + frames made of textured parts (see gfx.c).
# Frame origin = (0,0); parts are placed at (ox, oy) texels from it.
# ---------------------------------------------------------------------------
class Sheet:
    def __init__(self, name, scale, fmt=FMT_256COL, colors=255, dither=False, atlas_w=256):
        self.name = name
        self.scale = scale          # texels per drawn unit
        self.fmt = fmt
        self.ncolors = colors
        self.dither = dither
        self.atlas_w = atlas_w
        self.items = []

    def add_tiled(self, fname, im, ox=0, oy=0):
        self.items.append((fname, 'tiled', im, (ox, oy)))

    def add_packed(self, fname, im, ox=0, oy=0):
        self.items.append((fname, 'packed', im, (ox, oy)))

    def build(self):
        imgs = [it[2] for it in self.items]
        if self.fmt == FMT_256COL:
            palimg, colors = build_palette(imgs, self.ncolors)
        else:
            palimg, colors = build_palette(imgs, 15)
        textures = []
        frames = []
        parts = []
        atlas = None
        packed = [it for it in self.items if it[1] == 'packed']
        # pack tall images first for a tighter atlas, but keep frame order
        order = sorted(range(len(self.items)), key=lambda i: -self.items[i][2].height if self.items[i][1] == 'packed' else 0)
        placed = {}
        for i in order:
            fname, kind, im, (ox, oy) = self.items[i]
            if kind != 'packed':
                continue
            w, h = im.size
            if atlas is None or atlas['x'] + w > self.atlas_w:
                if atlas is not None:
                    atlas['y'] += atlas['rowh'] + 1
                    atlas['x'] = 0
                    atlas['rowh'] = 0
            if atlas is None or atlas['y'] + h > 256:
                atlas = {'img': Image.new('RGBA', (self.atlas_w, 256), (0, 0, 0, 0)), 'x': 0, 'y': 0, 'rowh': 0, 'used_h': 0}
                textures.append(('atlas', atlas))
                atlas['tex'] = len(textures) - 1
            atlas['img'].paste(im, (atlas['x'], atlas['y']))
            placed[i] = (atlas['tex'], atlas['x'], atlas['y'], w, h, ox, oy)
            atlas['x'] += w + 1
            atlas['rowh'] = max(atlas['rowh'], h)
            atlas['used_h'] = max(atlas['used_h'], atlas['y'] + h)
        for i, (fname, kind, im, (ox, oy)) in enumerate(self.items):
            if kind == 'tiled':
                w, h = im.size
                first = len(parts)
                ys = 0
                for th in pow2_pieces(h):
                    xs = 0
                    for tw in pow2_pieces(w):
                        tile = Image.new('RGBA', (tw, th), (0, 0, 0, 0))
                        tile.paste(im.crop((xs, ys, min(xs + tw, w), min(ys + th, h))), (0, 0))
                        cw = min(tw, w - xs)
                        ch = min(th, h - ys)
                        if tile.getchannel('A').getbbox() is not None:
                            textures.append(('img', tile))
                            parts.append((len(textures) - 1, 0, 0, cw, ch, ox + xs, oy + ys))
                        xs += tw
                    ys += th
                frames.append((fname, first, len(parts) - first))
            else:
                parts.append(placed[i])
                frames.append((fname, len(parts) - 1, 1))
        blob = bytearray(pal_bytes(colors, 256 if self.fmt == FMT_256COL else 16))
        texdefs = []
        for kind, obj in textures:
            if kind == 'atlas':
                hh = 8
                while hh < obj['used_h']:
                    hh *= 2
                ww = self.atlas_w
                # shrink the atlas width when possible
                maxx = obj['img'].getchannel('A').getbbox()
                if maxx:
                    while ww > 8 and ww // 2 >= maxx[2]:
                        ww //= 2
                timg = obj['img'].crop((0, 0, ww, hh))
            else:
                timg = obj
            tw, th = timg.size
            idx = index_image(timg, palimg, dither=self.dither)
            if self.fmt == FMT_16COL:
                pk = bytearray(len(idx) // 2)
                for k in range(0, len(idx), 2):
                    pk[k // 2] = (idx[k] & 15) | ((idx[k + 1] & 15) << 4)
                idx = pk
            off = len(blob)
            blob += idx
            while len(blob) % 4:
                blob.append(0)
            texdefs.append((self.fmt, log2size(tw), log2size(th), off, len(idx)))
        fn = 'gfx/%s.bin' % self.name
        with open(outpath(fn), 'wb') as f:
            f.write(blob)
        n = self.name
        C_OUT.append('static const TexDef %s_tex[] = {%s};' % (n, ','.join('{%d,%d,%d,0,%d,%d}' % t for t in texdefs)))
        C_OUT.append('static const PartDef %s_parts[] = {%s};' % (n, ','.join('{%d,0,%d,%d,%d,%d,%d,%d}' % p for p in parts)))
        C_OUT.append('static const FrameDef %s_frames[] = {%s};' % (n, ','.join('{%d,%d}' % (f[1], f[2]) for f in frames)))
        C_OUT.append('const SheetDef sheet_%s = {%s, %s, %d, %d, %d, 0, %s_tex, %s_frames, %s_parts};' % (
            n, cstr('/' + fn), cfloat(self.scale), len(texdefs), len(frames),
            256 if self.fmt == FMT_256COL else 16, n, n, n))
        H_OUT.append('extern const SheetDef sheet_%s;' % n)
        for i, f in enumerate(frames):
            H_OUT.append('#define %s_%s %d' % (n.upper(), cid(f[0]), i))
        self.vram = sum(t[4] for t in texdefs)
        print('sheet %-12s %3d tex %3d frames %7d bytes vram' % (n, len(texdefs), len(frames), self.vram))
        return self


# ---------------------------------------------------------------------------
# Flixel sprite math
#   world(p) = (p - origin - frameOffset) * scale + origin + (x - offset)
# (p = point in the untrimmed source box of the frame, origin = center of the
# first frame's source box)
# ---------------------------------------------------------------------------
def sprite_box(fw, fh, sx, sy, x=0, y=0):
    """Top-left world position and size of a full (untrimmed) image drawn
    with scale (sx, sy) around its center (no updateHitbox)."""
    ox, oy = fw / 2.0, fh / 2.0
    return x + ox - ox * sx, y + oy - oy * sy, fw * sx, fh * sy


# ---------------------------------------------------------------------------
# Static stage layers: one sheet per layer. LayerDef holds world position.
# ---------------------------------------------------------------------------
LAYERS = []


def add_layer(sheetname, im, wx, wy, ww, wh, sfx, sfy, tex_scale, fmt=FMT_256COL, colors=255, thr=8, post=None):
    """im drawn at world (wx, wy) with world size (ww, wh). tex_scale =
    texels per world unit."""
    kx = ww / im.width
    ky = wh / im.height
    t, (bx, by) = trim(im, thr)
    tw = max(1, int(round(t.width * kx * tex_scale)))
    th = max(1, int(round(t.height * ky * tex_scale)))
    ts = t.resize((tw, th), Image.LANCZOS)
    if post:
        ts = post(ts)
    sh = Sheet(sheetname, tex_scale, fmt=fmt, colors=colors)
    sh.add_tiled('main', ts, 0, 0)
    sh.build()
    LAYERS.append((sheetname, wx + bx * kx, wy + by * ky, sfx, sfy))
    return sh


def emit_layers():
    H_OUT.append('typedef struct { const SheetDef *sheet; float x, y, sfx, sfy; } LayerDef;')
    for n, x, y, sfx, sfy in LAYERS:
        C_OUT.append('const LayerDef layer_%s = {&sheet_%s, %s, %s, %s, %s};' % (n, n, cfloat(x), cfloat(y), cfloat(sfx), cfloat(sfy)))
        H_OUT.append('extern const LayerDef layer_%s;' % n)


def stage_xml(name):
    return ET.parse(os.path.join(MOD, 'data/stages', name + '.xml')).getroot()


def node_scale(a):
    s = float(a.get('scale', 1))
    return float(a.get('scalex', s)), float(a.get('scaley', s))


def convert_youtubeland():
    r = stage_xml('youtubeland')
    folder = r.attrib['folder']
    # texels per world unit chosen per layer (VRAM budget ~300 KB)
    TS = {'Sky': 0.05, 'Clouds': 0.085, 'CityLeft': 0.13, 'CityRight': 0.13, 'Road': 0.09,
          'Buildings': 0.105, 'maxshadow': 0.12, 'nugshadow': 0.12, 'MDP': 0.16,
          'TreeRight': 0.09, 'TreeLeft': 0.09}
    for n in r.findall('sprite'):
        a = n.attrib
        im = img(folder + a['sprite'] + '.png')
        sx, sy = node_scale(a)
        x, y = float(a['x']), float(a['y'])
        sc = float(a.get('scroll', 1))
        sfx, sfy = float(a.get('scrollx', sc)), float(a.get('scrolly', sc))
        ang = float(a.get('angle', 0))
        wx, wy, ww, wh = sprite_box(im.width, im.height, sx, sy, x, y)
        if ang:
            # bake the rotation (around the center) into the image
            big = im.resize((max(1, int(im.width * sx)), max(1, int(im.height * sy))), Image.LANCZOS)
            rot = big.rotate(-ang, resample=Image.BICUBIC, expand=True)
            cx, cy = wx + ww / 2, wy + wh / 2
            im = rot
            ww, wh = rot.width, rot.height
            wx, wy = cx - ww / 2, cy - wh / 2
        nm = 'yt_' + a['name'].lower()
        add_layer(nm, im, wx, wy, ww, wh, sfx, sfy, TS[a['name']], fmt=FMT_16COL if 'shadow' in nm else FMT_256COL,
                  post=deepfry if a['name'] == 'MDP' else None)
    pos = {}
    for k in ('dad', 'boyfriend'):
        a = r.find(k).attrib
        pos[k] = (float(a['x']), float(a['y']), float(a.get('scale', 1)), float(a.get('scrollx', a.get('scroll', 1))),
                  float(a.get('scrolly', a.get('scroll', 1))))
    H_OUT.append('#define YT_ZOOM %s' % cfloat(r.attrib['zoom']))
    H_OUT.append('#define YT_CAMX %s' % cfloat(r.attrib['startCamPosX']))
    H_OUT.append('#define YT_CAMY %s' % cfloat(r.attrib['startCamPosY']))
    return pos


def convert_abbey():
    r = stage_xml('Abbey')
    folder = r.attrib['folder']
    TS = {'bg': 0.1, 'back': 0.022, 'face': 0.06, 'signs': 0.065, 'iloveyou': 0.05, 'ground': 0.065}
    FMT = {'back': FMT_16COL}
    anims = {}
    for n in r.findall('sprite'):
        a = n.attrib
        sx, sy = node_scale(a)
        x, y = float(a['x']), float(a['y'])
        sc = float(a.get('scroll', 1))
        name = a['name']
        base = os.path.join(IMG, folder + a['sprite'])
        if os.path.exists(base + '.xml'):
            # animated stage sprite: idle = every frame of the atlas, 24 fps loop
            im, frames = parse_sparrow(base + '.xml', base + '.png')
            f0 = frames[0]
            ox, oy = f0['fw'] / 2.0, f0['fh'] / 2.0
            ts = TS[name]
            sh = Sheet('ab_' + name, ts, fmt=FMT.get(name, FMT_256COL))
            seen = {}
            ids = []
            for f in frames:
                key = (f['x'], f['y'], f['w'], f['h'], f['fx'], f['fy'])
                if key not in seen:
                    seen[key] = len(seen)
                    c = crop_frame(im, f)
                    # world top-left of the trimmed image (relative to x, y)
                    dx = ox + (-f['fx'] - ox) * sx
                    dy = oy + (-f['fy'] - oy) * sy
                    cs = c.resize((max(1, int(round(c.width * sx * ts))), max(1, int(round(c.height * sy * ts)))), Image.LANCZOS)
                    sh.add_packed('f%d' % seen[key], cs, int(round(dx * ts)), int(round(dy * ts)))
                ids.append(seen[key])
            sh.build()
            anims[name] = ids
            LAYERS.append(('ab_' + name, x, y, sc, sc))
        else:
            im = img(folder + a['sprite'] + '.png')
            wx, wy, ww, wh = sprite_box(im.width, im.height, sx, sy, x, y)
            add_layer('ab_' + name, im, wx, wy, ww, wh, sc, sc, TS[name])
    for name, ids in anims.items():
        C_OUT.append('const u8 ab_%s_seq[] = {%s};' % (name, ','.join(map(str, ids))))
        H_OUT.append('extern const u8 ab_%s_seq[];' % name)
        H_OUT.append('#define AB_%s_LEN %d' % (name.upper(), len(ids)))
    b = r.find('box').attrib
    H_OUT.append('#define AB_BOX_X %s' % cfloat(b['x']))
    H_OUT.append('#define AB_BOX_Y %s' % cfloat(b['y']))
    H_OUT.append('#define AB_BOX_W %s' % cfloat(b['width']))
    H_OUT.append('#define AB_BOX_H %s' % cfloat(b['height']))
    H_OUT.append('#define AB_ZOOM %s' % cfloat(r.attrib['zoom']))
    pos = {}
    for k in ('dad', 'boyfriend'):
        a = r.find(k).attrib
        pos[k] = (float(a['x']), float(a['y']), 1.0, 1.0, 1.0)
    return pos


# ---------------------------------------------------------------------------
# Animated sprites / characters (streamed to a VRAM slot frame by frame)
# ---------------------------------------------------------------------------
CHRS = []          # (cname)


def build_animsprite(cname, xmlpath, pngpath, anims, scale, tex_scale, flip=False, gx=0.0, gy=0.0,
                     camx=0.0, camy=0.0, hold=4.0, is_player=False, icon=0, colors=255, post=None):
    """anims: list of (name, prefix, fps, loop, offx, offy, indices)."""
    im, frames = parse_sparrow(xmlpath, pngpath)
    f0 = frames[0]
    ox, oy = f0['fw'] / 2.0, f0['fh'] / 2.0
    uniq = {}
    uframes = []
    adefs = []
    for (an, prefix, fps, loop, aox, aoy, indices) in anims:
        sel = frames_by_prefix(frames, prefix)
        if indices:
            sel = [sel[i] for i in indices if i < len(sel)]
        if not sel:
            print('  warning: %s has no frames for %s (%s)' % (cname, an, prefix))
            continue
        ids = []
        for f in sel:
            key = (f['x'], f['y'], f['w'], f['h'], f['fx'], f['fy'], f['rot'], f['fw'])
            if key not in uniq:
                uniq[key] = len(uframes)
                uframes.append(f)
            ids.append(uniq[key])
        adefs.append((an, fps, loop, -aox * scale, -aoy * scale, ids))
    crops = []
    meta = []
    for f in uframes:
        c = crop_frame(im, f)
        sx0, sy0 = -f['fx'], -f['fy']
        if flip:
            c = c.transpose(Image.FLIP_LEFT_RIGHT)
            sx0 = f['fw'] - (sx0 + c.width)
        cs = c.resize((max(1, int(round(c.width * scale * tex_scale))), max(1, int(round(c.height * scale * tex_scale)))), Image.LANCZOS)
        if post:
            cs = post(cs)
        dx = ox + (sx0 - ox) * scale
        dy = oy + (sy0 - oy) * scale
        crops.append(cs)
        meta.append((dx, dy, c.width * scale, c.height * scale))
    palimg, colors_ = build_palette(crops, colors)
    blob = bytearray(pal_bytes(colors_, 256))
    cframes = []
    maxw = maxh = 0
    for cs, (dx, dy, ww, wh) in zip(crops, meta):
        w4 = (cs.width + 3) & ~3
        pad = Image.new('RGBA', (w4, cs.height), (0, 0, 0, 0))
        pad.paste(cs, (0, 0))
        idx = index_image(pad, palimg)
        cframes.append((len(blob), w4, cs.height, cs.width, dx, dy, ww, wh))
        blob += idx
        maxw = max(maxw, w4)
        maxh = max(maxh, cs.height)
    fn = 'chr/%s.bin' % cname
    with open(outpath(fn), 'wb') as fo:
        fo.write(blob)
    n = 'chr_' + cname
    C_OUT.append('static const ChrFrame %s_frames[] = {%s};' % (n, ','.join(
        '{%d,%d,%d,%d,0,%s,%s,%s,%s}' % (o, w4, h, cw, cfloat(dx), cfloat(dy), cfloat(ww), cfloat(wh))
        for (o, w4, h, cw, dx, dy, ww, wh) in cframes)))
    for ai, a in enumerate(adefs):
        C_OUT.append('static const u16 %s_a%d[] = {%s};' % (n, ai, ','.join(map(str, a[5]))))
    C_OUT.append('static const ChrAnim %s_anims[] = {%s};' % (n, ','.join(
        '{%s,%s,%d,%s,%s,%d,%s_a%d}' % (cstr(a[0]), cfloat(a[1]), 1 if a[2] else 0, cfloat(a[3]), cfloat(a[4]), len(a[5]), n, ai)
        for ai, a in enumerate(adefs))))
    C_OUT.append('const ChrDef %s = {%s, %s, %d, %d, %s, %s, %s, %s, %s, %s, %s, %d, %d, %d, %s_frames, %d, %s_anims};' % (
        n, cstr('/' + fn), cfloat(tex_scale), log2size(maxw), log2size(maxh), cfloat(f0['fw']), cfloat(f0['fh']),
        cfloat(gx), cfloat(gy), cfloat(camx), cfloat(camy), cfloat(hold), icon, 1 if is_player else 0,
        len(cframes), n, len(adefs), n))
    CHRS.append(cname)
    print('chr %-14s %3d frames slot %dx%d  ram %d' % (cname, len(cframes), 8 << log2size(maxw), 8 << log2size(maxh), len(blob)))


def convert_character(cname, stage_scale, is_player, tex_scale, post=None):
    r = ET.parse(os.path.join(MOD, 'data/characters', cname + '.xml')).getroot()
    a = r.attrib
    sprite = a['sprite']
    base = os.path.join(IMG, 'characters', sprite)
    xml_scale = float(a.get('scale', 1))
    flip = a.get('flipX') == 'true'
    if is_player:
        flip = not flip
    player_offsets = a.get('isPlayer') == 'true'
    if is_player != player_offsets:
        raise ValueError('left/right swap not supported (%s)' % cname)
    anims = []
    for n in r.findall('anim'):
        b = n.attrib
        anims.append((b['name'], b['anim'], float(b.get('fps', 24)), b.get('loop') == 'true',
                      float(b.get('x', 0)), float(b.get('y', 0)), None))
    build_animsprite(cname, base + '.xml', base + '.png', anims, xml_scale * stage_scale, tex_scale, flip=flip,
                     gx=float(a.get('x', 0)), gy=float(a.get('y', 0)), camx=float(a.get('camx', 0)),
                     camy=float(a.get('camy', 0)), hold=float(a.get('holdTime', 4)), is_player=is_player, post=post)


def convert_characters(ytpos, abpos):
    H_OUT.append('#define YT_DAD_X %s\n#define YT_DAD_Y %s\n#define YT_DAD_SFY %s' % (cfloat(ytpos['dad'][0]), cfloat(ytpos['dad'][1]), cfloat(ytpos['dad'][4])))
    H_OUT.append('#define YT_BF_X %s\n#define YT_BF_Y %s\n#define YT_BF_SFY %s' % (cfloat(ytpos['boyfriend'][0]), cfloat(ytpos['boyfriend'][1]), cfloat(ytpos['boyfriend'][4])))
    H_OUT.append('#define AB_DAD_X %s\n#define AB_DAD_Y %s' % (cfloat(abpos['dad'][0]), cfloat(abpos['dad'][1])))
    H_OUT.append('#define AB_BF_X %s\n#define AB_BF_Y %s' % (cfloat(abpos['boyfriend'][0]), cfloat(abpos['boyfriend'][1])))
    yd, yb = ytpos['dad'][2], ytpos['boyfriend'][2]
    convert_character('maxmdp', yd, False, 0.16, post=deepfry)
    convert_character('nugmdp', yb, True, 0.16, post=deepfry)
    convert_character('max', yd, False, 0.12)
    convert_character('nugget', yb, True, 0.13)
    convert_character('maximillian', 1, False, 0.07)
    convert_character('maximillianSCARY', 1, False, 0.05)
    convert_character('darling', 1, True, 0.12)
    # One Last Bite script sprites
    C = os.path.join(IMG, 'characters')
    build_animsprite('maxrun', C + '/OLB/MaxRUN.xml', C + '/OLB/MaxRUN.png', [
        ('run', '!!!mak pro and', 22, True, 0, 0, None),
        ('huh', '!!!mak pro HUH', 16, False, 0, 0, None),
        ('crank', '!!!mak pro crank', 20, True, 0, 0, None)], 1.0, 0.13)
    build_animsprite('nugrun', C + '/OLB/NugRUN.xml', C + '/OLB/NugRUN.png', [
        ('run', '!NUGGETRUN', 22, True, 0, 0, None),
        ('slide', '!NUGGETslid', 22, True, 0, 0, None),
        ('what', '!NUGGETWHAT', 22, False, 0, 0, None),
        ('help', '!NUGGETHELP', 23.6, True, 0, 0, None)], 1.0, 0.16)
    build_animsprite('pibby', C + '/pibbyGlitch.xml', C + '/pibbyGlitch.png', [
        ('scary', 'pibbyGlitch glitch', 22, True, 0, 0, None)], 1.5, 0.12, colors=64)
    # To You: heart + borders
    A = os.path.join(IMG, 'stages/abbey')
    build_animsprite('heart', A + '/heart.xml', A + '/heart.png', [
        ('start', 'heart start', 9, True, 0, 0, None),
        ('anim', 'heart anim', 9, False, 0, 0, None),
        ('stay', 'heart stay', 9, True, 0, 0, None)], 1.0, 0.25)
    build_animsprite('borders', A + '/loveBorders.xml', A + '/loveBorders.png', [
        ('loop', 'Symbol', 3, True, 0, 0, None)], 0.7, 0.27)
    H_OUT.append('enum { %s, CHR_COUNT };' % ', '.join('CHR_' + c.upper() for c in CHRS))
    C_OUT.append('const ChrDef *const chr_defs[] = {%s};' % ','.join('&chr_' + c for c in CHRS))
    H_OUT.append('extern const ChrDef *const chr_defs[];')


# ---------------------------------------------------------------------------
# "deepfry" saturation shader of the MDP section, baked into MDP-only images
# ---------------------------------------------------------------------------
def deepfry(im):
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            c = [r / 255.0, g / 255.0, b / 255.0]
            l = c[0] * 0.2125 + c[1] * 0.7154 + c[2] * 0.0721
            c = [min(1, max(0, ((l + (v - l) * 1.1) - 0.5) * 1.12 + 0.5)) for v in c]
            px[x, y] = (int(c[0] * 255 + .5), int(c[1] * 255 + .5), int(c[2] * 255 + .5), a)
    return im


# ---------------------------------------------------------------------------
# HUD: note skins (strums, notes, sustains), splashes, ratings
# ---------------------------------------------------------------------------
KHUD = K          # DS px per camHUD unit (hud zoom 1)
NOTE_TS = 0.7 * 0.9 * KHUD   # texels per note-sprite source pixel


def convert_noteskin(name):
    im, frames = parse_sparrow(os.path.join(IMG, 'game/notes', name + '.xml'), os.path.join(IMG, 'game/notes', name + '.png'))
    s = NOTE_TS
    sh = Sheet('note_' + name, s / 0.7)   # texels per (unscaled * 0.7) unit -> drawn with strumScale * hudZoom * K
    dirs = ['left', 'down', 'up', 'right']
    colors = ['purple', 'blue', 'green', 'red']
    nmax = {}

    def add_centered(fname, f):
        full = full_frame(im, f)
        t, (bx, by) = trim(full)
        ts = scaled(t, s)
        ox = (bx - full.width / 2.0) * s
        oy = (by - full.height / 2.0) * s
        sh.add_packed(fname, ts, int(round(ox)), int(round(oy)))

    def uniq(sel):
        out = []
        for f in sel:
            k = (f['x'], f['y'], f['w'], f['h'], f['fx'], f['fy'])
            out.append(k)
        return out

    for i in range(4):
        add_centered('static%d' % i, frames_by_prefix(frames, 'arrow' + dirs[i].upper())[0])
        add_centered('note%d' % i, frames_by_prefix(frames, colors[i] + '0')[0])
        for kind in ('press', 'confirm'):
            sel = frames_by_prefix(frames, '%s %s' % (dirs[i], kind))
            keys = uniq(sel)
            ids = []
            seen = {}
            for f, k in zip(sel, keys):
                if k not in seen:
                    seen[k] = len(seen)
                    add_centered('%s%d_%d' % (kind, i, seen[k]), f)
                ids.append(seen[k])
            nmax['%s%d' % (kind, i)] = ids
    end = {'purple': 'pruple end hold', 'blue': 'blue hold end', 'green': 'green hold end', 'red': 'red hold end'}
    for i in range(4):
        for nm, key in (('hold', colors[i] + ' hold piece'), ('end', end[colors[i]])):
            f = frames_by_prefix(frames, key)[0]
            c = crop_frame(im, f)
            cs = scaled(c, s)
            sh.add_packed('%s%d' % (nm, i), cs, -cs.width // 2, 0)
    sh.build()
    fidx = {it[0]: i for i, it in enumerate(sh.items)}
    # animation sequences (24 fps)
    for k, ids in nmax.items():
        C_OUT.append('static const u8 ns_%s_%s[] = {%s};' % (name, k, ','.join(map(str, ids))))
    nf = frames_by_prefix(frames, 'purple0')[0]
    C_OUT.append('const NoteSkin noteskin_%s = {&sheet_note_%s, {%s}, {%s}, {%s}, {%s}, %s, %s, %s, %s};' % (
        name, name,
        ','.join('ns_%s_press%d' % (name, i) for i in range(4)), ','.join(str(len(nmax['press%d' % i])) for i in range(4)),
        ','.join('ns_%s_confirm%d' % (name, i) for i in range(4)), ','.join(str(len(nmax['confirm%d' % i])) for i in range(4)),
        cfloat(frames[0]['fw']), cfloat(frames[0]['fh']), cfloat(nf['fh']),
        ','.join('{%s}' % ','.join(str(fidx[k % i]) for i in range(4)) for k in ('static%d', 'note%d', 'press%d_0', 'confirm%d_0', 'hold%d', 'end%d'))))
    H_OUT.append('extern const NoteSkin noteskin_%s;' % name)


def convert_splashes(name):
    im, frames = parse_sparrow(os.path.join(IMG, 'game/splashes', name + '.xml'), os.path.join(IMG, 'game/splashes', name + '.png'))
    s = 0.6 * KHUD
    sh = Sheet('splash_' + name, s, fmt=FMT_16COL)
    colors = ['purple', 'blue', 'green', 'red']
    counts = []
    for i in range(4):
        for v in (1, 2):
            sel = frames_by_prefix(frames, 'note impact %d %s' % (v, colors[i]))
            for j, f in enumerate(sel):
                full = full_frame(im, f)
                t, (bx, by) = trim(full)
                ts = scaled(t, s)
                sh.add_packed('s%d_%d_%d' % (i, v, j), ts, int(round((bx - full.width / 2.0) * s)), int(round((by - full.height / 2.0) * s)))
            counts.append(len(sel))
    sh.build()
    H_OUT.append('#define SPLASH_%s_FRAMES %d' % (name.upper(), counts[0]))


def convert_ratings():
    s = 0.16
    sh = Sheet('ratings', s, fmt=FMT_16COL)
    for n in ['sick', 'good', 'bad', 'shit'] + ['num%d' % i for i in range(10)]:
        im = img('game/score/%s.png' % n)
        sh.add_packed(n, scaled(im, s), 0, 0)
        H_OUT.append('#define RATING_%s_W %d\n#define RATING_%s_H %d' % (n.upper(), im.width, n.upper(), im.height))
    sh.build()


def convert_toyou_ui():
    k = K
    for nm, fn, sc, ts in (('ab_frame', 'stages/abbey/frame.png', 0.67, 0.12), ('ab_aspect', 'stages/abbey/aspect.png', 1, 0.1),
                           ('ab_love', 'stages/abbey/Love.png', 1.7, 0.25)):
        im = img(fn)
        add_layer(nm, im, 0, 0, im.width * sc, im.height * sc, 1, 1, ts)
        H_OUT.append('#define %s_W %s\n#define %s_H %s' % (nm.upper(), cfloat(im.width), nm.upper(), cfloat(im.height)))


def convert_gameui():
    # One Last Bite UI (camHUD / camBorder at hud zoom 1)
    k = KHUD
    sh = Sheet('olbui', k)
    bg = img('game/batteryGradient.pNg').crop((0, 0, 210, 100))
    sh.add_packed('battgrad', scaled(bg, k), 0, 0)
    sh.add_packed('battgrad_ds', scaled(img('game/batteryGradient.pNg').crop((0, 40, 210, 140)), k), 0, 0)
    bb = img('game/batteryHealth.png')
    sh.add_packed('battborder', bb.resize((int(round(bb.width * 0.7 * k)), int(round(bb.height * 0.7 * k))), Image.LANCZOS), 0, 0)
    g = img('game/shortGradient.png')
    gs = g.resize((int(round(g.width * 2.1 * k)), int(round(g.height * 1.5 * k))), Image.LANCZOS)
    sh.add_packed('gradtop', gs, 0, 0)
    sh.add_packed('gradbot', gs.transpose(Image.FLIP_TOP_BOTTOM), 0, 0)
    sh.build()
    hm = img('game/helpMeBruh.png')
    sh = Sheet('helpme', k)
    sh.add_packed('main', scaled(hm, 0.7 * k), 0, 0)
    sh.build()
    # watermark text
    font = ImageFont.truetype(os.path.join(MOD, 'fonts/watermark.ttf'), 32)
    tw = int(font.getlength('@MaxDesignPro')) + 4
    t = Image.new('RGBA', (tw, 48), (0, 0, 0, 0))
    ImageDraw.Draw(t).text((2, 2), '@MaxDesignPro', font=font, fill=(0x0e, 0x05, 0x00, 255))
    H_OUT.append('#define WATERMARK_W %d' % tw)
    t2, (bx, by) = trim(t)
    sh2 = Sheet('watermark', k * 0.5)
    sh2.add_packed('main', scaled(t2, k * 0.5), 0, 0)
    sh2.build()
    H_OUT.append('#define WATERMARK_OX %d\n#define WATERMARK_OY %d' % (bx, by))
    # song logos (camBorder), broDied (camGame)
    for nm, fn in (('logo_olb', 'logo/OLB.png'), ('logo_olbwhite', 'logo/OLBWHITE.png')):
        im = img(fn)
        x0, y0, w, h = sprite_box(im.width, im.height, 0.4, 0.4, -100, -300)
        add_layer(nm, im, x0, y0, w, h, 1, 1, k * 0.4 * 1.0)
    im = Image.open(os.path.join(IMG, 'characters/OLB/broDied.png')).convert('RGBA')
    add_layer('brodied', im, -630, -60, im.width, im.height, 1, 1, 0.08)


# ---------------------------------------------------------------------------
# Charts + events (Codename chart format)
# ---------------------------------------------------------------------------
EASES = ['linear', 'quad', 'cube', 'quart', 'quint', 'sine', 'circ', 'expo', 'back', 'smoothStep', 'smootherStep', 'elastic', 'bounce']
EVENTS = ['Camera Movement', 'Camera Zoom', 'Camera Position', 'Scroll Speed Change', 'BPM Change',
          'Alt Animation Toggle', 'Play Animation', 'Camera Bop', 'Camera Modulo Change', 'Change Character', 'lyrics']


def ease_id(name, d):
    if name is None or name == 'CLASSIC':
        return 255
    base = EASES.index(name)
    return base * 3 + {'In': 0, 'Out': 1, 'InOut': 2}.get(d, 0) if base else 0


def convert_chart(song, tag):
    d = json.load(open(os.path.join(MOD, 'songs', song, 'charts/hard.json')))
    meta = json.load(open(os.path.join(MOD, 'songs', song, 'meta.json')))
    evs = json.load(open(os.path.join(MOD, 'songs', song, 'events.json')))['events'] + d.get('events', [])
    sls = d['strumLines']
    for i, sl in enumerate(sls):
        notes = sorted((n['time'], n.get('sLen', 0), n['id']) for n in sl['notes'])
        C_OUT.append('static const NoteDef %s_n%d[] = {%s};' % (tag, i, ','.join(
            '{%s,%s,%d}' % (cfloat(t), cfloat(l), k) for t, l, k in notes)))
    out = []
    bpms = [(0.0, float(meta['bpm']))]
    for e in evs:
        n, p, t = e['name'], e['params'], e['time']
        f = [0.0, 0.0, 0.0, 0.0]
        i0 = i1 = 0
        st = None
        ease = 255
        if n == 'Camera Movement':
            i0 = p[0]
            i1 = 1 if (len(p) > 1 and p[1]) else 0
            f[0] = p[2] if len(p) > 2 else 4
            ease = ease_id(p[3] if len(p) > 3 else None, p[4] if len(p) > 4 else None)
        elif n == 'Camera Zoom':
            i0 = 1 if p[0] else 0
            f[0] = p[1]
            i1 = (1 if p[2] == 'camHUD' else 0) | (2 if p[6] != 'direct' else 0) | (4 if p[7] else 0)
            f[1] = p[3]
            ease = ease_id(p[4], p[5])
        elif n == 'Camera Position':
            f[0], f[1] = p[0], p[1]
            i0 = 1 if p[2] else 0
            f[2] = p[3]
            ease = ease_id(p[4], p[5])
            i1 = 1 if p[6] else 0
        elif n == 'Scroll Speed Change':
            i0 = 1 if p[0] else 0
            f[0] = p[1]
            f[1] = p[2]
            ease = ease_id(p[3], p[4])
            i1 = 1 if p[5] else 0
        elif n == 'BPM Change':
            f[0] = p[0]
            bpms.append((t, float(p[0])))
        elif n == 'Alt Animation Toggle':
            i0 = 1 if p[0] else 0
            i1 = 1 if p[1] else 0
            f[0] = p[2]
        elif n == 'Play Animation':
            i0 = p[0]
            i1 = 1 if p[2] else 0
            st = p[1]
        elif n == 'Camera Bop':
            f[0] = p[0]
        elif n == 'Camera Modulo Change':
            f[0], f[1] = p[0], p[1]
            i0 = {'BEAT': 0, 'STEP': 1, 'MEASURE': 2}[p[2].upper()]
            f[2] = p[3]
        elif n == 'Change Character':
            i0 = p[0]
            st = p[1]
        elif n == 'lyrics':
            st = p[0]
        else:
            print('unknown event', n, p)
            continue
        out.append((t, EVENTS.index(n), ease, i0, i1, f, st))
    out.sort(key=lambda e: e[0])
    C_OUT.append('static const EventDef %s_ev[] = {%s};' % (tag, ','.join(
        '{%s,%d,%d,%d,%d,{%s},%s}' % (cfloat(t), k, ea, i0, i1, ','.join(cfloat(v) for v in f), cstr(st) if st is not None else 'NULL')
        for t, k, ea, i0, i1, f, st in out)))
    bpms.sort()
    C_OUT.append('static const BpmChange %s_bpm[] = {%s};' % (tag, ','.join('{%s,%s}' % (cfloat(t), cfloat(b)) for t, b in bpms)))
    sl = []
    for i, x in enumerate(sls):
        sl.append('{%s_n%d,%d,%s,%s,%s,%s,%d}' % (tag, i, len(x['notes']), cfloat(x.get('strumScale', 1)), cfloat(x.get('strumSpacing', 1)),
                                                cfloat(x.get('strumLinePos', 0.25 if x['type'] == 0 else 0.75)), cfloat(x.get('strumPos', [0, 50])[1]),
                                                0 if x.get('visible') is False else 1))
    C_OUT.append('const SongDef song_%s = {{%s}, %s_ev, %d, %s_bpm, %d, %s, %d, %d};' % (
        tag, ','.join(sl), tag, len(out), tag, len(bpms), cfloat(d['scrollSpeed']), meta.get('beatsPerMeasure', 4), meta.get('stepsPerBeat', 4)))
    H_OUT.append('extern const SongDef song_%s;' % tag)
    print('chart %s: %s notes, %d events' % (tag, [len(x['notes']) for x in sls], len(out)))


# ---------------------------------------------------------------------------
# Fonts: A3I5 glyph atlases (alpha in the top 3 bits, luminance in the low 5:
# white fill / black outline). Used as 3D textures and for the bottom screen.
# ---------------------------------------------------------------------------
MDPF = os.path.join(MOD, 'fonts/mdp.ttf')
VCRF = os.path.join(CA, 'fonts/vcr.ttf')
FONTS = [
    # name, file, px size, outline px
    ('mdp_lyr', MDPF, 11, 1),
    ('mdp6', MDPF, 6, 0),
    ('mdp9', MDPF, 9, 0),
    ('mdp9o', MDPF, 9, 1),
    ('mdp13', MDPF, 13, 0),
    ('mdp13o', MDPF, 13, 1),
    ('mdp17', MDPF, 17, 0),
    ('vcr10', MDPF, 10, 0),
    ('vcr16', MDPF, 16, 0),
]


def convert_fonts():
    H_OUT.append('enum { %s, FONT_COUNT };' % ', '.join('FONT_' + f[0].upper() for f in FONTS))
    names = []
    for nm, path, size, ol in FONTS:
        font = ImageFont.truetype(path, size)
        asc, desc = font.getmetrics()
        lh = asc + desc
        glyphs = []
        for ch in range(32, 127):
            c = chr(ch)
            adv = int(round(font.getlength(c)))
            bb = font.getbbox(c)
            w = max(1, bb[2] - min(0, bb[0])) + 2 * ol + 4
            gi = Image.new('RGBA', (w, lh + 2 * ol + 4), (0, 0, 0, 0))
            dr = ImageDraw.Draw(gi)
            pos = (2 + ol - min(0, bb[0]), 2 + ol)
            if ol:
                dr.text(pos, c, font=font, fill=(255, 255, 255, 255), stroke_width=ol, stroke_fill=(0, 0, 0, 255))
            else:
                dr.text(pos, c, font=font, fill=(255, 255, 255, 255))
            bbx = gi.getchannel('A').getbbox()
            if bbx is None:
                glyphs.append((ch, None, 0, 0, adv))
                continue
            g = gi.crop(bbx)
            glyphs.append((ch, g, bbx[0] - pos[0] + min(0, bb[0]), bbx[1] - pos[1], adv))
        W = 256
        x = y = rowh = 0
        place = []
        for ch, g, ox, oy, adv in glyphs:
            if g is None:
                place.append((0, 0, 0, 0, 0, 0, adv))
                continue
            if x + g.width > W:
                x = 0
                y += rowh + 1
                rowh = 0
            place.append((x, y, g.width, g.height, ox, oy, adv))
            x += g.width + 1
            rowh = max(rowh, g.height)
        H = 8
        while H < y + rowh:
            H *= 2
        atlas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        for (ch, g, *_), (px, py, *_) in zip(glyphs, place):
            if g is not None:
                atlas.paste(g, (px, py))
        out = bytearray(W * H)
        data = atlas.tobytes()
        for i in range(W * H):
            r, gg, b, a = data[i * 4:i * 4 + 4]
            a = min(255, int(255 * (a / 255.0) ** 0.6))
            a3 = (a + 18) // 36
            if a3 <= 0:
                continue
            out[i] = (min(7, a3) << 5) | (r >> 3)
        fn = 'font/%s.bin' % nm
        with open(outpath(fn), 'wb') as fo:
            fo.write(out)
        C_OUT.append('static const Glyph font_%s_g[] = {%s};' % (nm, ','.join('{%d,%d,%d,%d,%d,%d,%d}' % p for p in place)))
        C_OUT.append('const FontDef font_%s = {%s, %d, %d, %d, %d, font_%s_g};' % (nm, cstr('/' + fn), log2size(W), log2size(H), lh, asc, nm))
        names.append('&font_' + nm)
    C_OUT.append('const FontDef *const font_defs[] = {%s};' % ','.join(names))
    H_OUT.append('extern const FontDef *const font_defs[];')


# ---------------------------------------------------------------------------
# Audio
# ---------------------------------------------------------------------------
RATE = 22050


def ff(srcs, dst, extra=(), filt=None):
    cmd = ['ffmpeg', '-v', 'error', '-y']
    for s_ in srcs:
        cmd += ['-i', s_]
    if filt:
        cmd += ['-filter_complex', filt]
    subprocess.run(cmd + list(extra) + [dst], check=True)


RAW = ['-ac', '1', '-ar', str(RATE), '-f', 's16le', '-acodec', 'pcm_s16le']
SFX = [  # name, source
    ('scroll', 'sounds/menu/scroll.ogg'), ('confirm', 'sounds/menu/confirm.ogg'), ('cancel', 'sounds/menu/cancel.ogg'),
    ('chomp', 'sounds/menu/chomp.ogg'), ('buzzer', 'sounds/menu/buzzer.ogg'), ('ball', 'sounds/soccer/ball.ogg'),
    ('missnote1', CA + '/sounds/missnote1.ogg'), ('missnote2', CA + '/sounds/missnote2.ogg'), ('missnote3', CA + '/sounds/missnote3.ogg'),
    ('confirmmenu', CA + '/sounds/menu/confirm.ogg'),
]
STREAMS = [  # long sounds streamed from NitroFS
    ('johnjingle', 'sounds/menu/johnJingle.ogg'), ('mdpjingle', 'sounds/menu/mdpJingle.ogg'),
    ('jingle_cf', 'sounds/Jingle_CF.ogg'), ('goodsfx', 'sounds/goodSFX.ogg'), ('highscore', 'sounds/soccer/highscore.ogg'),
]


def convert_audio():
    S = os.path.join(MOD, 'songs')
    olb = os.path.join(S, 'One Last Bite/song')
    ff([olb + '/Inst.ogg', olb + '/Voices-Max.ogg'], outpath('snd', 'olb_inst.raw'), RAW, '[0:a][1:a]amix=inputs=2:normalize=0')
    ff([olb + '/Voices-Nugget.ogg'], outpath('snd', 'olb_voices.raw'), RAW)
    ty = os.path.join(S, 'To You/song')
    ff([ty + '/Inst.ogg'], outpath('snd', 'toyou_inst.raw'), RAW)
    ff([ty + '/Voices.ogg'], outpath('snd', 'toyou_voices.raw'), RAW)
    for nm, src in (('menu', 'freakyMenu'), ('menuold', 'freakyMenuOld'), ('soccer', 'soccerTime'), ('soccerweird', 'soccerWeird')):
        ff([os.path.join(MOD, 'music', src + '.ogg')], outpath('snd', nm + '.raw'), RAW)
    ff([os.path.join(MOD, 'videos/death/One Last Bite.mp4')], outpath('snd', 'death_olb.raw'), ['-vn'] + RAW)
    for nm, src in STREAMS:
        ff([os.path.join(MOD, src)], outpath('snd', nm + '.raw'), RAW)
    for d in ('chars', 'select'):
        for f in sorted(os.listdir(os.path.join(MOD, 'sounds/soccer', d))):
            ff([os.path.join(MOD, 'sounds/soccer', d, f)], outpath('snd/soccer', d + '_' + f[:-4].lower() + '.raw'), RAW)
    md = os.path.join(ROOT, 'maxmod_data')
    os.makedirs(md, exist_ok=True)
    for f in os.listdir(md):
        os.remove(os.path.join(md, f))
    for nm, src in SFX:
        ff([src if src.startswith('/') else os.path.join(MOD, src)], os.path.join(md, nm + '.wav'),
           ['-ac', '1', '-ar', str(RATE), '-acodec', 'pcm_u8'])


# ---------------------------------------------------------------------------
# Bottom-screen images (RGB15 + alpha bit)
# ---------------------------------------------------------------------------
def write_b16(name, im, bg=None):
    im = im.convert('RGBA')
    if bg is not None:
        b = Image.new('RGBA', im.size, bg)
        b.alpha_composite(im)
        im = b
    out = bytearray((im.width & 255, im.width >> 8, im.height & 255, im.height >> 8))
    data = im.tobytes()
    for i in range(im.width * im.height):
        r, g, b, a = data[i * 4:i * 4 + 4]
        v = rgb15(r, g, b) | (0x8000 if a >= 128 else 0)
        out += bytes((v & 255, v >> 8))
    with open(outpath('sub', name + '.b16'), 'wb') as f:
        f.write(out)


def convert_sub_game():
    im, frames = parse_sparrow(os.path.join(IMG, 'icons/NugIcon.xml'), os.path.join(IMG, 'icons/NugIcon.png'))
    for i, f in enumerate(frames_by_prefix(frames, 'NUGGET')):
        c = full_frame(im, f)
        write_b16('icon_nug_%d' % i, scaled(c, 0.7 * 0.32))
    hm = img('game/helpMeBruh.png')
    write_b16('helpme', scaled(hm, 232.0 / hm.width))


def convert_pause():
    sh = Sheet('pause', K)
    pat = img('menu/menuPattern.png')
    sh.add_packed('pattern', scaled(pat, 2 * K), 0, 0)
    pt = img('menu/pauseText.png')
    sh.add_packed('pausetext', scaled(pt, 0.8 * K), 0, 0)
    pp = img('menu/pausePointer.png')
    sh.add_packed('pointer', scaled(pp, K), 0, 0)
    sh.build()
    H_OUT.append('#define PAUSETEXT_W %d\n#define PAUSETEXT_H %d' % (pt.width, pt.height))


# ---------------------------------------------------------------------------
# Menus: drawn letterboxed (the whole 1280x720 view in 256x144), MS DS px per
# virtual px. Sheet scale = MS: an image baked at sprite scale `bake` is drawn
# with sheet_draw(..., MS * scale / bake).
# ---------------------------------------------------------------------------
MS = 0.2


class MSheet(Sheet):
    def __init__(self, name, fmt=FMT_256COL, colors=255):
        Sheet.__init__(self, name, MS, fmt=fmt, colors=colors)

    def image(self, fname, im, bake, tiled=False, trimit=True):
        """box top-left = frame origin"""
        if trimit:
            t, (bx, by) = trim(im)
        else:
            t, bx, by = im, 0, 0
        d = MS * bake
        ts = t.resize((max(1, int(round(t.width * d))), max(1, int(round(t.height * d)))), Image.LANCZOS)
        ox, oy = int(round(bx * d)), int(round(by * d))
        (self.add_tiled if tiled else self.add_packed)(fname, ts, ox, oy)

    def sparrow(self, prefix, xmlpath, pngpath, bake, names=None):
        im, frames = parse_sparrow(xmlpath, pngpath)
        seen = {}
        out = []
        for f in frames:
            if names and not any(f['name'].startswith(n) for n in names):
                continue
            k = (f['x'], f['y'], f['w'], f['h'], f['fx'], f['fy'])
            if k not in seen:
                seen[k] = len(self.items)
                self.image('%s%d' % (prefix, len(seen) - 1), full_frame(im, f), bake)
            out.append((f['name'], seen[k]))
        return out, frames


def emit_seq(name, idx):
    C_OUT.append('const u8 %s[] = {%s};' % (name, ','.join(str(i) for i in idx)))
    H_OUT.append('extern const u8 %s[];\n#define %s_LEN %d' % (name, name.upper(), len(idx)))


def convert_menus():
    I = IMG
    # --- Intro -------------------------------------------------------------
    sh = MSheet('intro')
    sh.image('backcircle', img('intro/backcircle.png'), 0.75)
    sh.image('logo', img('intro/mdpEnglishLogo.png'), 0.26)
    seq, frames = sh.sparrow('john', I + '/intro/JohnnyAnimate.xml', I + '/intro/JohnnyAnimate.png', 0.75)
    base = len(sh.items) - len(set(i for _, i in seq))
    sh.build()
    emit_seq('intro_john_seq', [i for _, i in sorted(seq, key=lambda x: x[0])])
    f0 = frames[0]
    H_OUT.append('#define JOHN_W %s\n#define JOHN_H %s' % (cfloat(f0['fw']), cfloat(f0['fh'])))
    for nm, fn in (('BACKCIRCLE', 'intro/backcircle.png'), ('MDPLOGO', 'intro/mdpEnglishLogo.png')):
        im = img(fn)
        H_OUT.append('#define %s_W %s\n#define %s_H %s' % (nm, cfloat(im.width), nm, cfloat(im.height)))
    # --- Main menu ---------------------------------------------------------
    sh = MSheet('mainbg')
    sh.image('bg', img('menu/main/menuBackground.png'), 1, tiled=True, trimit=False)
    sh.build()
    sh = MSheet('mainmenu')
    cs = img('menu/main/menuChainsaw.png')
    sh.image('chainsaw', cs.resize((int(cs.width * 1.6), int(cs.height * 1.2)), Image.LANCZOS), 1, tiled=True)
    sh.image('crumbs', img('menu/main/menuCrumbs.png'), 0.92)
    sh.image('pattern', img('menu/menuPattern.png'), 2, trimit=False)
    sh.image('bubby', img('menu/main/bubby.png'), 0.2)
    seq, frames = sh.sparrow('nug', I + '/menu/main/menuNugs.xml', I + '/menu/main/menuNugs.png', 1.0)
    sh.build()
    H_OUT.append('#define CHAINSAW_W %s\n#define CHAINSAW_H %s' % (cfloat(cs.width), cfloat(cs.height)))
    names = dict(seq)
    order = []
    for b in ('story', 'bonus', 'settings', 'credits'):
        for k in (1, 2, 3):
            order.append(names[b + '%d' % k] if b + '%d' % k in names else names[[n for n in names if n.startswith(b + '%d' % k)][0]])
    emit_seq('menu_nug_frames', order)
    # frame sizes of each button (first frame of the atlas sets the hitbox)
    fr = {f['name']: f for f in frames}
    sizes = []
    for b in ('story', 'bonus', 'settings', 'credits'):
        f = [x for x in frames if x['name'].startswith(b + '1')][0]
        sizes.append((f['fw'], f['fh']))
    f0 = frames[0]
    C_OUT.append('const float menu_nug_size[4][2] = {%s};' % ','.join('{%s,%s}' % (cfloat(w), cfloat(h)) for w, h in sizes))
    H_OUT.append('extern const float menu_nug_size[4][2];')
    for nm, fn in (('chars_normal', 'menu/main/GayFuck.png'), ('chars_true', 'menu/main/TrueGayFuck.png'), ('chars_old', 'menu/main/OldFuck.png'),
                   ('eye_normal', 'menu/main/SkibbyBlud.png'), ('eye_true', 'menu/main/Diddyblud.png')):
        sh = MSheet(nm)
        sh.image('main', img(fn), 0.51, tiled=True)
        sh.build()
    # --- Story menu ------------------------------------------------------------
    sh = MSheet('storybg')
    sh.image('table', img('menu/story/table.png'), 0.5, tiled=True, trimit=False)
    sh.build()
    sh = MSheet('story')
    for i in range(1, 5):
        im = img('menu/story/%d.png' % i)
        sh.image('paper%d' % i, im, 0.6)
        H_OUT.append('#define PAPER%d_W %s\n#define PAPER%d_H %s' % (i, cfloat(im.width), i, cfloat(im.height)))
    sp = img('menu/story/spotlight.png')
    sh.image('spotlight', sp, 2.5, trimit=False)
    H_OUT.append('#define SPOTLIGHT_W %s\n#define SPOTLIGHT_H %s' % (cfloat(sp.width), cfloat(sp.height)))
    sh.build()
    # --- Bonus menu ------------------------------------------------------------
    sh = MSheet('bonus')
    sh.image('toyou', img('menu/bonus/To You.png'), 0.47)
    sh.image('null', img('menu/bonus/null.png'), 0.47)
    sh.image('pattern', img('menu/menuPattern.png'), 2, trimit=False)
    sh.build()
    # --- Next up -------------------------------------------------------------
    for nm, fn in (('nextup_text', 'menu/nextup/Text.png'), ('nextup_img', 'menu/nextup/AintMyFirstRodeo.png')):
        im = img(fn)
        sh = MSheet(nm)
        sh.image('main', im.resize((1280, 720), Image.LANCZOS), 1, tiled=True, trimit=False)
        sh.build()


def convert_soccer():
    I = IMG + '/soccer/'

    def nonuni(sh, prefix, xmlp, pngp, gw, gh, names=None):
        im, frames = parse_sparrow(I + xmlp, I + pngp)
        f0 = frames[0]
        sx, sy = gw / f0['fw'], gh / f0['fh']
        ox, oy = f0['fw'] / 2.0, f0['fh'] / 2.0
        seen = {}
        seqs = {}
        for f in frames:
            k = (f['x'], f['y'], f['w'], f['h'], f['fx'], f['fy'])
            if k not in seen:
                seen[k] = len(sh.items)
                c = crop_frame(im, f)
                # world top-left relative to the hitbox (updateHitbox: box = gw x gh)
                dx = ox + (-f['fx'] - ox) * sx - (ox - ox * sx) + (0)
                dy = oy + (-f['fy'] - oy) * sy - (oy - oy * sy)
                cs = c.resize((max(1, int(round(c.width * sx * MS))), max(1, int(round(c.height * sy * MS)))), Image.LANCZOS)
                sh.add_packed('%s%d' % (prefix, len(seen)), cs, int(round(dx * MS)), int(round(dy * MS)))
            base = re.sub(r'\d+$', '', f['name']).strip()
            seqs.setdefault(base, []).append((f['name'], seen[k]))
        return {k: [i for _, i in sorted(v)] for k, v in seqs.items()}

    sh = MSheet('soccer')
    pl = nonuni(sh, 'pl', 'player.xml', 'player.png', 100, 150)
    ty = nonuni(sh, 'ty', 'soccerKing123.xml', 'soccerKing123.png', 130, 130)
    sh.image('ball', img('soccer/ball.png'), 1, trimit=False)
    ti = img('soccer/tylerIcon.png')
    sh.image('tylericon', ti, 0.5, trimit=False)
    im, frames = parse_sparrow(I + 'icons.xml', I + 'icons.png')
    icons = {}
    for f in frames:
        icons[re.sub(r'\d+$', '', f['name']).strip()] = len(sh.items)
        sh.image('icon_' + f['name'].strip(), full_frame(im, f), 1, trimit=False)
    im, frames = parse_sparrow(I + 'newHighScore.xml', I + 'newHighScore.png')
    nhs = {}
    for f in frames:
        nhs[re.sub(r'\d+$', '', f['name']).strip()] = len(sh.items)
        sh.image('nhs_' + f['name'].strip(), full_frame(im, f), 1, trimit=False)
    sh.build()
    skins = ['Jimmy', 'Ollie', 'Duncan', 'TrifleJimmy', 'Oxaya', 'Bubby']
    for i, sk in enumerate(skins):
        emit_seq('soc_idle_%d' % i, pl[sk + '_Idle'])
        emit_seq('soc_run_%d' % i, pl[sk + '_Run'])
    C_OUT.append('const u8 *const soc_idle[6] = {%s};' % ','.join('soc_idle_%d' % i for i in range(6)))
    C_OUT.append('const u8 *const soc_run[6] = {%s};' % ','.join('soc_run_%d' % i for i in range(6)))
    C_OUT.append('const u8 soc_idle_len[6] = {%s};' % ','.join('SOC_IDLE_%d_LEN' % i for i in range(6)))
    C_OUT.append('const u8 soc_run_len[6] = {%s};' % ','.join('SOC_RUN_%d_LEN' % i for i in range(6)))
    C_OUT.append('const u8 soc_icon[6] = {%s};' % ','.join(str(icons[k]) for k in skins))
    C_OUT.append('const u8 soc_nhs[6] = {%s};' % ','.join(str(nhs[k]) for k in skins))
    H_OUT.append('extern const u8 *const soc_idle[6];\nextern const u8 *const soc_run[6];\nextern const u8 soc_idle_len[6];\nextern const u8 soc_run_len[6];\nextern const u8 soc_icon[6];\nextern const u8 soc_nhs[6];')
    for k in ('up', 'down', 'win', 'lose'):
        emit_seq('soc_ty_' + k, ty[k])
    sh = MSheet('soccerfield')
    sh.image('field', img('soccer/field.png'), 1280.0 / 1280, tiled=True, trimit=False)
    sh.build()
    mb = img('soccer/menubg.png')
    sh = MSheet('soccermenu')
    sh.image('bg', mb, 1280.0 / mb.width, tiled=True, trimit=False)
    lg = img('soccer/logo.png')
    sh.image('logo', lg, 0.67, trimit=False)
    for nm, fn in (('normal', 'soccer/normal.png'), ('superfast', 'soccer/super fast.png')):
        im = img(fn)
        sh.image(nm, im, 200.0 / im.width, trimit=False)
    sh.build()
    H_OUT.append('#define SOC_MENUBG_W %s\n#define SOC_MENUBG_H %s\n#define SOC_LOGO_W %s\n#define SOC_LOGO_H %s' % (
        cfloat(mb.width), cfloat(mb.height), cfloat(lg.width), cfloat(lg.height)))


def main():
    H_OUT.append('#pragma once')
    H_OUT.append('#include "assets.h"')
    C_OUT.append('#include "gen_assets.h"')
    yt = convert_youtubeland()
    ab = convert_abbey()
    convert_characters(yt, ab)
    H_OUT.append('enum { %s };' % ', '.join('EV_' + cid(e) for e in EVENTS))
    convert_chart('One Last Bite', 'olb')
    convert_chart('To You', 'toyou')
    for sk in ('default', 'mdp', 'red'):
        convert_noteskin(sk)
    convert_splashes('default')
    convert_splashes('red')
    convert_ratings()
    convert_gameui()
    convert_toyou_ui()
    emit_layers()
    convert_fonts()
    convert_sub_game()
    convert_pause()
    convert_menus()
    convert_soccer()
    if '--no-audio' not in sys.argv:
        convert_audio()
    H_OUT.append('#define AUDIO_RATE %d' % RATE)
    with open(os.path.join(ROOT, 'source/gen_assets.c'), 'w') as f:
        f.write('// Generated by tools/convert.py - do not edit\n' + '\n'.join(C_OUT) + '\n')
    with open(os.path.join(ROOT, 'include/gen_assets.h'), 'w') as f:
        f.write('// Generated by tools/convert.py - do not edit\n' + '\n'.join(H_OUT) + '\n')


if __name__ == '__main__':
    main()
