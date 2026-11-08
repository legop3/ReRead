#!/usr/bin/env python3
"""Extract a complete Bayer frame as a contrast-scaled grayscale PGM.

This research preview deliberately does not claim calibrated color/exposure or
stock image processing. Grayscale requires only Python's standard library;
optional color conversion uses FFmpeg's existing Bayer demosaicing and encoder.
"""
import argparse
from array import array
from pathlib import Path
import sys
import subprocess


def extract_frame(raw, width=648, height=480):
    words = array("H")
    words.frombytes(raw)
    if sys.byteorder != "little":
        words.byteswap()
    starts = [i for i, value in enumerate(words) if value & 0x8000]
    size = width * height
    for start, end in zip(starts, starts[1:] + [len(words)]):
        if end - start != size:
            continue
        frame = words[start:end]
        # Bit 12 marks the first pixel of every line; bit 15 marks a frame.
        if all(bool(value & 0x1000) == (i % width == 0)
               for i, value in enumerate(frame)):
            for i in range(len(frame)):
                frame[i] &= 0x0fff
            return frame, start
    raise ValueError(f"No complete {width}x{height} frame with intact line markers")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw")
    parser.add_argument("output")
    parser.add_argument("--width", type=int, default=648)
    parser.add_argument("--height", type=int, default=480)
    parser.add_argument("--bayer", choices=("bggr", "gbrg"),
                        help="use FFmpeg to demosaic and encode the output (still: bggr, stream: gbrg)")
    parser.add_argument("--vflip", action="store_true", help="flip the preview vertically")
    args = parser.parse_args()
    pixels, start = extract_frame(Path(args.raw).read_bytes(), args.width, args.height)
    # A histogram avoids sorting millions of Python integers on the 512 MB Reader.
    histogram = [0] * 4096
    for value in pixels:
        histogram[value] += 1
    def percentile(fraction):
        count = 0
        for value, frequency in enumerate(histogram):
            count += frequency
            if count > len(pixels) * fraction:
                return value
        return 4095
    low, high = percentile(0.01), percentile(0.99)
    span = max(1, high-low)
    lut = bytes(round(min(1, max(0, (value-low)/span)) ** 0.5 * 255) for value in range(4096))
    preview = bytes(lut[value] for value in pixels)
    if args.bayer:
        command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                   "-f", "rawvideo", "-pixel_format", f"bayer_{args.bayer}8",
                   "-video_size", f"{args.width}x{args.height}", "-i", "pipe:0",
                   "-frames:v", "1"]
        if args.vflip:
            command += ["-vf", "vflip"]
        subprocess.run(command + [args.output], input=preview, check=True)
    else:
        if args.vflip:
            preview = b"".join(preview[i:i+args.width]
                               for i in range(len(preview)-args.width, -1, -args.width))
        Path(args.output).write_bytes(f"P5\n{args.width} {args.height}\n255\n".encode() + preview)
    print(f"Complete frame at word {start}; preview: {args.output}")


if __name__ == "__main__":
    main()
