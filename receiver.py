import socket
import struct
import numpy as np
import cv2
import time
import lz4.block

HEADER_SIZE = 30

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

    magic, frame, index, count, width, height, pitch, compressedSize, originalSize, payloadSize, compression, keyframe = struct.unpack("!IIHHHHHIIHBB", header)

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

            pixels = pixels.reshape((height, width))

            r = (pixels & 0x1F).astype(np.uint8)
            g = ((pixels >> 5) & 0x3F).astype(np.uint8)
            b = ((pixels >> 11) & 0x1F).astype(np.uint8)

            r = (r << 3) | (r >> 2)
            g = (g << 2) | (g >> 4)
            b = (b << 3) | (b >> 2)

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