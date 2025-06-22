import asyncio

class ConnectionPool:
    def __init__(self, host: str, port: int, size: int):
        self.host = host
        self.port = port
        self.size = size
        self.connections = asyncio.Queue()
        self.initialized = False

    async def initialize(self):
        for _ in range(self.size):
            reader, writer = await asyncio.open_connection(self.host, self.port)
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
        except Exception:
            pass
