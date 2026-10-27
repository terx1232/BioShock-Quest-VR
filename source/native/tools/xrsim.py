"""Stands in for WinlatorXR on the PC: sends XrAPI 0.5 headset packets to the mod over UDP.

The head turns slowly left and right (and nods a little), so the camera in the game should
sweep the same way if the mod's camera hook and math are right. Also prints what the mod sends
back (VR mode, alternate eye, FOV).

    py -3 xrsim.py [--seconds 60] [--yaw 35] [--pitch 10]
"""
import argparse
import math
import socket
import threading
import time


def quat_from_yaw_pitch(yaw_deg, pitch_deg):
    """OpenXR quaternion: yaw around +Y (left positive), then pitch around +X (up positive)."""
    y = math.radians(yaw_deg) / 2
    p = math.radians(pitch_deg) / 2
    qy = (0.0, math.sin(y), 0.0, math.cos(y))
    qp = (math.sin(p), 0.0, 0.0, math.cos(p))
    # q = qy * qp
    x1, y1, z1, w1 = qy
    x2, y2, z2, w2 = qp
    return (w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
            w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2)


def packet(t, args, sync):
    if args.static:
        yaw, pitch = args.yaw, args.pitch
    else:
        yaw = args.yaw * math.sin(t * 2 * math.pi / 8)        # 8 s per sweep
        pitch = args.pitch * math.sin(t * 2 * math.pi / 5)
    hq = quat_from_yaw_pitch(yaw, pitch)
    head = (args.x, 1.7 if args.static else 1.7 + 0.05 * math.sin(t), args.z)
    left = (0, 0, 0, 1)
    right = (0, 0, 0, 1)
    nums = [*left, 0, 0, -0.18, 1.45, -0.35,
            *right, 0, 0, args.rx, args.ry, args.rz,
            *hq, *head,
            0.063, 100.0, 100.0]
    text = "client0 " + " ".join(f"{v:.4f}" for v in nums)
    text += f" {sync} " + "F" * 19
    text += " 0.000" + " 0 0 0 1" * 2 + " FF"
    return text.encode(), yaw, pitch


def listen_replies():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.bind(("127.0.0.1", 7278))
    except OSError as e:
        print(f"(cannot listen on 7278 for replies: {e})")
        return
    last = None
    while True:
        data, _ = s.recvfrom(512)
        msg = data.decode(errors="replace")
        if msg != last:
            print(f"  mod -> WinlatorXR: {msg}  (haptics L R, VR, 3D mode, FOV x y)")
            last = msg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--yaw", type=float, default=35)
    ap.add_argument("--pitch", type=float, default=10)
    ap.add_argument("--x", type=float, default=0.0, help="head position x, m (right)")
    ap.add_argument("--z", type=float, default=0.0, help="head position z, m (back; forward is negative)")
    ap.add_argument("--rx", type=float, default=0.18, help="right hand x, m")
    ap.add_argument("--ry", type=float, default=1.45, help="right hand y (height), m")
    ap.add_argument("--rz", type=float, default=-0.35, help="right hand z (forward is negative), m")
    ap.add_argument("--static", action="store_true", help="head held at --yaw/--pitch instead of sweeping")
    args = ap.parse_args()
    threading.Thread(target=listen_replies, daemon=True).start()
    out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    start = time.time()
    sync = 0
    n = 0
    while time.time() - start < args.seconds:
        t = time.time() - start
        data, yaw, pitch = packet(t, args, sync)
        for port in (7872, 7873):
            out.sendto(data, ("127.0.0.1", port))
        sync = (sync + 12) % 252
        if n % 90 == 0:
            print(f"t={t:5.1f}s head yaw={yaw:+6.1f} pitch={pitch:+5.1f}")
        n += 1
        time.sleep(1 / 90)


if __name__ == "__main__":
    main()
