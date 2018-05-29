# data builder for mariopants
# builds contents of data/ folder into data.h and data.cpp
#
# python 3

import array
import struct
import os

folder = "data"

def get_files(folder):
    f = []
    for (root, folders, files) in os.walk(folder):
        for filename in files:
            f.append(os.path.join(root,filename))
    return f

def build_icon(file):
    f = open(file, mode="rb")
    # read header
    header = f.read(struct.calcsize("<BBBHHBHHHHBB"))
    (padding, colormap, imagetype, \
        cms, cml, cmb, xo, yo, w, h, bpp, flip) = \
        struct.unpack("<BBBHHBHHHHBB",header)
    #print("TGA header: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d" % \
    #      (padding, colormap, imagetype, cms, cml, cmb, xo, yo, w, h, bpp, flip))
    flipv = ((flip & 0x20) == 0)
    fliph = ((flip & 0x10) != 0)
    abits = (flip & 0x0F)
    f.read(padding)
    if (colormap != 0) or (imagetype != 2) or ((abits & 8) != abits):
        print("Unknown TGA type!")
        return None
    # read pixel data
    d = array.array("B",[0]*(w*h*4))
    for y in range(0,h):
        for x in range(0,w):
            ix = x
            iy = y
            if (fliph):
                x = (w - x) - 1
            if (flipv):
                y = (h - y) - 1
            idx = ((y*w)+x)*4
            if abits > 0:
                b = f.read(4)
                cr = b[2]
                cg = b[1]
                cb = b[0]
                ca = b[3]
                # convert to 1-bit alpha
                alpha = b[3]
                if (ca < 128):
                    ca = 0
                    cr = 255
                    cg = 0
                    cb = 255
                else:
                    ca = 0xFF
                d[idx+0] = cr
                d[idx+1] = cg
                d[idx+2] = cb
                d[idx+3] = ca
            else:
                b = f.read(3)
                d[idx+0] = b[2]
                d[idx+1] = b[1]
                d[idx+2] = b[0]
                d[idx+3] = 0xFF
    print("build_icon(%s) > %d x %d" % (file,w,h))
    (path, fn) = os.path.split(file)
    tga = (fn[:-4],w,h,d)
    return tga

# noise threshold for the given wav (adjust to get correct number of samples)
def is_silent(sample):
    threshold = 26
    return sample < threshold and sample > -threshold

def build_samples(file):
    f = open(file,"rb")
    b = f.read()
    print("build_samples(%s) > %d bytes" % (file,len(b)))
    # WAV is presumed 32000hz, 16bit, no nonsense chunks
    entries = []
    # extract 16-bit signed samples
    s = []
    for i in range(44,len(b),2):
        h = struct.unpack("<h",b[i:i+2])
        s.append(h[0]);
    print("  %d samples unpacked..." % (len(s)))
    # find silence-separated samples
    sample_start = 0
    while (True):
        # take up leading silence
        while sample_start < len(s) and is_silent(s[sample_start]):
            sample_start += 1
        if sample_start >= len(s):
            break
        # find length of sample
        threshhold = 3200 # point to presume sample is ended
        silent_count = 0
        sample_end = sample_start + 1
        while (sample_end < len(s) and silent_count < threshhold):
            if is_silent(s[sample_end]):
                silent_count += 1
            else:
                silent_count = 0
            sample_end += 1
        sample_end -= silent_count
        # skip brief noise spikes
        if (sample_end - sample_start) <= 5:
            sample_start = sample_end
            continue
        # save sample
        tail_size = 400 # add a small fadeout tail to minimize end-pop
        sample_end += tail_size
        d = s[sample_start:sample_end]
        for i in range(0,tail_size):
            p = len(d)-(i+1)
            d[p] = int(i * d[p] / tail_size)
        si = len(entries)
        print("  sample[% 2d,% 2d] > %5d samples [%5d:%5d]" % (int(si/13),si%13,len(d),sample_start,sample_end))
        entries.append((len(d), d))
        sample_start = sample_end
    return entries

def build_bin(file):
    f = open(file,"rb")
    d = f.read()
    print("build_zst(%s) > %d bytes" % (file,len(d)))
    return d

def hex_block(d):
    s = "  { "
    for i in range(0,len(d)):
        s += ("0x%02X," % (d[i]))
        if (i & 15) == 15:
            s += "\n    "
    s += "}"
    return s

