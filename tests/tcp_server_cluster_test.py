import os
import sys
import time
import logging
import asyncio
import multiprocessing
import argparse
import zlib

def hash64(key_bytes: bytes) -> int:
    # Simple 64-bit hash based on CRC32; inexpensive and stable
    h32 = zlib.crc32(key_bytes) & 0xffffffff
    return (h32 << 32) | h32


def jump_consistent_hash(key_hash: int, buckets: int) -> int:
    # John Lamping & Eric Veach (Google)
    if buckets <= 0:
        raise ValueError("buckets must be > 0")
    b, j = -1, 0
    while j < buckets:
        b = j
        key_hash = (key_hash * 2862933555777941757 + 1) & 0xffffffffffffffff
        j = int((b + 1) * ((1 << 31) / ((key_hash >> 33) + 1)))
    return b

# discover how many ports are actually listening
def detect_workers(base_port, max_workers=256):
    import socket
    count = 0
    for i in range(max_workers):
        s = socket.socket()
        try:
            s.settimeout(0.05)
            s.connect((host, base_port + i))
            count += 1
        except:
            break
        finally:
            s.close()
    return count

parser = argparse.ArgumentParser(description="Clustered cache server functional tests")
parser.add_argument("-p", "--pipeline", action="store_true", help="Use pipelining for requests")
parser.add_argument("-b", "--batch_size", type=int, default=16, help="Batch size for pipelined requests")
args, _ = parser.parse_known_args()
pipelining_enabled = args.pipeline
batch_size = args.batch_size

# Logger config
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)
handler = logging.StreamHandler(sys.stdout)
formatter = logging.Formatter("%(asctime)s - %(levelname)s - %(message)s")
handler.setFormatter(formatter)
logger.addHandler(handler)

# Env config
host = os.environ.get("CACHE_HOST", "localhost")
cluster_base_port = int(os.environ.get("PMC_CLUSTER_BASE_PORT", os.environ.get("CACHE_PORT", 9001)))
delay_sec = float(os.environ.get("TEST_DELAY_SEC", 1))
iterations_count = int(os.environ.get("TEST_ITERATIONS", 1000))
num_processes = int(os.environ.get("TEST_POOL_SIZE", multiprocessing.cpu_count()))
cluster_worker_count = detect_workers(cluster_base_port)

MSG_SEPARATOR = "\x1F"
ENCODED_SEPARATOR = MSG_SEPARATOR.encode()

async def _send_single_command(command: str, reader, writer, buf_size: int = 1024) -> str:
    command_with_sep = command + MSG_SEPARATOR
    writer.write(command_with_sep.encode())
    await writer.drain()

    response = bytearray()
    # For SET/DEL we expect short replies; for GET we allow streaming chunks.
    if command.startswith(("SET", "DEL")):
        response = await reader.readuntil(ENCODED_SEPARATOR)
        if not response:
            raise ConnectionError("Server closed connection (EOF) unexpectedly.")
    else:
        while True:
            chunk = await reader.read(buf_size)
            if not chunk:
                raise ConnectionError("Server closed connection (EOF) unexpectedly.")
            response.extend(chunk)
            if ENCODED_SEPARATOR in chunk:
                break

    return response.decode().rstrip(MSG_SEPARATOR)


def shard_for_index(i: int) -> int:
    # Legacy fallback: shard based on key instead of modulo index
    key_str = f"key{i}"
    key_bytes = key_str.encode("ascii")
    h = hash64(key_bytes)
    return jump_consistent_hash(h, cluster_worker_count)


