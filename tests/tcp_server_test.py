import os
import sys
import time
import logging
import asyncio
import multiprocessing

# Logger config
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)
handler = logging.StreamHandler(sys.stdout)
formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
logger.addHandler(handler)

# Env config
host = os.environ.get('CACHE_HOST', 'localhost')
port = int(os.environ.get('CACHE_PORT', 9001))
delay_sec = float(os.environ.get('TEST_DELAY_SEC', 1))
iterations_count = int(os.environ.get('TEST_ITERATIONS', 1000))
pool_size = int(os.environ.get('TEST_POOL_SIZE', multiprocessing.cpu_count()))
data_folder = os.environ.get('TEST_DATA_FOLDER', './data')
MSG_SEPARATOR = '\x1F'
ENCODED_SEPARATOR = MSG_SEPARATOR.encode()

class ConnectionPool:
    def __init__(self, size):
        self.size = size
        self.connections = asyncio.Queue()
        self.initialized = False

    async def initialize(self):
        for _ in range(self.size):
            reader, writer = await asyncio.open_connection(host, port)
            await self.connections.put((reader, writer))
        self.initialized = True

    async def acquire(self):
        return await self.connections.get()

    async def release(self, conn):
        await self.connections.put(conn)

    async def close(self):
        while not self.connections.empty():
            reader, writer = await self.connections.get()
            writer.close()
            await writer.wait_closed()

    async def discard(self, conn):
        reader, writer = conn
        try:
            writer.close()
            await writer.wait_closed()
        except:
            pass

async def send_command(command: str, conn_pool: ConnectionPool, buf_size=1024):
    reader, writer = await conn_pool.acquire()
    discard_connection = False

    try:
        command += MSG_SEPARATOR
        writer.write(command.encode())
        await writer.drain()

        response = bytearray()
        if command.startswith(("SET", "DEL")):
            response = await reader.readuntil(ENCODED_SEPARATOR)
            if not response:
                discard_connection = True
                raise ConnectionError("Server closed connection (EOF) unexpectedly.")
        else:
            while True:
                chunk = await reader.read(buf_size)
                if not chunk:
                    discard_connection = True
                    raise ConnectionError("Server closed connection (EOF) unexpectedly.")
                response.extend(chunk)
                if ENCODED_SEPARATOR in chunk:
                    break

        return response.decode().rstrip(MSG_SEPARATOR)

    except (BrokenPipeError, ConnectionResetError, ConnectionError) as e:
        discard_connection = True
        logger.warning(f"Connection error, discarding connection: {e}")
        raise

    finally:
        if discard_connection:
            await conn_pool.discard((reader, writer))
            # Re-establish a new connection to keep pool size constant
            try:
                new_conn = await asyncio.open_connection(host, port)
                await conn_pool.release(new_conn)
            except Exception as e:
                logger.error(f"Failed to replenish discarded connection: {e}")
        else:
            await conn_pool.release((reader, writer))



async def send_with_retry(command, expected_response, conn_pool, retries=1, delay=None):
    delay = delay or (delay_sec / 2)
    for attempt in range(retries + 1):
        try:
            response = await send_command(command, conn_pool)
            if response == expected_response:
                return True
        except Exception as e:
            logger.error(f"Exception during '{command}': {e}")
        if attempt < retries:
            await asyncio.sleep(delay)
    logger.error(f"Command failed after retries: {command}")
    return False

async def preload_json_files(conn_pool):
    logger.info("Preloading JSON files...")
    if not os.path.exists(data_folder):
        logger.warning("Data folder not found. Skipping.")
        return

    for file_name in filter(lambda f: f.endswith('.json'), os.listdir(data_folder)):
        with open(os.path.join(data_folder, file_name), "r", encoding="utf-8") as f:
            content = f.read()
        key = file_name.rsplit('.', 1)[0]
        if not await send_with_retry(f'SET {key} {content}', 'OK', conn_pool):
            logger.error(f"Failed storing {key}")
            sys.exit(1)
        logger.info(f"Stored {key}")
        await asyncio.sleep(delay_sec)

def build_iteration_func(name):
    async def workflow(x, pool):  # SET, GET, GET (miss)
        return await send_with_retry(f"SET key{x} value{x}", "OK", pool) and \
               await send_with_retry(f"GET key{x}", f"value{x}", pool) and \
               await send_with_retry("GET non_existent_key", "(nil)", pool)

    async def op(x, pool):        
        if name == "set":
            return await send_with_retry(f"SET key{x} value{x}", "OK", pool)
        elif name == "get":
            return await send_with_retry(f"GET key{x}", f"value{x}", pool)
        elif name == "del":
            return await send_with_retry(f"DEL key{x}", "OK", pool)
        else:
            raise ValueError(f"Invalid operation: {name}")

    return workflow if name == "workflow" else op

async def safe_call(coro):
    try:
        return await coro
    except Exception as e:
        logger.error(f"Error in task: {e}")
        return False

def run_worker(start_idx, end_idx, task_type):
    async def worker_main():
        pool = ConnectionPool(1)
        await pool.initialize()
        func = build_iteration_func(task_type)

        tasks = [asyncio.create_task(func(i, pool)) for i in range(start_idx, end_idx)]
        results = await asyncio.gather(*tasks)

        await pool.close()  # <- Properly close here
        return len(results) - sum(results)  # return just failures

    return asyncio.run(worker_main())


def run_parallel(task_type, test_name, requests_multiplier=1):
    num_processes = pool_size
    chunk_size = iterations_count // num_processes
    args_list = [
        (i * chunk_size, (i + 1) * chunk_size if i != num_processes - 1 else iterations_count, task_type)
        for i in range(num_processes)
    ]
    logger.info(f"Running {test_name} with {iterations_count} iterations {num_processes} processes and {chunk_size} chunks per process ...")
    start = time.time()
    ctx = multiprocessing.get_context("fork")

    with ctx.Pool(processes=num_processes) as pool:
        failures = sum(pool.starmap(run_worker, args_list))
    duration = time.time() - start

    total_requests = iterations_count * requests_multiplier
    rps = total_requests / duration
    print(f"{test_name} completed in {duration:.2f}s — RPS: {rps:.2f}, Failures: {failures}, Processes: {num_processes}")
    return 1 if failures else 0

def run_set_tests(): return run_parallel("set", "SET tests")
def run_get_tests(): return run_parallel("get", "GET tests")
def run_del_tests(): return run_parallel("del", "DEL tests")
def run_workflow_tests(): return run_parallel("workflow", "Workflow tests", requests_multiplier=3)

def main():
    if run_set_tests(): sys.exit(1)
    time.sleep(delay_sec)

    if run_get_tests(): sys.exit(1)
    time.sleep(delay_sec)

    if run_del_tests(): sys.exit(1)
    time.sleep(delay_sec)

    asyncio.run(preload_json_files(ConnectionPool(pool_size)))
    time.sleep(delay_sec)

    result = run_workflow_tests()
    sys.exit(result)

main()
