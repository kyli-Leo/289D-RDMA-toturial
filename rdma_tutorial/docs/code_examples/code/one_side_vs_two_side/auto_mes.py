#!/usr/bin/env python3
import subprocess
import re
import csv
from pathlib import Path
import matplotlib.pyplot as plt


# ================== 配置区 ==================

# 改成你的 server IP
SERVER_IP = "144.202.54.39"
PORT = 9000

# 可执行文件路径（相对脚本位置或绝对路径）
BENCH_CLIENT = "./bench_client"
BENCH_SERVER = "./bench_server"

# 结果输出（用一个新的文件，避免和之前混在一起）
RESULT_CSV = "rdma_msg_sweep_test.csv"
PLOT_DIR = Path("plots_msg_sweep_test")

# 实验参数
FIXED_WINDOW = 4  # 固定 window
ITERS = 200000  # 每组实验的迭代次数
MSG_LIST = [32, 64, 128, 256, 512, 1024, 2048, 4096, 8192]

MODES = ["write", "send"]  # 测这两个模式


# ================== 工具函数 ==================

CLIENT_LINE_RE = re.compile(
    r"\[client\]\s+(\w+)\s+done:\s+([0-9.]+)\s+Mops,\s+([0-9.]+)\s+GiB/s"
)


def run_client(mode: str, msg: int, iters: int, window: int):
    """运行 bench_client 并解析 Mops / GiB/s。
    如果 bench_client 返回非 0，则返回 None。
    """
    cmd = [
        BENCH_CLIENT,
        SERVER_IP,
        str(PORT),
        "--mode",
        mode,
        "--msg",
        str(msg),
        "--iters",
        str(iters),
        "--window",
        str(window),
    ]
    print("\n=== Running client ===")
    print(" ".join(cmd))

    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print("!! bench_client exited with non-zero code:", proc.returncode)
        print("stdout:\n", proc.stdout)
        print("stderr:\n", proc.stderr)
        # 比如 RNR retry exceeded 之类的错误，返回 None 让上层记录 NaN
        return None

    print("client stdout:\n", proc.stdout.strip())

    # 找最后一行 [client] ... done
    m = None
    for line in proc.stdout.splitlines()[::-1]:
        m = CLIENT_LINE_RE.search(line)
        if m:
            break
    if not m:
        raise RuntimeError("Cannot parse client output for Mops/GiB/s")

    mode_str, mops_str, gib_str = m.groups()
    return {
        "mode": mode_str,
        "mops": float(mops_str),
        "gib": float(gib_str),
        "raw_stdout": proc.stdout.strip(),
    }


def ask_start_server(mode: str, msg: int, iters: int):
    """提示你在 server 上启动 bench_server, 并等待回车。"""
    if mode == "send":
        srv_cmd = f"{BENCH_SERVER} {PORT} --mode send --msg {msg} --iters {iters} --recv-depth {max(256, FIXED_WINDOW*4)}"
    elif mode in ("write", "read"):
        srv_cmd = f"{BENCH_SERVER} {PORT} --mode {mode} --msg {msg} --iters {iters}"
    else:
        raise ValueError(f"Unknown mode: {mode}")

    print("\n========================================")
    print(f"在 SERVER 机器上运行（手动）:")
    print(f"  {srv_cmd}")
    print("确认 server 已经启动后, 在本窗口按回车继续...")
    input("Press ENTER to run client...")