async def worker_main_cluster(start_idx: int, end_idx: int, task_type: str, pipeline: bool = False, batch_size: int = 1) -> int:
    # Open one persistent TCP connection per shard.
    connections = []
    for shard in range(cluster_worker_count):
        port = cluster_base_port + shard
        reader, writer = await asyncio.open_connection(host, port)
        connections.append((reader, writer))

    async def send_command_shard(shard: int, command: str) -> str:
        reader, writer = connections[shard]
        return await _send_single_command(command, reader, writer, buf_size=8096)

    async def send_with_retry_shard(shard: int, command: str, expected_response: str, retries: int = 1) -> bool:
        nonlocal connections
        for attempt in range(retries + 1):
            try:
                response = await send_command_shard(shard, command)
                if response == expected_response:
                    return True
            except (BrokenPipeError, ConnectionResetError, ConnectionError, asyncio.IncompleteReadError) as e:
                logger.warning(f"Shard {shard}: connection error, reconnecting: {e}")
                # Recreate connection for this shard only.
                reader, writer = connections[shard]
                try:
                    writer.close()
                    await writer.wait_closed()
                except Exception:
                    pass
                port = cluster_base_port + shard
                reader, writer = await asyncio.open_connection(host, port)
                connections[shard] = (reader, writer)
            if attempt < retries:
                await asyncio.sleep(delay_sec / 2)
        logger.error(f"Command failed after retries on shard {shard}: {command}")
        return False

    results = []

    try:
        if not pipeline:
            for i in range(start_idx, end_idx):
                shard = shard_for_index(i)
                if task_type == "set":
                    ok = await send_with_retry_shard(shard, f"SET key{i} value{i}", "OK")
                elif task_type == "get":
                    ok = await send_with_retry_shard(shard, f"GET key{i}", f"value{i}")
                elif task_type == "del":
                    ok = await send_with_retry_shard(shard, f"DEL key{i}", "OK")
                elif task_type == "workflow":
                    ok = (
                        await send_with_retry_shard(shard, f"SET key{i} value{i}", "OK")
                        and await send_with_retry_shard(shard, f"GET key{i}", f"value{i}")
                        and await send_with_retry_shard(shard, "GET non_existent_key", "(nil)")
                    )
                else:
                    raise ValueError("Unknown task_type")
                results.append(ok)
        else:
            i = start_idx
            while i < end_idx:
                batch_end = min(i + batch_size, end_idx)
                # Build per-shard batches.
                commands_by_shard = {}
                expected_by_shard = {}
                for j in range(i, batch_end):
                    shard = shard_for_index(j)
                    cmds = commands_by_shard.setdefault(shard, [])
                    exps = expected_by_shard.setdefault(shard, [])
                    if task_type == "set":
                        cmds.append(f"SET key{j} value{j}")
                        exps.append("OK")
                    elif task_type == "get":
                        cmds.append(f"GET key{j}")
                        exps.append(f"value{j}")
                    elif task_type == "del":
                        cmds.append(f"DEL key{j}")
                        exps.append("OK")
                    elif task_type == "workflow":
                        cmds.extend([
                            f"SET key{j} value{j}",
                            f"GET key{j}",
                            "GET non_existent_key",
                        ])
                        exps.extend(["OK", f"value{j}", "(nil)"])
                    else:
                        raise ValueError("Unknown task_type")

                # Send all shard batches
                for shard, cmds in commands_by_shard.items():
                    reader, writer = connections[shard]
                    payload = MSG_SEPARATOR.join(cmds).encode() + ENCODED_SEPARATOR
                    writer.write(payload)
                    await writer.drain()

                # Read all shard responses
                for shard, expected_list in expected_by_shard.items():
                    reader, writer = connections[shard]
                    for exp in expected_list:
                        try:
                            resp = await reader.readuntil(ENCODED_SEPARATOR)
                        except Exception as exc:
                            logger.error(f"Shard {shard}: pipeline read failed: {exc}")
                            results.append(False)
                            continue
                        results.append(resp.decode().rstrip(MSG_SEPARATOR) == exp)

                i = batch_end
    finally:
        # Cleanup
        for reader, writer in connections:
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass

    return len(results) - sum(results)  # number of failures


def run_worker_cluster(start_idx: int, end_idx: int, task_type: str, pipeline: bool = False, batch_size: int = 1) -> int:
    return asyncio.run(worker_main_cluster(start_idx, end_idx, task_type, pipeline, batch_size))


def run_parallel_cluster(task_type: str, test_name: str, requests_multiplier: int = 1, pipeline: bool = False, batch_size: int = 1) -> int:
    chunk_size = iterations_count // num_processes
    args_list = [
        (
            i * chunk_size,
            (i + 1) * chunk_size if i != num_processes - 1 else iterations_count,
            task_type,
            pipeline,
            batch_size,
        )
        for i in range(num_processes)
    ]
    pipelining_log_str = ""
    if pipeline:
        pipelining_log_str = f"Pipelining is enabled, batch_size = {batch_size}"
    logger.info(
        f"[CLUSTER] Running {test_name} with {iterations_count} iterations, "
        f"{num_processes} processes, {chunk_size} chunk size. {pipelining_log_str}"
    )
    start = time.time()
    ctx = multiprocessing.get_context("fork")

    with ctx.Pool(processes=num_processes) as pool:
        failures = sum(pool.starmap(run_worker_cluster, args_list))
    duration = time.time() - start

    total_requests = iterations_count * requests_multiplier
    rps = total_requests / duration
    print(
        f"{test_name} completed in {duration:.2f}s — RPS: {rps:.2f}, "
        f"Failures: {failures}, Processes: {num_processes}"
    )
    return 1 if failures else 0


def run_set_tests() -> int:
    return run_parallel_cluster("set", "SET tests (cluster)", pipeline=pipelining_enabled, batch_size=batch_size)


def run_get_tests() -> int:
    return run_parallel_cluster("get", "GET tests (cluster)", pipeline=pipelining_enabled, batch_size=batch_size)


def run_del_tests() -> int:
    return run_parallel_cluster("del", "DEL tests (cluster)", pipeline=pipelining_enabled, batch_size=batch_size)


def run_workflow_tests() -> int:
    return run_parallel_cluster(
        "workflow",
        "Workflow tests (cluster)",
        requests_multiplier=3,
        pipeline=pipelining_enabled,
        batch_size=batch_size,
    )


def main() -> None:
    if run_set_tests():
        sys.exit(1)
    time.sleep(delay_sec)

    if run_get_tests():
        sys.exit(1)
    time.sleep(delay_sec * 2)

    if run_del_tests():
        sys.exit(1)
    time.sleep(delay_sec)

    # No JSON preload for cluster tests yet; we focus on core workflow.
    result = run_workflow_tests()
    sys.exit(result)


if __name__ == "__main__":
    main()
