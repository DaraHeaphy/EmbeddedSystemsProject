#!/usr/bin/env python3
"""
Step 1: Test connectivity to the MQTT broker at alderaan.software-engineering.ie
This script will verify if you can reach the broker and if authentication is required.
"""

import paho.mqtt.client as mqtt
import time
import sys

BROKER_HOST = "alderaan.software-engineering.ie"
BROKER_PORT = 1883
TEST_TOPIC = "test/connectivity"

def on_connect(client, userdata, flags, rc):
    """Callback when connection is established"""
    if rc == 0:
        print("✅ Successfully connected to broker!")
        print(f"   Host: {BROKER_HOST}")
        print(f"   Port: {BROKER_PORT}")
        print(f"   Anonymous access: ALLOWED")
        userdata['connected'] = True
    else:
        error_messages = {
            1: "Connection refused - incorrect protocol version",
            2: "Connection refused - invalid client identifier",
            3: "Connection refused - server unavailable",
            4: "Connection refused - bad username or password",
            5: "Connection refused - not authorized"
        }
        print(f"❌ Connection failed!")
        print(f"   Error code: {rc}")
        print(f"   Reason: {error_messages.get(rc, 'Unknown error')}")
        userdata['connected'] = False
        userdata['error_code'] = rc

def on_disconnect(client, userdata, rc):
    """Callback when disconnected"""
    if rc != 0:
        print(f"⚠️  Unexpected disconnection (code: {rc})")

def test_anonymous_connection():
    """Test connection without credentials"""
    print("=" * 70)
    print("🧪 Testing MQTT Broker Connectivity")
    print("=" * 70)
    print()
    print(f"🔍 Attempting to connect to: mqtt://{BROKER_HOST}:{BROKER_PORT}")
    print(f"   Mode: Anonymous (no username/password)")
    print()
    
    userdata = {'connected': False, 'error_code': None}
    
    client = mqtt.Client("connectivity_tester")
    client.user_data_set(userdata)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    
    try:
        print("🔌 Connecting...")
        client.connect(BROKER_HOST, BROKER_PORT, 60)
        client.loop_start()
        
        # Wait for connection
        timeout = 10
        start_time = time.time()
        while not userdata['connected'] and userdata['error_code'] is None:
            if time.time() - start_time > timeout:
                print("❌ Connection timeout!")
                print(f"   Could not connect within {timeout} seconds")
                print()
                print("💡 Possible issues:")
                print("   • Firewall blocking port 1883")
                print("   • Broker is down")
                print("   • Network connectivity issues")
                return False
            time.sleep(0.1)
        
        if userdata['connected']:
            print()
            print("🎉 SUCCESS! Broker is reachable and allows anonymous access")
            print()
            print("📋 Next steps:")
            print("   1. Run subscriber: python test_subscriber.py")
            print("   2. Run publisher: python test_publisher.py")
            print("   3. Update your ESP32 C code with this broker address")
            print()
            client.loop_stop()
            client.disconnect()
            return True
        else:
            error_code = userdata['error_code']
            if error_code == 4 or error_code == 5:
                print()
                print("🔐 Authentication Required!")
                print()
                print("The broker requires username and password.")
                print("Please ask your instructor/admin for credentials.")
                print()
                print("Once you have credentials, you can test with:")
                print(f"  mosquitto_sub -h {BROKER_HOST} -p {BROKER_PORT} \\")
                print(f"    -u YOUR_USERNAME -P YOUR_PASSWORD -t test/topic")
            return False
            
    except Exception as e:
        print(f"❌ Error: {e}")
        print()
        print("💡 This usually means:")
        print("   • Network connectivity issues")
        print("   • DNS resolution failed")
        print("   • Firewall is blocking the connection")
        return False
    finally:
        try:
            client.loop_stop()
            client.disconnect()
        except:
            pass

def test_with_credentials(username, password):
    """Test connection with credentials"""
    print("=" * 70)
    print("🔐 Testing with Credentials")
    print("=" * 70)
    print()
    
    userdata = {'connected': False, 'error_code': None}
    
    client = mqtt.Client("connectivity_tester_auth")
    client.user_data_set(userdata)
    client.username_pw_set(username, password)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    
    try:
        print(f"🔌 Connecting with username: {username}")
        client.connect(BROKER_HOST, BROKER_PORT, 60)
        client.loop_start()
        
        timeout = 10
        start_time = time.time()
        while not userdata['connected'] and userdata['error_code'] is None:
            if time.time() - start_time > timeout:
                print("❌ Connection timeout!")
                return False
            time.sleep(0.1)
        
        if userdata['connected']:
            print()
            print("🎉 SUCCESS! Authentication works!")
            client.loop_stop()
            client.disconnect()
            return True
        else:
            print()
            print("❌ Authentication failed - check your credentials")
            return False
            
    except Exception as e:
        print(f"❌ Error: {e}")
        return False
    finally:
        try:
            client.loop_stop()
            client.disconnect()
        except:
            pass

def main():
    if len(sys.argv) > 2:
        # Test with credentials
        username = sys.argv[1]
        password = sys.argv[2]
        test_with_credentials(username, password)
    else:
        # Test anonymous connection
        success = test_anonymous_connection()
        sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()

