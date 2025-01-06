import socket
import time
import os


host = os.environ.get('CACHE_HOST', 'localhost')
port = int(os.environ.get('CACHE_PORT', 9001))
delay_sec = int(os.environ.get('TEST_DELAY', 1))

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

if __name__ == "__main__":
    time.sleep(delay_sec)
    # Test SET command
    print("Testing SET command:")
    response = send_command("SET key value")
    print(f"Response: {response}")

    time.sleep(delay_sec/2)
    # Test GET command for an existing key
    print("\nTesting GET command for existing key:")
    response = send_command("GET key")
    print(f"Response: {response}")

    time.sleep(delay_sec/2)
    # Test GET command for a non-existent key
    print("\nTesting GET command for non-existent key:")
    response = send_command("GET non_existent_key")
    print(f"Response: {response}")
