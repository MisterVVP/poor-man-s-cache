import socket
import time
import os
from multiprocessing import Pool

host = os.environ.get('CACHE_HOST', 'localhost')
port = int(os.environ.get('CACHE_PORT', 9001))
delay_sec = int(os.environ.get('TEST_DELAY', 1))
iterations_count = int(os.environ.get('TEST_ITERATIONS', 1000))
debug = (os.environ.get('DEBUG', 'False').lower() == 'true')

def send_command(command):  
    """Send a command to the TCP server and return the response."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((host, port))
            s.sendall(command.encode('utf-8'))
            response = s.recv(1024).decode('utf-8')
            return response.strip()
    except Exception as e:
        return f"Error: {e}"

# TODO: replace all if (debug): with normal logger and don't send "Debug" level to stdout unless DEBUG=True 
def testIteration(x):
    # Test SET command
    set_cmd = f"SET key{x} {x}"
    if (debug):
        print(f"{set_cmd}\n")
    response = send_command(f"SET key{x} {x}")
    if (debug):
        print(f"Response: {response}\n")

    # Test GET command for an existing key
    get_cmd = f"GET key{x}"
    if (debug):
        print(f"{get_cmd}\n")
    response = send_command(f"GET key{x}")
    if (debug):
        print(f"Response: {response}\n")

    # Test GET command for a non-existent key
    if (debug):
        print("GET non_existent_key\n")
    response = send_command("GET non_existent_key")
    if (debug):
        print(f"Response: {response}")


if __name__ == "__main__":
    print(f"Starting test in {delay_sec}, iterations count is {iterations_count}\n")
    time.sleep(delay_sec)

    with Pool() as pool:
        pool.map(testIteration, range(iterations_count))

    print(f"Testing has been completed\n")