import socket
import time
import os
from multiprocessing import Pool

host = os.environ.get('CACHE_HOST', 'localhost')
port = int(os.environ.get('CACHE_PORT', 9001))
delay_sec = int(os.environ.get('TEST_DELAY', 1))
iterations_count = int(os.environ.get('TEST_ITERATIONS', 1000))

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

def testIteration(x):
    result = True
    # Test SET command
    response = send_command(f"SET key{x} {x}")
    if (response != "OK"):
        result = False
        print(f"Request: SET key{x} {x} | Response: {response}\n")

    # Test GET command for an existing key
    response = send_command(f"GET key{x}")
    if (response != f"{x}"):
        result = False
        print(f"Request: GET key{x} | Response: {response}\n")

    # Test GET command for a non-existent key
    response = send_command("GET non_existent_key")
    if (response != "(nil)"):
        result = False
        print(f"Request: GET non_existent_key | Response: {response}\n")

    return result    


if __name__ == "__main__":
    print(f"Starting test in {delay_sec}, iterations count is {iterations_count}\n")
    time.sleep(delay_sec)

    with Pool() as pool:
        for res in pool.map(testIteration, range(iterations_count)):
            if (not res):
                print(f"An error was detected during test execution.\n")
                exit(1)

    print(f"The test was completed successfully.")
