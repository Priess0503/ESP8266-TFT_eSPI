import asyncio
import websockets
import json
from datetime import datetime, timezone, timedelta
from aiohttp import web
import sys
import logging

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class SimpleServerMonitor:
    def __init__(self):
        self.websocket_url = "wss://tz.bbskali.cn/api/v1/ws/server"
        self.http_host = "192.168.50.48"
        self.http_port = 8080
        self.data_cache = None
        self.last_fetch_time = None
        
    async def fetch_data(self):
        """获取数据 - 最简单的方式"""
        try:
            # 使用最简单的连接方式
            async with websockets.connect(self.websocket_url) as websocket:
                response = await websocket.recv()
                return json.loads(response)
        except Exception as e:
            logger.error(f"获取数据失败: {e}")
            return None
    
    def is_server_online(self, last_active_str):
        """判断服务器是否在线"""
        if not last_active_str or last_active_str == "0001-01-01T00:00:00Z":
            return False
        
        try:
            if last_active_str.endswith('Z'):
                last_active = datetime.fromisoformat(last_active_str.replace('Z', '+00:00'))
            else:
                last_active = datetime.fromisoformat(last_active_str)
            
            if last_active.tzinfo is None:
                last_active_utc = last_active.replace(tzinfo=timezone.utc)
            else:
                last_active_utc = last_active.astimezone(timezone.utc)
            
            now_utc = datetime.now(timezone.utc)
            return (now_utc - last_active_utc) <= timedelta(days=1)
        except Exception as e:
            logger.error(f"解析时间错误: {e}")
            return False
    
    def process_data(self, raw_data):
        """处理数据"""
        if not raw_data:
            return {"error": "无法获取数据"}
        
        result = {
            "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "total_servers": len(raw_data['servers']),
            "online_servers": 0,
            "offline_servers": 0,
            "servers": []
        }
        
        for server in raw_data['servers']:
            is_online = self.is_server_online(server.get('last_active'))
            
            if is_online:
                result['online_servers'] += 1
                host = server.get('host', {})
                state = server.get('state', {})
                
                mem_total = host.get('mem_total', 0)
                mem_used = state.get('mem_used', 0)
                memory_usage = (mem_used / mem_total * 100) if mem_total > 0 else 0
                
                disk_total = host.get('disk_total', 0)
                disk_total_gb = disk_total / (1024**3) if disk_total > 0 else 0
                
                # 获取磁盘已使用空间（可能在host或state中）
                disk_used = host.get('disk_used', 0) or state.get('disk_used', 0)
                disk_usage = (disk_used / disk_total * 100) if disk_total > 0 else 0
                
                upload_speed_kb = state.get('net_out_speed', 0) / 1024
                download_speed_kb = state.get('net_in_speed', 0) / 1024
                
                server_info = {
                    "id": server['id'],
                    "name": server['name'],
                    "platform": host.get('platform', '未知'),
                    "cpu_usage": round(state.get('cpu', 0), 2),
                    "memory_usage": round(memory_usage, 2),
                    "disk_total_gb": round(disk_total_gb, 2),
                    "disk_usage": round(disk_usage, 2),
                    "upload_speed_kb": round(upload_speed_kb, 2),
                    "download_speed_kb": round(download_speed_kb, 2)
                }
                
                result['servers'].append(server_info)
            else:
                result['offline_servers'] += 1
        
        return result
    
    async def handle_request(self, request):
        """处理请求"""
        # 如果缓存为空或超过30秒，更新数据
        if (self.data_cache is None or 
            self.last_fetch_time is None or 
            (datetime.now() - self.last_fetch_time).total_seconds() > 30):
            
            raw_data = await self.fetch_data()
            if raw_data:
                self.data_cache = self.process_data(raw_data)
                self.last_fetch_time = datetime.now()
        
        if self.data_cache:
            response = web.json_response(self.data_cache)
        else:
            response = web.json_response({"error": "无数据"}, status=500)
        
        response.headers['Access-Control-Allow-Origin'] = '*'
        return response
    
    async def run(self):
        """运行服务"""
        app = web.Application()
        app.router.add_get('/', self.handle_request)
        app.router.add_get('/api', self.handle_request)
        
        runner = web.AppRunner(app)
        await runner.setup()
        site = web.TCPSite(runner, self.http_host, self.http_port)
        
        print(f"服务器监控API运行在: http://{self.http_host}:{self.http_port}")
        print("访问 / 或 /api 获取数据")
        
        await site.start()
        await asyncio.Future()

async def main():
    monitor = SimpleServerMonitor()
    await monitor.run()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n服务已停止")