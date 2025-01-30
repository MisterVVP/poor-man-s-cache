import socket
import time
import os
from multiprocessing import Pool, cpu_count

# Configuration from environment or defaults
host = os.environ.get('CACHE_HOST', 'localhost')
port = int(os.environ.get('CACHE_PORT', 9001))
delay_sec = int(os.environ.get('TEST_DELAY_SEC', 90))
iterations_count = int(os.environ.get('TEST_ITERATIONS', 1000))
metrics_port = int(os.environ.get('METRICS_PORT', 8080))
cache_type = os.environ.get('CACHE_TYPE', 'custom')  # "custom" or "redis"
redis_password = os.environ.get('REDIS_PASSWORD', None)  # Only for Redis

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

def send_command_to_custom_cache(command):
    """Send a command to the custom TCP server and return the response."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((host, port))
            s.sendall(command.encode('utf-8'))
            response = s.recv(1024).decode('utf-8')
            return response.strip()
    except socket.error as e:
        return f"Socket error: {e}"
    except Exception as e:
        return f"Error: {e}"

def send_command_to_redis(command):
    """Send a command to the Redis server and return the response."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((host, port))
            if redis_password:
                s.sendall(f"AUTH {redis_password}\r\n".encode('utf-8'))
                auth_response = s.recv(1024).decode('utf-8').strip()
                if not auth_response.startswith("+OK"):
                    return f"Auth failed: {auth_response}"

            s.sendall((command + "\r\n").encode('utf-8'))
            response = s.recv(1024).decode('utf-8')
            if response.startswith("$"):
                return response.split("\r\n", 1)[-1].strip()
            elif response.startswith("+") or response.startswith("-"):
                return response[1:].strip()
            return response.strip()
    except socket.error as e:
        return f"Socket error: {e}"
    except Exception as e:
        return f"Error: {e}"

def send_command(command):
    """Abstracted method to send commands to the appropriate cache."""
    if cache_type == 'redis':
        return send_command_to_redis(command)
    return send_command_to_custom_cache(command)

def test_iteration(x):
    """Perform one test iteration with SET and GET commands."""
    result = True
    try:
        # Test SET command
        response = send_command(f"SET key{x} value{x}")
        if response != "OK":
            print(f"Retrying request: SET key{x} value{x}\n")
            time.sleep(delay_sec / 2)
            response = send_command(f"SET key{x} value{x}")
            if response != "OK":
                result = False
                print(f"Request failed: SET key{x} value{x} | response: {response}\n")

        # Test GET command for an existing key
        response = send_command(f"GET key{x}")
        expected_response = f"value{x}" if cache_type == "redis" else f"value{x}"
        if response != expected_response:
            print(f"Retrying request: GET key{x}\n")
            # Retry one more time after short delay
            time.sleep(delay_sec / 2)
            response = send_command(f"GET key{x}")
            if response != expected_response:
                result = False
                print(f"Request failed: GET key{x} | response: {response}\n")

        # Test GET command for a non-existent key
        response = send_command("GET non_existent_key")
        expected_response = "" if cache_type == "redis" else "(nil)"
        if response != expected_response:
            print(f"Retrying request: GET non_existent_key\n")
            # Retry one more time after short delay
            time.sleep(delay_sec / 2)
            response = send_command("GET non_existent_key")
            if response != expected_response:
                result = False
                print(f"Request failed: GET non_existent_key | response: {response}\n")

    except Exception as e:
        result = False
        print(f"Error during test iteration {x}: {e}\n")

    return result

if __name__ == "__main__":
    thread_pool_size = calc_thread_pool_size()
    print(f"Starting test in {delay_sec} seconds with {iterations_count} iterations and thread pool size {thread_pool_size}...\n")
    time.sleep(delay_sec)

    failed_iterations = 0
    start = time.time()
    with Pool(thread_pool_size) as pool:
        results = pool.map(test_iteration, range(iterations_count))
        failed_iterations = sum(not res for res in results)
    end = time.time()

    print(f"Waiting for {delay_sec}...\n")

    time.sleep(delay_sec)
    test_time = end - start
    num_requests = 3 * iterations_count
    rps = num_requests / test_time
    if failed_iterations > 0:
        print(f"Test completed with {failed_iterations} failed iterations. Test time: {test_time} seconds. Requests count: {num_requests}. Average RPS: {rps}\n")
        exit(1)
    else:
        print(f"The test was completed successfully with no errors. Test time: {test_time} seconds. Requests count: {num_requests}. Average RPS: {rps} \n")
