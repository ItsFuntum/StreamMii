import socket
import struct
import numpy as np
import cv2
import time
import lz4.block

sRGBGammaLUT = np.array([
    0x00,0x0C,0x15,0x1C,0x21,0x26,0x2A,0x2E,0x31,0x34,0x37,0x3A,0x3D,0x3F,0x42,0x44,
    0x46,0x49,0x4B,0x4D,0x4F,0x51,0x52,0x54,0x56,0x58,0x59,0x5B,0x5D,0x5E,0x60,0x61,
    0x63,0x64,0x66,0x67,0x68,0x6A,0x6B,0x6D,0x6E,0x6F,0x70,0x72,0x73,0x74,0x75,0x76,
    0x78,0x79,0x7A,0x7B,0x7C,0x7D,0x7E,0x7F,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,
    0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,
    0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9D,0x9E,0x9F,0xA0,0xA1,0xA1,0xA2,0xA3,0xA4,
    0xA5,0xA5,0xA6,0xA7,0xA8,0xA8,0xA9,0xAA,0xAB,0xAB,0xAC,0xAD,0xAE,0xAE,0xAF,0xB0,
    0xB0,0xB1,0xB2,0xB3,0xB3,0xB4,0xB5,0xB5,0xB6,0xB7,0xB7,0xB8,0xB9,0xB9,0xBA,0xBB,
    0xBB,0xBC,0xBD,0xBD,0xBE,0xBF,0xBF,0xC0,0xC1,0xC1,0xC2,0xC2,0xC3,0xC4,0xC4,0xC5,
    0xC5,0xC6,0xC7,0xC7,0xC8,0xC9,0xC9,0xCA,0xCA,0xCB,0xCC,0xCC,0xCD,0xCD,0xCE,0xCE,
    0xCF,0xD0,0xD0,0xD1,0xD1,0xD2,0xD2,0xD3,0xD3,0xD4,0xD4,0xD5,0xD5,0xD6,0xD6,0xD7,
    0xD7,0xD8,0xD9,0xD9,0xDA,0xDA,0xDB,0xDB,0xDC,0xDC,0xDD,0xDD,0xDE,0xDE,0xDF,0xDF,
    0xE0,0xE1,0xE1,0xE2,0xE2,0xE3,0xE3,0xE4,0xE4,0xE5,0xE5,0xE6,0xE6,0xE7,0xE7,0xE8,
    0xE9,0xE9,0xEA,0xEA,0xEB,0xEB,0xEC,0xEC,0xED,0xED,0xED,0xEE,0xEE,0xEF,0xEF,0xF0,
    0xF0,0xF1,0xF1,0xF2,0xF2,0xF3,0xF3,0xF4,0xF4,0xF5,0xF5,0xF5,0xF6,0xF6,0xF7,0xF7,
    0xF8,0xF8,0xF9,0xF9,0xFA,0xFA,0xFB,0xFB,0xFB,0xFC,0xFC,0xFD,0xFD,0xFE,0xFE,0xFE
], dtype=np.uint8)

HEADER_SIZE = 31

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

sock.bind(("0.0.0.0", 4242))

sock.settimeout(1.0)

print("Listening")

frames = {}
frame_times = {}

previous_frame = None

bytes_received = 0
last_stat_time = time.time()
mbps = 0

frames_received = 0
last_fps_time = time.time()

while True:
    try:
        packet, addr = sock.recvfrom(65535)

    except TimeoutError:
        now = time.time()

        for old_frame in list(frames):
            if now - frame_times.get(old_frame, now) > 1:
                frames.pop(old_frame, None)
                frame_times.pop(old_frame, None)

        continue

    bytes_received += len(packet)

    if len(frames) > 10:
        oldest = min(frames.keys())
        frames.pop(oldest, None)
        frame_times.pop(oldest, None)

    header = packet[:HEADER_SIZE]
    payload = packet[HEADER_SIZE:]

    magic, frame, index, count, width, height, pitch, compressedSize, originalSize, payloadSize, compression, keyframe, needsSRGB = struct.unpack("!IIHHHHHIIHBBB", header)

    payload = packet[HEADER_SIZE:HEADER_SIZE+payloadSize]

    if len(payload) != payloadSize:
        print("Bad packet")
        continue

    if len(packet) < HEADER_SIZE:
        print("Short packet")
        continue

    if magic != 0x5354524D:
        continue

    if frame not in frames:
        frames[frame] = [None] * count
        frame_times[frame] = time.time()

    frames[frame][index] = payload

    if all(frames[frame]):

        raw = b''.join(frames[frame])

        if compression == 0:    # None
            pass

        elif compression == 1:  # LZ4 full frame/keyframe
            raw = lz4.block.decompress(
                raw,
                uncompressed_size=originalSize
            )

            if keyframe:
                previous_frame = raw

        elif compression == 2:  # Delta LZ4
            if previous_frame is None:
                print("Waiting for keyframe")
                del frames[frame]
                del frame_times[frame]
                continue

            if len(previous_frame) != originalSize:
                print("Previous frame size mismatch, waiting for keyframe")
                previous_frame = None
                continue

            raw = lz4.block.decompress(
                raw,
                uncompressed_size=originalSize
            )

            delta = np.frombuffer(raw, dtype=np.uint8)
            previous = np.frombuffer(previous_frame, dtype=np.uint8)

            raw = np.bitwise_xor(delta, previous).tobytes()

            previous_frame = raw

        elif compression == 3:  # JPEG
            image = cv2.imdecode(
                np.frombuffer(raw, dtype=np.uint8),
                cv2.IMREAD_COLOR
            )

            if image is None:
                print("JPEG decode failed")
                del frames[frame]
                del frame_times[frame]
                continue

        else:
            print("Unknown compression:", compression)
            del frames[frame]
            del frame_times[frame]
            continue

        now = time.time()

        if now - last_stat_time >= 1.0:
            elapsed = now - last_stat_time

            mbps = bytes_received * 8 / elapsed / 1_000_000.0

            bytes_received = 0
            last_stat_time = now

        print(
            "Frame:",
            frame,
            "Width:",
            width,
            "Height:",
            height,
            "Original Size:",
            originalSize,
            "Compressed Size:",
            compressedSize,
        )

        if compression != 3:
            if len(raw) != originalSize:
                print("Incomplete frame")
                del frames[frame]
                continue

            pixels = np.frombuffer(raw, dtype=np.uint16)

            row_pixels = pitch // 2

            pixels = pixels.reshape((height, row_pixels))
            
            # Remove GX2 pitch padding
            pixels = pixels[:, :width]

            r = (pixels & 0x1F).astype(np.uint8)
            g = ((pixels >> 5) & 0x3F).astype(np.uint8)
            b = ((pixels >> 11) & 0x1F).astype(np.uint8)

            r = (r << 3) | (r >> 2)
            g = (g << 2) | (g >> 4)
            b = (b << 3) | (b >> 2)

            if needsSRGB:
                r = sRGBGammaLUT[r]
                g = sRGBGammaLUT[g]
                b = sRGBGammaLUT[b]

            image = np.stack((b, g, r), axis=-1)

        frames_received += 1

        now = time.time()

        if now - last_fps_time >= 1.0:
            fps = frames_received / (now - last_fps_time)

            print(f"{mbps:.2f} Mb/s | FPS: {fps:.1f}")

            frames_received = 0
            last_fps_time = now

        cv2.imshow("StreamMii", image)

        cv2.waitKey(1)

        now = time.time()

        del frames[frame]
        del frame_times[frame]