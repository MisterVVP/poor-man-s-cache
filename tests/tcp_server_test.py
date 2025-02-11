import socket
import logging
import sys
import time
import os
import redis

from multiprocessing import Pool, cpu_count

# configure logger
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)
handler = logging.StreamHandler(sys.stdout)
handler.setLevel(logging.DEBUG)
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
logger.addHandler(handler)

# Configuration from environment or defaults
host = os.environ.get('CACHE_HOST', 'localhost')
port = int(os.environ.get('CACHE_PORT', 9001))
delay_sec = int(os.environ.get('TEST_DELAY_SEC', 90))
iterations_count = int(os.environ.get('TEST_ITERATIONS', 1000))
cache_type = os.environ.get('CACHE_TYPE', 'custom')  # "custom" or "redis"
redis_password = os.environ.get('REDIS_PASSWORD', None)  # Only for Redis
data_folder = os.environ.get('TEST_DATA_FOLDER', './data')

redis_client = None

if cache_type == 'redis':
    # Create a single Redis client globally
    redis_client = redis.StrictRedis(host=host, port=port, password=redis_password, decode_responses=True)

def calc_thread_pool_size():
    thread_pool_size = 2
    if iterations_count >= 100000000:
        thread_pool_size = 24
    elif iterations_count >= 10000000:
        thread_pool_size = 16
    elif iterations_count >= 1000000:
        thread_pool_size = 12
    elif iterations_count >= 100000:
        thread_pool_size = 8
    elif iterations_count >= 10000:
        thread_pool_size = 4

    return min(thread_pool_size, cpu_count())

def send_command_to_custom_cache(command: str, bufSize: int):
    """Send a command to the custom TCP server, ensuring a unit separator (0x1F) is sent at the end.
       When executing a GET command, it will read until the MSG_SEPARATOR (0x1F) is found.
    """
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((host, port))
            command += "\x1F"
            s.sendall(command.encode('utf-8'))

            if command.startswith("SET"):
                response = s.recv(bufSize).decode('utf-8')
                return response.strip().rstrip("\x1F")
            else:  # Handling GET or other commands
                s.settimeout(10)
                response = bytearray()
                while True:
                    try:
                        chunk = s.recv(bufSize)
                        if not chunk:
                            break  # Server closed connection

                        response.extend(chunk)

                        # Check if MSG_SEPARATOR (0x1F) is in the received data
                        if b'\x1F' in chunk:
                            break
                    except socket.timeout as e:
                        logger.error("Socket read timeout")
                        exit(1)
                    except socket.error as e:
                        logger.error(e)
                        exit(1)

            # Remove MSG_SEPARATOR from the response before returning
            return response.decode('utf-8').strip().rstrip("\x1F")

    except socket.error as e:
        return f"Socket error: {e}"
    except Exception as e:
        return f"Error: {e}"


def send_command_to_redis(command: str):
    try:
        parts = command.split(" ", 2)
        cmd = parts[0].upper()

        if cmd == "SET" and len(parts) == 3:
            key, value = parts[1], parts[2]
            response = redis_client.set(key, value)
            return "+OK" if response else "ERROR"
        elif cmd == "GET" and len(parts) == 2:
            key = parts[1]
            response = redis_client.get(key)
            return response if response else "nil"
        else:
            return redis_client.execute_command(*command.split())

    except redis.RedisError as e:
        return f"Redis error: {e}"
    except Exception as e:
        return f"Error: {e}"

def send_command(command:str, bufSize = 1024):
    """Abstracted method to send commands to the appropriate cache."""
    if cache_type == 'redis':
        return send_command_to_redis(command)
    return send_command_to_custom_cache(command, bufSize)

def preload_json_files():
    if not os.path.exists(data_folder):
        logger.warning(f"Data folder '{data_folder}' does not exist. Skipping JSON preloading.")
        return
    
    json_files = [f for f in os.listdir(data_folder) if f.endswith(".json")]
    
    for file_name in json_files:
        file_path = os.path.join(data_folder, file_name)
        
        try:
            with open(file_path, "r", encoding="utf-8") as f:
                json_content = f.read()
            key = file_name.rsplit('.', maxsplit=1)[0]
            requestSize = len(json_content)
            isBigData = requestSize > 1000000
            readBufferSize = 131072 if isBigData else 1024

            response = send_command(f'SET {key} {json_content}')
            expectedResponse = "+OK" if cache_type == 'redis' else "OK"

            if response != expectedResponse:
                logger.error(f"Failed to store {key} in cache. Response: {response}")
                exit(1)
            
            logger.info(f"Successfully stored {key} in cache.")
            time.sleep(delay_sec)
            response = send_command(f"GET {key}", bufSize=readBufferSize)
            
            if cache_type == 'redis':
                response = response.lstrip("$").split("\r\n", 1)[-1].strip()
            
            if response != json_content:
                logger.debug(f"Retrying request: GET {key}")
                time.sleep(delay_sec / 2)
                response = send_command(f'GET {key}', bufSize=readBufferSize)
                if isBigData:
                    if len(response) != requestSize:
                        logger.error(f"Failed to retrieve {key} from cache! requestSize:{requestSize}, len(response): {len(response)}")
                        exit(1)
                else:
                    if response != json_content:
                        logger.error(f"Failed to retrieve {key} from cache! Response: {response}")
                        exit(1)
            
            logger.info(f"Successfully retrieved {key} from cache.")
        except Exception as e:
            logger.error(f"Error processing {file_name}: {e}")
            exit(1)

def test_iteration(x):
    """Perform one test iteration with SET and GET commands."""
    result = True
    try:
        response = send_command(f"SET key{x} value{x}")
        if response != "OK":
            logger.debug(f"Retrying request: SET key{x} value{x}\n")
            time.sleep(delay_sec / 2)
            response = send_command(f"SET key{x} value{x}")
            if response != "OK":
                result = False

        response = send_command(f"GET key{x}")
        if response != f"value{x}":
            logger.debug(f"Retrying request: GET key{x}\n")
            time.sleep(delay_sec / 2)
            response = send_command(f"GET key{x}")
            if response != f"value{x}":
                result = False

        response = send_command("GET non_existent_key")
        expected_response = "" if cache_type == "redis" else "(nil)"
        if response != expected_response:
            logger.debug(f"Retrying request: GET non_existent_key\n")
            time.sleep(delay_sec / 2)
            response = send_command("GET non_existent_key")
            if response != expected_response:
                result = False

    except Exception as e:
        result = False
        logger.error(f"Error during test iteration {x}: {e}\n")

    return result

def run_load_tests():
    """Run load tests after JSON preloading."""
    thread_pool_size = calc_thread_pool_size()
    logger.info(f"Running load tests with {iterations_count} iterations and thread pool size {thread_pool_size}...\n")
    
    failed_iterations = 0
    start = time.time()
    with Pool(thread_pool_size) as pool:
        results = pool.map(test_iteration, range(iterations_count))
        failed_iterations = sum(not res for res in results)
    end = time.time()

    test_time = end - start
    num_requests = 3 * iterations_count
    rps = num_requests / test_time

    logger.info(f"Load test completed. Test time: {test_time:.2f} seconds. Average RPS: {rps:.2f}\n")
    
    if failed_iterations > 0:
        exit(1)
    else:
        exit(0)

if __name__ == "__main__":
    logger.info(f"Sending large JSON data from files ...\n")
    preload_json_files()

    logger.info(f"Starting load tests in {delay_sec} seconds...\n")
    time.sleep(delay_sec)

    run_load_tests()
