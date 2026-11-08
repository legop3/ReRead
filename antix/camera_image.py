"""Validate the Reader's still frame, then use packaged image libraries."""
import ctypes
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageMath

WIDTH, HEIGHT = 2592, 1920
PREVIEW_GAMMA = [round((value / 255) ** .5 * 255) for value in range(256)]


def frame_image(raw, width=WIDTH, height=HEIGHT, pattern=515, flip=True, live=False):
    if len(raw) != width * height * 2:
        raise ValueError('The camera returned an incomplete image. Please try again.')
    sensor = Image.frombytes('I', (width, height), raw, 'raw', 'I;16')
    # These checks run in Pillow's C image operations rather than creating
    # millions of Python integers on a 512 MB machine.
    def mask(bits):
        return ImageMath.lambda_eval(lambda a: a['im'] & bits, im=sensor)
    lines = mask(0x1000)
    if (mask(0x8000).getbbox() != (0, 0, 1, 1)
            or lines.getbbox() != (0, 0, 1, height)
            or lines.crop((0, 0, 1, height)).getextrema() != (0x1000, 0x1000)):
        raise ValueError('The camera lost image rows. Please try again.')
    del lines
    pixels = mask(0x0fff)
    histogram = pixels.histogram(extrema=(0, 4095))
    def percentile(fraction):
        count = 0
        for index, frequency in enumerate(histogram):
            count += frequency
            if count > width * height * fraction:
                return index * 16
        return 4095
    low, high = percentile(.01), percentile(.99)
    span = max(16, high - low)
    # Basic diagnostic tone mapping, shared with the original research preview.
    # White balance, lens shading and calibrated exposure remain future work.
    if live:
        # Pillow evaluates this affine operation in C. Building and rounding
        # a 65,536-entry 16-bit LUT per preview frame dominated Atom CPU time.
        bayer = pixels.point(lambda value: (value-low)*255/span).convert('L').point(PREVIEW_GAMMA).tobytes()
    else:
        lut = [round(min(1, max(0, (value-low)/span)) ** .5 * 255) for value in range(4096)]
        bayer = pixels.point(lut + [255] * (65536 - len(lut)), 'L').tobytes()
    del sensor, pixels
    library = ctypes.CDLL('libdc1394.so.25')
    decode = library.dc1394_bayer_decoding_8bit
    decode.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32,
                       ctypes.c_uint32, ctypes.c_int, ctypes.c_int]
    decode.restype = ctypes.c_int
    rgb = ctypes.create_string_buffer(width * height * 3)
    # libdc1394 enums: BGGR=515, bilinear=2. Only its image converter is used;
    # the camera transport remains the Reader's actual USB/FPGA protocol.
    result = decode(bayer, rgb, width, height, pattern, 2)
    if result:
        raise RuntimeError(f'Bayer conversion failed ({result}).')
    image = Image.frombytes('RGB', (width, height), rgb.raw)
    del rgb, bayer
    if flip:
        image = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    return image


def encode(raw_path, photo_path, preview_path):
    image = frame_image(Path(raw_path).read_bytes())
    image.save(photo_path, 'JPEG', quality=90)
    image.thumbnail((960, 720))
    image.save(preview_path, 'JPEG', quality=85)


def stream_jpeg(raw):
    """Pick the newest complete marked frame from a three-frame USB burst."""
    words = len(raw) // 2
    sensor = Image.frombytes('I', (words, 1), raw, 'raw', 'I;16')
    # Locate frame flags in C, then search only their compact byte mask.
    markers = ImageMath.lambda_eval(lambda a: a['im'] & 0x8000, im=sensor).convert('L').tobytes()
    starts, offset = [], 0
    while (offset := markers.find(b'\xff', offset)) >= 0:
        starts.append(offset)
        offset += 1
    size = 648 * 480
    for start, end in reversed(list(zip(starts, starts[1:] + [words]))):
        if end - start < size:
            continue
        try:
            image = frame_image(raw[start*2:(start+size)*2], 648, 480, 513, False, live=True)
        except ValueError:
            continue
        output = BytesIO()
        image.save(output, 'JPEG', quality=75)
        return output.getvalue()
    raise ValueError('No complete live-view frame in this burst.')
