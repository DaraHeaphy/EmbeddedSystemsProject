#!/usr/bin/env python3
"""
Custom MQTT Broker Implementation
A lightweight MQTT 3.1.1 broker written in Python
"""

import socket
import struct
import threading
import time
from typing import Dict, List, Set
from collections import defaultdict

# MQTT Control Packet Types
CONNECT = 0x10
CONNACK = 0x20
PUBLISH = 0x30
PUBACK = 0x40
SUBSCRIBE = 0x80
SUBACK = 0x90
PINGREQ = 0xC0
PINGRESP = 0xD0
DISCONNECT = 0xE0

class MQTTBroker:
    """Simple MQTT Broker"""
    
    def __init__(self, host='0.0.0.0', port=1883):
        self.host = host
        self.port = port
        self.clients: Dict[socket.socket, dict] = {}
        self.subscriptions: Dict[str, Set[socket.socket]] = defaultdict(set)
        self.running = False
        self.lock = threading.Lock()
        
    def start(self):
        """Start the broker"""
        self.running = True
        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_socket.bind((self.host, self.port))
        server_socket.listen(5)
        
        print("=" * 60)
        print("🚀 Custom MQTT Broker Started")
        print("=" * 60)
        print(f"📡 Listening on {self.host}:{self.port}")
        print("⏳ Waiting for connections... (Press Ctrl+C to stop)\n")
        
        try:
            while self.running:
                try:
                    server_socket.settimeout(1.0)
                    client_socket, address = server_socket.accept()
                    print(f"🔌 New connection from {address}")
                    
                    # Handle client in new thread
                    client_thread = threading.Thread(
                        target=self.handle_client,
                        args=(client_socket, address)
                    )
                    client_thread.daemon = True
                    client_thread.start()
                except socket.timeout:
                    continue
                    
        except KeyboardInterrupt:
            print("\n\n👋 Shutting down broker...")
        finally:
            self.running = False
            server_socket.close()
            
    def handle_client(self, client_socket: socket.socket, address):
        """Handle individual client connection"""
        client_id = None
        
        try:
            while self.running:
                # Read fixed header
                first_byte = client_socket.recv(1)
                if not first_byte:
                    break
                    
                packet_type = first_byte[0] & 0xF0
                
                # Read remaining length
                remaining_length = self.decode_remaining_length(client_socket)
                
                # Read packet data
                if remaining_length > 0:
                    data = client_socket.recv(remaining_length)
                else:
                    data = b''
                
                # Handle packet
                if packet_type == CONNECT:
                    client_id = self.handle_connect(client_socket, data)
                    with self.lock:
                        self.clients[client_socket] = {
                            'id': client_id,
                            'address': address
                        }
                    print(f"✅ Client connected: {client_id} ({address})")
                    
                elif packet_type == PUBLISH:
                    self.handle_publish(client_socket, first_byte[0], data)
                    
                elif packet_type == SUBSCRIBE:
                    self.handle_subscribe(client_socket, data)
                    
                elif packet_type == PINGREQ:
                    self.send_pingresp(client_socket)
                    
                elif packet_type == DISCONNECT:
                    print(f"👋 Client disconnecting: {client_id}")
                    break
                    
        except Exception as e:
            print(f"❌ Error handling client {client_id}: {e}")
        finally:
            self.cleanup_client(client_socket)
            
    def handle_connect(self, client_socket: socket.socket, data: bytes) -> str:
        """Handle CONNECT packet"""
        # Parse protocol name
        pos = 0
        proto_len = struct.unpack(">H", data[pos:pos+2])[0]
        pos += 2
        protocol_name = data[pos:pos+proto_len].decode('utf-8')
        pos += proto_len
        
        # Protocol level
        protocol_level = data[pos]
        pos += 1
        
        # Connect flags
        connect_flags = data[pos]
        pos += 1
        
        # Keep alive
        keep_alive = struct.unpack(">H", data[pos:pos+2])[0]
        pos += 2
        
        # Client ID
        client_id_len = struct.unpack(">H", data[pos:pos+2])[0]
        pos += 2
        client_id = data[pos:pos+client_id_len].decode('utf-8')
        
        # Send CONNACK
        connack = bytes([CONNACK, 2, 0, 0])  # Session present=0, Return code=0 (accepted)
        client_socket.send(connack)
        
        return client_id
        
    def handle_publish(self, client_socket: socket.socket, flags: int, data: bytes):
        """Handle PUBLISH packet"""
        qos = (flags & 0x06) >> 1
        
        # Parse topic
        pos = 0
        topic_len = struct.unpack(">H", data[pos:pos+2])[0]
        pos += 2
        topic = data[pos:pos+topic_len].decode('utf-8')
        pos += topic_len
        
        # Message ID (if QoS > 0)
        msg_id = None
        if qos > 0:
            msg_id = struct.unpack(">H", data[pos:pos+2])[0]
            pos += 2
        
        # Payload
        payload = data[pos:]
        
        client_id = self.clients.get(client_socket, {}).get('id', 'unknown')
        print(f"📨 PUBLISH from {client_id}: topic='{topic}', payload={payload[:50]}...")
        
        # Forward to subscribers
        self.forward_publish(topic, payload, flags)
        
        # Send PUBACK if QoS 1
        if qos == 1 and msg_id:
            puback = struct.pack(">BBH", PUBACK, 2, msg_id)
            client_socket.send(puback)
            
    def handle_subscribe(self, client_socket: socket.socket, data: bytes):
        """Handle SUBSCRIBE packet"""
        # Message ID
        pos = 0
        msg_id = struct.unpack(">H", data[pos:pos+2])[0]
        pos += 2
        
        subscribed_topics = []
        return_codes = []
        
        # Parse topic filters
        while pos < len(data):
            topic_len = struct.unpack(">H", data[pos:pos+2])[0]
            pos += 2
            topic = data[pos:pos+topic_len].decode('utf-8')
            pos += topic_len
            qos = data[pos]
            pos += 1
            
            # Add subscription
            with self.lock:
                self.subscriptions[topic].add(client_socket)
            subscribed_topics.append(topic)
            return_codes.append(qos)  # Grant requested QoS
            
        client_id = self.clients.get(client_socket, {}).get('id', 'unknown')
        print(f"📌 SUBSCRIBE from {client_id}: topics={subscribed_topics}")
        
        # Send SUBACK
        suback = struct.pack(">BBH", SUBACK, 2 + len(return_codes), msg_id)
        suback += bytes(return_codes)
        client_socket.send(suback)
        
    def forward_publish(self, topic: str, payload: bytes, flags: int):
        """Forward PUBLISH to all matching subscribers"""
        with self.lock:
            subscribers = set()
            
            # Exact match
            if topic in self.subscriptions:
                subscribers.update(self.subscriptions[topic])
            
            # Wildcard matches
            for sub_topic, clients in self.subscriptions.items():
                if self.topic_matches(topic, sub_topic):
                    subscribers.update(clients)
            
            # Build PUBLISH packet
            topic_bytes = topic.encode('utf-8')
            publish_packet = bytes([flags, 0])  # Placeholder for remaining length
            publish_packet += struct.pack(">H", len(topic_bytes))
            publish_packet += topic_bytes
            publish_packet += payload
            
            # Update remaining length
            remaining_length = len(publish_packet) - 2
            publish_packet = bytes([flags]) + self.encode_remaining_length(remaining_length) + publish_packet[2:]
            
            # Send to all subscribers
            for subscriber in subscribers:
                try:
                    subscriber.send(publish_packet)
                except:
                    pass  # Client disconnected
                    
    def topic_matches(self, topic: str, pattern: str) -> bool:
        """Check if topic matches subscription pattern (with wildcards)"""
        if pattern == '#':
            return True
            
        topic_parts = topic.split('/')
        pattern_parts = pattern.split('/')
        
        if len(pattern_parts) > len(topic_parts):
            if '#' not in pattern_parts:
                return False
        
        for i, pattern_part in enumerate(pattern_parts):
            if pattern_part == '#':
                return True
            if i >= len(topic_parts):
                return False
            if pattern_part != '+' and pattern_part != topic_parts[i]:
                return False
                
        return len(topic_parts) == len(pattern_parts)
        
    def send_pingresp(self, client_socket: socket.socket):
        """Send PINGRESP"""
        pingresp = bytes([PINGRESP, 0])
        client_socket.send(pingresp)
        
    def cleanup_client(self, client_socket: socket.socket):
        """Clean up client connection"""
        with self.lock:
            # Remove from subscriptions
            for topic, subscribers in self.subscriptions.items():
                subscribers.discard(client_socket)
            
            # Remove from clients
            if client_socket in self.clients:
                client_id = self.clients[client_socket].get('id', 'unknown')
                print(f"🔌 Client disconnected: {client_id}")
                del self.clients[client_socket]
        
        try:
            client_socket.close()
        except:
            pass
            
    @staticmethod
    def decode_remaining_length(sock: socket.socket) -> int:
        """Decode MQTT remaining length"""
        multiplier = 1
        value = 0
        
        while True:
            byte = sock.recv(1)[0]
            value += (byte & 0x7F) * multiplier
            if (byte & 0x80) == 0:
                break
            multiplier *= 128
            
        return value
        
    @staticmethod
    def encode_remaining_length(length: int) -> bytes:
        """Encode MQTT remaining length"""
        result = bytearray()
        
        while True:
            byte = length % 128
            length = length // 128
            if length > 0:
                byte |= 0x80
            result.append(byte)
            if length == 0:
                break
                
        return bytes(result)


def main():
    """Main function"""
    broker = MQTTBroker(host='0.0.0.0', port=1883)
    broker.start()


if __name__ == "__main__":
    main()

