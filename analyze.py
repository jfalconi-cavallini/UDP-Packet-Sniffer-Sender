#!/usr/bin/env python3
"""
analyze.py

Reads a sniffer CSV (seq,recv_bytes,latency_us,recv_ns) and prints:
- Count of valid latencies
- Mean / Median / p99 latency (microseconds)
- Estimated packet loss from sequence gaps

Usage:
    python3 analyze.py out.csv
"""

import csv
import sys
import statistics

def p_quantile(values, q):
    """
    Compute the q-quantile (e.g., q=0.99 for p99) using
    the common linear interpolation between closest ranks.
    Assumes values is non-empty and sorted.
    """
    if not values:
        return float("nan")
    if q <= 0: return values[0]
    if q >= 1: return values[-1]
    # position in 0..n-1
    pos = (len(values) - 1) * q
    lo = int(pos)
    hi = min(lo + 1, len(values) - 1)
    frac = pos - lo
    return values[lo] * (1 - frac) + values[hi] * frac

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 analyze.py <out.csv>")
        sys.exit(1)

    path = sys.argv[1]
    latencies = []  # store latency_us >= 0
    seq_min = None
    seq_max = None
    seen = 0

    try:
        with open(path, newline="") as f:
            rdr = csv.DictReader(f)
            # Expect headers: seq,recv_bytes,latency_us,recv_ns
            for row in rdr:
                try:
                    seq = int(row["seq"])
                    seen += 1
                    if seq_min is None or seq < seq_min: seq_min = seq
                    if seq_max is None or seq > seq_max: seq_max = seq
                except Exception:
                    # Ignore bad/missing seq lines
                    pass

                try:
                    lat_us = float(row["latency_us"])
                    if lat_us >= 0:
                        latencies.append(lat_us)
                except Exception:
                    # ignore malformed latency
                    pass
    except FileNotFoundError:
        print(f"File not found: {path}")
        sys.exit(1)

    # Stats
    valid = len(latencies)
    if valid == 0:
        print("No valid latency records found.")
        sys.exit(0)

    latencies.sort()
    mean_us = statistics.fmean(latencies)
    median_us = statistics.median(latencies)
    p99_us = p_quantile(latencies, 0.99)

    # Simple packet-loss estimate from seq gaps (best-effort)
    loss_est = None
    if seq_min is not None and seq_max is not None and seen > 0:
        expected = seq_max - seq_min + 1
        if expected > 0:
            missing = expected - seen
            loss_est = max(0, missing) / expected * 100.0

    print("Latency stats (microseconds):")
    print(f"  count : {valid}")
    print(f"  mean  : {mean_us:.2f} µs")
    print(f"  median: {median_us:.2f} µs")
    print(f"  p99   : {p99_us:.2f} µs")

    if loss_est is not None:
        print(f"\nEstimated packet loss: {loss_est:.3f}% "
              f"(seen={seen}, seq_range=[{seq_min},{seq_max}])")

if __name__ == "__main__":
    main()
