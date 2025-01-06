import websocket
import time
import os


host = os.environ.get('CACHE_HOST', 'ws://localhost')
port = os.environ.get('CACHE_PORT', 9001)
delay_sec = os.environ.get('TEST_DELAY', 1)

cache_url = f"{host}:{port}/"

def send_command(command):
    try:
        # Connect to the WebSocket server
        ws = websocket.WebSocket()
        ws.connect(cache_url)
        
        # Send the command
        ws.send(command)
        
        # Receive the response
        response = ws.recv()
        print(f"Command: {command}, Response: {response}")
        
        ws.close()
    except Exception as e:
        print(f"Error: {e}")

time.sleep(delay_sec)

# Test SET command
send_command("SET myKey myValue")

# Test GET command
send_command("GET myKey")

# Test GET for a non-existing key
send_command("GET unknownKey")
