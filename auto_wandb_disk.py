import subprocess
import time
import sys
import os
import psutil
import wandb

HOST = "127.0.0.1"
PORT = 8080
DURATION = 300
WARMUP_PERIOD = 60
THREAD_LEVELS = [1, 5, 10, 30, 60, 90, 120]
SERVER_PROCESS_NAME = "server"

def find_server_process():
    for proc in psutil.process_iter(['pid', 'name']):
        if proc.info['name'] == SERVER_PROCESS_NAME:
            return proc
    return None

def run_sweep(workload):
    server_proc = find_server_process()
    if not server_proc:
        print(f"ERROR: Could not find running './{SERVER_PROCESS_NAME}' process.")
        print("Please start the server in a separate terminal first!")
        return

    run_name = f"Auto_Scalability_Disk_Final_{workload}"
    print(f"\n=== STARTING DISK SCALABILITY RUN: {run_name} ===")

    wandb.init(project="kv-store-scalability", name=run_name)

    wandb.define_metric("Threads")
    wandb.define_metric("Throughput", step_metric="Threads")
    wandb.define_metric("Latency", step_metric="Threads")
    wandb.define_metric("Server CPU %", step_metric="Threads")

    for threads in THREAD_LEVELS:
        print(f"\n------------------------------------------------")
        print(f" RUNNING: {threads} Threads | {DURATION} Seconds")
        print(f"------------------------------------------------")

        cmd = [
            "taskset", "-c", "2-4",
            "./load_generator", HOST, str(PORT), str(threads), str(DURATION), workload
        ]

        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1
        )

        ss_throughput = []
        ss_latency = []
        ss_cpu = []

        start_time = time.time()

        while True:
            line = process.stdout.readline()
            if not line and process.poll() is not None:
                break

            if line:
                print(line.strip())

                if "[METRICS]" in line:
                    current_time = time.time()
                    elapsed = current_time - start_time

                    try:
                        parts = line.strip().split()
                        t = float(parts[1])
                        l = float(parts[2])
                    except:
                        continue

                    try:
                        cpu_usage = server_proc.cpu_percent(interval=None)
                    except:
                        cpu_usage = 0

                    if int(elapsed) % 30 == 0 and int(elapsed) > 0:
                        sys.stdout.write(f"\r[Progress] {int(elapsed)}s / {DURATION}s")
                        sys.stdout.flush()

                    if elapsed > WARMUP_PERIOD:
                        ss_throughput.append(t)
                        ss_latency.append(l)
                        ss_cpu.append(cpu_usage)

        print(f"\nLevel Complete.")

        avg_t = sum(ss_throughput) / len(ss_throughput) if ss_throughput else 0
        avg_l = sum(ss_latency) / len(ss_latency) if ss_latency else 0
        avg_cpu = sum(ss_cpu) / len(ss_cpu) if ss_cpu else 0

        print(f" Sending to W&B: Threads={threads}, T={avg_t:.0f}, L={avg_l:.2f}, CPU={avg_cpu:.1f}%")
        wandb.log({
            "Threads": threads,
            "Throughput": avg_t,
            "Latency": avg_l,
            "Server CPU %": avg_cpu
        })

        time.sleep(5)

    wandb.finish()
    print("\n=== SWEEP COMPLETE ===")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python auto_wandb_disk.py <WORKLOAD>")
    else:
        run_sweep(sys.argv[1])