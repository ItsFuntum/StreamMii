import socket
import struct
import numpy as np
import cv2
import time
import lz4.block

HEADER_SIZE = 29

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

sock.bind(("0.0.0.0", 4242))

sock.settimeout(1.0)

print("Listening")

frames = {}
frame_times = {}

while True:
    try:
        packet, addr = sock.recvfrom(8192)

    except TimeoutError:
        now = time.time()

        for old_frame in list(frames):
            if now - frame_times.get(old_frame, now) > 1:
                frames.pop(old_frame, None)
                frame_times.pop(old_frame, None)

        continue

    if len(frames) > 10:
        oldest = min(frames.keys())
        frames.pop(oldest, None)
        frame_times.pop(oldest, None)

    header = packet[:HEADER_SIZE]
    payload = packet[HEADER_SIZE:]

    magic, frame, index, count, width, height, pitch, compressedSize, originalSize, payloadSize, compression = struct.unpack("!IIHHHHHIIHB", header)

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

        if compression == 1:
            raw = lz4.block.decompress(
                raw,
                uncompressed_size=originalSize
            )

        print(
            "Frame:",
            frame,
            "Width:",
            width,
            "Height:",
            height,
            "Original Size:",
            originalSize,
            "compressed Size:",
            compressedSize,
        )

        if len(raw) != originalSize:
            print("Incomplete frame")
            del frames[frame]
            continue

        pixels = np.frombuffer(raw, dtype=np.uint16)

        pixels = pixels.reshape((height, width))

        # Remove padding pixels
        pixels = pixels[:, :width]

        # Convert RGB565 to RGB888
        r = (pixels & 0x1F).astype(np.uint8)
        g = ((pixels >> 5) & 0x3F).astype(np.uint8)
        b = ((pixels >> 11) & 0x1F).astype(np.uint8)

        r = (r << 3) | (r >> 2)
        g = (g << 2) | (g >> 4)
        b = (b << 3) | (b >> 2)

        image = np.stack((b, g, r), axis=-1)

        cv2.imshow("StreamMii", image)

        cv2.waitKey(1)

        now = time.time()

        del frames[frame]
        del frame_times[frame]