def append_result_csv(rows):
    """把结果追加写入 CSV 文件。第一次写会加表头。"""
    file_exists = Path(RESULT_CSV).exists()
    fieldnames = [
        "experiment",
        "mode",
        "msg",
        "window",
        "iters",
        "mops",
        "gib",
    ]
    with open(RESULT_CSV, "a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        if not file_exists:
            writer.writeheader()
        for r in rows:
            writer.writerow(r)


# ================== 实验：固定 window, 扫 msg ==================


def run_msg_sweep():
    """固定 window, 扫 message size。"""
    print(f"\n\n===== 固定 window={FIXED_WINDOW}, 扫 message size (write & send) =====")
    results = []

    for msg in MSG_LIST:
        for mode in MODES:
            print(f"\n--- Msg sweep: msg={msg}, window={FIXED_WINDOW}, mode={mode} ---")
            ask_start_server(mode, msg, ITERS)
            data = run_client(
                mode=mode,
                msg=msg,
                iters=ITERS,
                window=FIXED_WINDOW,
            )

            if data is None:
                # 某个组合失败，比如 RNR retry exceeded
                print(
                    f"*** 组合失败: msg={msg}, window={FIXED_WINDOW}, mode={mode}，记录为 NaN，继续下一组 ***"
                )
                row = {
                    "experiment": "msg_sweep",
                    "mode": mode,
                    "msg": msg,
                    "window": FIXED_WINDOW,
                    "iters": ITERS,
                    "mops": float("nan"),
                    "gib": float("nan"),
                }
            else:
                row = {
                    "experiment": "msg_sweep",
                    "mode": mode,
                    "msg": msg,
                    "window": FIXED_WINDOW,
                    "iters": ITERS,
                    "mops": data["mops"],
                    "gib": data["gib"],
                }

            results.append(row)
            print(
                f"Recorded: mode={mode}, msg={msg}, window={FIXED_WINDOW}, "
                f"Mops={row['mops']}, GiB/s={row['gib']}"
            )

    append_result_csv(results)
    print("\nMsg sweep 实验完成, 结果已写入", RESULT_CSV)


# ================== 画图 ==================


def load_results():
    import pandas as pd

    df = pd.read_csv(RESULT_CSV)
    return df


def plot_results():
    import pandas as pd

    PLOT_DIR.mkdir(exist_ok=True)
    df = load_results()

    sweep = df[(df["experiment"] == "msg_sweep")]
    if sweep.empty:
        print("没有 msg_sweep 实验数据，先跑实验再画图。")
        return

    # GiB/s vs msg
    plt.figure()
    for mode in MODES:
        s = sweep[sweep["mode"] == mode].sort_values("msg")
        if s.empty:
            continue
        plt.plot(s["msg"], s["gib"], marker="o", label=f"{mode}")
    plt.xlabel("Message size (bytes)")
    plt.ylabel("Throughput (GiB/s)")
    plt.title(f"Throughput vs message size (window={FIXED_WINDOW})")
    plt.xscale("log", base=2)  # 消息大小差距大，用 log2 x 轴更清晰
    plt.legend()
    plt.grid(True, linestyle="--", alpha=0.5)
    plt.tight_layout()
    plt.savefig(PLOT_DIR / f"msg_sweep_gib_w{FIXED_WINDOW}.png", dpi=200)
    plt.close()

    # Mops vs msg
    plt.figure()
    for mode in MODES:
        s = sweep[sweep["mode"] == mode].sort_values("msg")
        if s.empty:
            continue
        plt.plot(s["msg"], s["mops"], marker="o", label=f"{mode}")
    plt.xlabel("Message size (bytes)")
    plt.ylabel("Operations (Mops)")
    plt.title(f"Ops vs message size (window={FIXED_WINDOW})")
    plt.xscale("log", base=2)
    plt.legend()
    plt.grid(True, linestyle="--", alpha=0.5)
    plt.tight_layout()
    plt.savefig(PLOT_DIR / f"msg_sweep_mops_w{FIXED_WINDOW}.png", dpi=200)
    plt.close()

    print(f"\n画图完成, 图片保存在目录: {PLOT_DIR.resolve()}")


# ================== main ==================


def main():
    print("本脚本假设:")
    print(f"  client 上可以直接运行: {BENCH_CLIENT}")
    print(f"  server 上可以直接运行: {BENCH_SERVER}")
    print(f"  server IP = {SERVER_IP}, port = {PORT}")
    print(f"  固定 window = {FIXED_WINDOW}")
    print("\n操作选项:")

    while True:
        print("\n请选择操作:")
        print("  1) 运行 固定 window 的 msg sweep 实验")
        print("  2) 只画图（使用已有 CSV)")
        print("  q) 退出")
        choice = input("> ").strip().lower()
        if choice == "1":
            run_msg_sweep()
        elif choice == "2":
            plot_results()
        elif choice == "q":
            break
        else:
            print("无效输入, 请重新选择。")


if __name__ == "__main__":
    main()