def int_block(d):
    s = "  { "
    for i in range(0,len(d)):
        s += ("%6d," % (d[i]))
        if (i & 15) == 15:
            s += "\n    "
    s += "}"
    return s

# main

if __name__ == "__main__":
    files = get_files(folder)
    # built data storage
    icons = []
    samples = []
    zst = array.array("B",[])
    s9x = array.array("B",[])
    # scan files and build data
    for f in files:
        if (f.endswith(".tga")):
            icons.append(build_icon(f))
        if (f.endswith(".wav")):
            samples = build_samples(f)
        if (f.endswith(".zst")):
            zst = build_bin(f)
        if (f.endswith(".000")):
            s9x = build_bin(f)
    # format data into text
    # header
    h  = "#pragma once\n"
    h += "\n"
    h += "// data.h\n"
    h += "//   auto generated by data.py\n"
    h += "\n"
    h += "#include \"os.h\"\n"
    h += "\n"
    h += "typedef struct\n"
    h += "{\n"
    h += "    int w;\n"
    h += "    int h;\n"
    h += "    const unsigned char* d;\n"
    h += "} IconData;\n"
    h += "\n"
    h += "typedef struct\n"
    h += "{\n"
    h += "    unsigned int len;\n"
    h += "    const sint16* d;\n"
    h += "} SampleData;\n"
    h += "\n"
    h += ("const unsigned int ZST_SIZE = %d;\n" % (len(zst)))
    h += "extern const unsigned char zst_block[ZST_SIZE];\n"
    h += "\n"
    h += ("const unsigned int S9X_SIZE = %d;\n" % (len(s9x)))
    h += "extern const unsigned char s9x_block[S9X_SIZE];\n"
    h += "\n"
    h += "extern const sint16 sample_block[];\n"
    h += "extern const SampleData sampledata[(15*13)+8];\n"
    h += "\n"
    h += ("const int ICON_COUNT = %d;\n" % (len(icons)))
    h += "extern const unsigned char icon_block[];\n"
    h += "extern const IconData* icondata[ICON_COUNT];\n"
    h += "\n"
    h += "enum {\n"
    for (name,w,h_,d) in icons:
        h += ("    ICON_%s,\n" % (name.upper()))
    h += "};\n"
    h += "\n"
    h += "// end of file\n"
    # save header
    o = open("data.h", "w")
    o.write(h)
    o.close()
    print("data.h saved.");
    # cpp
    s  = "// data.cpp\n"
    s += "//   auto generated by data.py\n"
    s += "\n"
    s += "#include \"data.h\"\n"
    s += "\n"
    s += "const unsigned char zst_block[ZST_SIZE] =\n"
    s += hex_block(zst)
    s += ";\n"
    s += "\n"
    print("zst_block[] complete.");
    s += "const unsigned char s9x_block[S9X_SIZE] =\n"
    s += hex_block(s9x)
    s += ";\n"
    s += "\n"
    print("s9x_block[] complete.");
    data_block = array.array("i",[])
    for (l,d) in samples:
        data_block.extend(d)
    s += "const sint16 sample_block[] =\n"
    s += int_block(data_block)
    s += ";\n"
    s += "\n"
    print("sample_block[] complete.");
    data_block = array.array("B",[])
    for (name,w,h,d) in icons:
        data_block.extend(d)
    s += "const unsigned char icon_block[] =\n"
    s += hex_block(data_block)
    s += ";\n"
    s += "\n"
    print("icon_block[] complete.");
    s += "const SampleData sampledata[(15*13)+8] = {\n"
    data_offset = 0
    for (l,d) in samples:
        s += ("    { %d, sample_block + %d },\n" % \
              (l,data_offset))
        data_offset += len(d)
    s += "};\n"
    s += "\n"
    print("sampledata[] complete.");
    data_offset = 0
    for (name,w,h,d) in icons:
        s += ("const IconData icondata_%s = { %d, %d, icon_block + %d };\n" % \
              (name,w,h,data_offset))
        data_offset += len(d)
    s += "\n"
    s += "const IconData* icondata[ICON_COUNT] = {\n"
    for (name,w,h,d) in icons:
        s += ("    &icondata_%s,\n" % (name))
    s += "};\n"
    s += "\n"
    print("icondata[] complete.");
    s += "// end of file\n"
    # save cpp
    o = open("data.cpp", "w")
    o.write(s)
    o.close()
    print("data.cpp saved.");
