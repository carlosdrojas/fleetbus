"""MOCK(M6): gateway entry point. Real version batches CAN frames into FBP datagrams, keeps a retransmit window and a disk spool."""
import argparse
import socket
import time

import fbp

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", action="store_true", help="use simulated nodes instead of a CAN adapter")
    ap.add_argument("--backend", default="127.0.0.1:9000")
    args = ap.parse_args()
    host, port = args.backend.split(":")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for seq in range(1, 4):  # MOCK: three canned heartbeats, no CAN input, no retransmit
        sock.sendto(fbp.encode(2, 1, seq), (host, int(port)))
        time.sleep(0.1)
    print("MOCK(M6): sent 3 canned heartbeats")

if __name__ == "__main__":
    main()
